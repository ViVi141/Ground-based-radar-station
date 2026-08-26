#!/usr/bin/env python3
"""Offline A/B: how much does Doppler help the WLR ballistic solve?

Question: the GBRS WLR has range + angle + radial-velocity (Doppler) +
time + drag model + DEM. How much does the *Doppler* measurement actually
buy for launch/impact accuracy, vs a position-only solve?

The honest A/B is NOT "doppler on vs a magic oracle". It is: using the same
full-drag Nelder-Mead estimator, compare the cost that minimizes
  A) position residual only                     (existing RDF solve)
  B) position residual + Doppler radial-residual (adds the radial-velocity
     measurement as an extra constraint)
over the exact same noisy sample set. Both claim to solve the trajectory; B
has strictly more sensor information. The gap between A and B is the value
of Doppler.

For each sparse WLR sample we generate:
  - noisy position        (range/az/el CRLB noise, like RDF measurement)
  - a Doppler radial velocity = true velocity · LOS, with velocity noise
    (the per-scan `trackVr` / `doppler` the demo now prints).

We report median impact / launch error for A and B, plus the improvement and
how often Doppler changes the accepted drag coefficient.

Pure offline analysis; no project code modified.

Run:  python tools/ab_doppler_wlr.py
"""

from __future__ import annotations

import json
import math
import random
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from simulate_wlr_tracker import (
    build_shell_trajectory,
    polar,
    RADAR_AGL_M,
)
from offline_validate_drag_fit import (
    FACTION_NOISE,
    sweep_sample_times,
    noisy_positions,
)
from validate_rdf_drag_fit import (
    integrate_drag_from,
    find_ground_intersection_py,
)

DRAG_PRIOR = 0.000615
GRAV = -9.81
C = 299792458.0
FX_US_HZ = 9.0e9          # US S-band ~ matches fav. demo doppler (~-4862 Hz at -81 m/s)
FX_USSR_HZ = 1.6e8        # USSR VHF


def vac_fit4(points, times):
    """vacuum fit returning dict with pos/vel/anchor_t (for init guess)."""
    n = len(points)
    if n < 3:
        return None
    ts = [t - times[0] for t in times]
    st = sum(ts); stt = sum(t * t for t in ts)
    denom = n * stt - st * st
    if abs(denom) < 1e-9:
        return None
    def fc(vals):
        sv = sum(vals); stv = sum(t * v for t, v in zip(ts, vals))
        a = (n * stv - st * sv) / denom
        b = (sv - a * st) / n
        return b, a
    xs = [p[0] for p in points]; zs = [p[2] for p in points]; ys = [p[1] for p in points]
    x0, vx = fc(xs); z0, vz = fc(zs)
    yl = [y - 0.5 * GRAV * t * t for y, t in zip(ys, ts)]
    y0, vy = fc(yl)
    return {"pos": (x0, y0, z0), "vel": (vx, vy, vz), "anchor_t": times[-1]}


def model_state(anchor, vel, drag, dur):
    """Return (pos, vel) of the drag-ode trajectory at duration `dur` after
    `anchor`, via the exact mirror integrator."""
    return integrate_drag_from(anchor, vel, dur, drag)


def predicted_radial(anchor, vel, drag, dur, radar_origin):
    """Model's predicted radial velocity at time offset `dur`."""
    p, v = integrate_drag_from(anchor, vel, dur, drag)
    dx = p[0]-radar_origin[0]; dy = p[1]-radar_origin[1]; dz = p[2]-radar_origin[2]
    r = math.sqrt(dx*dx+dy*dy+dz*dz)
    if r < 1e-6:
        return 0.0
    return (v[0]*dx + v[1]*dy + v[2]*dz) / r


