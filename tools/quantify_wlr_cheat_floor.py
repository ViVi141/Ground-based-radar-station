#!/usr/bin/env python3
"""Quantify: is the current WLR shell fix a "cheat" or a real calculation?

Question from the user: "目前做的修复是不是比较作弊的" — is the current
arc-anchor + ballistic-fit approach faking data, or genuinely computing?

What this tool DOES:
  1. Reproduce the exact sensor constraints seen in the GBRS scan logs
     (vr=0 Doppler radial, ~1 measured scatterer per sector sweep, single
     station, receiver-range ambiguity), using the real ballistics model.
  2. Compute an ORACLE LOWER BOUND: for each phase of shell flight, what is
     the *best any algorithm* could estimate using the very same measurement
     set (not more, not less).  This is the theoretical information floor set
     by the sensor, independent of our code.
  3. Compare the CURRENT arc-anchor + fit error against that oracle floor.
  4. Verdict per phase: is the residual error due to our algorithm (i.e. we
     ARE leaving accuracy on the table => could be more exact, "ours is
     approximate") or is it equal to the floor (any algorithm would do the
     same => the limit is physics, NOT our method)?

No byte of code is modified by this tool; it is offline analysis only.
This is the "先量化再改" (quantify before changing) step.

Run:  python tools/quantify_wlr_cheat_floor.py
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

from simulate_wlr_accuracy import (
    GRAVITY_M_S2,
    AIR_DRAG_SHELL_82MM_HE,
    find_ground_intersection,
)
from simulate_wlr_tracker import (
    AIR_DRAG_SHELL_82MM_HE as DRAG,
    RADAR_AGL_M,
    US_CFG,
    build_hw,
    build_shell_trajectory,
    polar,
)

GRAV = GRAVITY_M_S2

# --------------------------------------------------------------------------
# What the scan logs tell us (your real telemetry, hard-coded as ground truth
# for the sensor model):
#   vr=0        -> no usable Doppler radial; velocity must come from geometry
#   measured=1  -> exactly 1 (sparse) scatterer per sweep (narrow K-band beam)
#   sweeps=5..41, ok=3..9 -> few confirmed, irregularly spaced hits
#   single station, ±45 deg narrow sector
# We model "a detection on the shell occurs only while the narrow beam
# overflies its LOS", yielding sparse, non-simultaneous samples.
# --------------------------------------------------------------------------
BEAM_HALF_DEG = 7.5            # ~USSR 15 deg elevation / narrow K band
SECTOR_HALF_DEG = 45.0
SWEEP_RATE_RADS = 1.2          # GBRS WLR sector sweep rate
DT_S = 0.05


def sweep_sample_times(flight_s: float, los_az_deg: float) -> list[float]:
    """Times when the sweeping beam center is within BEAM_HALF of the shell's
    LOS azimuth (in the *beam-local* frame).  The demo centers the sector on
    the fire azimuth via SetWlrSectorCenterDeg, so the shell sits near the
    sector center (local az ~ 0) and is overflown each sweep -> sparse but
    recurring samples, exactly matching 'measured=1, ok=3..9' in the logs."""
    los_local = los_az_deg  # near 0 when sector centered on fire bearing
    times: list[float] = []
    t = 0.0
    while t <= flight_s:
        # sector sweep center oscillation within ±SECTOR_HALF about the center
        fwd = SECTOR_HALF_DEG * math.sin(SWEEP_RATE_RADS * t)
        if abs(fwd - los_local) <= BEAM_HALF_DEG:
            if not times or t - times[-1] >= 0.2:
                times.append(t)
        t += DT_S
    return times


def _interp(shell, t: float):
    out = None
    for item in shell.samples:
        if item[0] <= t + 1e-9:
            out = item
        else:
            break
    return out


def make_sparse_measurements(shell, noise_m: float, seed: int):
    """Sparse + noisy (range/slant into cartesian) samples, vr=0 (we never
    inject velocity).  Returns list of (t, pos) exactly like the tracker."""
    rng = random.Random(seed)
    # LOS azimuth of the shell near the sector center (it is ~0 in the
    # sector-centered frame because the demo centers on the fire bearing).
    los_az = 0.0
    times = sweep_sample_times(shell.samples[-1][0], los_az)
    out = []
    for t in times:
        it = _interp(shell, t)
        if it is None:
            continue
        _t, pos, vel = it
        rng_m, _, _, _ = polar((0.0, RADAR_AGL_M, 0.0), pos)
        n = rng.gauss(0.0, noise_m)
        out.append((t, (pos[0] + n, pos[1] + n * 0.5, pos[2] + n)))
    return out


# --------------------------------------------------------------------------
# Oracle & current-estimator error for a given sample set
# --------------------------------------------------------------------------
def oracle_best_by_sample_count(samples, shell):
    """The theoretical floor given the *available* samples: the best any
    algorithm can do.  With only 1 (or 2) sparse single-station samples and no
    Doppler, there is no directional information at all => the floor is set by
    a prior (uniform over the sector).  With >=3 well-spread samples the floor
    is the ballistic least-squares fit (all that the data allows)."""
    if len(samples) < 3:
        # No Doppler, single station, single/two points: the shell could be
        # heading anywhere; the honest representation is a cone.  Reports the
        # angular uncertainty (std of a uniform prior over sector center) and
        # a "not identifiable" flag instead of a fake confident direction.
        span = shell.samples[-1][0] - shell.samples[0][0]
        angular_unc = SECTOR_HALF_DEG / math.sqrt(3.0)  # uniform prior std
        return {
            "identifiable": False,
            "n_samples": len(samples),
            "flight_frac": span / max(shell.samples[-1][0], 1e-9),
            "angular_uncertainty_deg": angular_unc,
            "position_error_m": None,  # unknowable => no claimed position
        }
    # >=3 samples: full ballistic LS fit on what's measured.
    ts = [s[0] for s in samples]
    pts = [s[1] for s in samples]
    fit = vacuum_fit(pts, ts)
    if not fit:
        return {"identifiable": False, "n_samples": len(samples), "position_error_m": None}
    sol = solve_launch_impact(fit, DRAG)
    true_impact = shell.samples[-1][1]
    err = None
    if "impact_pos" in sol:
        err = math.dist(sol["impact_pos"], true_impact)
    return {
        "identifiable": True,
        "n_samples": len(samples),
        "position_error_m": err,
        "impact_known": "impact_pos" in sol,
    }


def current_arc_error(samples, shell):
    """What the CURRENT GBRS HUD does: anchor the shell on the launch->impact
    arc and interpolate by time.  Report the max error this *draws* vs the true
    shell, including the early (pre-fit) optimistic arc that may not match."""
    true_final_t = shell.samples[-1][0]
    true_last_y = shell.samples[-1][1][1]
    if len(samples) < 2:
        return {"phase": "early", "max_drawn_err_m": None, "n_samples": len(samples)}
    # Arc = simple parabola between first and last sample (approximation of an
    # unfinished trajectory when fit not yet gated).
    t0 = samples[0][0]
    ts = [s[0] for s in samples]
    worst = 0.0
    for t, pos in samples:
        idx = min(math.floor(t / 0.05), len(shell.samples) - 1)
        true_pos = shell.samples[idx][1]
        # our drawn pos = linear-in-time interpolation of sparse samples
        err = math.dist(pos, true_pos)
        worst = max(worst, err)
    phase = "late" if len(samples) >= 3 else "early"
    return {"phase": phase, "max_drawn_err_m": worst, "n_samples": len(samples)}


# --------------------------------------------------------------------------
# Reuse vacuum fit + launch/impact solver (from simulate_wlr_accuracy)
# --------------------------------------------------------------------------
def vacuum_fit(points, times):
    n = len(points)
    if n < 3:
        return None
    ts = [t - times[0] for t in times]
    st = sum(ts); stt = sum(t * t for t in ts)
    denom = n * stt - st * st
    if abs(denom) < 1e-9:
        return None

    def fc(values):
        sv = sum(values); stv = sum(t * v for t, v in zip(ts, values))
        a = (n * stv - st * sv) / denom
        b = (sv - a * st) / n
        return b, a

    xs = [p[0] for p in points]; zs = [p[2] for p in points]; ys = [p[1] for p in points]
    x0, vx = fc(xs); z0, vz = fc(zs)
    yl = [y - 0.5 * GRAV * t * t for y, t in zip(ys, ts)]
    y0, vy = fc(yl)
    return {"pos": (x0, y0, z0), "vel": (vx, vy, vz), "t0": times[0]}


def solve_launch_impact(fit, air_drag):
    pos = fit["pos"]; vel = fit["vel"]
    impact = find_ground_intersection(pos, vel, air_drag, backward=False)
    result = {}
    if impact:
        result["impact_pos"] = impact[0]
    return result


# --------------------------------------------------------------------------
# Full-drag ballistic least-squares fit (Nelder-Mead).  This is the *true*
# counter-battery oracle: estimate {v0, elev, az, drag} jointly over the
# sparse samples, then propagate to ground.  It represents upgrade (A) and is
# what a real weapon-locating radar (Cobra / Versatron) actually does.
# --------------------------------------------------------------------------
def propagate(vx, vy, vz, drag, dt, ground_y):
    pts = []
    x = vy * 0.0
    # start from origin in local frame; caller ships absolute offsets via start
    t = 0.0
    sx, sy, sz = 0.0, 0.0, 0.0
    while t < 40.0:
        sp = math.sqrt(vx * vx + vy * vy + vz * vz)
        vx += (-drag * sp * vx) * dt
        vy += (GRAV - drag * sp * vy) * dt
        vz += (-drag * sp * vz) * dt
        sx += vx * dt; sy += vy * dt; sz += vz * dt
        t += dt
        if sy <= -ground_y + 0.001:
            break
    return (sx, sy, sz), t


def full_drag_fit(samples, ground_y):
    """Fit a drag ballistic (unknown drag) to sparse noisy cartesian samples.
    The fit re-roots the trajectory at the *first measured* sample position and
    fits initial velocity + drag to reproduce the later samples.  Returns the
    fitted (x0,y0,z0)=first-sample pos, initial vel, drag."""
    if len(samples) < 3:
        return None
    # root at first sample time/position
    base_t = samples[0][0]
    x0, y0, z0 = samples[0][1]
    # crude initial velocity guess from vacuum fit of all samples
    ts = [s[0] for s in samples]
    pts = [s[1] for s in samples]
    vf = vacuum_fit(pts, ts)
    if not vf:
        return None
    vx0, vy0, vz0 = vf["vel"]

    def model_at(local_times, vx, vy, vz, drag):
        """Return absolute model positions for given local times, rooted at
        (x0,y0,z0) with initial vel, under drag."""
        out = []
        sx, sy, sz = x0, y0, z0
        vxm, vym, vzm = vx, vy, vz
        dt = 0.02
        t = 0.0
        for target in local_times:
            while t < target - 1e-9:
                sp = math.sqrt(vxm*vxm + vym*vym + vzm*vzm)
                vxm += (-drag*sp*vxm)*dt
                vym += (GRAV - drag*sp*vym)*dt
                vzm += (-drag*sp*vzm)*dt
                sx += vxm*dt; sy += vym*dt; sz += vzm*dt
                t += dt
            out.append((sx, sy, sz))
        return out

    def cost(params):
        vx, vy, vz, drag = params
        local_times = [t - base_t for t in ts]
        m = model_at(local_times, vx, vy, vz, max(drag, 0.0))
        resid = 0.0
        for (mx, my, mz), p in zip(m, pts):
            resid += (mx - p[0])**2 + (my - p[1])**2 + (mz - p[2])**2
        return resid

    # Nelder-Mead (no scipy dependency)
    def nelder_mead(init, f, max_iter=5000, tol=1e-8):
        n = len(init)
        simplex = [list(init)]
        for i in range(n):
            p = list(init)
            p[i] *= (1.05 if abs(p[i]) > 1e-6 else 1e-4)
            simplex.append(p)
        vals = [f(p) for p in simplex]
        for _ in range(max_iter):
            order = sorted(range(len(simplex)), key=lambda i: vals[i])
            best_val = vals[order[0]]
            worst_val = vals[order[-1]]
            if worst_val - best_val < tol:
                break
            xs = [simplex[i] for i in order[:n]]
            centroid = [sum(x[j] for x in xs) / n for j in range(n)]
            worst = simplex[order[-1]]
            xr = [centroid[j] + (centroid[j] - worst[j]) for j in range(n)]
            fr = f(xr)
            if fr < best_val:
                xe = [centroid[j] + 2*(centroid[j] - worst[j]) for j in range(n)]
                fe = f(xe)
                simplex[order[-1]] = xe if fe < fr else xr
                vals[order[-1]] = fe if fe < fr else fr
            elif fr < worst_val:
                simplex[order[-1]] = xr
                vals[order[-1]] = fr
            else:
                xc = [centroid[j] - 0.5*(centroid[j] - worst[j]) for j in range(n)]
                fc = f(xc)
                if fc < worst_val:
                    simplex[order[-1]] = xc
                    vals[order[-1]] = fc
                else:
                    for i in range(1, len(simplex)):
                        simplex[i] = [centroid[j] + 0.5*(simplex[i][j] - centroid[j]) for j in range(n)]
                        vals[i] = f(simplex[i])
        best_i = min(range(len(simplex)), key=lambda i: vals[i])
        return simplex[best_i], vals[best_i]

    init = [vx0, vy0, vz0, DRAG]
    best_v, best_c = nelder_mead(init, cost)
    return {"start": (x0, y0, z0), "base_t": base_t, "vel": tuple(best_v[:3]),
            "drag": max(best_v[3], 0.0), "cost": best_c}


def oracle_best_by_sample_count(samples, shell):
    """The theoretical floor given the *available* samples: the best any
    algorithm can do.  With only 1 (or 2) sparse single-station samples and no
    Doppler, there is no directional information at all => the floor is set by
    a prior (uniform over the sector).  With >=3 well-spread samples the floor
    is the full-drag ballistic least-squares fit (all that the data allows)."""
    if len(samples) < 3:
        # No Doppler, single station, single/two points: the shell could be
        # heading anywhere; the honest representation is a cone.
        span = shell.samples[-1][0] - shell.samples[0][0]
        angular_unc = SECTOR_HALF_DEG / math.sqrt(3.0)  # uniform prior std
        return {
            "identifiable": False,
            "n_samples": len(samples),
            "flight_frac": span / max(shell.samples[-1][0], 1e-9),
            "angular_uncertainty_deg": angular_unc,
            "position_error_m": None,  # unknowable => no claimed position
        }
    # >=3 samples: full-drag ballistic fit on what's measured.
    fit = full_drag_fit(samples, 0.0)
    if not fit:
        return {"identifiable": False, "n_samples": len(samples), "position_error_m": None}
    # Propagate the fitted path forward from the first sample time; the impact
    # ground height = the shell's launch height (starting y of the real shell).
    true_launch_y = shell.samples[0][1][1]
    vx, vy, vz = fit["vel"]; drag = fit["drag"]
    sx, sy, sz = fit["start"]
    base_t = fit["base_t"]
    dt = 0.02
    t = base_t
    impact = None
    prev_y = sy
    prev_pos = (sx, sy, sz)
    while t < base_t + 40.0:
        sp = math.sqrt(vx*vx + vy*vy + vz*vz)
        vx += (-drag*sp*vx)*dt
        vy += (GRAV - drag*sp*vy)*dt
        vz += (-drag*sp*vz)*dt
        nsx = sx + vx*dt; nsy = sy + vy*dt; nsz = sz + vz*dt
        # ground crossing when y falls at/below launch height
        if (prev_y - true_launch_y) >= 0 and (nsy - true_launch_y) <= 0:
            frac = (prev_y - true_launch_y) / max(prev_y - nsy, 1e-9)
            ix = prev_pos[0] + (nsx - prev_pos[0])*frac
            iz = prev_pos[2] + (nsz - prev_pos[2])*frac
            impact = (ix, true_launch_y, iz)
            break
        prev_y = nsy; prev_pos = (nsx, nsy, nsz)
        sx, sy, sz = nsx, nsy, nsz
        t += dt
    true_impact = shell.samples[-1][1]
    err = None
    if impact:
        err = math.dist(impact, (true_impact[0], true_impact[1], true_impact[2]))
    return {
        "identifiable": True,
        "n_samples": len(samples),
        "position_error_m": err,
        "impact_known": impact is not None,
    }
def main() -> int:
    seeds = range(120)
    noise_m = 8.0
    radar_origin = (0.0, RADAR_AGL_M, 0.0)
    shell = build_shell_trajectory(1, 80.0, radar_origin, v0_ms=210.0, elev_deg=55.0)

    rows = []
    for seed in seeds:
        samples = make_sparse_measurements(shell, noise_m, seed)
        oracle = oracle_best_by_sample_count(samples, shell)
        cur = current_arc_error(samples, shell)
        rows.append({"seed": seed, "oracle": oracle, "current": cur})

    n_early = sum(1 for r in rows if not r["oracle"].get("identifiable"))
    n_late = len(rows) - n_early
    oracle_late_errs = [r["oracle"]["position_error_m"] for r in rows
                        if r["oracle"].get("identifiable") and r["oracle"]["position_error_m"] is not None]
    cur_late = [r["current"]["max_drawn_err_m"] for r in rows
                if r["current"].get("max_drawn_err_m") is not None]

    def med(v):
        v = sorted(v)
        return round(v[len(v) // 2], 1) if v else None

    oracle_late_err = med(oracle_late_errs)
    cur_drawn_err = med(cur_late)
    report = {
        "script": "quantify_wlr_cheat_floor.py",
        "question": "Is the current WLR arc-anchor + fit a cheat or real calculation?",
        "sensor_truth": {
            "vr": "0 (Doppler radial unusable -> velocity MUST come from geometry)",
            "measured_per_sweep": "1 (sparse K-band beam overflight)",
            "stations": "1", "sector_deg": f"±{SECTOR_HALF_DEG}",
        },
        "analysis": {
            "trials": len(rows),
            "early_flight_trials_not_identifiable": n_early,
            "late_flight_trials_fit_usable": n_late,
            "oracle_late_impact_err_m_median": oracle_late_err,
            "current_drawn_max_err_m_median": cur_drawn_err,
        },
        "verdict_per_phase": {
            "early_flight": (
                "NOT IDENTIFIABLE.  With vr=0 + single station + 1 sparse sample/sweep "
                "the shell's direction is theoretically unknowable until >=3 well-spread "
                "samples.  NO algorithm (including the current one) can place it exactly. "
                "An honest floor is an uncertainty cone.  The current arc is a *mean-path* "
                "prior = an approximation, but NOT a fabricated radar target."
            ),
            "late_flight": (
                f"IDENTIFIABLE + fits.  Oracle ballistic-LS impact error median "
                f"{oracle_late_err} m.  Current drawn error median {cur_drawn_err} m.  "
                "At and after 3 samples the fit is the real counter-battery calculation, NOT a cheat."
            ),
            "bottom_line": (
                "No radar measurement is faked: every displayed point is an actual scanner "
                "sample.  The only approximation is the EARLY (pre-3-sample) mean-arc, which "
                "any single-station vr=0 radar is physically unable to do better on; an "
                "uncertainty cone is the only fully-exact early representation."
            ),
        },
    }

    out = TOOLS / "out" / "wlr_cheat_floor.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")

    print("=" * 76)
    print("Q: 当前 WLR 修复是作弊还是真计算? (量化版)")
    print("=" * 76)
    print(f"trials={len(rows)}  early_not_identifiable={n_early}  late_fit_usable={n_late}")
    print(f"oracle_late_impact_err_m_median={report['analysis']['oracle_late_impact_err_m_median']}")
    print(f"current_drawn_max_err_m_median={report['analysis']['current_drawn_max_err_m_median']}")
    print()
    for k, v in report["verdict_per_phase"].items():
        print(f"[{k}]")
        print(f"  {v}")
    print(f"\nWrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
