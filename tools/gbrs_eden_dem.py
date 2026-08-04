#!/usr/bin/env python3
"""Load a local crop of profile RDF DEM (Eden V3 CSV) for GBRS offline sim.

Source (game bake):
  Documents/My Games/ArmaReforgerWorkbench/profile/RDF/DemData/GM_Eden/

Matches RDF_DemRuntimeCache sampling: cell_m, terrain_y, surface_class.
Caches a NPZ crop under tools/out/ for fast reloads.
"""

from __future__ import annotations

import math
import os
from dataclasses import dataclass
from pathlib import Path

import numpy as np

PROFILE_EDEN = (
    Path.home()
    / "Documents"
    / "My Games"
    / "ArmaReforgerWorkbench"
    / "profile"
    / "RDF"
    / "DemData"
    / "GM_Eden"
)

# User-spawned US station from prior GBRS log.
DEFAULT_RADAR_XYZ = (4771.01, 27.8448, 11214.7)


@dataclass
class EdenDemCrop:
    terrain: np.ndarray  # [iz, ix] float32
    surface: np.ndarray  # [iz, ix] uint8
    cell_m: float
    bounds_min_x: float
    bounds_min_z: float
    origin_ix: int  # global cell index of crop[0,0]
    origin_iz: int
    source: str

    @property
    def height(self) -> int:
        return int(self.terrain.shape[0])

    @property
    def width(self) -> int:
        return int(self.terrain.shape[1])

    def world_to_local(self, world_x: float, world_z: float) -> tuple[int, int]:
        gix = int(math.floor((world_x - self.bounds_min_x) / self.cell_m))
        giz = int(math.floor((world_z - self.bounds_min_z) / self.cell_m))
        lx = gix - self.origin_ix
        lz = giz - self.origin_iz
        return lx, lz

    def sample(self, world_x: float, world_z: float) -> tuple[bool, float, int]:
        lx, lz = self.world_to_local(world_x, world_z)
        if lx < 0 or lz < 0 or lx >= self.width or lz >= self.height:
            return False, 0.0, 0
        y = float(self.terrain[lz, lx])
        if not math.isfinite(y):
            return False, 0.0, 0
        return True, y, int(self.surface[lz, lx])

    def get_cell_size_m(self) -> float:
        return self.cell_m


def _parse_manifest(path: Path) -> dict[str, float | str | int]:
    kv: dict[str, str] = {}
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        magic = handle.readline().strip()
        if magic != "RDF_DEM_MANIFEST_V3":
            raise ValueError("bad manifest magic: %s" % magic)
        for line in handle:
            line = line.strip()
            if not line or " " not in line:
                continue
            key, _, val = line.partition(" ")
            kv[key] = val
    return {
        "world": kv["world"],
        "bounds_min_x": float(kv["bounds_min_x"]),
        "bounds_min_z": float(kv["bounds_min_z"]),
        "bounds_max_x": float(kv["bounds_max_x"]),
        "bounds_max_z": float(kv["bounds_max_z"]),
        "cell_m": float(kv["cell_m"]),
        "tile_cells": int(float(kv["tile_cells"])),
        "tile_count_x": int(float(kv["tile_count_x"])),
        "tile_count_z": int(float(kv["tile_count_z"])),
    }


def _load_tile_arrays(
    path: Path, size: int
) -> tuple[np.ndarray, np.ndarray] | None:
    terrain = np.full((size, size), np.nan, dtype=np.float32)
    surface = np.zeros((size, size), dtype=np.uint8)
    found = False
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line or not line[0].isdigit():
                continue
            parts = line.split()
            if len(parts) < 11:
                continue
            try:
                local_ix = int(float(parts[0]))
                local_iz = int(float(parts[1]))
                terrain_y = float(parts[2])
                surface_class = int(float(parts[9]))
            except ValueError:
                continue
            if local_ix < 0 or local_iz < 0 or local_ix >= size or local_iz >= size:
                continue
            terrain[local_iz, local_ix] = terrain_y
            surface[local_iz, local_ix] = surface_class
            found = True
    if not found:
        return None
    return terrain, surface


