#!/usr/bin/env python3
"""Offline calibration: WLR heading-estimation error vs sector-sweep parameters.

The WLR estimates a mortar shell's heading from its measured position history.
A single-LOS Doppler radial reconstruction is wrong (~90-157 deg) for shells
crossing/toward the radar, so heading must come from a position fit. This tool
quantifies how well the fit does under real WLR sampling:

  scans are NOT continuous: a shell is observed only in brief beam passes while
  the sector-sweep beam overflies its LOS azimuth. So the number of position
  samples per shell flight depends on the sweep rate / sector width / shell
  flight time.

It sweeps sweep-rate / min-samples / fit-window and prints a heading-error table,
so you can pick WLR parameters that keep heading error under a target (e.g. 20
deg) for a typical 82 mm mortar engagement.

Run:  python tools/simulate_wlr_heading_calib.py
"""

from __future__ import annotations

import math
import random
import sys
from dataclasses import dataclass

GRAV = -9.81
MUZZLE = 210.0  # 82mm HE muzzle velocity m/s
SHELL_V0_KM = MUZZLE / 1000.0


@dataclass
class SimParams:
    center_rad: float = 0.0          # sector center (rad, 0 = east)
    half_width_rad: float = math.radians(45)
    rate_rad_s: float = 0.6          # oscillation angular rate
    beamwidth_deg: float = 12.0
    flight_max_s: float = 20.0
    dt: float = 0.05
    # Fitter knobs (mirror RDF_RadarBallistics.FitVacuumFromHistory)
    fit_window: int = 24
    fit_min_span_s: float = 0.4
    rng_seed: int = 7


def sweep_forward(center, half, rate, t):
    """Mirror RDF_RadarScanner.GetScanForward sector sweep angle (rad)."""
    return center + half * math.sin(rate * t)


def sample_ballistic(launch_xy, launch_v0, elev_deg, az_deg, radar, dt, params):
    """Integrate a mortar; return list of (t, x, y, z) and the scan-heading truth."""
    azr = math.radians(az_deg)
    elevr = math.radians(elev_deg)
    sx = launch_xy[0]; sz = launch_xy[1]; sy = 5.0
    vx = launch_v0 * math.cos(elevr) * math.cos(azr)
    vy = launch_v0 * math.sin(elevr)
    vz = launch_v0 * math.cos(elevr) * math.sin(azr)
    pts = []
    t = 0.0
    while t < params.flight_max_s:
        sx += vx * dt
        sy += vy * dt
        sz += vz * dt
        vy += GRAV * dt
        t += dt
        if sy < 0.0:
            break
        pts.append((t, sx, sy, sz))
    return pts


