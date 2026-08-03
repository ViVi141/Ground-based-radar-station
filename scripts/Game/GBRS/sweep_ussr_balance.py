#!/usr/bin/env python3
"""Focused USSR balance sweep: gate + dem_clutter_scale (+ mild RF)."""

from __future__ import annotations

import json
import math
import random
from pathlib import Path

from gbrs_eden_dem import load_eden_crop, los_probe
from simulate_clutter_cover import (
    Hardware,
    Settings,
    ElevationBeam,
    make_us,
    physical_detect,
    instant_rcs_uh1,
    scan_azimuth_offset_for_illumination,
    find_land_bearing,
    TARGET_AGL_M,
)
from simulate_uh1_southbound import (
    AIRFIELD_XZ,
    RADAR_OFFSET_XZ,
    RADAR_MAST_AGL_M,
    build_trajectory,
    detect_one,
    GBRS,
)


def make_ussr_variant(
    peak_power_w: float,
    antenna_gain_dbi: float,
    detection_snr_db: float,
    pulses_integrated: int,
    dem_clutter_scale: float,
) -> tuple[Hardware, Settings]:
    hw = Hardware(
        name="USSR_TPN19",
        frequency_hz=1.6e8,
        peak_power_w=peak_power_w,
        antenna_gain_dbi=antenna_gain_dbi,
        az_beamwidth_deg=6.0,
        system_loss_db=8.0,
        noise_figure_db=6.0,
        pulse_width_s=6.0e-6,
        bandwidth_hz=166666.0,
        pulses_integrated=pulses_integrated,
        coherent_integration=True,
        enable_mti=True,
        mti_clutter_floor=0.01,
        prf_hz=200.0,
        scan_rpm=6.0,
        elevation_beams=[
            ElevationBeam("low", 2.0, 16.0, 0.0),
            ElevationBeam("mid", 18.0, 24.0, 0.0),
            ElevationBeam("high", 42.0, 30.0, -1.0),
        ],
    )
    settings = Settings(
        range_m=10000.0,
        update_interval_s=0.04,
        detection_snr_db=detection_snr_db,
        dem_clutter_scale=dem_clutter_scale,
    )
    return hw, settings


def track_pd(hw, settings, dem, radar_xyz, radar_agl, traj) -> float:
    rng = random.Random(7)
    hits = 0
    for i, uh in enumerate(traj):
        det = detect_one(hw, settings, dem, radar_xyz, radar_agl, uh, True, rng, i)
        if det["detected_cfar"]:
            hits = hits + 1
    return hits / max(1, len(traj))


def paint_4km(
    hw,
    settings,
    dem,
    radar_xyz,
    radar_agl,
    bearing_deg: float,
    mode: str,
    trials: int = 100,
) -> float:
    rng = random.Random(11)
    hits = 0
    range_m = 4000.0
    for trial in range(trials):
        rad = math.radians(bearing_deg)
        tx = radar_xyz[0] + range_m * math.cos(rad)
        tz = radar_xyz[2] + range_m * math.sin(rad)
        ok, terr, surf = dem.sample(tx, tz)
        if not ok:
            continue
        ty = terr + TARGET_AGL_M
        dx = tx - radar_xyz[0]
        dy = ty - radar_xyz[1]
        dz = tz - radar_xyz[2]
        slant = math.sqrt(dx * dx + dy * dy + dz * dz)
        los_az = math.degrees(math.atan2(dz, dx))
        horiz = math.sqrt(dx * dx + dz * dz)
        los_el = math.degrees(math.atan2(dy, max(0.001, horiz)))
        if mode == "broadside":
            yaw = los_az + 90.0
        else:
            yaw = los_az
        clear, hit_u, max_h, max_u = los_probe(
            dem,
            radar_xyz[0],
            radar_xyz[1],
            radar_xyz[2],
            tx,
            ty,
            tz,
        )
        los_blocked = True if mode == "blocked" else (not clear)
        az_off = scan_azimuth_offset_for_illumination(hw, rng)
        rcs = instant_rcs_uh1(yaw, los_az, los_el, True, trial)
        res = physical_detect(
            hw,
            settings,
            slant,
            rcs,
            50.0,
            az_off,
            TARGET_AGL_M,
            radar_agl,
            los_blocked,
            hit_u,
            max_h,
            max_u,
            rng,
            surface_class=int(surf) if ok else 0,
            cell_size_m=dem.get_cell_size_m(),
        )
        if res.detected_cfar:
            hits = hits + 1
    return hits / max(1, trials)


