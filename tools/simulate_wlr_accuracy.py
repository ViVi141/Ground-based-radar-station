#!/usr/bin/env python3
"""Offline WLR ballistic solution accuracy validation.

Simulates noisy radar measurements of a known 82 mm HE trajectory, fits the
same vacuum-parabola + AirDrag model used by RDF, and reports launch/impact
position error statistics.
"""

from __future__ import annotations

import argparse
import json
import math
import random
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from simulate_wlr_tracker import (
    AIR_DRAG_SHELL_82MM_HE,
    LAUNCH_HEIGHT_M,
    LAUNCH_RANGE_M,
    RADAR_AGL_M,
    SHELL_ELEV_DEG,
    SHELL_V0_MS,
    US_CFG,
    apply_measurement_noise,
    build_hw,
    build_shell_trajectory,
    polar,
    range_bin_m,
)

GRAVITY_M_S2 = -9.81
GROUND_Y_M = RADAR_AGL_M
MAX_ERR_REPORT = 500.0

def vacuum_fit(points, times):
    """Fit x(t), z(t), y(t)=y0+vy*t+0.5*g*t^2 using linear least squares."""
    n = len(points)
    if n < 3:
        return None
    t0 = times[0]
    ts = [t - t0 for t in times]
    sum_t = sum(ts)
    sum_tt = sum(t * t for t in ts)
    denom = n * sum_tt - sum_t * sum_t
    if abs(denom) < 1e-9:
        return None

    def fit_coord(values):
        sum_v = sum(values)
        sum_tv = sum(t * v for t, v in zip(ts, values))
        a = (n * sum_tv - sum_t * sum_v) / denom
        b = (sum_v - a * sum_t) / n
        return b, a

    xs = [p[0] for p in points]
    zs = [p[2] for p in points]
    ys = [p[1] for p in points]
    x0, vx = fit_coord(xs)
    z0, vz = fit_coord(zs)
    y_lin = [y - 0.5 * GRAVITY_M_S2 * t * t for y, t in zip(ys, ts)]
    y0, vy = fit_coord(y_lin)

    rms_sum = 0.0
    for p, t in zip(points, ts):
        hx = x0 + vx * t
        hz = z0 + vz * t
        hy = y0 + vy * t + 0.5 * GRAVITY_M_S2 * t * t
        rms_sum += (p[0] - hx) ** 2 + (p[1] - hy) ** 2 + (p[2] - hz) ** 2
    rms = math.sqrt(rms_sum / n)
    return {
        "pos": (x0, y0, z0),
        "vel": (vx, vy, vz),
        "t0": t0,
        "rms": rms,
        "n": n,
    }

def find_ground_intersection(pos, vel, air_drag, backward, ground_y=GROUND_Y_M, max_time_s=90.0, dt=0.02):
    p = list(pos)
    v = list(vel)
    step = -dt if backward else dt
    t = 0.0
    prev_p = p[:]
    prev_t = 0.0
    while abs(t) <= max_time_s:
        speed = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2)
        ax = -air_drag * speed * v[0]
        ay = GRAVITY_M_S2 - air_drag * speed * v[1]
        az = -air_drag * speed * v[2]
        v[0] += ax * step
        v[1] += ay * step
        v[2] += az * step
        p[0] += v[0] * step
        p[1] += v[1] * step
        p[2] += v[2] * step
        t += step

        h0 = prev_p[1] - ground_y
        h1 = p[1] - ground_y
        if (h0 <= 0.0 and h1 >= 0.0) or (h0 >= 0.0 and h1 <= 0.0):
            if abs(h1 - h0) < 1e-9:
                frac = 0.0
            else:
                frac = h0 / (h0 - h1)
            pos_cross = (
                prev_p[0] + (p[0] - prev_p[0]) * frac,
                ground_y,
                prev_p[2] + (p[2] - prev_p[2]) * frac,
            )
            time_cross = prev_t + (t - prev_t) * frac
            return pos_cross, time_cross

        prev_p = p[:]
        prev_t = t
    return None

def solve_launch_impact(fit, air_drag):
    pos = fit["pos"]
    vel = fit["vel"]
    impact = find_ground_intersection(pos, vel, air_drag, backward=False)
    launch = find_ground_intersection(pos, vel, air_drag, backward=True)
    result = {"fit_rms_m": fit["rms"], "hit_count": fit["n"]}
    if impact:
        result["impact_pos"] = impact[0]
        result["impact_time_offset_s"] = impact[1]
    if launch:
        result["launch_pos"] = launch[0]
        result["launch_time_offset_s"] = launch[1]
    return result

