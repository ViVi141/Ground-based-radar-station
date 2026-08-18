#!/usr/bin/env python3
"""Export a downsampled Eden DEM crop for the GBRS web coverage viewer."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

TOOLS = Path(__file__).resolve().parent.parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from gbrs_eden_dem import DEFAULT_RADAR_XYZ, load_eden_crop  # noqa: E402
from simulate_conflict_radar_coverage import RADAR_MAST_AGL_M  # noqa: E402
from simulate_gbrs_blind_zone import CROP_CACHE, radar_origin  # noqa: E402

WEB_DIR = Path(__file__).resolve().parent
DATA_DIR = WEB_DIR / "data"


def export(n: int = 192, half_m: float = 6500.0) -> Path:
    dem = load_eden_crop(
        radar_xyz=DEFAULT_RADAR_XYZ,
        radius_m=9000.0,
        cache_path=CROP_CACHE,
    )
    origin = radar_origin(dem, DEFAULT_RADAR_XYZ)
    ox, _oy, oz = origin

    xs = np.linspace(ox - half_m, ox + half_m, n, dtype=np.float64)
    zs = np.linspace(oz - half_m, oz + half_m, n, dtype=np.float64)
    terrain = np.full((n, n), np.nan, dtype=np.float32)
    surface = np.zeros((n, n), dtype=np.uint8)
    for iz in range(n):
        for ix in range(n):
            ok, ty, surf = dem.sample(float(xs[ix]), float(zs[iz]))
            if not ok:
                continue
            terrain[iz, ix] = ty
            surface[iz, ix] = surf

    finite = terrain[np.isfinite(terrain)]
    y_min = 0.0
    y_max = 1.0
    if finite.size > 0:
        y_min = float(np.min(finite))
        y_max = float(np.max(finite))

    cell_m = (2.0 * half_m) / float(n - 1)
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    terrain.tofile(DATA_DIR / "terrain.f32")
    surface.tofile(DATA_DIR / "surface.u8")
    meta = {
        "width": n,
        "height": n,
        "cell_m": cell_m,
        "origin_x": float(xs[0]),
        "origin_z": float(zs[0]),
        "y_min": y_min,
        "y_max": y_max,
        "radar": [origin[0], origin[1], origin[2]],
        "mast_agl_m": RADAR_MAST_AGL_M,
        "half_m": half_m,
        "source": dem.source,
    }
    meta_path = DATA_DIR / "meta.json"
    meta_path.write_text(json.dumps(meta, indent=2), encoding="utf-8")
    print(
        "Exported %dx%d cell=%.1f m  y=[%.1f, %.1f] -> %s"
        % (n, n, cell_m, y_min, y_max, DATA_DIR)
    )
    return meta_path


if __name__ == "__main__":
    size = 192
    if len(sys.argv) > 1:
        size = int(sys.argv[1])
    export(n=size)
