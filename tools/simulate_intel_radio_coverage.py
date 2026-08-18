#!/usr/bin/env python3
"""Offline balance of GBRS intel-net range vs Conflict / GM radio relays.

Models the live rules in GBRS_IntelRadioNet:
  - station TX radius 2 km (was 25 km)
  - handhelds do not hop
  - a radar injects into a faction net if it sits inside a SEND-capable
    coverage radio, or within 2 km of that radio
  - listeners hear if they are inside 2 km of the station, or inside a
    RECEIVE coverage radio on the same connected net
  - Game Master RelayTransceiver chains hop the same way (range-only, XZ)

Uses Eden Conflict base positions from tools/out/conflict_eden_bases.json.
Radio physics here is vanilla range-only (DistanceSqXZ), not DEM LOS.
Maps reuse the Conflict coverage DEM hillshade + green overlay.

Outputs under tools/out/:
  intel_radio_coverage_report.json
  intel_radio_listen_isolated_2km.png
  intel_radio_listen_old_25km.png
  intel_radio_listen_north_1km.png
  intel_radio_listen_north_2km.png
  intel_radio_listen_south_2km.png
  intel_radio_coverage_us_2km.png
  intel_radio_coverage_ussr_2km.png
  intel_radio_count_us.png
  intel_radio_count_ussr.png
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from gbrs_eden_dem import PROFILE_EDEN, EdenDemCrop, load_eden_full  # noqa: E402
from simulate_conflict_radar_coverage import (  # noqa: E402
    ConflictBase,
    _save_map,
    land_area_m2,
)

OUT_DIR = TOOLS / "out"
BASES_JSON = OUT_DIR / "conflict_eden_bases.json"

RADAR_TX_M = 2000.0
OLD_RADAR_TX_M = 25000.0
HANDHELD_PRC77_M = 2000.0
HANDHELD_PRC68_M = 1300.0
BASE_RADIO_DEFAULT_M = 1000.0
ANTENNA_01_M = 2000.0
RELAY_TOWER_M = 3000.0
TX_TOWER_SMALL_M = 4000.0
TX_TOWER_LARGE_M = 5000.0
COMMAND_VEHICLE_M = 2000.0


def dicts_to_bases(rows: list[dict]) -> list[ConflictBase]:
    out: list[ConflictBase] = []
    for row in rows:
        y = 0.0
        if "y" in row:
            y = float(row["y"])
        out.append(
            ConflictBase(
                name=str(row["name"]),
                kind=str(row["kind"]),
                x=float(row["x"]),
                y=y,
                z=float(row["z"]),
            )
        )
    return out


def dist_xz(a: ConflictBase, b: ConflictBase) -> float:
    dx = a.x - b.x
    dz = a.z - b.z
    return math.sqrt(dx * dx + dz * dz)


def nearest_neighbor(bases: list[ConflictBase]) -> list[dict]:
    rows = []
    for i, base in enumerate(bases):
        best_d = 1.0e12
        best_name = ""
        for j, other in enumerate(bases):
            if i == j:
                continue
            d = dist_xz(base, other)
            if d < best_d:
                best_d = d
                best_name = other.name
        rows.append(
            {
                "name": base.name,
                "kind": base.kind,
                "nn_m": best_d,
                "nn_name": best_name,
            }
        )
    rows.sort(key=lambda r: r["nn_m"])
    return rows


def faction_split(
    bases: list[ConflictBase],
) -> tuple[list[ConflictBase], list[ConflictBase]]:
    north = None
    south = None
    for base in bases:
        if base.name == "MainBaseNorth":
            north = base
        if base.name == "MainBaseSouth":
            south = base
    if north is None or south is None:
        raise RuntimeError("MOB bases missing")

    us = []
    ussr = []
    for base in bases:
        dn = dist_xz(base, north)
        ds = dist_xz(base, south)
        if dn <= ds:
            us.append(base)
        else:
            ussr.append(base)
    return us, ussr


def connected_components(
    bases: list[ConflictBase], radio_range_m: float
) -> list[list[int]]:
    n = len(bases)
    parent = list(range(n))

    def find(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def union(i: int, j: int) -> None:
        ri = find(i)
        rj = find(j)
        if ri != rj:
            parent[rj] = ri

    r2 = radio_range_m * radio_range_m
    for i in range(n):
        for j in range(i + 1, n):
            dx = bases[i].x - bases[j].x
            dz = bases[i].z - bases[j].z
            if (dx * dx + dz * dz) <= r2:
                union(i, j)

    groups: dict[int, list[int]] = {}
    for i in range(n):
        root = find(i)
        if root not in groups:
            groups[root] = []
        groups[root].append(i)
    comps = list(groups.values())
    comps.sort(key=len, reverse=True)
    return comps


def hq_component(
    bases: list[ConflictBase], radio_range_m: float, hq_name: str
) -> set[int]:
    comps = connected_components(bases, radio_range_m)
    for comp in comps:
        for idx in comp:
            if bases[idx].name == hq_name:
                return set(comp)
    return set()


def radar_can_inject(
    radar: ConflictBase,
    bases: list[ConflictBase],
    net: set[int],
    radio_range_m: float,
    radar_tx_m: float,
) -> bool:
    reach = radar_tx_m
    if radio_range_m > reach:
        reach = radio_range_m
    reach2 = reach * reach
    for idx in net:
        dx = radar.x - bases[idx].x
        dz = radar.z - bases[idx].z
        if (dx * dx + dz * dz) <= reach2:
            return True
    return False


def land_mask(dem: EdenDemCrop) -> np.ndarray:
    valid = np.isfinite(dem.terrain)
    return valid & (dem.surface != 1)


def apply_land(dem: EdenDemCrop, grid: np.ndarray) -> np.ndarray:
    out = grid.copy()
    out[~land_mask(dem)] = 0
    return out


def cover_stats(dem: EdenDemCrop, cover: np.ndarray, land_m2: float) -> tuple[float, float]:
    masked = apply_land(dem, cover)
    cells = int(np.count_nonzero(masked))
    m2 = float(cells) * dem.cell_m * dem.cell_m
    pct = 0.0
    if land_m2 > 0.0:
        pct = 100.0 * m2 / land_m2
    return m2 / 1.0e6, pct


def paint_disk(
    grid: np.ndarray,
    dem: EdenDemCrop,
    cx: float,
    cz: float,
    radius_m: float,
) -> None:
    if radius_m <= 0.0:
        return
    cell = dem.cell_m
    r = float(radius_m)
    lx0, lz0 = dem.world_to_local(cx - r, cz - r)
    lx1, lz1 = dem.world_to_local(cx + r, cz + r)
    if lx0 < 0:
        lx0 = 0
    if lz0 < 0:
        lz0 = 0
    if lx1 > dem.width - 1:
        lx1 = dem.width - 1
    if lz1 > dem.height - 1:
        lz1 = dem.height - 1
    if lx1 < lx0 or lz1 < lz0:
        return
    xs = dem.bounds_min_x + (dem.origin_ix + np.arange(lx0, lx1 + 1) + 0.5) * cell
    zs = dem.bounds_min_z + (dem.origin_iz + np.arange(lz0, lz1 + 1) + 0.5) * cell
    xx, zz = np.meshgrid(xs, zs)
    disk = (xx - cx) ** 2 + (zz - cz) ** 2 <= r * r
    sl = grid[lz0 : lz1 + 1, lx0 : lx1 + 1]
    sl[disk] = 1


def paint_listen(
    dem: EdenDemCrop,
    radar: ConflictBase,
    bases: list[ConflictBase],
    net: set[int],
    radio_range_m: float,
    radar_tx_m: float,
    inject: bool,
) -> np.ndarray:
    cover = np.zeros((dem.height, dem.width), dtype=np.uint8)
    paint_disk(cover, dem, radar.x, radar.z, radar_tx_m)
    if inject:
        for idx in net:
            node = bases[idx]
            paint_disk(cover, dem, node.x, node.z, radio_range_m)
    return apply_land(dem, cover)


def summarize_nn(rows: list[dict]) -> dict:
    vals = [r["nn_m"] for r in rows]
    arr = np.array(vals, dtype=np.float64)
    return {
        "min_m": float(arr.min()),
        "p25_m": float(np.percentile(arr, 25)),
        "median_m": float(np.median(arr)),
        "p75_m": float(np.percentile(arr, 75)),
        "max_m": float(arr.max()),
        "mean_m": float(arr.mean()),
        "below_1km": int(np.count_nonzero(arr < 1000.0)),
        "below_2km": int(np.count_nonzero(arr < 2000.0)),
        "below_3km": int(np.count_nonzero(arr < 3000.0)),
        "below_5km": int(np.count_nonzero(arr < 5000.0)),
        "n": int(arr.size),
    }


def eval_faction(
    label: str,
    hq_name: str,
    bases: list[ConflictBase],
    dem: EdenDemCrop,
    land_m2: float,
) -> dict:
    radio_cases = [
        ("base_1km", BASE_RADIO_DEFAULT_M),
        ("antenna_2km", ANTENNA_01_M),
        ("relay_3km", RELAY_TOWER_M),
        ("tower_5km", TX_TOWER_LARGE_M),
    ]
    out = {
        "label": label,
        "hq": hq_name,
        "base_count": len(bases),
        "radio_cases": {},
        "per_base_default": [],
    }
    for case_name, radio_m in radio_cases:
        comps = connected_components(bases, radio_m)
        net = hq_component(bases, radio_m, hq_name)
        sizes = [len(c) for c in comps]
        inject_count = 0
        listen_areas = []
        isolated_areas = []
        old_areas = []
        for base in bases:
            inject = radar_can_inject(base, bases, net, radio_m, RADAR_TX_M)
            if inject:
                inject_count += 1
            listen = paint_listen(
                dem, base, bases, net, radio_m, RADAR_TX_M, inject
            )
            local = paint_listen(
                dem, base, bases, set(), radio_m, RADAR_TX_M, False
            )
            old = paint_listen(
                dem, base, bases, set(), radio_m, OLD_RADAR_TX_M, False
            )
            listen_km2, _pct = cover_stats(dem, listen, land_m2)
            local_km2, _local_pct = cover_stats(dem, local, land_m2)
            old_km2, _old_pct = cover_stats(dem, old, land_m2)
            listen_areas.append(listen_km2)
            isolated_areas.append(local_km2)
            old_areas.append(old_km2)

        listen_arr = np.array(listen_areas, dtype=np.float64)
        local_arr = np.array(isolated_areas, dtype=np.float64)
        old_arr = np.array(old_areas, dtype=np.float64)
        largest = 0
        if sizes:
            largest = max(sizes)
        out["radio_cases"][case_name] = {
            "radio_range_m": radio_m,
            "hq_net_size": len(net),
            "component_count": len(comps),
            "largest_component": largest,
            "injecting_radars": inject_count,
            "listen_mean_km2": float(listen_arr.mean()),
            "listen_median_km2": float(np.median(listen_arr)),
            "listen_max_km2": float(listen_arr.max()),
            "local_mean_km2": float(local_arr.mean()),
            "old_25km_mean_km2": float(old_arr.mean()),
        }

    default_net = hq_component(bases, BASE_RADIO_DEFAULT_M, hq_name)
    antenna_net = hq_component(bases, ANTENNA_01_M, hq_name)
    relay_net = hq_component(bases, RELAY_TOWER_M, hq_name)
    tower_net = hq_component(bases, TX_TOWER_LARGE_M, hq_name)
    for base in bases:
        row = {
            "name": base.name,
            "kind": base.kind,
            "inject_1km": radar_can_inject(
                base, bases, default_net, BASE_RADIO_DEFAULT_M, RADAR_TX_M
            ),
            "inject_2km": radar_can_inject(
                base, bases, antenna_net, ANTENNA_01_M, RADAR_TX_M
            ),
            "inject_3km": radar_can_inject(
                base, bases, relay_net, RELAY_TOWER_M, RADAR_TX_M
            ),
            "inject_5km": radar_can_inject(
                base, bases, tower_net, TX_TOWER_LARGE_M, RADAR_TX_M
            ),
        }
        out["per_base_default"].append(row)
    return out


def pick_named(
    bases: list[ConflictBase], name: str, hq_name: str
) -> ConflictBase:
    for base in bases:
        if base.name == name:
            return base
    for base in bases:
        if base.kind == "town":
            return base
    for base in bases:
        if base.name != hq_name:
            return base
    return bases[0]


def find_isolated_site(
    dem: EdenDemCrop, bases: list[ConflictBase], stride: int
) -> tuple[ConflictBase, float]:
    land = land_mask(dem)
    bx = np.array([b.x for b in bases], dtype=np.float64)
    bz = np.array([b.z for b in bases], dtype=np.float64)
    best_d = -1.0
    best_x = 2000.0
    best_z = 2000.0
    cell = dem.cell_m
    for lz in range(0, dem.height, stride):
        for lx in range(0, dem.width, stride):
            if not land[lz, lx]:
                continue
            wx = dem.bounds_min_x + (dem.origin_ix + lx + 0.5) * cell
            wz = dem.bounds_min_z + (dem.origin_iz + lz + 0.5) * cell
            d2 = (bx - wx) ** 2 + (bz - wz) ** 2
            d = math.sqrt(float(d2.min()))
            if d > best_d:
                best_d = d
                best_x = wx
                best_z = wz
    site = ConflictBase(
        name="ForwardIsolated",
        kind="forward",
        x=best_x,
        y=0.0,
        z=best_z,
    )
    return site, best_d


def faction_union_maps(
    dem: EdenDemCrop,
    faction_bases: list[ConflictBase],
    hq_name: str,
    radio_m: float,
) -> tuple[np.ndarray, np.ndarray]:
    net = hq_component(faction_bases, radio_m, hq_name)
    cover = np.zeros((dem.height, dem.width), dtype=np.uint8)
    count = np.zeros((dem.height, dem.width), dtype=np.uint16)
    for base in faction_bases:
        inject = radar_can_inject(
            base, faction_bases, net, radio_m, RADAR_TX_M
        )
        one = paint_listen(
            dem, base, faction_bases, net, radio_m, RADAR_TX_M, inject
        )
        cover[one >= 1] = 1
        count[one >= 1] = count[one >= 1] + 1
    return apply_land(dem, cover), apply_land(dem, count)


def run(args: argparse.Namespace) -> int:
    bases_path = Path(args.bases)
    if not bases_path.is_file():
        print("ERROR: missing %s" % bases_path)
        return 1

    rows = json.loads(bases_path.read_text(encoding="utf-8"))
    if not rows:
        print("ERROR: empty base list")
        return 1

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    bases = dicts_to_bases(rows)
    dem_cache = out_dir / ("eden_full_ds%d.npz" % args.dem_downsample)
    print("Loading Eden DEM (downsample=%d)..." % args.dem_downsample)
    dem_root = None
    if args.dem_root:
        dem_root = Path(args.dem_root)
    dem = load_eden_full(
        dem_root=dem_root,
        cache_path=dem_cache,
        downsample=args.dem_downsample,
    )
    print(
        "DEM %dx%d cell=%.1fm source=%s"
        % (dem.width, dem.height, dem.cell_m, dem.source)
    )
    land_m2 = land_area_m2(dem)
    print("Land area ≈ %.1f km^2" % (land_m2 / 1.0e6))

    nn_rows = nearest_neighbor(bases)
    nn_stats = summarize_nn(nn_rows)
    us_bases, ussr_bases = faction_split(bases)

    us = eval_faction("north_us", "MainBaseNorth", us_bases, dem, land_m2)
    ussr = eval_faction("south_ussr", "MainBaseSouth", ussr_bases, dem, land_m2)

    isolated, isolated_nn_m = find_isolated_site(dem, bases, stride=16)
    isolated_2 = paint_listen(
        dem, isolated, bases, set(), 0.0, RADAR_TX_M, False
    )
    isolated_25 = paint_listen(
        dem, isolated, bases, set(), 0.0, OLD_RADAR_TX_M, False
    )
    iso_2_km2, iso_2_pct = cover_stats(dem, isolated_2, land_m2)
    iso_25_km2, iso_25_pct = cover_stats(dem, isolated_25, land_m2)

    us_example = pick_named(us_bases, "TownBaseEntre_Deux", "MainBaseNorth")
    ussr_example = pick_named(ussr_bases, "TownBaseChotain", "MainBaseSouth")
    us_net_1 = hq_component(us_bases, BASE_RADIO_DEFAULT_M, "MainBaseNorth")
    us_net_2 = hq_component(us_bases, ANTENNA_01_M, "MainBaseNorth")
    ussr_net_2 = hq_component(ussr_bases, ANTENNA_01_M, "MainBaseSouth")

    us_1_inject = radar_can_inject(
        us_example, us_bases, us_net_1, BASE_RADIO_DEFAULT_M, RADAR_TX_M
    )
    us_2_inject = radar_can_inject(
        us_example, us_bases, us_net_2, ANTENNA_01_M, RADAR_TX_M
    )
    ussr_2_inject = radar_can_inject(
        ussr_example, ussr_bases, ussr_net_2, ANTENNA_01_M, RADAR_TX_M
    )
    us_mask_1 = paint_listen(
        dem,
        us_example,
        us_bases,
        us_net_1,
        BASE_RADIO_DEFAULT_M,
        RADAR_TX_M,
        us_1_inject,
    )
    us_mask_2 = paint_listen(
        dem,
        us_example,
        us_bases,
        us_net_2,
        ANTENNA_01_M,
        RADAR_TX_M,
        us_2_inject,
    )
    ussr_mask_2 = paint_listen(
        dem,
        ussr_example,
        ussr_bases,
        ussr_net_2,
        ANTENNA_01_M,
        RADAR_TX_M,
        ussr_2_inject,
    )
    us_1_km2, us_1_pct = cover_stats(dem, us_mask_1, land_m2)
    us_2_km2, us_2_pct = cover_stats(dem, us_mask_2, land_m2)
    ussr_2_km2, ussr_2_pct = cover_stats(dem, ussr_mask_2, land_m2)

    us_union, us_count = faction_union_maps(
        dem, us_bases, "MainBaseNorth", ANTENNA_01_M
    )
    ussr_union, ussr_count = faction_union_maps(
        dem, ussr_bases, "MainBaseSouth", ANTENNA_01_M
    )
    us_u_km2, us_u_pct = cover_stats(dem, us_union, land_m2)
    ussr_u_km2, ussr_u_pct = cover_stats(dem, ussr_union, land_m2)
    us_max_overlap = 0
    ussr_max_overlap = 0
    if us_count.size:
        us_max_overlap = int(us_count.max())
    if ussr_count.size:
        ussr_max_overlap = int(ussr_count.max())

    hop_thresholds = [1000.0, 2000.0, 3000.0, 4000.0, 5000.0]
    all_comps = []
    for r in hop_thresholds:
        comps = connected_components(bases, r)
        largest = 0
        if comps:
            largest = len(comps[0])
        all_comps.append(
            {
                "radio_range_m": r,
                "components": len(comps),
                "largest": largest,
            }
        )

    report = {
        "source": str(bases_path),
        "date": "2026-08-19",
        "dem_cell_m": dem.cell_m,
        "land_area_km2": land_m2 / 1.0e6,
        "rules": {
            "radar_tx_m": RADAR_TX_M,
            "old_radar_tx_m": OLD_RADAR_TX_M,
            "handheld_prc77_m": HANDHELD_PRC77_M,
            "handheld_prc68_m": HANDHELD_PRC68_M,
            "handhelds_hop": False,
            "base_radio_default_m": BASE_RADIO_DEFAULT_M,
            "antenna_01_m": ANTENNA_01_M,
            "relay_tower_m": RELAY_TOWER_M,
            "tx_tower_large_m": TX_TOWER_LARGE_M,
            "command_vehicle_m": COMMAND_VEHICLE_M,
        },
        "geometry": {
            "ideal_2km_disk_km2": math.pi * (RADAR_TX_M / 1000.0) ** 2,
            "ideal_25km_disk_km2": math.pi * (OLD_RADAR_TX_M / 1000.0) ** 2,
            "isolated_forward": {
                "x": isolated.x,
                "z": isolated.z,
                "nn_base_m": isolated_nn_m,
                "listen_2km_km2": iso_2_km2,
                "listen_2km_land_pct": iso_2_pct,
                "listen_25km_km2": iso_25_km2,
                "listen_25km_land_pct": iso_25_pct,
                "handheld_chain_does_not_grow": True,
            },
        },
        "nn_spacing": nn_stats,
        "nn_rows": nn_rows,
        "island_components_if_all_bases_one_faction": all_comps,
        "north_us": us,
        "south_ussr": ussr,
        "examples": {
            "north_town_1km_base": {
                "radar": us_example.name,
                "inject": us_1_inject,
                "listen_km2": us_1_km2,
                "land_pct": us_1_pct,
            },
            "north_town_2km_antenna": {
                "radar": us_example.name,
                "inject": us_2_inject,
                "listen_km2": us_2_km2,
                "land_pct": us_2_pct,
            },
            "south_town_2km_antenna": {
                "radar": ussr_example.name,
                "inject": ussr_2_inject,
                "listen_km2": ussr_2_km2,
                "land_pct": ussr_2_pct,
            },
            "north_union_2km_antenna": {
                "listen_km2": us_u_km2,
                "land_pct": us_u_pct,
                "max_overlap": us_max_overlap,
            },
            "south_union_2km_antenna": {
                "listen_km2": ussr_u_km2,
                "land_pct": ussr_u_pct,
                "max_overlap": ussr_max_overlap,
            },
        },
    }

    report_path = out_dir / "intel_radio_coverage_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("Wrote %s" % report_path)
    print(
        "NN spacing median %.0f m  p25 %.0f  p75 %.0f  <2km %d/%d"
        % (
            nn_stats["median_m"],
            nn_stats["p25_m"],
            nn_stats["p75_m"],
            nn_stats["below_2km"],
            nn_stats["n"],
        )
    )
    print(
        "North HQ net 1/2/3/5 km: %s"
        % [
            us["radio_cases"][k]["hq_net_size"]
            for k in ["base_1km", "antenna_2km", "relay_3km", "tower_5km"]
        ]
    )
    print(
        "South HQ net 1/2/3/5 km: %s"
        % [
            ussr["radio_cases"][k]["hq_net_size"]
            for k in ["base_1km", "antenna_2km", "relay_3km", "tower_5km"]
        ]
    )

    isolated_bases = list(bases)
    isolated_bases.append(isolated)
    _save_map(
        out_dir / "intel_radio_listen_isolated_2km.png",
        dem,
        isolated_bases,
        cover=isolated_2,
        title=(
            "Intel listen · isolated 2 km TX — land coverage %.1f%% (%.1f km^2)"
            % (iso_2_pct, iso_2_km2)
        ),
    )
    _save_map(
        out_dir / "intel_radio_listen_old_25km.png",
        dem,
        isolated_bases,
        cover=isolated_25,
        title=(
            "Intel listen · old 25 km omni — land coverage %.1f%% (%.1f km^2)"
            % (iso_25_pct, iso_25_km2)
        ),
    )
    _save_map(
        out_dir / "intel_radio_listen_north_1km.png",
        dem,
        bases,
        cover=us_mask_1,
        title=(
            "Intel listen · %s · 2 km TX + 1 km HQ — land coverage %.1f%%"
            % (us_example.name, us_1_pct)
        ),
    )
    _save_map(
        out_dir / "intel_radio_listen_north_2km.png",
        dem,
        bases,
        cover=us_mask_2,
        title=(
            "Intel listen · %s · 2 km TX + 2 km antenna — land coverage %.1f%%"
            % (us_example.name, us_2_pct)
        ),
    )
    _save_map(
        out_dir / "intel_radio_listen_south_2km.png",
        dem,
        bases,
        cover=ussr_mask_2,
        title=(
            "Intel listen · %s · 2 km TX + 2 km antenna — land coverage %.1f%%"
            % (ussr_example.name, ussr_2_pct)
        ),
    )
    _save_map(
        out_dir / "intel_radio_coverage_us_2km.png",
        dem,
        bases,
        cover=us_union,
        title=(
            "North intel union · every owned base · 2 km antenna — land %.1f%%"
            % us_u_pct
        ),
    )
    _save_map(
        out_dir / "intel_radio_coverage_ussr_2km.png",
        dem,
        bases,
        cover=ussr_union,
        title=(
            "South intel union · every owned base · 2 km antenna — land %.1f%%"
            % ussr_u_pct
        ),
    )
    _save_map(
        out_dir / "intel_radio_count_us.png",
        dem,
        bases,
        count=us_count,
        title="North intel · 2 km antenna — overlapping listen count",
    )
    _save_map(
        out_dir / "intel_radio_count_ussr.png",
        dem,
        bases,
        count=ussr_count,
        title="South intel · 2 km antenna — overlapping listen count",
    )

    stale = [
        "intel_radio_nn_spacing.png",
        "intel_radio_mesh_components.png",
        "intel_radio_listen_km2.png",
        "intel_radio_north_map.png",
        "intel_radio_south_map.png",
    ]
    for name in stale:
        stale_path = out_dir / name
        if stale_path.is_file():
            stale_path.unlink()
            print("Removed histogram/plain map %s" % stale_path.name)

    print("Wrote DEM maps under %s" % out_dir)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bases", default=str(BASES_JSON))
    parser.add_argument("--out-dir", default=str(OUT_DIR))
    parser.add_argument(
        "--dem-root",
        default=str(PROFILE_EDEN),
        help="RDF DemData/GM_Eden root",
    )
    parser.add_argument(
        "--dem-downsample",
        type=int,
        default=8,
        help="DEM downsample factor (native ~2 m; 8 => 16 m)",
    )
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