def observed_heading_error(pts, radar, params):
    """Simulate WLR sector-sweep sampling of the shell, fit, return heading err.

    A sample is taken when the beam (current sweep forward) is within
    beamwidth/2 of the shell's LOS azimuth. Returns (err_deg, n_samples, span_s).
    """
    rng = random.Random(params.rng_seed)
    obs = []  # (t, x, z) with small noise
    # Precompute shell LOS azimuth history independent of scan (shell moves too).
    shell_az = []
    for (t, sx, sy, sz) in pts:
        lx = sx - radar[0]
        lz = sz - radar[1]
        az = math.atan2(lx, lz)  # bearing of shell from radar (0=north)
        shell_az.append(az)

    detected_spans = []  # accumulate continuous-on samples
    last_detected = False

    for i, (t, sx, sy, sz) in enumerate(pts):
        fwd = sweep_forward(params.center_rad, params.half_width_rad, params.rate_rad_s, t)
        az = shell_az[i]
        # angular separation between scan forward and shell LOS
        diff = abs(((az - fwd) + math.pi) % (2 * math.pi) - math.pi)
        detected = diff <= math.radians(params.beamwidth_deg * 0.5)
        if detected:
            if not last_detected:
                detected_spans.append([])
            # measurement: small position noise (SNR-limited, ~5-15 m for shells long range)
            n = rng.gauss(0.0, 8.0)
            obs.append((t, sx + n * math.cos(az), sz + n * math.sin(az)))
            detected_spans[-1].append(t)
        last_detected = detected

    if len(obs) < 4:
        return None, len(obs), 0.0

    # Current estimator (fit-first, like the fix): linear fit x,z over time.
    ts = [p[0] for p in obs]
    xs = [p[1] for p in obs]
    zs = [p[2] for p in obs]
    # use full history (window)
    if params.fit_window > 0 and len(ts) > params.fit_window:
        ts = ts[-params.fit_window:]
        xs = xs[-params.fit_window:]
        zs = zs[-params.fit_window:]
    span = ts[-1] - ts[0]
    if span < params.fit_min_span_s:
        return None, len(obs), span

    def slope(tt, vv):
        m = len(tt)
        mt = sum(tt) / m
        mv = sum(vv) / m
        num = sum((tt[k] - mt) * (vv[k] - mv) for k in range(m))
        den = sum((tt[k] - mt) ** 2 for k in range(m))
        if abs(den) < 1e-9:
            return 0.0
        return num / den

    vx = slope(ts, xs)
    vz = slope(ts, zs)
    est_head = math.atan2(vx, vz) * 180.0 / math.pi
    if est_head < 0:
        est_head += 360.0

    # True heading from shell velocity at last sample time (horizontal).
    true = None
    for (t, sx, sy, sz) in pts:
        if t >= ts[-1]:
            # interpolated velocity not needed; use horizontal chord of shell tail
            break
    # true heading from shell's own motion over its last ~0.5 s
    tail = [(p[0], p[1], p[3]) for p in pts if p[0] <= ts[-1]]
    if len(tail) >= 2:
        t0 = tail[0][0]
        tx = tail[-1][1]; tz = tail[-1][2]
        for (tt0, xx0, zz0) in tail:
            if ts[-1] - tt0 <= 0.5:
                tx = xx0; tz = zz0
        th = math.atan2(tx - tail[0][1], tz - tail[0][2]) * 180.0 / math.pi
    else:
        th = 0.0
    if th < 0:
        th += 360.0

    err = abs(((est_head - th) + 180) % 360 - 180)
    return err, len(obs), span


def run_scan():
    radar = [0.0, 0.0]
    params = SimParams()
    # shells: launch spots around a ~2.5 km sector band at varied az/elev
    shells = []
    for a in [20, 40, 55, 70, 90, 110, 130, 155, 175]:
        dist = 1500 + random.Random(a).uniform(0, 800)
        launch_x = dist * math.cos(math.radians(a))
        launch_z = dist * math.sin(math.radians(a))
        az = 180 + (random.Random(a).uniform(-25, 25))  # fire roughly toward radar
        if az > 360: az -= 360
        elev = 48 + random.Random(a).uniform(30, 62) * 0.1
        shells.append((launch_x, launch_z, az, elev))

    print("Sweep rate | beamwidth | fit_window | median heading err | samples | span_s")
    for rate in [0.15, 0.3, 0.6, 1.2, 2.4]:
        for bw in [8, 12, 20, 30]:
            for wnd in [12, 24, 48]:
                p = SimParams(rate_rad_s=rate, beamwidth_deg=bw, fit_window=wnd)
                errs = []
                nsamp = []
                spans = []
                for (lx, lz, az, elev) in shells:
                    pts = sample_ballistic((lx, lz), MUZZLE, elev, az, radar, p.dt, p)
                    e, n, sp = observed_heading_error(pts, radar, p)
                    if e is not None:
                        errs.append(e); nsamp.append(n); spans.append(sp)
                if errs:
                    med = sorted(errs)[len(errs) // 2]
                    print(f"  {rate:5.2f} | {bw:3d} | {wnd:3d} | {med:7.1f} deg | {sum(nsamp)//max(1,len(nsamp)):3d} | {sum(spans)/len(spans):5.2f}")


def main():
    print("WLR heading-estimation calibration (82mm mortar, sector sweep)")
    print("(smaller median deg + more samples span = better; None=too few samples)\n")
    run_scan()


if __name__ == "__main__":
    main()