def make_measurements(shell, radar_origin, hw, noise_scale, range_bias, az_bias, el_bias, rng, every=1):
    """Return (times, points) for every-th trajectory sample with optional noise."""
    times = []
    points = []
    for idx, (t, pos, vel) in enumerate(shell.samples):
        if idx % every != 0:
            continue
        rng_m, az_deg, el_deg, delta = polar(radar_origin, pos)
        mr, ma, me, mpos = apply_measurement_noise(
            hw, radar_origin, rng_m, az_deg, el_deg, 30.0,
            noise_scale, range_bias, az_bias, el_bias, rng,
        )
        times.append(t)
        points.append(mpos)
    return times, points

def run_trial(shell, radar_origin, hw, noise_scale, every, seed):
    rng = random.Random(seed)
    times, points = make_measurements(
        shell, radar_origin, hw, noise_scale, 5.0, 0.3, 0.22, rng, every,
    )
    # Use a window near the middle of the flight, like a real track fit.
    mid = len(points) // 2
    start = max(0, mid - 10)
    end = min(len(points), mid + 10)
    if end - start < 3:
        return None
    fit = vacuum_fit(points[start:end], times[start:end])
    if not fit or fit["rms"] > 80.0:
        return None
    sol = solve_launch_impact(fit, AIR_DRAG_SHELL_82MM_HE)
    return {"fit": fit, "solution": sol}

def main() -> int:
    parser = argparse.ArgumentParser(description="WLR ballistic solution accuracy validation")
    parser.add_argument("--trials", type=int, default=200)
    parser.add_argument("--noise", type=float, default=0.0)
    parser.add_argument("--out", default=str(TOOLS / "out" / "wlr_accuracy_validation.json"))
    args = parser.parse_args()

    radar_origin = (0.0, RADAR_AGL_M, 0.0)
    hw = build_hw(US_CFG, "US")
    # True shell: same launch used by the WLR demo.
    shell = build_shell_trajectory(1, 80.0, radar_origin)
    true_launch = shell.samples[0][1]
    true_impact = shell.samples[-1][1]

    launch_errors = []
    impact_errors = []
    rms_values = []
    solved = 0
    for seed in range(args.trials):
        trial = run_trial(shell, radar_origin, hw, args.noise, every=2, seed=seed)
        if not trial:
            continue
        sol = trial["solution"]
        if "launch_pos" in sol:
            lp = sol["launch_pos"]
            launch_errors.append(math.dist(lp, true_launch))
        if "impact_pos" in sol:
            ip = sol["impact_pos"]
            impact_errors.append(math.dist(ip, true_impact))
        rms_values.append(trial["fit"]["rms"])
        solved += 1

    def stats(values):
        if not values:
            return {"count": 0}
        values_sorted = sorted(values)
        n = len(values_sorted)
        return {
            "count": n,
            "mean_m": round(sum(values_sorted) / n, 1),
            "median_m": round(values_sorted[n // 2], 1),
            "p90_m": round(values_sorted[int(n * 0.9) - 1], 1),
            "max_m": round(values_sorted[-1], 1),
        }

    report = {
        "script": "simulate_wlr_accuracy.py",
        "description": "WLR launch/impact solution accuracy from noisy measurements",
        "trials": args.trials,
        "noise_scale": args.noise,
        "true_launch": list(true_launch),
        "true_impact": list(true_impact),
        "solved_trials": solved,
        "launch_error_m": stats(launch_errors),
        "impact_error_m": stats(impact_errors),
        "fit_rms_m": stats(rms_values),
    }

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print("=" * 72)
    print("WLR ballistic solution accuracy offline validation")
    print("=" * 72)
    print(f"trials={args.trials} solved={solved} noise_scale={args.noise}")
    print(f"true_launch={tuple(round(x,1) for x in true_launch)}")
    print(f"true_impact={tuple(round(x,1) for x in true_impact)}")
    print(f"launch_error_m={report['launch_error_m']}")
    print(f"impact_error_m={report['impact_error_m']}")
    print(f"fit_rms_m={report['fit_rms_m']}")
    print(f"Wrote {out_path}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