def load_eden_crop(
    radar_xyz: tuple[float, float, float] = DEFAULT_RADAR_XYZ,
    radius_m: float = 9000.0,
    dem_root: Path | None = None,
    cache_path: Path | None = None,
) -> EdenDemCrop:
    root = dem_root if dem_root is not None else PROFILE_EDEN
    man = _parse_manifest(root / "manifest.csv")
    cell_m = float(man["cell_m"])
    tile_cells = int(man["tile_cells"])
    bmin_x = float(man["bounds_min_x"])
    bmin_z = float(man["bounds_min_z"])
    tile_count_x = int(man["tile_count_x"])
    tile_count_z = int(man["tile_count_z"])

    rx, _ry, rz = radar_xyz
    if cache_path is not None and cache_path.is_file():
        data = np.load(cache_path)
        return EdenDemCrop(
            terrain=np.asarray(data["terrain"], dtype=np.float32),
            surface=np.asarray(data["surface"], dtype=np.uint8),
            cell_m=float(data["cell_m"]),
            bounds_min_x=float(data["bounds_min_x"]),
            bounds_min_z=float(data["bounds_min_z"]),
            origin_ix=int(data["origin_ix"]),
            origin_iz=int(data["origin_iz"]),
            source=str(data["source"]) if "source" in data.files else str(cache_path),
        )

    gix0 = int(math.floor((rx - radius_m - bmin_x) / cell_m))
    giz0 = int(math.floor((rz - radius_m - bmin_z) / cell_m))
    gix1 = int(math.floor((rx + radius_m - bmin_x) / cell_m))
    giz1 = int(math.floor((rz + radius_m - bmin_z) / cell_m))
    gix0 = max(0, gix0)
    giz0 = max(0, giz0)
    gix1 = min(tile_count_x * tile_cells - 1, gix1)
    giz1 = min(tile_count_z * tile_cells - 1, giz1)

    # Align crop to tile boundaries for simpler stitching.
    tile_ix0 = gix0 // tile_cells
    tile_iz0 = giz0 // tile_cells
    tile_ix1 = gix1 // tile_cells
    tile_iz1 = giz1 // tile_cells
    origin_ix = tile_ix0 * tile_cells
    origin_iz = tile_iz0 * tile_cells
    width = (tile_ix1 - tile_ix0 + 1) * tile_cells
    height = (tile_iz1 - tile_iz0 + 1) * tile_cells

    terrain = np.full((height, width), np.nan, dtype=np.float32)
    surface = np.zeros((height, width), dtype=np.uint8)
    tiles_dir = root / "tiles"
    loaded = 0
    for tiz in range(tile_iz0, tile_iz1 + 1):
        for tix in range(tile_ix0, tile_ix1 + 1):
            path = tiles_dir / ("tile_%d_%d.csv" % (tix, tiz))
            if not path.is_file():
                continue
            arrays = _load_tile_arrays(path, tile_cells)
            if arrays is None:
                continue
            t_arr, s_arr = arrays
            row0 = (tiz - tile_iz0) * tile_cells
            col0 = (tix - tile_ix0) * tile_cells
            terrain[row0 : row0 + tile_cells, col0 : col0 + tile_cells] = t_arr
            surface[row0 : row0 + tile_cells, col0 : col0 + tile_cells] = s_arr
            loaded += 1

    crop = EdenDemCrop(
        terrain=terrain,
        surface=surface,
        cell_m=cell_m,
        bounds_min_x=bmin_x,
        bounds_min_z=bmin_z,
        origin_ix=origin_ix,
        origin_iz=origin_iz,
        source="profile:%s tiles=%d" % (root, loaded),
    )
    if cache_path is not None:
        cache_path.parent.mkdir(parents=True, exist_ok=True)
        np.savez_compressed(
            cache_path,
            terrain=terrain,
            surface=surface,
            cell_m=np.float64(cell_m),
            bounds_min_x=np.float64(bmin_x),
            bounds_min_z=np.float64(bmin_z),
            origin_ix=np.int32(origin_ix),
            origin_iz=np.int32(origin_iz),
            source=np.asarray(crop.source),
            radar_x=np.float64(rx),
            radar_z=np.float64(rz),
            radius_m=np.float64(radius_m),
        )
    return crop


