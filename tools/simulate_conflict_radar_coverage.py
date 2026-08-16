#!/usr/bin/env python3
"""Parse Eden Conflict bases and simulate GBRS radar coverage maps.

Reads CTI_Campaign_Eden.ent (EBIN), places a search radar at each military /
town / major / MOB base, then ray-casts DEM LOS coverage at UH-1 cruise AGL.

Outputs under tools/out/:
  conflict_eden_bases.json
  conflict_eden_bases_map.png
  conflict_coverage_us_7km.png
  conflict_coverage_ussr_10km.png
  conflict_coverage_count_us.png
  conflict_coverage_report.json
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from gbrs_eden_dem import (  # noqa: E402
    PROFILE_EDEN,
    EdenDemCrop,
    load_eden_full,
    los_probe,
)

DEFAULT_ENT = Path(
    os.environ.get(
        "GBRS_EDEN_ENT",
        str(
            Path.home()
            / "Documents"
            / "arma_reforger_code"
            / "worlds"
            / "MP"
            / "CTI_Campaign_Eden.ent"
        ),
    )
)

# EBIN property marker used before vector3 coords/angles on Eden Conflict.
VEC3_MARKER = bytes([0x0D, 0x0B, 0x00, 0x08])

RADAR_MAST_AGL_M = 8.0
TARGET_AGL_M = 80.0
US_RANGE_M = 7000.0
USSR_RANGE_M = 10000.0

BASE_PREFIXES = (
    "MilitaryBase",
    "TownBase",
    "SmallBase",
    "MajorBase",
    "MainBase",
)


@dataclass
class ConflictBase:
    name: str
    kind: str
    x: float
    y: float
    z: float


def _collect_strings(data: bytes) -> list[tuple[int, str]]:
    out: list[tuple[int, str]] = []
    cur = bytearray()
    start = 0
    for i, byte in enumerate(data):
        if 32 <= byte <= 126:
            if not cur:
                start = i
            cur.append(byte)
        else:
            if len(cur) >= 4:
                out.append((start, cur.decode("ascii")))
            cur.clear()
    if len(cur) >= 4:
        out.append((start, cur.decode("ascii")))
    return out


def _is_world_coord(x: float, y: float, z: float) -> bool:
    if not math.isfinite(x) or not math.isfinite(y) or not math.isfinite(z):
        return False
    if y < -50.0 or y > 600.0:
        return False
    if x < -200.0 or x > 14000.0:
        return False
    if z < -200.0 or z > 14000.0:
        return False
    if abs(x) < 200.0 and abs(z) < 200.0:
        return False
    return True


def _kind_from_name(name: str) -> str:
    if name.startswith("MainBase"):
        return "MOB"
    if name.startswith("MajorBase"):
        return "major"
    if name.startswith("MilitaryBase"):
        return "military"
    if name.startswith("TownBase"):
        return "town"
    if name.startswith("SmallBase"):
        return "small"
    if name.startswith("Relay"):
        return "relay"
    return "other"


def parse_conflict_eden_bases(
    ent_path: Path,
    include_relays: bool = False,
) -> list[ConflictBase]:
    data = ent_path.read_bytes()
    if data[:4] != b"EBIN":
        raise ValueError("expected EBIN world file: %s" % ent_path)

    strings = _collect_strings(data)
    prefixes = BASE_PREFIXES
    if include_relays:
        prefixes = BASE_PREFIXES + ("Relay",)

    names = [
        (offset, text)
        for offset, text in strings
        if text.startswith(prefixes)
    ]

    bases: list[ConflictBase] = []
    seen: set[str] = set()
    for offset, name in names:
        if name in seen:
            continue
        region = data[offset : offset + len(name) + 320]
        world_hit: tuple[float, float, float] | None = None
        pos = 0
        while True:
            marker = region.find(VEC3_MARKER, pos)
            if marker < 0:
                break
            floff = marker + len(VEC3_MARKER)
            if floff + 12 > len(region):
                break
            x, y, z = struct.unpack_from("<fff", region, floff)
            if _is_world_coord(x, y, z):
                world_hit = (x, y, z)
                break
            pos = marker + 1

        if world_hit is None:
            print("WARNING: no coords for %s" % name)
            continue

        seen.add(name)
        bases.append(
            ConflictBase(
                name=name,
                kind=_kind_from_name(name),
                x=float(world_hit[0]),
                y=float(world_hit[1]),
                z=float(world_hit[2]),
            )
        )

    bases.sort(key=lambda b: (b.kind, b.name))
    return bases


def _radar_origin(
    dem: EdenDemCrop,
    base: ConflictBase,
    mast_agl_m: float,
) -> tuple[float, float, float]:
    ok, terrain_y, _surf = dem.sample(base.x, base.z)
    if ok:
        return base.x, terrain_y + mast_agl_m, base.z
    return base.x, base.y + mast_agl_m, base.z


def cover_base_rays(
    dem: EdenDemCrop,
    origin: tuple[float, float, float],
    range_m: float,
    target_agl_m: float,
    cover: np.ndarray,
    count: np.ndarray | None,
    azimuth_steps: int,
    step_m: float,
    los_step_m: float,
) -> float:
    """Paint coverage into cover/count grids. Returns covered area m^2 for this radar."""
    ox, oy, oz = origin
    cell = dem.cell_m
    h = dem.height
    w = dem.width
    covered_cells = 0
    visited = np.zeros((h, w), dtype=np.bool_)

    for ai in range(azimuth_steps):
        az = (2.0 * math.pi) * (ai / float(azimuth_steps))
        cos_a = math.cos(az)
        sin_a = math.sin(az)
        dist = step_m
        while dist <= range_m + 0.5:
            tx = ox + dist * cos_a
            tz = oz + dist * sin_a
            ok, terrain_y, surf = dem.sample(tx, tz)
            if not ok:
                break
            # Skip open water for land-coverage stats / paint.
            if surf == 1:
                dist += step_m
                continue
            ty = terrain_y + target_agl_m
            clear, _frac, _h, _u = los_probe(
                dem, ox, oy, oz, tx, ty, tz, step_m=los_step_m, slack_m=2.0
            )
            if not clear:
                break

            lx, lz = dem.world_to_local(tx, tz)
            if 0 <= lx < w and 0 <= lz < h:
                if not visited[lz, lx]:
                    visited[lz, lx] = True
                    covered_cells += 1
                cover[lz, lx] = 1
                if count is not None:
                    count[lz, lx] = count[lz, lx] + 1
            dist += step_m

    return float(covered_cells) * cell * cell


def land_area_m2(dem: EdenDemCrop) -> float:
    valid = np.isfinite(dem.terrain)
    land = valid & (dem.surface != 1)
    return float(np.count_nonzero(land)) * dem.cell_m * dem.cell_m


def _terrain_rgb(dem: EdenDemCrop) -> np.ndarray:
    y = dem.terrain.astype(np.float64)
    valid = np.isfinite(y)
    water = dem.surface == 1
    rgb = np.zeros((dem.height, dem.width, 3), dtype=np.float32)
    if np.any(valid):
        ymin = float(np.nanmin(y))
        ymax = float(np.nanmax(y))
        span = max(1.0, ymax - ymin)
        norm = np.clip((y - ymin) / span, 0.0, 1.0)
        rgb[..., 0] = 0.18 + 0.55 * norm
        rgb[..., 1] = 0.28 + 0.45 * norm
        rgb[..., 2] = 0.16 + 0.25 * norm
        rgb[~valid] = 0.05
    rgb[water] = (0.12, 0.28, 0.48)
    return rgb


def _save_map(
    path: Path,
    dem: EdenDemCrop,
    bases: list[ConflictBase],
    cover: np.ndarray | None = None,
    count: np.ndarray | None = None,
    title: str = "",
    cover_alpha: float = 0.45,
) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import Normalize

    rgb = _terrain_rgb(dem)
    extent = [
        dem.bounds_min_x + dem.origin_ix * dem.cell_m,
        dem.bounds_min_x + (dem.origin_ix + dem.width) * dem.cell_m,
        dem.bounds_min_z + dem.origin_iz * dem.cell_m,
        dem.bounds_min_z + (dem.origin_iz + dem.height) * dem.cell_m,
    ]

    fig_w = 12.0
    fig_h = 12.0 * (dem.height / max(1, dem.width))
    fig, ax = plt.subplots(figsize=(fig_w, max(8.0, fig_h)), dpi=140)
    ax.imshow(rgb, origin="lower", extent=extent, interpolation="nearest")

    if cover is not None:
        overlay = np.zeros((dem.height, dem.width, 4), dtype=np.float32)
        hit = cover >= 1
        overlay[hit, 0] = 0.25
        overlay[hit, 1] = 0.95
        overlay[hit, 2] = 0.35
        overlay[hit, 3] = cover_alpha
        ax.imshow(
            overlay,
            origin="lower",
            extent=extent,
            interpolation="nearest",
        )

    if count is not None:
        cm = np.ma.masked_where(count < 1, count.astype(np.float32))
        im = ax.imshow(
            cm,
            origin="lower",
            extent=extent,
            cmap="plasma",
            alpha=0.65,
            interpolation="nearest",
            norm=Normalize(vmin=1, vmax=max(1, int(count.max()))),
        )
        fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04, label="radars covering")

    kind_color = {
        "MOB": "#ff4d4d",
        "major": "#ff9f1a",
        "military": "#ffe66d",
        "town": "#4ecdc4",
        "small": "#95e1d3",
        "relay": "#aaaaaa",
    }
    for base in bases:
        color = kind_color.get(base.kind, "#ffffff")
        ax.plot(base.x, base.z, "o", color=color, markersize=5, markeredgecolor="k", markeredgewidth=0.4)
        if base.kind in ("MOB", "major", "military"):
            ax.text(
                base.x + 80.0,
                base.z + 80.0,
                base.name.replace("Base", ""),
                color="white",
                fontsize=6,
                ha="left",
                va="bottom",
            )

    ax.set_aspect("equal")
    ax.set_xlabel("world X (m)")
    ax.set_ylabel("world Z (m)")
    ax.set_title(title)
    ax.set_xlim(extent[0], extent[1])
    ax.set_ylim(extent[2], extent[3])
    fig.tight_layout()
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path)
    plt.close(fig)


def run(args: argparse.Namespace) -> int:
    ent_path = Path(args.ent)
    if not ent_path.is_file():
        print("ERROR: Conflict world not found: %s" % ent_path)
        return 1

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    bases = parse_conflict_eden_bases(ent_path, include_relays=args.include_relays)
    if not bases:
        print("ERROR: no bases parsed from %s" % ent_path)
        return 1

    bases_json = out_dir / "conflict_eden_bases.json"
    bases_json.write_text(
        json.dumps([asdict(b) for b in bases], indent=2),
        encoding="utf-8",
    )
    print("Parsed %d bases -> %s" % (len(bases), bases_json))
    for base in bases:
        print(
            "  %-10s %-28s  %8.1f %7.1f %8.1f"
            % (base.kind, base.name, base.x, base.y, base.z)
        )

    dem_cache = out_dir / ("eden_full_ds%d.npz" % args.dem_downsample)
    print("Loading Eden DEM (downsample=%d)..." % args.dem_downsample)
    dem = load_eden_full(
        dem_root=Path(args.dem_root) if args.dem_root else None,
        cache_path=dem_cache,
        downsample=args.dem_downsample,
    )
    print(
        "DEM %dx%d cell=%.1fm source=%s"
        % (dem.width, dem.height, dem.cell_m, dem.source)
    )

    land_m2 = land_area_m2(dem)
    print("Land area ≈ %.1f km^2" % (land_m2 / 1.0e6))

    _save_map(
        out_dir / "conflict_eden_bases_map.png",
        dem,
        bases,
        title="Eden Conflict bases (CTI_Campaign_Eden)",
    )

    presets = [
        ("us", US_RANGE_M, "US AN/TPN-19 PD SEARCH 7 km"),
        ("ussr", USSR_RANGE_M, "USSR RPL-5 PD SEARCH 10 km"),
    ]

    report: dict = {
        "ent": str(ent_path),
        "base_count": len(bases),
        "bases": [asdict(b) for b in bases],
        "dem_cell_m": dem.cell_m,
        "land_area_km2": land_m2 / 1.0e6,
        "target_agl_m": TARGET_AGL_M,
        "radar_mast_agl_m": RADAR_MAST_AGL_M,
        "presets": {},
    }

    for key, range_m, label in presets:
        print("Simulating %s..." % label)
        cover = np.zeros((dem.height, dem.width), dtype=np.uint8)
        count = np.zeros((dem.height, dem.width), dtype=np.uint16)
        per_base = []
        for base in bases:
            origin = _radar_origin(dem, base, RADAR_MAST_AGL_M)
            area = cover_base_rays(
                dem,
                origin,
                range_m,
                TARGET_AGL_M,
                cover,
                count,
                azimuth_steps=args.azimuth_steps,
                step_m=args.step_m,
                los_step_m=args.los_step_m,
            )
            per_base.append(
                {
                    "name": base.name,
                    "kind": base.kind,
                    "origin": {"x": origin[0], "y": origin[1], "z": origin[2]},
                    "covered_km2": area / 1.0e6,
                }
            )
            print(
                "  %s  covered≈%.2f km^2"
                % (base.name, area / 1.0e6)
            )

        union_cells = int(np.count_nonzero(cover))
        union_m2 = float(union_cells) * dem.cell_m * dem.cell_m
        coverage_pct = 0.0
        if land_m2 > 0.0:
            coverage_pct = 100.0 * union_m2 / land_m2

        _save_map(
            out_dir / ("conflict_coverage_%s_%.0fkm.png" % (key, range_m / 1000.0)),
            dem,
            bases,
            cover=cover,
            title="%s — island land coverage %.1f%%" % (label, coverage_pct),
        )
        _save_map(
            out_dir / ("conflict_coverage_count_%s.png" % key),
            dem,
            bases,
            count=count,
            title="%s — overlapping radar count" % label,
        )

        report["presets"][key] = {
            "label": label,
            "range_m": range_m,
            "union_covered_km2": union_m2 / 1.0e6,
            "land_coverage_pct": coverage_pct,
            "max_overlap": int(count.max()) if count.size else 0,
            "per_base": per_base,
        }
        print(
            "  UNION land coverage %.1f%% (%.1f km^2)"
            % (coverage_pct, union_m2 / 1.0e6)
        )

    report_path = out_dir / "conflict_coverage_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("Wrote %s" % report_path)
    return 0


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Eden Conflict base radar coverage maps"
    )
    parser.add_argument(
        "--ent",
        default=str(DEFAULT_ENT),
        help="Path to CTI_Campaign_Eden.ent",
    )
    parser.add_argument(
        "--out-dir",
        default=str(TOOLS / "out"),
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
        help="DEM downsample factor (native cell from manifest, default 2 m; 8 => 16 m)",
    )
    parser.add_argument(
        "--azimuth-steps",
        type=int,
        default=360,
        help="Rays per radar",
    )
    parser.add_argument(
        "--step-m",
        type=float,
        default=64.0,
        help="Ground step along each ray (m)",
    )
    parser.add_argument(
        "--los-step-m",
        type=float,
        default=24.0,
        help="LOS sample step (m)",
    )
    parser.add_argument(
        "--include-relays",
        action="store_true",
        help="Also place radars on radio relays",
    )
    return parser


if __name__ == "__main__":
    sys.exit(run(build_arg_parser().parse_args()))