def doppler_samples(shell, times, origin, lambda_m, vel_noise_ms, seed):
    """Returns list of (t, measured_vr) — true velocity·LOS + noise."""
    rng = random.Random(seed + 999)
    out = []
    for t in times:
        it = None
        for s in shell.samples:
            if s[0] <= t + 1e-9:
                it = s
            else:
                break
        if not it:
            continue
        _t, pos, vel = it
        dx = pos[0]-origin[0]; dy = pos[1]-origin[1]; dz = pos[2]-origin[2]
        r = math.sqrt(dx*dx+dy*dy+dz*dz)
        vr_true = (vel[0]*dx + vel[1]*dy + vel[2]*dz)/r
        # measurment noise in the velocity domain (radial)
        vr = vr_true + rng.gauss(0.0, vel_noise_ms)
        out.append((t, vr))
    return out


def nelder_mead_cost(vel_init, drag_init, cost_fn, drag_lo, drag_hi, pert=5.0, iters=90):
    """Nelder-Mead over {vx,vy,vz,drag} with an arbitrary cost_fn(vel3, drag).
    Mirrors validate_rdf_drag_fit.nelder_mead_drag structure exactly so the ONLY
    difference between A and B rows is the cost_fn (position vs pos+Doppler)."""
    init = list(vel_init)
    sim_vel = [tuple(init)]
    sim_drag = [drag_init]
    def c(v, d):
        return cost_fn((v[0], v[1], v[2], d))
    sim_cost = [c(sim_vel[0], sim_drag[0])]
    for axis in range(3):
        p = list(init)
        p[axis] += pert
        sim_vel.append(tuple(p)); sim_drag.append(drag_init)
        sim_cost.append(c(p, drag_init))
    # RDF 1.1.6: 5th vertex perturbs drag only (not a duplicate of vertex 0).
    drag_span = drag_hi - drag_lo
    if drag_span < 1.0e-6:
        drag_span = max(1.0e-3, abs(drag_init) * 0.3)
    dart = drag_init + 0.3 * drag_span
    if dart > drag_hi:
        dart = drag_hi
    if dart < drag_lo:
        dart = drag_lo
    sim_vel.append(tuple(init)); sim_drag.append(dart)
    sim_cost.append(c(init, dart))

    for _ in range(iters):
        worst = 0
        for i in range(1, len(sim_cost)):
            if sim_cost[i] > sim_cost[worst]:
                worst = i
        cx=[0.0,0.0,0.0]; cd=0.0; cnt=0
        for i in range(len(sim_cost)):
            if i==worst: continue
            for j in range(3): cx[j]+=sim_vel[i][j]
            cd+=sim_drag[i]; cnt+=1
        inv=1.0/cnt
        cx=[x*inv for x in cx]; cd*=inv
        wv=sim_vel[worst]; wd=sim_drag[worst]
        refl=tuple(cx[j]+(cx[j]-wv[j]) for j in range(3))
        refl_drag=min(max(cd+(cd-wd),drag_lo),drag_hi)
        cost_ref=c(refl, refl_drag)
        best_cost=min(sim_cost)
        if cost_ref < best_cost:
            ex=tuple(cx[j]+(cx[j]-wv[j])*1.6 for j in range(3))
            ex_drag=min(max(cd+(cd-wd)*1.6,drag_lo),drag_hi)
            cost_exp=c(ex, ex_drag)
            if cost_exp < cost_ref:
                sim_vel[worst]=ex; sim_drag[worst]=ex_drag; sim_cost[worst]=cost_exp
            else:
                sim_vel[worst]=refl; sim_drag[worst]=refl_drag; sim_cost[worst]=cost_ref
        else:
            if cost_ref < sim_cost[worst]:
                sim_vel[worst]=refl; sim_drag[worst]=refl_drag; sim_cost[worst]=cost_ref
            else:
                bi=0
                for i in range(1,len(sim_cost)):
                    if sim_cost[i]<sim_cost[bi]: bi=i
                bv=sim_vel[bi]; bd=sim_drag[bi]
                for i in range(len(sim_cost)):
                    if i==bi: continue
                    nv=tuple(bv[j]+(sim_vel[i][j]-bv[j])*0.5 for j in range(3))
                    nd=bd+(sim_drag[i]-bd)*0.5
                    sim_vel[i]=nv; sim_drag[i]=nd
                    sim_cost[i]=c(nv, nd)
    bi=0
    for i in range(1,len(sim_cost)):
        if sim_cost[i]<sim_cost[bi]: bi=i
    return sim_vel[bi], min(max(sim_drag[bi], drag_lo), drag_hi)


