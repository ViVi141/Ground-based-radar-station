#!/usr/bin/env python3
"""Conflict US+USSR pair coverage with RDF physics negatives.

Extends geometric LOS maps by requiring PhysicalDetect CFAR hits under:
  DEM clutter, MTI (slow radial), atmospheric + light rain, mid-scan
  beam offset, pessimistic UH-1 RCS, knife/NLOS when blocked.

Precomputes per-base masks, then unions all ordered pairs (1260).
"""

from __future__ import annotations

import json
import math
import random
import sys
import time
from dataclasses import asdict
from pathlib import Path

import numpy as np

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from gbrs_eden_dem import load_eden_full, los_probe  # noqa: E402
from simulate_clutter_cover import (  # noqa: E402
    TARGET_AGL_M,
    Hardware,
    Settings,
    make_us,
    make_ussr,
    mean_rcs_uh1,
    physical_detect,
)
from simulate_conflict_radar_coverage import (  # noqa: E402
    DEFAULT_ENT,
    RADAR_MAST_AGL_M,
    ConflictBase,
    _radar_origin,
    _save_map,
    land_area_m2,
    parse_conflict_eden_bases,
)

# Harsh-but-plausible look: slow Doppler (MTI hurts), off-boresight scan,
# light rain, RCS below mean aspect.
RADIAL_MS = 20.0
AZ_OFFSET_BEAM_FRAC = 0.40
RAIN_DB_PER_KM = 0.25
RCS_SCALE = 0.70
AZIMUTH_STEPS = 288
STEP_M = 80.0
LOS_STEP_M = 32.0


def _apply_negatives(hw: Hardware, settings: Settings) -> None:
    settings.enable_dem_clutter = True
    settings.enable_atmospheric_loss = True
    settings.enable_cfar_gate = True
    settings.enable_cfar_thermal_fill = True
    settings.enable_nlos_multipath = True
    settings.enable_knife_edge = True
    settings.rain_loss_db_per_km_one_way = RAIN_DB_PER_KM
    hw.enable_mti = True


def cover_base_physics(
    dem,
    origin: tuple[float, float, float],
    hw: Hardware,
    settings: Settings,
    cover: np.ndarray,
) -> float:
    """Ray-cast; paint cell only if PhysicalDetect CFAR passes."""
    ox, oy, oz = origin
    ok0, terrain0, _s0 = dem.sample(ox, oz)
    if ok0:
        radar_agl = oy - terrain0
    else:
        radar_agl = RADAR_MAST_AGL_M
    if radar_agl < 1.0:
        radar_agl = 1.0

    az_offset = hw.az_beamwidth_deg * AZ_OFFSET_BEAM_FRAC
    rcs = mean_rcs_uh1() * RCS_SCALE
    rng = random.Random(1)
    cell = dem.cell_m
    h = dem.height
    w = dem.width
    visited = np.zeros((h, w), dtype=np.bool_)
    covered_cells = 0
    range_m = settings.range_m

    for ai in range(AZIMUTH_STEPS):
        az = (2.0 * math.pi) * (ai / float(AZIMUTH_STEPS))
        cos_a = math.cos(az)
        sin_a = math.sin(az)
        dist = STEP_M
        while dist <= range_m + 0.5:
            tx = ox + dist * cos_a
            tz = oz + dist * sin_a
            ok, terrain_y, surf = dem.sample(tx, tz)
            if not ok:
                break
            if surf == 1:
                dist += STEP_M
                continue

            ty = terrain_y + TARGET_AGL_M
            clear, hit_u, max_h, max_u = los_probe(
                dem, ox, oy, oz, tx, ty, tz, step_m=LOS_STEP_M, slack_m=2.0
            )
            slant = math.sqrt(
                (tx - ox) * (tx - ox)
                + (ty - oy) * (ty - oy)
                + (tz - oz) * (tz - oz)
            )
            if slant < settings.min_distance_m:
                dist += STEP_M
                continue
            if slant > range_m:
                break

            target_agl = TARGET_AGL_M
            res = physical_detect(
                hw,
                settings,
                slant,
                rcs,
                RADIAL_MS,
                az_offset,
                target_agl,
                radar_agl,
                not clear,
                hit_u,
                max_h,
                max_u,
                rng,
                surface_class=int(surf),
                cell_size_m=dem.get_cell_size_m(),
            )
            if not res.detected_cfar:
                # Terrain block with no usable multipath → ray ends.
                # Clear LOS but below SNR/CFAR → skip cell, keep looking farther
                # (range rings are not monotonic under clutter).
                if not clear:
                    break
                dist += STEP_M
                continue

            lx, lz = dem.world_to_local(tx, tz)
            if 0 <= lx < w and 0 <= lz < h:
                if not visited[lz, lx]:
                    visited[lz, lx] = True
                    covered_cells += 1
                cover[lz, lx] = 1
            dist += STEP_M

    return float(covered_cells) * cell * cell