def main() -> int:
    out_dir = GBRS / "out"
    out_dir.mkdir(parents=True, exist_ok=True)

    af_x, af_z = AIRFIELD_XZ
    rx = af_x + RADAR_OFFSET_XZ[0]
    rz = af_z + RADAR_OFFSET_XZ[1]
    dem = load_eden_crop(
        radar_xyz=(rx, 30.0, rz),
        radius_m=10000.0,
        cache_path=out_dir / "eden_crop_north_airfield.npz",
    )
    ok, ty, _ = dem.sample(rx, rz)
    if not ok:
        raise SystemExit("radar outside DEM")
    radar_xyz = (rx, ty + RADAR_MAST_AGL_M, rz)
    radar_agl = RADAR_MAST_AGL_M
    traj = build_trajectory(dem, af_x + 40.0, af_z)

    dem2 = load_eden_crop(
        radar_xyz=(4700.0, 40.0, 11200.0),
        radius_m=9000.0,
        cache_path=out_dir / "eden_crop_radar.npz",
    )
    ok2, ty2, _ = dem2.sample(4700.0, 11200.0)
    if ok2:
        radar2 = (4700.0, ty2 + 0.5, 11200.0)
        agl2 = 0.5
    else:
        radar2 = (4700.0, 40.0, 11200.0)
        agl2 = 12.0
    bearing = find_land_bearing(dem2, radar2, 4000.0, TARGET_AGL_M)

    us_hw, us_settings = make_us()
    us = {
        "track": track_pd(us_hw, us_settings, dem, radar_xyz, radar_agl, traj),
        "radial": paint_4km(us_hw, us_settings, dem2, radar2, agl2, bearing, "radial"),
        "broad": paint_4km(
            us_hw, us_settings, dem2, radar2, agl2, bearing, "broadside"
        ),
        "blocked": paint_4km(
            us_hw, us_settings, dem2, radar2, agl2, bearing, "blocked"
        ),
    }
    print(
        "US track=%.1f%% radial=%.1f%% broad=%.1f%% blocked=%.1f%%"
        % (
            us["track"] * 100.0,
            us["radial"] * 100.0,
            us["broad"] * 100.0,
            us["blocked"] * 100.0,
        )
    )

    candidates = []
    # Keep VHF character: modest RF, lower DEM clutter scale than X-band.
    for peak, gain, pulses in (
        (250000.0, 18.0, 8),
        (350000.0, 20.0, 12),
        (450000.0, 22.0, 16),
    ):
        for scale in (1.0, 0.5, 0.35, 0.25, 0.15):
            for gate in (6.0, 4.0, 3.0, 2.0, 1.0, 0.0):
                hw, settings = make_ussr_variant(peak, gain, gate, pulses, scale)
                track = track_pd(hw, settings, dem, radar_xyz, radar_agl, traj)
                radial = paint_4km(
                    hw, settings, dem2, radar2, agl2, bearing, "radial"
                )
                if track < 0.40 or track > 0.72:
                    continue
                if radial < 0.70:
                    continue
                broad = paint_4km(
                    hw, settings, dem2, radar2, agl2, bearing, "broadside"
                )
                blocked = paint_4km(
                    hw, settings, dem2, radar2, agl2, bearing, "blocked"
                )
                if broad >= us["broad"] and blocked >= us["blocked"]:
                    continue
                # Prefer inside windows and smaller RF / higher gate / not-zero scale.
                track_pen = 0.0
                if track < 0.45:
                    track_pen = (0.45 - track) * 8.0
                elif track > 0.65:
                    track_pen = (track - 0.65) * 4.0
                radial_pen = 0.0
                if radial < 0.75:
                    radial_pen = (0.75 - radial) * 8.0
                elif radial > 0.90:
                    radial_pen = (radial - 0.90) * 3.0
                rf = peak * (10.0 ** (gain / 10.0)) ** 2 * pulses
                score = (
                    track_pen
                    + radial_pen
                    + (1.0 - scale) * 0.15
                    + rf * 1.0e-13
                    - gate * 0.02
                )
                row = {
                    "peak": peak,
                    "gain": gain,
                    "pulses": pulses,
                    "scale": scale,
                    "gate": gate,
                    "track": track,
                    "radial": radial,
                    "broad": broad,
                    "blocked": blocked,
                    "score": score,
                }
                candidates.append(row)
                print(
                    "peak=%.0fkW G=%.0f N=%d scale=%.2f gate=%.1f"
                    " track=%.1f%% radial=%.1f%% broad=%.1f%% blk=%.1f%%"
                    % (
                        peak / 1000.0,
                        gain,
                        pulses,
                        scale,
                        gate,
                        track * 100.0,
                        radial * 100.0,
                        broad * 100.0,
                        blocked * 100.0,
                    )
                )

    if not candidates:
        print("No candidates")
        return 1

    candidates.sort(key=lambda c: c["score"])
    best = candidates[0]
    print("=== BEST ===")
    print(best)
    out = out_dir / "ussr_balance_sweep.json"
    out.write_text(
        json.dumps({"us": us, "best": best, "candidates": candidates[:25]}, indent=2),
        encoding="utf-8",
    )
    print("Wrote", out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