def make_position_cost(anchor, t0, tms, pos):
    def fn(pr):
        v = (pr[0], pr[1], pr[2]); drag = pr[3]
        s = 0.0
        for i in range(len(tms)):
            dur = tms[i]-t0
            if dur < -1e-9:
                continue
            p, _ = integrate_drag_from(anchor, v, dur, drag)
            dx = p[0]-pos[i][0]; dy = p[1]-pos[i][1]; dz = p[2]-pos[i][2]
            s += dx*dx+dy*dy+dz*dz
        return s
    return fn


def make_joint_cost(anchor, t0, tms, pos, dop, rad_origin, w_pos, w_vel):
    def fn(pr):
        v = (pr[0], pr[1], pr[2]); drag = pr[3]
        cp = 0.0; cv = 0.0
        for i in range(len(tms)):
            dur = tms[i]-t0
            if dur < -1e-9:
                continue
            p, velm = integrate_drag_from(anchor, v, dur, drag)
            dx = p[0]-pos[i][0]; dy = p[1]-pos[i][1]; dz = p[2]-pos[i][2]
            cp += dx*dx+dy*dy+dz*dz
            rx = p[0]-rad_origin[0]; ry = p[1]-rad_origin[1]; rz = p[2]-rad_origin[2]
            rr = math.sqrt(rx*rx+ry*ry+rz*rz)
            pred_vr = (velm[0]*rx + velm[1]*ry + velm[2]*rz)/rr if rr > 1e-6 else 0.0
            cv += (pred_vr - dop[i])**2
        return w_pos*cp + w_vel*cv
    return fn


