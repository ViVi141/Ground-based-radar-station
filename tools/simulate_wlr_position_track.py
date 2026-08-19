#!/usr/bin/env python3
"""Quantify WLR live-track position instability: alpha-beta filter vs ballistic-arc lerp.

Symptom report: shell live position jitters / reverses / overshoots the impact
point. Hypothesis: the RDF alpha-beta Cartesian tracker (m_FilteredPosition)
uses an almost-zero Doppler radial velocity (vr=0) plus sparse irregular
samples, so its velocity integrates measurement noise and the filtered position
lags / overshoots / reverses. A WLR ballistic fix already yields launch->impact;
interpolating the live shell along that arc by time cannot reverse or overshoot.

This tool simulates both under WLR-like sensing and prints position error stats.

Run:  python tools/simulate_wlr_position_track.py
"""

from __future__ import annotations

import math
import random

GRAV = -9.81
MUZZLE = 210.0


def ballistic(radar, v0, elev, az, dt=0.02, max_s=22.0):
    """Integrate a mortar from launch; return list of (t, x, y, z)."""
    azr = math.radians(az)
    er = math.radians(elev)
    x, y, z = radar[0], 5.0, radar[1]
    vx = v0 * math.cos(er) * math.cos(azr)
    vy = v0 * math.sin(er)
    vz = v0 * math.cos(er) * math.sin(azr)
    pts = []
    t = 0.0
    while t < max_s:
        x += vx * dt; y += vy * dt; z += vz * dt
        vy += GRAV * dt
        t += dt
        if y < 0.0:
            break
        pts.append((t, x, y, z))
    return pts


def meas_times(flight_s, sweep_rate, beamwidth_deg, half_deg):
    """Samples of the shell while a sector-sweep beam overflies its LOS azimuth."""
    times = []
    last_t = -1e9
    t = 0.0
    while t < flight_s:
        fwd = half_deg * math.sin(sweep_rate * t)  # sector sweep about 0
        # approximate shell LOS az ~ constant (launched toward radar / crossing)
        los_az = 0.0  # sample when beam center near shell az; use time-derivative
        diff = abs(fwd - los_az)
        if diff <= beamwidth_deg * 0.5:
            if t - last_t >= 0.2:  # ~25 Hz max update
                times.append(t)
                last_t = t
        t += 0.02
    return times


def alpha_beta_track(pts_out, meas):
    """Run the RDF alpha-beta cartesian tracker on (t,x,y,z) measures.

    vr=0 -> velocity seed ~ 0; returns filtered positions + reversals.
    """
    rng = random.Random(3)
    alpha, beta = 0.6, 0.15
    fx, fy, fz = meas[0][1], meas[0][2], meas[0][3]
    vx, vy, vz = 0.0, 0.0, 0.0  # vr=0
    out = [(meas[0][0], fx, fy, fz)]
    rev = 0
    for k in range(1, len(meas)):
        t, mx, my, mz = meas[k]
        dt = max(0.001, t - out[-1][0])
        # prediction
        px = fx + vx * dt
        pz = fz + vz * dt
        # residual
        rx = mx - px
        rz = mz - pz
        # update
        fx = px + rx * alpha
        fz = pz + rz * alpha
        vx = vx + rx * (beta / dt)
        vz = vz + rz * (beta / dt)
        # true motion direction of shell over this dt (from dense pts)
        out.append((t, fx, fy, fz))
        # fy simple: keep measure y
        fy = my
    return out


def lerp_on_arc(meas, arc):
    """Interpolate launch->impact arc by sample time; can't overshoot/reverse."""
    out = []
    # arc endpoints
    t0 = arc[0][0]; x0 = arc[0][1]; z0 = arc[0][3]
    t1 = arc[-1][0]; x1 = arc[-1][1]; z1 = arc[-1][3]
    for (t, _, _, _) in meas:
        u = 0.0 if t1 <= t0 else (t - t0) / (t1 - t0)
        u = max(0.0, min(1.0, u))
        # pick point on arc nearest to this fraction (approx by time index)
        idx = min(int(u * (len(arc) - 1) + 0.5), len(arc) - 1)
        out.append((t, arc[idx][1], arc[idx][2], arc[idx][3]))
    return out


def run():
    random.seed(1)
    for sweep_rate in [0.6, 1.2]:
        for beams in [8, 12, 20]:
            errs_ab = []
            errs_lerp = []
            for trial in range(200):
                az = random.uniform(-40, 40)
                elev = random.uniform(45, 55)
                launch_xy = (random.uniform(1200, 1500), random.uniform(1200, 1500))
                # radar at 0; launch_xy is where the mortar sits; fire toward radar at az
                radar = (0.0, 0.0)
                target = (launch_xy[0], launch_xy[1] + random.uniform(-150, 150))
                # shot toward target→radar roughly: build a simple ballistic toward radar
                dx = radar[0] - target[0]
                dz = radar[1] - target[1]
                az_deg = math.degrees(math.atan2(dx, dz))
                pts = ballistic(radar, MUZZLE, elev, az_deg, dt=0.02)
                flight = pts[-1][0]
                ts = meas_times(flight, sweep_rate, beams, 45.0)
                if len(ts) < 4:
                    continue
                rng = random.Random(trial)
                meas = []
                for t in ts:
                    # find dense point at t
                    b = min(pts, key=lambda p: abs(p[0] - t))
                    n = rng.gauss(0.0, 8.0)  # position noise
                    meas.append((t, b[1] + n * 0.7, b[2] + n * 0.7, b[3] + n * 0.7))
                ab = alpha_beta_track(pts, meas)
                lr = lerp_on_arc(meas, pts)
                # error of final filtered pos vs true final, max error, reversals
                true_last = pts[min(len(pts) - 1, int(len(pts) * 0.9))]
                # max position error over flight
                eab = max(math.hypot(ab[k][1] - pts[min(int(ab[k][0] / 0.02), len(pts) - 1)][1],
                                      ab[k][3] - pts[min(int(ab[k][0] / 0.02), len(pts) - 1)][3]) for k in range(len(ab)))
                elr = max(math.hypot(lr[k][1] - pts[min(int(lr[k][0] / 0.02), len(pts) - 1)][1],
                                      lr[k][3] - pts[min(int(lr[k][0] / 0.02), len(pts) - 1)][3]) for k in range(len(lr)))
                errs_ab.append(eab)
                errs_lerp.append(elr)
            if errs_ab:
                print(f"rate={sweep_rate:4.2f} bw={beams:2d} | alpha-beta max pos err med={sorted(errs_ab)[len(errs_ab)//2]:6.0f} m | arc-lerp max pos err med={sorted(errs_lerp)[len(errs_lerp)//2]:6.0f} m")


if __name__ == "__main__":
    print("Live-track position error under WLR sensing (median of max error over flight)")
    run()
