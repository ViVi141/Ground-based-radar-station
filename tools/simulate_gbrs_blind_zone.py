#!/usr/bin/env python3
"""Offline GBRS pulse-eclipsing blind zone (近程盲区) vs Eden DEM.

Rmin = c * (tau + receiver_recovery) / 2. Pulse compression does not shrink it.

Mirrors GBRS_RadarStationConfig + RDF_RadarHardware.GetMinDetectableRangeM:

  US PD search   0.5 us + 0.2 us ferrite   ~105 m
  USSR P-18 EW   6.0 us + 1.0 us VHF TR    ~1050 m
  WLR locating   1.0 us + 0.5 us           ~225 m

Green paint is DEM LOS coverage of a UH-1 at cruise AGL. The inner hole is
not painted (TX blanking). Dashed rings mark geometric Rmin / instrumented
range. US 105 m is only resolved on the native ~2 m close-up; island ds8
shows the USSR ~1 km hole.

Outputs under tools/out/:
  gbrs_blind_zone_report.json
  gbrs_blind_us_search_closeup.png
  gbrs_blind_ussr_search_closeup.png
  gbrs_blind_wlr_closeup.png
  gbrs_blind_us_search_island.png
  gbrs_blind_ussr_search_island.png
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from gbrs_eden_dem import (  # noqa: E402
    DEFAULT_RADAR_XYZ,
    PROFILE_EDEN,
    EdenDemCrop,
    load_eden_crop,
    load_eden_full,
)
from simulate_conflict_radar_coverage import (  # noqa: E402
    ConflictBase,
    RADAR_MAST_AGL_M,
    TARGET_AGL_M,
    _save_map,
    cover_base_rays,
    land_area_m2,
)
import simulate_clutter_cover as s  # noqa: E402
import simulate_wlr_projectile as wlr  # noqa: E402

OUT_DIR = TOOLS / "out"
CROP_CACHE = OUT_DIR / "eden_crop_radar.npz"
ISLAND_CACHE = OUT_DIR / "eden_full_ds8.npz"

CLOSEUP_HALF_M = 2200.0
CLOSEUP_PAINT_M = 2500.0
US_RANGE_M = 7000.0
USSR_RANGE_M = 10000.0
WLR_US_RANGE_M = 8000.0

C_LIGHT = s.C_LIGHT


def expected_rmin_m(pulse_width_s: float, receiver_recovery_s: float) -> float:
    blank = pulse_width_s
    if blank < 0.0:
        blank = 0.0
    if receiver_recovery_s > 0.0:
        blank = blank + receiver_recovery_s
    return C_LIGHT * blank * 0.5


def radar_origin(dem: EdenDemCrop, xyz: tuple[float, float, float]) -> tuple[float, float, float]:
    ok, terrain_y, _surf = dem.sample(xyz[0], xyz[2])
    if ok:
        return xyz[0], terrain_y + RADAR_MAST_AGL_M, xyz[2]
    return xyz[0], xyz[1], xyz[2]


def pd_center_trial(
    hw: s.Hardware,
    settings: s.Settings,
    range_m: float,
    trials: int,
    seed: int,
) -> dict:
    result = s.run_scenario_pd(
        hw,
        settings,
        range_m,
        50.0,
        0.0,
        False,
        1.0,
        0.0,
        0.0,
        False,
        False,
        True,
        trials,
        seed,
    )
    return {
        "range_m": range_m,
        "pd_snr": result["pd_snr"],
        "pd_cfar": result["pd_cfar"],
        "mean_snr_db": result["mean_snr_db"],
        "trials": trials,
    }


def wlr_center_trial(
    hw: s.Hardware,
    settings: s.Settings,
    range_m: float,
    seed: int,
) -> dict:
    res = wlr.snr_at(hw, settings, range_m, wlr.PROJECTILE_RCS_M2, 180.0, 15.0, 0.0, seed)
    detected = False
    if res.detected_snr:
        detected = True
    return {
        "range_m": range_m,
        "detected_snr": detected,
        "snr_db": res.snr_db,
    }


def product_pd_report(
    label: str,
    hw: s.Hardware,
    settings: s.Settings,
    kind: str,
    trials: int,
) -> dict:
    rmin = s.effective_min_distance_m(hw, settings)
    expected = expected_rmin_m(hw.pulse_width_s, hw.receiver_recovery_s)
    inside_range = 0.5 * rmin
    outside_range = 1.1 * rmin
    rmin_match = abs(rmin - expected) < 0.01

    if kind == "wlr":
        inside = wlr_center_trial(hw, settings, inside_range, 1)
        outside = wlr_center_trial(hw, settings, outside_range, 1)
        pd_inside = 0.0
        if inside["detected_snr"]:
            pd_inside = 1.0
        pd_outside = 0.0
        if outside["detected_snr"]:
            pd_outside = 1.0
        inside_ok = pd_inside == 0.0
        outside_ok = pd_outside > 0.0
    else:
        inside = pd_center_trial(hw, settings, inside_range, trials, 11)
        outside = pd_center_trial(hw, settings, outside_range, trials, 13)
        pd_inside = inside["pd_snr"]
        pd_outside = outside["pd_snr"]
        inside_ok = pd_inside == 0.0
        outside_ok = pd_outside >= 0.5

    passed = False
    if rmin_match and inside_ok and outside_ok:
        passed = True

    print(
        "  %-14s Rmin=%.1f m  inside 0.5 Rmin Pd=%.0f%%  outside 1.1 Rmin Pd=%.0f%%  %s"
        % (
            label,
            rmin,
            pd_inside * 100.0,
            pd_outside * 100.0,
            "PASS" if passed else "FAIL",
        )
    )
    return {
        "label": label,
        "pulse_width_s": hw.pulse_width_s,
        "receiver_recovery_s": hw.receiver_recovery_s,
        "expected_rmin_m": expected,
        "effective_rmin_m": rmin,
        "rmin_matches_formula": rmin_match,
        "inside": inside,
        "outside": outside,
        "inside_pd_zero": inside_ok,
        "outside_pd_recovers": outside_ok,
        "pass": passed,
    }


def paint_station(
    dem: EdenDemCrop,
    origin: tuple[float, float, float],
    range_m: float,
    min_range_m: float,
    azimuth_steps: int,
    step_m: float,
    los_step_m: float,
) -> tuple[np.ndarray, float]:
    cover = np.zeros((dem.height, dem.width), dtype=np.uint8)
    area = cover_base_rays(
        dem,
        origin,
        range_m,
        TARGET_AGL_M,
        cover,
        None,
        azimuth_steps=azimuth_steps,
        step_m=step_m,
        los_step_m=los_step_m,
        min_range_m=min_range_m,
    )
    return cover, area


def closeup_limits(xyz: tuple[float, float, float]) -> tuple[tuple[float, float], tuple[float, float]]:
    half = CLOSEUP_HALF_M
    xlim = (xyz[0] - half, xyz[0] + half)
    ylim = (xyz[2] - half, xyz[2] + half)
    return xlim, ylim


def save_blind_map(
    path: Path,
    dem: EdenDemCrop,
    origin: tuple[float, float, float],
    cover: np.ndarray,
    rmin_m: float,
    range_m: float,
    title: str,
    xlim: tuple[float, float] | None = None,
    ylim: tuple[float, float] | None = None,
) -> None:
    station = ConflictBase(
        name="GBRS",
        kind="radar",
        x=origin[0],
        y=origin[1],
        z=origin[2],
    )
    rings = [
        (origin[0], origin[2], rmin_m, "#ff3355", "--", "Rmin %.0f m" % rmin_m),
        (origin[0], origin[2], range_m, "#f4f1de", ":", "paint %.0f m" % range_m),
    ]
    _save_map(
        path,
        dem,
        [station],
        cover=cover,
        title=title,
        rings=rings,
        xlim=xlim,
        ylim=ylim,
    )
    print("  wrote %s" % path.name)


def run(args: argparse.Namespace) -> int:
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    xyz = DEFAULT_RADAR_XYZ

    us_hw, us_set = s.make_us()
    ussr_hw, ussr_set = s.make_ussr()
    wlr_cfg = wlr.ScanConfig(
        beamwidth_deg=25.0,
        rpm=0.0,
        snr_gate_db=4.0,
        update_interval_s=0.15,
        elevation_beams=[
            ("mortar_low", 15.0, 28.0, 0.0),
            ("mortar_mid", 35.0, 30.0, 0.0),
            ("mortar_high", 55.0, 28.0, -0.5),
        ],
    )
    wlr_hw = wlr.build_hw(wlr_cfg, "US")
    wlr_set = wlr.build_settings(wlr_cfg, WLR_US_RANGE_M, wlr_hw)

    print("=" * 76)
    print("GBRS pulse blind zone (近程盲区) offline validation")
    print("=" * 76)
    print("\n--- Formula check + beam-center Pd ---")
    products = {
        "us_search": product_pd_report("US search", us_hw, us_set, "pd", args.trials),
        "ussr_search": product_pd_report("USSR search", ussr_hw, ussr_set, "pd", args.trials),
        "wlr": product_pd_report("WLR locating", wlr_hw, wlr_set, "wlr", args.trials),
    }

    print("\nLoading native Eden crop (close-up)...")
    crop = load_eden_crop(
        radar_xyz=xyz,
        radius_m=9000.0,
        dem_root=Path(args.dem_root) if args.dem_root else None,
        cache_path=CROP_CACHE,
    )
    origin_crop = radar_origin(crop, xyz)
    xlim, ylim = closeup_limits(xyz)
    print(
        "  crop %dx%d cell=%.2f m  origin=(%.1f, %.1f, %.1f)"
        % (
            crop.width,
            crop.height,
            crop.cell_m,
            origin_crop[0],
            origin_crop[1],
            origin_crop[2],
        )
    )

    closeup_step = max(crop.cell_m * 4.0, 8.0)
    closeup_los = max(crop.cell_m * 6.0, 12.0)
    close_specs = [
        (
            "us_search",
            products["us_search"]["effective_rmin_m"],
            CLOSEUP_PAINT_M,
            "US PD search — TX blanking hole ~105 m",
            "gbrs_blind_us_search_closeup.png",
        ),
        (
            "ussr_search",
            products["ussr_search"]["effective_rmin_m"],
            CLOSEUP_PAINT_M,
            "USSR P-18 search — TX blanking hole ~1050 m",
            "gbrs_blind_ussr_search_closeup.png",
        ),
        (
            "wlr",
            products["wlr"]["effective_rmin_m"],
            CLOSEUP_PAINT_M,
            "WLR locating pulse — TX blanking hole ~225 m",
            "gbrs_blind_wlr_closeup.png",
        ),
    ]
    map_meta = []
    print("\n--- Close-up DEM maps (native cells, zoom +/- %.0f m) ---" % CLOSEUP_HALF_M)
    for key, rmin, paint_m, title, fname in close_specs:
        cover, area = paint_station(
            crop,
            origin_crop,
            paint_m,
            rmin,
            azimuth_steps=args.closeup_azimuth_steps,
            step_m=closeup_step,
            los_step_m=closeup_los,
        )
        path = out_dir / fname
        save_blind_map(
            path,
            crop,
            origin_crop,
            cover,
            rmin,
            paint_m,
            title,
            xlim=xlim,
            ylim=ylim,
        )
        map_meta.append(
            {
                "product": key,
                "kind": "closeup",
                "path": str(path),
                "paint_range_m": paint_m,
                "rmin_m": rmin,
                "covered_km2": area / 1.0e6,
                "dem_cell_m": crop.cell_m,
            }
        )

    print("\nLoading island DEM (downsample cache)...")
    island = load_eden_full(
        dem_root=Path(args.dem_root) if args.dem_root else None,
        cache_path=ISLAND_CACHE,
        downsample=args.dem_downsample,
    )
    origin_island = radar_origin(island, xyz)
    land_m2 = land_area_m2(island)
    island_step = max(island.cell_m, 16.0)
    print(
        "  island %dx%d cell=%.1f m  land=%.1f km^2"
        % (island.width, island.height, island.cell_m, land_m2 / 1.0e6)
    )

    island_specs = [
        (
            "us_search",
            products["us_search"]["effective_rmin_m"],
            US_RANGE_M,
            "US PD search 7 km — 105 m hole (circle; cell too coarse to fill)",
            "gbrs_blind_us_search_island.png",
        ),
        (
            "ussr_search",
            products["ussr_search"]["effective_rmin_m"],
            USSR_RANGE_M,
            "USSR P-18 search 10 km — ~1.05 km TX blanking hole",
            "gbrs_blind_ussr_search_island.png",
        ),
    ]
    print("\n--- Island DEM maps ---")
    for key, rmin, paint_m, title, fname in island_specs:
        cover, area = paint_station(
            island,
            origin_island,
            paint_m,
            rmin,
            azimuth_steps=args.island_azimuth_steps,
            step_m=island_step,
            los_step_m=island_step,
        )
        path = out_dir / fname
        save_blind_map(
            path,
            island,
            origin_island,
            cover,
            rmin,
            paint_m,
            title,
        )
        union_m2 = float(np.count_nonzero(cover)) * island.cell_m * island.cell_m
        land_pct = 0.0
        if land_m2 > 0.0:
            land_pct = 100.0 * union_m2 / land_m2
        map_meta.append(
            {
                "product": key,
                "kind": "island",
                "path": str(path),
                "paint_range_m": paint_m,
                "rmin_m": rmin,
                "covered_km2": area / 1.0e6,
                "land_coverage_pct": land_pct,
                "dem_cell_m": island.cell_m,
            }
        )
        print("    land coverage %.1f%% (%.2f km^2)" % (land_pct, area / 1.0e6))

    all_pass = True
    for row in products.values():
        if not row["pass"]:
            all_pass = False

    report = {
        "formula": "Rmin = c * (pulse_width_s + receiver_recovery_s) / 2",
        "c_m_s": C_LIGHT,
        "station_xyz": list(xyz),
        "radar_origin_crop": list(origin_crop),
        "target_agl_m": TARGET_AGL_M,
        "radar_mast_agl_m": RADAR_MAST_AGL_M,
        "old_floors_removed_m": {"search": 40.0, "wlr": 15.0},
        "products": products,
        "maps": map_meta,
        "pass": all_pass,
    }
    report_path = out_dir / "gbrs_blind_zone_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("\nWrote %s" % report_path)
    if all_pass:
        print("ALL CHECKS PASSED")
        return 0
    print("CHECKS FAILED")
    return 1


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="GBRS pulse-eclipsing blind zone DEM maps and Pd checks"
    )
    parser.add_argument(
        "--out-dir",
        default=str(OUT_DIR),
        help="Output directory",
    )
    parser.add_argument(
        "--dem-root",
        default=str(PROFILE_EDEN),
        help="RDF DemData/GM_Eden root",
    )
    parser.add_argument(
        "--dem-downsample",
        type=int,
        default=8,
        help="Island DEM downsample (native cell from manifest)",
    )
    parser.add_argument(
        "--trials",
        type=int,
        default=80,
        help="Beam-center Monte Carlo trials per PD product",
    )
    parser.add_argument(
        "--closeup-azimuth-steps",
        type=int,
        default=360,
        help="Rays for native close-up maps",
    )
    parser.add_argument(
        "--island-azimuth-steps",
        type=int,
        default=360,
        help="Rays for island maps",
    )
    return parser


if __name__ == "__main__":
    sys.exit(run(build_arg_parser().parse_args()))