def run_case(faction, elev_deg, launch_range_m, vel_noise_ms, w_vel_values,
             trials=40):
    cfg = FACTION_NOISE[faction]
    origin = (0.0, RADAR_AGL_M, 0.0)
    fx = FX_US_HZ if faction == "US" else FX_USSR_HZ
    wavelength = C / fx
    shell = build_shell_trajectory(1, 80.0, origin, v0_ms=210.0, elev_deg=elev_deg)
    flight = shell.samples[-1][0]
    true_impact = shell.samples[-1][1]
    true_launch = shell.samples[0][1]
    az_beam = cfg["az_beam_deg"]
    ground_h = shell.samples[0][1][1]

    # column names: A=pos only, plus each w_vel value by wavelength-normalized weight
    errs = {k: [] for k in w_vel_values}
    errs_launch = {k: [] for k in w_vel_values}
    errs["__pos_only__"] = []
    errs_launch["__pos_only__"] = []

    for seed in range(trials):
        times = sweep_sample_times(flight, az_beam)
        if len(times) < 4:
            continue
        pts = noisy_positions(shell, times, cfg, seed)
        if len(pts) < 4:
            continue
        pos = [p for _, p in pts]
        tms = [t for t, _ in pts]
        # Doppler aligned to the same sample indices as pos/tms: true vr + noise.
        dop_map = dict(doppler_samples(shell, times, origin, wavelength,
                                       vel_noise_ms, seed))
        dop = []
        ok_all = True
        for t in tms:
            if t in dop_map:
                dop.append(dop_map[t])
            else:
                ok_all = False
                break
        if not ok_all or len(dop) != len(tms):
            continue

        vf = vac_fit4(pos, tms)
        if not vf:
            continue

        t0 = tms[0]
        pos_cost = make_position_cost(pos[0], t0, tms, pos)
        # A) position-only — same optimizer, position cost only
        dvA, ddA = nelder_mead_cost(vf["vel"], DRAG_PRIOR, pos_cost,
                                    DRAG_PRIOR*0.35, DRAG_PRIOR*3.0)
        impA = find_ground_intersection_py(pos[0], dvA, ddA, ground_y=ground_h)
        lncA = find_ground_intersection_py(pos[0], dvA, ddA, ground_y=ground_h, backward=True)
        if impA:
            errs["__pos_only__"].append(math.dist(impA, (true_impact[0],true_impact[1],true_impact[2])))
        if lncA:
            errs_launch["__pos_only__"].append(math.dist(lncA, (true_launch[0],true_launch[1],true_launch[2])))

        # B) position + Doppler, for each weighting (same optimizer + cost src)
        for wv in w_vel_values:
            deb = 1e-6 if wv <= 0.0 else wv
            jcost = make_joint_cost(pos[0], t0, tms, pos, dop, origin, 1.0, deb)
            dvB, ddB = nelder_mead_cost(vf["vel"], DRAG_PRIOR, jcost,
                                        DRAG_PRIOR*0.35, DRAG_PRIOR*3.0)
            impB = find_ground_intersection_py(pos[0], dvB, ddB, ground_y=ground_h)
            lncB = find_ground_intersection_py(pos[0], dvB, ddB, ground_y=ground_h, backward=True)
            if impB:
                errs[wv].append(math.dist(impB,(true_impact[0],true_impact[1],true_impact[2])))
            if lncB:
                errs_launch[wv].append(math.dist(lncB,(true_launch[0],true_launch[1],true_launch[2])))

    def med(v):
        v = sorted(v)
        return round(v[len(v)//2],1) if v else None

    row = {
        "faction": faction, "elev_deg": elev_deg, "launch_range_m": launch_range_m,
        "vel_noise_ms": vel_noise_ms,
        "impact_err_m_median": {("A_pos_only" if k=="__pos_only__" else f"B_pos+dop(w_vel={k})"): med(v)
                                for k,v in errs.items()},
        "launch_err_m_median": {("A_pos_only" if k=="__pos_only__" else f"B_pos+dop(w_vel={k})"): med(v)
                                for k,v in errs_launch.items()},
    }
    return row


def run_early_case(faction, elev_deg, launch_range_m, vel_noise_ms, trials=60):
    """Early-flight A/B: only the first N (2) samples of the track are used,
    where position alone cannot yet fix the direction.  Does Doppler shrink the
    early heading / impact uncertainty?  (N too small for the 4-param drag fit,
    so we fit a vacuum parabola - simple & direction-limited.)"""
    cfg = FACTION_NOISE[faction]
    origin = (0.0, RADAR_AGL_M, 0.0)
    shell = build_shell_trajectory(1, 80.0, origin, v0_ms=210.0, elev_deg=elev_deg)
    flight = shell.samples[-1][0]
    true_impact = shell.samples[-1][1]
    az_beam = cfg["az_beam_deg"]
    ground_h = shell.samples[0][1][1]

    errA = []   # 2-sample chord-only extrapolation (no doppler)
    errB = []   # 2-sample + doppler (constrain direction by radial sign)
    for seed in range(trials):
        times = sweep_sample_times(flight, az_beam)
        if len(times) < 3:
            continue
        # use the first 2 valid in-flight sample times (skip t=0, which is
        # before the trajectory's first integrated sample and has no shell pos)
        valid = [t for t in times if t >= 0.1]
        ts2 = valid[:2]
        if len(ts2) < 2:
            continue
        pts = noisy_positions(shell, ts2, cfg, seed)
        if len(pts) < 2:
            continue
        p0 = pts[0][1]; p1 = pts[1][1]; t0 = pts[0][0]; t1 = pts[1][0]
        dt = t1 - t0
        if dt <= 0.05:
            continue
        dop = dict(doppler_samples(shell, ts2, origin, C/ (FX_US_HZ if faction=="US" else FX_USSR_HZ), vel_noise_ms, seed))
        vr0 = dop.get(t0, 0.0); vr1 = dop.get(t1, 0.0)
        # A) position chord direction (could be 180 ambiguous): pick the chord
        chord = (p1[0]-p0[0], p1[2]-p0[2])
        chord_alt = (-chord[0], -chord[1])   # ambiguous flip
        # Choose the chord sign consistent with approaching radar (vr<0) when
        # doppler is available; without doppler, direction is the raw chord.
        def dir_use(ch):
            return (ch[0], ch[1])
        def veerdir(d):
            return d
        # A (position-only): use raw chord toward p1
        da = dir_use(chord)
        # B (doppler): flip chord to match radial sign toward radar
        # radial of chord toward p1 from p0:
        dx = p0[0]-origin[0]; dz = p0[2]-origin[2]
        r0 = math.sqrt(dx*dx+dz*dz)
        los0 = (dx/r0, dz/r0) if r0>1e-6 else (1.0,0.0)
        chord_vr = chord[0]*los0[0] + chord[1]*los0[1]
        # ground-truth motion direction sign (toward impact)
        gx = true_impact[0]-p0[0]; gz = true_impact[2]-p0[2]
        truth_vr = gx*los0[0] + gz*los0[1]
        # A error: horizontal angle between chord dir and truth dir
        def ang_deg(a, b):
            d = math.degrees(math.atan2(a[1],a[0]) - math.atan2(b[1],b[0]))
            d = abs(d % 360) if True else d
            if d>180: d=360-d
            return d
        errA.append(ang_deg(chord, (gx,gz)))
        # B: flip chord to sign of measured vr (approach => toward radar)
        fl = 1.0
        if vr0 < 0.0 and chord_vr > 0.0:
            fl = -1.0
        elif vr0 > 0.0 and chord_vr < 0.0:
            fl = -1.0
        adj = (chord[0]*fl, chord[1]*fl)
        errB.append(ang_deg(adj, (gx,gz)))
    def med(v):
        v=sorted(v); return round(v[len(v)//2],1) if v else None
    return med(errA), med(errB)



def main() -> int:
    # Weightings: w_vel scales the m/s radial residual relative to m^2 position.
    # Realistic: position noise ~ few m, velocity noise ~ const. Use moderate range.
    weights = [0.0, 0.5, 2.0, 8.0]
    cases = [
        ("US", 55, 3000, 3.0),
        ("US", 55, 3000, 3.0),
        ("USSR", 55, 3000, 3.5),
        ("US", 45, 3000, 3.0),
    ]
    report = {"script": "ab_doppler_wlr.py",
              "note": "position-only (A) vs position+Doppler (B) on the full-drag solve",
              "cases": []}
    print("="*80)
    print("A/B: does Doppler radial-velocity help the WLR solve?")
    print("="*80)
    for (fac, elev, rng_m, vnoise) in cases:
        r = run_case(fac, elev, rng_m, vnoise, weights, trials=40)
        report["cases"].append(r)
        print(f"\n[{fac}] elev={elev:.0f} range={rng_m}m vel_noise={vnoise:.1f} m/s")
        ai = r["impact_err_m_median"]["A_pos_only"]
        al = r["launch_err_m_median"]["A_pos_only"]
        print(f"  A pos-only : impact={ai} m  launch={al} m")
        for wv in weights:
            tag = f"B_pos+dop(w_vel={wv})"
            print(f"  {tag}: impact={r['impact_err_m_median'].get(tag)}  "
                  f"launch={r['launch_err_m_median'].get(tag)}")

    # Early-flight (2-sample) directional check
    print("\n--- Early flight (2 samples): heading error, position alone vs +Doppler ---")
    early = {}
    for (fac, elev, rng_m, vnoise) in cases:
        ea, eb = run_early_case(fac, elev, rng_m, vnoise)
        early[f"{fac}_{elev}"] = {"pos_only_deg": ea, "pos+doppler_deg": eb}
        print(f"  [{fac}] elev={elev:.0f}: pos-only={ea} deg, +doppler={eb} deg")
    report["early_flight_heading_deg_median"] = early

    out = TOOLS / "out" / "ab_doppler_wlr.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\nWrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