def run() -> int:
    out = TOOLS / "out"
    out.mkdir(parents=True, exist_ok=True)

    bases = parse_conflict_eden_bases(DEFAULT_ENT)
    n = len(bases)
    print("bases", n, "ordered pairs", n * (n - 1))

    dem = load_eden_full(cache_path=out / "eden_full_ds8.npz", downsample=8)
    land = land_area_m2(dem)
    cell_area = dem.cell_m * dem.cell_m
    print("land_km2", round(land / 1.0e6, 3))

    hw_us, set_us = make_us()
    hw_ussr, set_ussr = make_ussr()
    _apply_negatives(hw_us, set_us)
    _apply_negatives(hw_ussr, set_ussr)

    print(
        "negatives: radial=%.0f m/s az=%.2f*beam rain=%.2f dB/km rcs*=%.2f CFAR+MTI+DEM"
        % (RADIAL_MS, AZ_OFFSET_BEAM_FRAC, RAIN_DB_PER_KM, RCS_SCALE)
    )

    t0 = time.time()
    us_masks: list[np.ndarray] = []
    ussr_masks: list[np.ndarray] = []
    us_areas: list[float] = []
    ussr_areas: list[float] = []
    for i, base in enumerate(bases):
        origin = _radar_origin(dem, base, RADAR_MAST_AGL_M)
        cu = np.zeros((dem.height, dem.width), dtype=np.uint8)
        au = cover_base_physics(dem, origin, hw_us, set_us, cu)
        cs = np.zeros((dem.height, dem.width), dtype=np.uint8)
        as_ = cover_base_physics(dem, origin, hw_ussr, set_ussr, cs)
        us_masks.append(cu)
        ussr_masks.append(cs)
        us_areas.append(au)
        ussr_areas.append(as_)
        print(
            "precompute %d/%d %s  us=%.2f  ussr=%.2f km2"
            % (i + 1, n, base.name, au / 1.0e6, as_ / 1.0e6)
        )
    print("precompute sec", round(time.time() - t0, 1))

    pairs = []
    union_pcts = []
    overlap_pcts = []
    best = None
    worst = None
    best_ij = (0, 1)
    worst_ij = (0, 1)
    t1 = time.time()
    for i in range(n):
        for j in range(n):
            if i == j:
                continue
            u = us_masks[i] | ussr_masks[j]
            o = us_masks[i] & ussr_masks[j]
            union_m2 = float(np.count_nonzero(u)) * cell_area
            overlap_m2 = float(np.count_nonzero(o)) * cell_area
            union_pct = 100.0 * union_m2 / land
            overlap_pct = 100.0 * overlap_m2 / land
            row = {
                "us_base": bases[i].name,
                "us_kind": bases[i].kind,
                "ussr_base": bases[j].name,
                "ussr_kind": bases[j].kind,
                "us_km2": us_areas[i] / 1.0e6,
                "ussr_km2": ussr_areas[j] / 1.0e6,
                "union_km2": union_m2 / 1.0e6,
                "union_land_pct": union_pct,
                "overlap_km2": overlap_m2 / 1.0e6,
                "overlap_land_pct": overlap_pct,
            }
            pairs.append(row)
            union_pcts.append(union_pct)
            overlap_pcts.append(overlap_pct)
            if best is None or union_pct > best["union_land_pct"]:
                best = row
                best_ij = (i, j)
            if worst is None or union_pct < worst["union_land_pct"]:
                worst = row
                worst_ij = (i, j)
    print("pair merge sec", round(time.time() - t1, 1))

    arr = np.array(union_pcts, dtype=np.float64)
    ov = np.array(overlap_pcts, dtype=np.float64)
    summary = {
        "mode": "physics_negatives",
        "negatives": {
            "radial_ms": RADIAL_MS,
            "az_offset_beam_frac": AZ_OFFSET_BEAM_FRAC,
            "rain_db_per_km": RAIN_DB_PER_KM,
            "rcs_scale": RCS_SCALE,
            "require_cfar": True,
            "dem_clutter": True,
            "mti": True,
            "atmospheric": True,
            "knife_nlos": True,
        },
        "base_count": n,
        "pair_count": len(pairs),
        "land_km2": land / 1.0e6,
        "union_land_pct": {
            "min": float(arr.min()),
            "p10": float(np.percentile(arr, 10)),
            "p25": float(np.percentile(arr, 25)),
            "median": float(np.median(arr)),
            "mean": float(arr.mean()),
            "p75": float(np.percentile(arr, 75)),
            "p90": float(np.percentile(arr, 90)),
            "max": float(arr.max()),
        },
        "overlap_land_pct": {
            "min": float(ov.min()),
            "median": float(np.median(ov)),
            "mean": float(ov.mean()),
            "max": float(ov.max()),
        },
        "single_us_km2": {
            "mean": float(np.mean(us_areas)) / 1.0e6,
            "median": float(np.median(us_areas)) / 1.0e6,
            "max": float(np.max(us_areas)) / 1.0e6,
            "min": float(np.min(us_areas)) / 1.0e6,
        },
        "single_ussr_km2": {
            "mean": float(np.mean(ussr_areas)) / 1.0e6,
            "median": float(np.median(ussr_areas)) / 1.0e6,
            "max": float(np.max(ussr_areas)) / 1.0e6,
            "min": float(np.min(ussr_areas)) / 1.0e6,
        },
        "best_pair": best,
        "worst_pair": worst,
        "compare_to_los_only": {
            "note": "previous LOS-only traverse median≈14.1% mean≈14.2% max≈27.0%",
        },
    }
    print(json.dumps(summary, indent=2))

    bi, bj = best_ij
    wi, wj = worst_ij
    _save_map(
        out / "conflict_pair_physics_best_union.png",
        dem,
        [bases[bi], bases[bj]],
        cover=(us_masks[bi] | ussr_masks[bj]).astype(np.uint8),
        title="PHYSICS BEST %.1f%% US@%s USSR@%s"
        % (best["union_land_pct"], best["us_base"], best["ussr_base"]),
    )
    _save_map(
        out / "conflict_pair_physics_worst_union.png",
        dem,
        [bases[wi], bases[wj]],
        cover=(us_masks[wi] | ussr_masks[wj]).astype(np.uint8),
        title="PHYSICS WORST %.1f%% US@%s USSR@%s"
        % (worst["union_land_pct"], worst["us_base"], worst["ussr_base"]),
    )

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(10, 5), dpi=140)
    ax.hist(arr, bins=40, color="#c44e4e", edgecolor="k", linewidth=0.3, label="physics")
    ax.axvline(arr.mean(), color="red", linestyle="--", label="mean %.1f%%" % arr.mean())
    ax.axvline(
        float(np.median(arr)),
        color="orange",
        linestyle="--",
        label="median %.1f%%" % float(np.median(arr)),
    )
    ax.axvline(14.1, color="gray", linestyle=":", label="LOS-only median 14.1%")
    ax.set_xlabel("Union land coverage % (US + USSR, one base each)")
    ax.set_ylabel("Pair count")
    ax.set_title("Physics negatives: all %d ordered pairs" % len(pairs))
    ax.legend()
    fig.tight_layout()
    fig.savefig(out / "conflict_pair_physics_hist.png")
    plt.close(fig)

    pairs_sorted = sorted(pairs, key=lambda r: -r["union_land_pct"])
    report = {
        "summary": summary,
        "top20": pairs_sorted[:20],
        "bottom20": list(reversed(pairs_sorted[-20:])),
        "all_pairs": pairs_sorted,
    }
    path = out / "conflict_pair_physics_report.json"
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("wrote", path)
    return 0


if __name__ == "__main__":
    sys.exit(run())
