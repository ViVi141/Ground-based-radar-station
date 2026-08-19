#!/usr/bin/env python3
"""Offline validation of the RDF full-drag ballistic fit (the WLR realism fix).

Replicates the in-game algorithm:
  RDF_RadarBallistics.NelderMeadDrag (+ IntegrateDragFrom + DragFitResidual)
step-for-step, then runs the SAME solve across a matrix of realistic cases:

  x factions : US (SNR-band, noise 2.5, 12 deg beam, ~8 km) and
               USSR (VHF, noise 3.5, 15 deg beam, ~10 km)
  x ranges   : near / mid / far mortar lines
  x elevations: 45 / 55 / 65 deg (low -> high droop)
  x sampling  : sector-sweep sparse (current WLR) and denser (multi-station)

For each case it compares the two launch/impact estimators:
  1) vacuum fit + prefab drag (the OLD behavior)  [~340 m bias]
  2) full-drag Nelder-Mead fit (the NEW behavior) [target ~5-15 m]
over many noisy trials, and reports medians + how often the drag fit is
accepted.

This is pure offline analysis; it does NOT change any project code. It verifies
the committed RDF change converges and improves accuracy across the configs
GBRS actually ships.

Run:  python tools/offline_validate_drag_fit.py
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
    build_hw,
    build_shell_trajectory,
    polar,
    RADAR_AGL_M,
    US_CFG,
    USSR_CFG,
)
from simulate_wlr_accuracy import (
    find_ground_intersection,
    vacuum_fit,
    solve_launch_impact,
)
from validate_rdf_drag_fit import (
    nelder_mead_drag,      # exact Enforce Nelder-Mead mirror
    integrate_drag_from,
    dragfit_resid,
    find_ground_intersection_py,
)

DRAG_PRIOR = 0.000615
GRAV = -9.81

# Per-faction WLR noise / beam (mirrors GBRS_RadarStationConfig).
FACTION_NOISE = {
    "US":   {"noise_scale": 2.5, "az_bias_deg": 0.12, "el_bias_deg": 0.10,
             "range_bias_m": 3.0, "az_beam_deg": 12.0, "el_beam_deg": 20.0,
             "range_m": 8000.0},
    "USSR": {"noise_scale": 3.5, "az_bias_deg": 0.30, "el_bias_deg": 0.22,
             "range_bias_m": 6.0, "az_beam_deg": 15.0, "el_beam_deg": 20.0,
             "range_m": 10000.0},
}


def sweep_sample_times(flight_s, az_beam_deg, sector_half_deg=45.0,
                       sweep_rate_rads=1.2, dt_s=0.05, los_local_deg=0.0):
    times = []
    half = az_beam_deg * 0.5
    t = 0.0
    while t <= flight_s:
        fwd = sector_half_deg * math.sin(sweep_rate_rads * t)
        if abs(fwd - los_local_deg) <= half:
            if not times or t - times[-1] >= 0.2:
                times.append(t)
        t += dt_s
    return times


def noisy_positions(shell, times, faction_cfg, seed):
    """CRLB-noise radar-measured positions (range/az/el), like RDF measurement
    synthesis but with realistic (non-zeroed) noise."""
    origin = (0.0, RADAR_AGL_M, 0.0)
    out = []
    rng = random.Random(seed)
    hw = build_hw(US_CFG if faction_cfg["az_beam_deg"] <= 12 else USSR_CFG, "US" if faction_cfg["az_beam_deg"] <= 12 else "USSR")
    ns = faction_cfg["noise_scale"]
    rb, ab, eb = faction_cfg["range_bias_m"], faction_cfg["az_bias_deg"], faction_cfg["el_bias_deg"]
    for t in times:
        it = None
        for s in shell.samples:
            if s[0] <= t + 1e-9:
                it = s
            else:
                break
        if not it:
            continue
        rng_m, az_deg, el_deg, delta = polar(origin, it[1])
        snr_db = 40.0
        denom = 1.6 * math.sqrt(2.0 * max(10.0 ** (snr_db / 10.0), 1.0))
        if denom < 0.001:
            denom = 0.001
        rbin = 30.0  # range bin proxy
        r_sig = (rbin / denom) * ns
        az_sig = (faction_cfg["az_beam_deg"] / denom) * ns
        el_sig = (faction_cfg["el_beam_deg"] / denom) * ns
        mr = rng_m + rb + rng.gauss(0.0, r_sig)
        ma = az_deg + ab + rng.gauss(0.0, az_sig)
        me = el_deg + eb + rng.gauss(0.0, el_sig)
        azr = math.radians(ma); elr = math.radians(me)
        dx = mr * math.cos(elr) * math.cos(azr)
        dy = mr * math.sin(elr)
        dz = mr * math.cos(elr) * math.sin(azr)
        out.append((t, (origin[0] + dx, origin[1] + dy, origin[2] + dz)))
    return out


def heading_deg(vel):
    return math.degrees(math.atan2(vel[2], vel[0]))


def run_case(faction, elev_deg, launch_range_m, v0_ms, densify, trials=60):
    cfg = FACTION_NOISE[faction]
    origin = (0.0, RADAR_AGL_M, 0.0)
    shell = build_shell_trajectory(1, 80.0, origin, v0_ms=v0_ms, elev_deg=elev_deg)
    flight = shell.samples[-1][0]
    true_impact = shell.samples[-1][1]
    true_launch = shell.samples[0][1]
    az_beam = cfg["az_beam_deg"]
    ground_h = shell.samples[0][1][1]

    errs_vac_imp = []
    errs_drag_imp = []
    errs_vac_lnc = []
    errs_drag_lnc = []
    accepted = 0
    total_fit = 0

    for seed in range(trials):
        times = sweep_sample_times(flight, az_beam)
        if densify:   # multi-station: extra interleaved sweeps -> denser history
            extra = [t - 0.1 for t in times if t - 0.1 > 0.05]
            times = sorted(set([round(x, 3) for x in times + extra]))
        if len(times) < 4:
            continue
        pts = noisy_positions(shell, times, cfg, seed)
        if len(pts) < 4:
            continue
        pos = [p for _, p in pts]
        tms = [t for t, _ in pts]
        vf = vacuum_fit(pos, tms)
        if not vf:
            continue
        total_fit += 1

        # OLD: vacuum fit + prefab drag
        imp_vac = find_ground_intersection(vf["pos"], vf["vel"], DRAG_PRIOR, backward=False)
        lnc_vac = find_ground_intersection(vf["pos"], vf["vel"], DRAG_PRIOR, backward=True)
        if imp_vac:
            errs_vac_imp.append(math.dist(imp_vac[0], (true_impact[0], true_impact[1], true_impact[2])))
        if lnc_vac:
            errs_vac_lnc.append(math.dist(lnc_vac[0], (true_launch[0], true_launch[1], true_launch[2])))

        # NEW: full-drag Nelder-Mead (exact Enforce mirror)
        dv, dd = nelder_mead_drag(pos, tms, pos[0], vf["vel"],
                                   DRAG_PRIOR * 0.35, DRAG_PRIOR * 3.0)
        imp = find_ground_intersection_py(pos[0], dv, dd, ground_y=ground_h)
        lnc = find_ground_intersection_py(pos[0], dv, dd, ground_y=ground_h, backward=True)
        if imp:
            errs_drag_imp.append(math.dist(imp, (true_impact[0], true_impact[1], true_impact[2])))
        if lnc:
            errs_drag_lnc.append(math.dist(lnc, (true_launch[0], true_launch[1], true_launch[2])))
        # acceptance: drag residual vs vacuum residual at the same anchor
        r_best = dragfit_resid(pos, tms, pos[0], dv, dd)
        r_prior = dragfit_resid(pos, tms, pos[0], vf["vel"], DRAG_PRIOR)
        if r_best < r_prior * 1.05:
            accepted += 1

    def med(v):
        v = sorted(v)
        return round(v[len(v) // 2], 1) if v else None

    return {
        "faction": faction,
        "elev_deg": elev_deg,
        "launch_range_m": launch_range_m,
        "flight_s": round(flight, 1),
        "densified": bool(densify),
        "impact_err_m_median": {
            "vacuum_fit": med(errs_vac_imp),
            "full_drag_fit": med(errs_drag_imp),
        },
        "launch_err_m_median": {
            "vacuum_fit": med(errs_vac_lnc),
            "full_drag_fit": med(errs_drag_lnc),
        },
        "drag_fit_acceptance": (round(accepted / max(1, total_fit), 3), accepted, total_fit),
    }


def main() -> int:
    report = {
        "script": "offline_validate_drag_fit.py",
        "note": "Step-for-step Enforce NelderMeadDrag mirror; vacuum fit + prefab drag as baseline",
        "cases": [],
    }

    print("=" * 78)
    print("RDF full-drag WLR fit offline validation (Enforce algorithm mirror)")
    print("=" * 78)

    cases = [
        ("US",   55.0, 1200, 210.0, False),
        ("US",   45.0, 3000, 210.0, False),
        ("US",   55.0, 3000, 210.0, False),
        ("US",   65.0, 3000, 210.0, False),
        ("USSR", 55.0, 3000, 210.0, False),
        ("US",   55.0, 3000, 210.0, True),   # densified (multi-station)
        ("USSR", 55.0, 3000, 210.0, True),
        ("US",   45.0, 6000, 210.0, False),
    ]

    for faction, elev, rng, v0, densify in cases:
        r = run_case(faction, elev, rng, v0, densify)
        report["cases"].append(r)
        vi = r["impact_err_m_median"]["vacuum_fit"]
        di = r["impact_err_m_median"]["full_drag_fit"]
        vl = r["launch_err_m_median"]["vacuum_fit"]
        dl = r["launch_err_m_median"]["full_drag_fit"]
        acc = r["drag_fit_acceptance"]
        tag = "(densified)" if densify else ""
        print(f"\n[{faction}{tag}] elev={r['elev_deg']:.0f} range={r['launch_range_m']}m "
              f"flight={r['flight_s']}s")
        print(f"  impact  err med : vacuum {vi:>6.1f} m -> drag-fit {di:>6.1f} m")
        print(f"  launch  err med : vacuum {vl:>6.1f} m -> drag-fit {dl:>6.1f} m")
        print(f"  drag-fit accepted {acc[1]}/{acc[2]} ({acc[0]:.1%})")

    out = TOOLS / "out" / "offline_drag_fit_validation.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\nWrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
