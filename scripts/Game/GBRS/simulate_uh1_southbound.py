#!/usr/bin/env python3
"""UH-1 takeoff from Eden north airfield, fly south past GBRS radar.

Radar sits beside the northern asphalt strip (~Everon north field).
UH-1 rotates, climbs, then tracks south. Each sample runs the same
PhysicalDetect + CFAR chain as simulate_clutter_cover.py (UH-1 signature RCS).
"""

from __future__ import annotations

import json
import math
import random
import sys
from pathlib import Path

import numpy as np

GBRS = Path(__file__).resolve().parent
if str(GBRS) not in sys.path:
    sys.path.insert(0, str(GBRS))

from gbrs_eden_dem import load_eden_crop, los_probe  # noqa: E402
from simulate_clutter_cover import (  # noqa: E402
    UH1_MEAN_RCS_M2,
    UH1_SIG_KEY,
    apply_cfar_single_target,
    dem_clutter_processed_w,
    instant_rcs_uh1,
    lin_to_db,
    make_us,
    make_ussr,
    physical_detect,
    scan_azimuth_offset_for_illumination,
)

# Northern airfield asphalt cluster from Eden DEM (surface=6).
AIRFIELD_XZ = (4800.0, 11950.0)
# Radar mast next to strip (west shoulder), ~12 m AGL like RDF demos.
RADAR_OFFSET_XZ = (-80.0, 40.0)
RADAR_MAST_AGL_M = 12.0

# UH-1 flight: rotate on strip, climb, then southbound cruise.
TAKEOFF_ROLL_S = 8.0
CLIMB_S = 25.0
CRUISE_S = 120.0
DT_S = 0.5
GROUND_SPEED_ROLL = 15.0
GROUND_SPEED_CLIMB = 35.0
GROUND_SPEED_CRUISE = 50.0  # ~180 km/h
CLIMB_RATE_MS = 4.0
CRUISE_AGL_M = 80.0
HEADING_SOUTH_DEG = 270.0  # world az: cos/sin → fly -Z (south if +Z is north)


def world_az_to_offset(az_deg: float, dist_m: float) -> tuple[float, float]:
    rad = math.radians(az_deg)
    return dist_m * math.cos(rad), dist_m * math.sin(rad)


def build_trajectory(
    dem,
    start_x: float,
    start_z: float,
) -> list[dict]:
    ok, ty0, _ = dem.sample(start_x, start_z)
    if not ok:
        raise SystemExit("start point outside DEM")
    samples: list[dict] = []
    t = 0.0
    x = start_x
    z = start_z
    agl = 0.5
    heading = HEADING_SOUTH_DEG
    phase = "roll"
    while t <= TAKEOFF_ROLL_S + CLIMB_S + CRUISE_S + 1e-6:
        if t <= TAKEOFF_ROLL_S:
            phase = "roll"
            speed = GROUND_SPEED_ROLL
            agl = 0.5
        elif t <= TAKEOFF_ROLL_S + CLIMB_S:
            phase = "climb"
            speed = GROUND_SPEED_CLIMB
            agl = min(CRUISE_AGL_M, 0.5 + CLIMB_RATE_MS * (t - TAKEOFF_ROLL_S))
        else:
            phase = "cruise"
            speed = GROUND_SPEED_CRUISE
            agl = CRUISE_AGL_M

        ok, terr, surf = dem.sample(x, z)
        if not ok:
            break
        y = terr + agl
        dx, dz = world_az_to_offset(heading, 1.0)
        vx = dx * speed
        vz = dz * speed
        # Climb adds vertical speed only in climb phase.
        vy = 0.0
        if phase == "climb" and agl < CRUISE_AGL_M - 0.1:
            vy = CLIMB_RATE_MS

        samples.append(
            {
                "t_s": round(t, 2),
                "phase": phase,
                "x": x,
                "y": y,
                "z": z,
                "agl_m": agl,
                "vx": vx,
                "vy": vy,
                "vz": vz,
                "yaw_deg": heading,
                "surface": int(surf),
                "terrain_y": float(terr),
            }
        )
        x = x + vx * DT_S
        z = z + vz * DT_S
        t = t + DT_S
    return samples