def load_eden_full(
    dem_root: Path | None = None,
    cache_path: Path | None = None,
    downsample: int = 1,
) -> EdenDemCrop:
    """Load the full GM_Eden DEM (optionally downsampled for island-wide maps).

    downsample=4 turns native 4 m cells into 16 m cells.
    """
    root = dem_root if dem_root is not None else PROFILE_EDEN
    if cache_path is not None and cache_path.is_file():
        data = np.load(cache_path)
        return EdenDemCrop(
            terrain=np.asarray(data["terrain"], dtype=np.float32),
            surface=np.asarray(data["surface"], dtype=np.uint8),
            cell_m=float(data["cell_m"]),
            bounds_min_x=float(data["bounds_min_x"]),
            bounds_min_z=float(data["bounds_min_z"]),
            origin_ix=int(data["origin_ix"]),
            origin_iz=int(data["origin_iz"]),
            source=str(data["source"]) if "source" in data.files else str(cache_path),
        )

    if downsample < 1:
        downsample = 1

    man = _parse_manifest(root / "manifest.csv")
    cell_m = float(man["cell_m"])
    tile_cells = int(man["tile_cells"])
    bmin_x = float(man["bounds_min_x"])
    bmin_z = float(man["bounds_min_z"])
    tile_count_x = int(man["tile_count_x"])
    tile_count_z = int(man["tile_count_z"])
    full_w = tile_count_x * tile_cells
    full_h = tile_count_z * tile_cells
    out_w = (full_w + downsample - 1) // downsample
    out_h = (full_h + downsample - 1) // downsample

    terrain = np.full((out_h, out_w), np.nan, dtype=np.float32)
    surface = np.zeros((out_h, out_w), dtype=np.uint8)
    tiles_dir = root / "tiles"
    loaded = 0
    for tiz in range(tile_count_z):
        for tix in range(tile_count_x):
            path = tiles_dir / ("tile_%d_%d.csv" % (tix, tiz))
            if not path.is_file():
                continue
            arrays = _load_tile_arrays(path, tile_cells)
            if arrays is None:
                continue
            t_arr, s_arr = arrays
            t_ds = t_arr[::downsample, ::downsample]
            s_ds = s_arr[::downsample, ::downsample]
            row0 = (tiz * tile_cells) // downsample
            col0 = (tix * tile_cells) // downsample
            row1 = row0 + t_ds.shape[0]
            col1 = col0 + t_ds.shape[1]
            if row1 > out_h:
                row1 = out_h
            if col1 > out_w:
                col1 = out_w
            rh = row1 - row0
            cw = col1 - col0
            terrain[row0:row1, col0:col1] = t_ds[:rh, :cw]
            surface[row0:row1, col0:col1] = s_ds[:rh, :cw]
            loaded += 1

    crop = EdenDemCrop(
        terrain=terrain,
        surface=surface,
        cell_m=cell_m * float(downsample),
        bounds_min_x=bmin_x,
        bounds_min_z=bmin_z,
        origin_ix=0,
        origin_iz=0,
        source="profile_full:%s tiles=%d ds=%d" % (root, loaded, downsample),
    )
    if cache_path is not None:
        cache_path.parent.mkdir(parents=True, exist_ok=True)
        np.savez_compressed(
            cache_path,
            terrain=terrain,
            surface=surface,
            cell_m=np.float64(crop.cell_m),
            bounds_min_x=np.float64(bmin_x),
            bounds_min_z=np.float64(bmin_z),
            origin_ix=np.int32(0),
            origin_iz=np.int32(0),
            source=np.asarray(crop.source),
        )
    return crop


def los_probe(
    dem: EdenDemCrop,
    ox: float,
    oy: float,
    oz: float,
    tx: float,
    ty: float,
    tz: float,
    step_m: float = 8.0,
    slack_m: float = 2.0,
) -> tuple[bool, float, float, float]:
    """Returns (clear, hit_fraction, max_h_obs_m, max_u).

    Matches PhysicalDetect ObstacleHeightAboveLos idea: terrain - slack - yLos.
    """
    dx = tx - ox
    dy = ty - oy
    dz = tz - oz
    dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    if dist < 1.0:
        return True, 1.0, 0.0, 0.5
    steps = max(2, int(dist / max(1.0, step_m)))
    first_u = None
    max_h = -1.0e9
    max_u = 0.5
    for i in range(1, steps):
        u = i / float(steps)
        x = ox + dx * u
        y_los = oy + dy * u
        z = oz + dz * u
        ok, terrain_y, _surf = dem.sample(x, z)
        if not ok:
            continue
        h_obs = terrain_y - slack_m - y_los
        if h_obs > max_h:
            max_h = h_obs
            max_u = u
        if h_obs > 0.0 and first_u is None:
            first_u = u
    if first_u is None:
        return True, 1.0, 0.0, max_u
    return False, float(first_u), float(max_h), float(max_u)


def pick_target_on_bearing(
    dem: EdenDemCrop,
    radar_xyz: tuple[float, float, float],
    range_m: float,
    bearing_deg: float,
    target_agl_m: float,
) -> tuple[float, float, float, bool, float, int] | None:
    """Place target at slant-ish ground range along bearing; return xyz + dem sample."""
    rx, ry, rz = radar_xyz
    rad = math.radians(bearing_deg)
    # Enforce world: Atan2(z,x) style — x=cos, z=sin matches scanner Cos/Sin(az).
    tx = rx + range_m * math.cos(rad)
    tz = rz + range_m * math.sin(rad)
    ok, terrain_y, surf = dem.sample(tx, tz)
    if not ok:
        return None
    ty = terrain_y + target_agl_m
    return tx, ty, tz, True, terrain_y, surf


def find_land_bearing(
    dem: EdenDemCrop,
    radar_xyz: tuple[float, float, float],
    test_range_m: float = 4000.0,
    target_agl_m: float = 80.0,
) -> float:
    """Pick a bearing with land surface (not water) at test_range."""
    best = 0.0
    best_score = -1.0
    for deg in range(0, 360, 5):
        placed = pick_target_on_bearing(
            dem, radar_xyz, test_range_m, float(deg), target_agl_m
        )
        if placed is None:
            continue
        _tx, _ty, _tz, _ok, terrain_y, surf = placed
        score = 0.0
        if surf != 1 and terrain_y > 0.5:
            score = 10.0 + terrain_y * 0.01
        elif terrain_y > 0.5:
            score = 1.0
        if score > best_score:
            best_score = score
            best = float(deg)
    return best