def detect_one(
    hw,
    settings,
    dem,
    radar_xyz,
    radar_agl,
    uh,
    paint: bool,
    rng: random.Random,
    trial: int,
) -> dict:
    rx, ry, rz = radar_xyz
    tx, ty, tz = uh["x"], uh["y"], uh["z"]
    dx = tx - rx
    dy = ty - ry
    dz = tz - rz
    slant = math.sqrt(dx * dx + dy * dy + dz * dz)
    if slant < settings.min_distance_m:
        return {
            "detected_cfar": False,
            "detected_snr": False,
            "snr_db": -300.0,
            "slant_m": slant,
            "radial_ms": 0.0,
            "los_clear": True,
            "in_beam": False,
            "rcs_m2": 0.0,
        }

    los_az = math.degrees(math.atan2(dz, dx))
    horiz = math.sqrt(dx * dx + dz * dz)
    los_el = math.degrees(math.atan2(dy, max(0.001, horiz)))

    # Radial speed toward radar (PhysicalDetect: -dot(v, los)).
    losx, losy, losz = dx / slant, dy / slant, dz / slant
    radial = -(uh["vx"] * losx + uh["vy"] * losy + uh["vz"] * losz)

    clear, hit_u, max_h, max_u = los_probe(dem, rx, ry, rz, tx, ty, tz)
    los_blocked = not clear

    # Mechanical scan: one paint uses az within ±bw/2 relative to boresight on target.
    if paint:
        az_off = scan_azimuth_offset_for_illumination(hw, rng)
        in_beam = True
    else:
        # Random antenna heading this tick.
        antenna = rng.uniform(-180.0, 180.0)
        rel = ((los_az - antenna + 180.0) % 360.0) - 180.0
        half = 0.5 * hw.az_beamwidth_deg
        in_beam = abs(rel) <= half
        if not in_beam:
            return {
                "detected_cfar": False,
                "detected_snr": False,
                "snr_db": -300.0,
                "slant_m": slant,
                "radial_ms": radial,
                "los_clear": clear,
                "in_beam": False,
                "rcs_m2": 0.0,
                "azimuth_deg": los_az,
            }
        az_off = rel

    rcs = instant_rcs_uh1(uh["yaw_deg"], los_az, los_el, True, trial)
    target_agl = uh["agl_m"]
    res = physical_detect(
        hw,
        settings,
        slant,
        rcs,
        radial,
        az_off,
        target_agl,
        radar_agl,
        los_blocked,
        hit_u,
        max_h,
        max_u,
        rng,
        surface_class=int(uh["surface"]),
        cell_size_m=dem.get_cell_size_m(),
    )
    return {
        "detected_cfar": res.detected_cfar,
        "detected_snr": res.detected_snr,
        "snr_db": res.snr_db,
        "slant_m": slant,
        "radial_ms": radial,
        "los_clear": clear,
        "in_beam": in_beam,
        "rcs_m2": rcs,
        "azimuth_deg": los_az,
        "elevation_deg": los_el,
        "multipath": res.multipath_factor,
        "mti_gain": res.mti_gain,
        "pattern_gain": res.pattern_gain,
    }


def run_faction(name_factory, dem, radar_xyz, radar_agl, traj, paint: bool) -> dict:
    hw, settings = name_factory()
    rng = random.Random(7)
    rows = []
    for i, uh in enumerate(traj):
        det = detect_one(
            hw, settings, dem, radar_xyz, radar_agl, uh, paint, rng, i
        )
        rows.append(
            {
                "t_s": uh["t_s"],
                "phase": uh["phase"],
                "uh_x": round(uh["x"], 1),
                "uh_y": round(uh["y"], 1),
                "uh_z": round(uh["z"], 1),
                "uh_agl_m": round(uh["agl_m"], 1),
                "slant_km": round(det["slant_m"] / 1000.0, 3),
                "radial_ms": round(det["radial_ms"], 1),
                "snr_db": round(det["snr_db"], 2),
                "rcs_m2": round(det["rcs_m2"], 2),
                "los_clear": det["los_clear"],
                "in_beam": det["in_beam"],
                "detected_snr": det["detected_snr"],
                "detected_cfar": det["detected_cfar"],
            }
        )
    det_n = sum(1 for r in rows if r["detected_cfar"])
    return {
        "faction": hw.name,
        "scale": settings.dem_clutter_scale,
        "gate_db": settings.detection_snr_db,
        "paint_mode": paint,
        "pd_cfar_track": det_n / max(1, len(rows)),
        "samples": rows,
    }


def write_png(report: dict, out_path: Path) -> None:
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception:
        return

    fig, axes = plt.subplots(2, 2, figsize=(12, 8.5))
    for ax_i, key in enumerate(("US_RPL5", "USSR_TPN19")):
        block = report["factions"][key]["paint"]
        rows = block["samples"]
        ts = [r["t_s"] for r in rows]
        snr = [r["snr_db"] if r["snr_db"] > -200 else None for r in rows]
        det = [100.0 if r["detected_cfar"] else 0.0 for r in rows]
        z = [r["uh_z"] for r in rows]

        ax = axes[0][ax_i]
        ax.plot(ts, snr, linewidth=1.2, label="SNR")
        ax.axhline(block["gate_db"], color="0.35", linestyle="--", label="gate")
        ax.set_xlabel("t (s)")
        ax.set_ylabel("SNR (dB)")
        ax.set_title("%s SNR vs time (paint)" % key)
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=7)

        ax2 = axes[1][ax_i]
        ax2.plot(ts, det, linewidth=1.0, label="CFAR detect")
        ax2.set_xlabel("t (s)")
        ax2.set_ylabel("Detected (%)")
        ax2.set_ylim(-5, 105)
        ax2.set_title("%s southbound Z=%.0f→%.0f" % (key, z[0], z[-1]))
        ax2.grid(True, alpha=0.3)

    fig.suptitle(
        "North airfield radar + UH-1 southbound  meanRCS=%.2f m2" % UH1_MEAN_RCS_M2,
        fontsize=11,
    )
    fig.tight_layout()
    fig.savefig(out_path, dpi=140)
    plt.close(fig)


def main() -> int:
    out_dir = GBRS / "out"
    out_dir.mkdir(parents=True, exist_ok=True)
    cache = out_dir / "eden_crop_north_airfield.npz"

    af_x, af_z = AIRFIELD_XZ
    rx = af_x + RADAR_OFFSET_XZ[0]
    rz = af_z + RADAR_OFFSET_XZ[1]
    # Load DEM centered on airfield.
    dem = load_eden_crop(
        radar_xyz=(rx, 30.0, rz),
        radius_m=10000.0,
        cache_path=cache,
    )
    ok, ty, surf = dem.sample(rx, rz)
    if not ok:
        raise SystemExit("radar site outside DEM")
    ry = ty + RADAR_MAST_AGL_M
    radar_xyz = (rx, ry, rz)
    radar_agl = RADAR_MAST_AGL_M

    # UH starts on runway centerline east of radar, then south.
    start_x = af_x + 40.0
    start_z = af_z
    traj = build_trajectory(dem, start_x, start_z)

    report = {
        "scenario": "north_airfield_uh1_southbound",
        "uh1_signature": UH1_SIG_KEY,
        "uh1_mean_rcs_m2": UH1_MEAN_RCS_M2,
        "airfield_xz": [af_x, af_z],
        "radar_xyz": [round(rx, 2), round(ry, 2), round(rz, 2)],
        "radar_agl_m": radar_agl,
        "radar_surface": int(surf),
        "heading_deg": HEADING_SOUTH_DEG,
        "dt_s": DT_S,
        "n_samples": len(traj),
        "dem_source": dem.source,
        "factions": {},
    }

    print("=== North airfield + UH-1 southbound ===")
    print(
        "Radar (%.1f, %.1f, %.1f) AGL=%.1f surf=%d"
        % (rx, ry, rz, radar_agl, surf)
    )
    print(
        "UH start (%.1f, z=%.1f) -> end z=%.1f  n=%d"
        % (start_x, start_z, traj[-1]["z"], len(traj))
    )

    for factory, tag in ((make_us, "US_RPL5"), (make_ussr, "USSR_TPN19")):
        paint = run_faction(factory, dem, radar_xyz, radar_agl, traj, True)
        tick = run_faction(factory, dem, radar_xyz, radar_agl, traj, False)
        report["factions"][tag] = {"paint": paint, "tick": tick}
        print(
            "%s paint Pd=%.1f%%  tick Pd=%.1f%%  (over track)"
            % (tag, paint["pd_cfar_track"] * 100.0, tick["pd_cfar_track"] * 100.0)
        )
        # Phase summary for paint.
        for phase in ("roll", "climb", "cruise"):
            sub = [r for r in paint["samples"] if r["phase"] == phase]
            if not sub:
                continue
            pd = sum(1 for r in sub if r["detected_cfar"]) / len(sub)
            mean_snr = sum(r["snr_db"] for r in sub if r["snr_db"] > -200) / max(
                1, sum(1 for r in sub if r["snr_db"] > -200)
            )
            print(
                "  %-6s  n=%3d  Pd=%.0f%%  meanSNR=%.1f dB  slant %.1f-%.1f km"
                % (
                    phase,
                    len(sub),
                    pd * 100.0,
                    mean_snr,
                    min(r["slant_km"] for r in sub),
                    max(r["slant_km"] for r in sub),
                )
            )

    png = out_dir / "uh1_southbound_north_airfield.png"
    write_png(report, png)
    # Drop full tick sample arrays from JSON to keep file smaller? keep paint full.
    slim = dict(report)
    for tag in slim["factions"]:
        slim["factions"][tag]["tick"] = {
            "pd_cfar_track": report["factions"][tag]["tick"]["pd_cfar_track"],
            "paint_mode": False,
        }
    out_json = out_dir / "uh1_southbound_report.json"
    out_json.write_text(json.dumps(slim, indent=2), encoding="utf-8")
    print("Wrote", out_json)
    print("Wrote", png)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
