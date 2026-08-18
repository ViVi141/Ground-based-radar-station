#!/usr/bin/env python3
"""Height-aware GBRS coverage: RHI slices + 3D DEM (not 80 m AGL top-down).

The top-down maps painted UH-1 cruise AGL only. Vehicles (~2 m), nap-of-earth
helicopters (~15 m), and high flyers see different terrain shadows. Pulse
eclipsing is a slant-range sphere, not a cylinder on the ground.

RHI (range vs ASL) is the readable product. 3D uses vertical exaggeration
so ridges show; the Rmin sphere is drawn true-scale on the RHI.

Outputs under tools/out/:
  gbrs_rhi_us_ridge.png
  gbrs_rhi_ussr_ridge.png
  gbrs_rhi_us_sea.png
  gbrs_rhi_wlr_ridge.png
  gbrs_volume_3d_us_agl_layers.png
  gbrs_volume_3d_min_agl.png
  gbrs_coverage_3d_report.json
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

from gbrs_eden_dem import (  # noqa: E402
    DEFAULT_RADAR_XYZ,
    PROFILE_EDEN,
    EdenDemCrop,
    load_eden_crop,
    los_probe,
)
from simulate_conflict_radar_coverage import RADAR_MAST_AGL_M  # noqa: E402
from simulate_gbrs_blind_zone import (  # noqa: E402
    CROP_CACHE,
    radar_origin,
)
import simulate_clutter_cover as s  # noqa: E402
import simulate_wlr_projectile as wlr  # noqa: E402

OUT_DIR = TOOLS / "out"

# Visibility codes for RHI pcolormesh.
CODE_INVALID = 0
CODE_TERRAIN = 1
CODE_BLIND = 2
CODE_SHADOW = 3
CODE_BEAM = 4
CODE_VISIBLE = 5

TWO_WAY_BEAM_MIN = 0.02
LOS_SLACK_M = 2.0
VERT_EXAG = 6.0
AGL_LAYERS_M = (5.0, 20.0, 80.0, 300.0)


def slant_range_m(ground_m: float, radar_asl: float, target_asl: float) -> float:
    dy = target_asl - radar_asl
    return math.hypot(ground_m, dy)


def elevation_deg(ground_m: float, radar_asl: float, target_asl: float) -> float:
    return math.degrees(math.atan2(target_asl - radar_asl, max(1.0, ground_m)))


def in_elevation_beam(hw: s.Hardware, el_deg: float) -> bool:
    gain, _name = s.strongest_beam_gain(hw, 0.0, el_deg)
    return gain >= TWO_WAY_BEAM_MIN


def beam_edge_degs(hw: s.Hardware) -> list[float]:
    edges: list[float] = []
    for beam in hw.elevation_beams:
        half = beam.beamwidth_deg * 0.5
        edges.append(beam.boresight_deg - half)
        edges.append(beam.boresight_deg + half)
    return edges


def sample_profile(
    dem: EdenDemCrop,
    origin: tuple[float, float, float],
    bearing_deg: float,
    range_max_m: float,
    step_m: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    ox, _oy, oz = origin
    rad = math.radians(bearing_deg)
    cos_a = math.cos(rad)
    sin_a = math.sin(rad)
    n = int(range_max_m / step_m) + 1
    ranges = np.zeros(n, dtype=np.float64)
    asl = np.full(n, np.nan, dtype=np.float64)
    water = np.zeros(n, dtype=np.bool_)
    for i in range(n):
        dist = float(i) * step_m
        ranges[i] = dist
        tx = ox + dist * cos_a
        tz = oz + dist * sin_a
        ok, terrain_y, surf = dem.sample(tx, tz)
        if not ok:
            continue
        asl[i] = terrain_y
        if surf == 1:
            water[i] = True
    return ranges, asl, water


def pick_bearings(
    dem: EdenDemCrop, origin: tuple[float, float, float], test_range_m: float = 2500.0
) -> tuple[float, float]:
    ox, _oy, oz = origin
    ridge_deg = 180.0
    ridge_y = -1.0e9
    sea_deg = 0.0
    sea_score = -1.0e9
    deg = 0.0
    while deg < 360.0:
        rad = math.radians(deg)
        tx = ox + test_range_m * math.cos(rad)
        tz = oz + test_range_m * math.sin(rad)
        ok, terrain_y, surf = dem.sample(tx, tz)
        if ok and terrain_y > ridge_y:
            ridge_y = terrain_y
            ridge_deg = deg
        if ok:
            score = 0.0
            if surf == 1:
                score = 1000.0 - terrain_y
            else:
                score = -terrain_y
            if score > sea_score:
                sea_score = score
                sea_deg = deg
        deg = deg + 10.0
    return ridge_deg, sea_deg


def rhi_codes(
    ranges: np.ndarray,
    terrain_asl: np.ndarray,
    radar_asl: float,
    asl_grid: np.ndarray,
    rmin_m: float,
    hw: s.Hardware,
    range_max_m: float,
) -> np.ndarray:
    n = ranges.size
    m = asl_grid.size
    codes = np.zeros((m, n), dtype=np.uint8)
    for i in range(n):
        r = float(ranges[i])
        t_asl = float(terrain_asl[i])
        if not math.isfinite(t_asl):
            continue
        if r > range_max_m + 0.5:
            continue
        for k in range(m):
            asl = float(asl_grid[k])
            if asl <= t_asl + 0.5:
                codes[k, i] = CODE_TERRAIN
                continue
            slant = slant_range_m(r, radar_asl, asl)
            if slant <= rmin_m:
                codes[k, i] = CODE_BLIND
                continue
            el = elevation_deg(r, radar_asl, asl)
            if not in_elevation_beam(hw, el):
                codes[k, i] = CODE_BEAM
                continue
            blocked = False
            if r > 1.0:
                j = 1
                while j < i:
                    rj = float(ranges[j])
                    tj = float(terrain_asl[j])
                    if math.isfinite(tj):
                        u = rj / r
                        y_los = radar_asl + (asl - radar_asl) * u
                        if tj > y_los + LOS_SLACK_M:
                            blocked = True
                            break
                    j = j + 1
            if blocked:
                codes[k, i] = CODE_SHADOW
            else:
                codes[k, i] = CODE_VISIBLE
    return codes


def save_rhi(
    path: Path,
    ranges: np.ndarray,
    asl_grid: np.ndarray,
    codes: np.ndarray,
    terrain_asl: np.ndarray,
    radar_asl: float,
    rmin_m: float,
    hw: s.Hardware,
    range_max_m: float,
    title: str,
) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import ListedColormap, BoundaryNorm

    cmap = ListedColormap(
        [
            "#1b2838",
            "#5c4033",
            "#c0392b",
            "#34495e",
            "#95a5a6",
            "#2ecc71",
        ]
    )
    bounds = [-0.5, 0.5, 1.5, 2.5, 3.5, 4.5, 5.5]
    norm = BoundaryNorm(bounds, cmap.N)

    fig, ax = plt.subplots(figsize=(14.0, 6.2), dpi=140)
    mesh = ax.pcolormesh(
        ranges,
        asl_grid,
        codes,
        cmap=cmap,
        norm=norm,
        shading="nearest",
        rasterized=True,
    )
    ax.plot(ranges, terrain_asl, color="#3d2914", linewidth=1.2, label="DEM terrain")
    ax.plot(0.0, radar_asl, "o", color="white", markeredgecolor="k", markersize=7, zorder=5)

    theta = np.linspace(0.0, math.pi, 180)
    ax.plot(
        rmin_m * np.sin(theta),
        radar_asl + rmin_m * np.cos(theta),
        color="#ff3355",
        linestyle="--",
        linewidth=1.4,
        label="Rmin sphere %.0f m" % rmin_m,
    )
    ax.plot(
        [range_max_m, range_max_m],
        [float(np.nanmin(asl_grid)), float(np.nanmax(asl_grid))],
        color="#f4f1de",
        linestyle=":",
        linewidth=1.0,
        label="instrumented %.0f m" % range_max_m,
    )
    r_line = np.linspace(80.0, range_max_m, 80)
    for el in beam_edge_degs(hw):
        y_line = radar_asl + r_line * math.tan(math.radians(el))
        ax.plot(r_line, y_line, color="#d4a84b", linewidth=0.7, alpha=0.85)

    cbar = fig.colorbar(mesh, ax=ax, ticks=[0, 1, 2, 3, 4, 5], fraction=0.03, pad=0.02)
    cbar.ax.set_yticklabels(
        ["off-map", "terrain", "pulse blind", "hill shadow", "out of beam", "detectable"]
    )
    ax.set_xlim(0.0, range_max_m)
    y0 = max(0.0, float(np.nanmin(terrain_asl)) - 20.0)
    y1 = max(y0 + 200.0, radar_asl + 650.0)
    ax.set_ylim(y0, y1)
    ax.set_xlabel("ground range (m)")
    ax.set_ylabel("ASL (m)")
    ax.set_title(title)
    ax.legend(loc="upper right", fontsize=8, framealpha=0.85)
    fig.tight_layout()
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path)
    plt.close(fig)


def window_grid(
    dem: EdenDemCrop,
    origin: tuple[float, float, float],
    half_m: float,
    n: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    ox, _oy, oz = origin
    xs = np.linspace(ox - half_m, ox + half_m, n)
    zs = np.linspace(oz - half_m, oz + half_m, n)
    xx, zz = np.meshgrid(xs, zs)
    terrain = np.full(xx.shape, np.nan, dtype=np.float64)
    land = np.zeros(xx.shape, dtype=np.bool_)
    for iz in range(n):
        for ix in range(n):
            ok, ty, surf = dem.sample(float(xx[iz, ix]), float(zz[iz, ix]))
            if not ok:
                continue
            terrain[iz, ix] = ty
            if surf != 1:
                land[iz, ix] = True
    return xx, zz, terrain, land


def classify_point(
    dem: EdenDemCrop,
    origin: tuple[float, float, float],
    tx: float,
    tz: float,
    target_asl: float,
    terrain_asl: float,
    rmin_m: float,
    range_max_m: float,
    hw: s.Hardware,
    los_step_m: float,
) -> int:
    if not math.isfinite(terrain_asl):
        return CODE_INVALID
    if target_asl <= terrain_asl + 0.5:
        return CODE_TERRAIN
    ox, oy, oz = origin
    ground = math.hypot(tx - ox, tz - oz)
    if ground > range_max_m + 0.5:
        return CODE_INVALID
    slant = slant_range_m(ground, oy, target_asl)
    if slant <= rmin_m:
        return CODE_BLIND
    el = elevation_deg(ground, oy, target_asl)
    if not in_elevation_beam(hw, el):
        return CODE_BEAM
    clear, _u, _h, _mu = los_probe(
        dem, ox, oy, oz, tx, target_asl, tz, step_m=los_step_m, slack_m=LOS_SLACK_M
    )
    if not clear:
        return CODE_SHADOW
    return CODE_VISIBLE


def min_visible_agl(
    dem: EdenDemCrop,
    origin: tuple[float, float, float],
    xx: np.ndarray,
    zz: np.ndarray,
    terrain: np.ndarray,
    rmin_m: float,
    range_max_m: float,
    hw: s.Hardware,
    los_step_m: float,
    agl_candidates: tuple[float, ...],
) -> np.ndarray:
    out = np.full(terrain.shape, np.nan, dtype=np.float64)
    h, w = terrain.shape
    for iz in range(h):
        for ix in range(w):
            t_asl = float(terrain[iz, ix])
            if not math.isfinite(t_asl):
                continue
            found = False
            for agl in agl_candidates:
                code = classify_point(
                    dem,
                    origin,
                    float(xx[iz, ix]),
                    float(zz[iz, ix]),
                    t_asl + agl,
                    t_asl,
                    rmin_m,
                    range_max_m,
                    hw,
                    los_step_m,
                )
                if code == CODE_VISIBLE:
                    out[iz, ix] = agl
                    found = True
                    break
            if not found:
                out[iz, ix] = np.nan
    return out


def save_agl_layers_3d(
    path: Path,
    xx: np.ndarray,
    zz: np.ndarray,
    terrain: np.ndarray,
    origin: tuple[float, float, float],
    vis_by_agl: dict[float, np.ndarray],
    title: str,
) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import cm

    ox, oy, oz = origin
    fig, axes = plt.subplots(
        2,
        2,
        figsize=(13.0, 12.0),
        dpi=130,
        subplot_kw={"projection": "3d"},
    )
    layers = list(vis_by_agl.keys())
    colors = ["#e74c3c", "#f39c12", "#2ecc71", "#3498db"]
    tmin = float(np.nanmin(terrain))
    tmax = float(np.nanmax(terrain))
    for ax, agl, color in zip(axes.ravel(), layers, colors):
        ax.plot_surface(
            xx,
            zz,
            terrain * VERT_EXAG,
            cmap=cm.terrain,
            linewidth=0,
            antialiased=False,
            alpha=0.85,
            vmin=tmin * VERT_EXAG,
            vmax=tmax * VERT_EXAG,
        )
        mask = vis_by_agl[agl]
        if np.any(mask):
            ax.scatter(
                xx[mask],
                zz[mask],
                (terrain[mask] + agl) * VERT_EXAG,
                c=color,
                s=4,
                alpha=0.55,
                linewidths=0,
            )
        ax.scatter([ox], [oz], [oy * VERT_EXAG], c="white", s=40, edgecolors="k")
        ax.set_title("AGL %.0f m — dots = detectable" % agl, fontsize=10)
        ax.set_xlabel("world X (m)")
        ax.set_ylabel("world Z (m)")
        ax.set_zlabel("ASL x%.0f (m)" % VERT_EXAG)
        ax.view_init(elev=28, azim=-70)
        ax.set_box_aspect((1.0, 1.0, 0.45))
    fig.suptitle(title, fontsize=13)
    fig.tight_layout()
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path)
    plt.close(fig)


def save_min_agl_3d(
    path: Path,
    xx: np.ndarray,
    zz: np.ndarray,
    terrain: np.ndarray,
    min_agl: np.ndarray,
    origin: tuple[float, float, float],
    rmin_m: float,
    title: str,
) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import cm
    from matplotlib.colors import Normalize

    ox, oy, oz = origin
    fig = plt.figure(figsize=(12.5, 10.0), dpi=140)
    ax = fig.add_subplot(111, projection="3d")
    tmin = float(np.nanmin(terrain))
    tmax = float(np.nanmax(terrain))
    ax.plot_surface(
        xx,
        zz,
        terrain * VERT_EXAG,
        cmap=cm.terrain,
        linewidth=0,
        antialiased=False,
        alpha=0.7,
        vmin=tmin * VERT_EXAG,
        vmax=tmax * VERT_EXAG,
    )
    vis = np.isfinite(min_agl)
    if np.any(vis):
        ax.scatter(
            xx[vis],
            zz[vis],
            (terrain[vis] + min_agl[vis]) * VERT_EXAG,
            c=min_agl[vis],
            cmap="cool",
            s=8,
            alpha=0.7,
            linewidths=0,
            norm=Normalize(vmin=5.0, vmax=300.0),
        )
    theta = np.linspace(0.0, 2.0 * math.pi, 96)
    ax.plot(
        ox + rmin_m * np.cos(theta),
        oz + rmin_m * np.sin(theta),
        np.full(theta.shape, oy * VERT_EXAG),
        color="#ff3355",
        linestyle="--",
        linewidth=1.6,
        label="Rmin ground circle %.0f m" % rmin_m,
    )
    ax.scatter([ox], [oz], [oy * VERT_EXAG], c="white", s=50, edgecolors="k")
    ax.set_xlabel("world X (m)")
    ax.set_ylabel("world Z (m)")
    ax.set_zlabel("ASL x%.0f (m)" % VERT_EXAG)
    ax.set_title(title)
    ax.view_init(elev=28, azim=-70)
    ax.set_box_aspect((1.0, 1.0, 0.5))
    mappable = cm.ScalarMappable(norm=Normalize(vmin=5.0, vmax=300.0), cmap="cool")
    mappable.set_array(min_agl[vis] if np.any(vis) else np.array([5.0]))
    fig.colorbar(mappable, ax=ax, shrink=0.55, label="lowest AGL that clears DEM + beam (m)")
    fig.tight_layout()
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path)
    plt.close(fig)


def land_visibility_stats(
    land: np.ndarray,
    vis_mask: np.ndarray,
    cell_area_m2: float,
) -> dict:
    land_n = int(np.count_nonzero(land))
    vis_n = int(np.count_nonzero(land & vis_mask))
    land_m2 = float(land_n) * cell_area_m2
    vis_m2 = float(vis_n) * cell_area_m2
    pct = 0.0
    if land_n > 0:
        pct = 100.0 * vis_n / land_n
    return {
        "land_km2": land_m2 / 1.0e6,
        "visible_km2": vis_m2 / 1.0e6,
        "visible_pct": pct,
    }


def wlr_hw() -> s.Hardware:
    cfg = wlr.ScanConfig(
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
    return wlr.build_hw(cfg, "US")


def run(args: argparse.Namespace) -> int:
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    xyz = DEFAULT_RADAR_XYZ

    print("Loading Eden crop...")
    dem = load_eden_crop(
        radar_xyz=xyz,
        radius_m=9000.0,
        dem_root=Path(args.dem_root) if args.dem_root else None,
        cache_path=CROP_CACHE,
    )
    origin = radar_origin(dem, xyz)
    ridge_deg, sea_deg = pick_bearings(dem, origin)
    print(
        "  origin=(%.1f, %.1f, %.1f)  ridge bearing=%.0f deg  sea=%.0f deg"
        % (origin[0], origin[1], origin[2], ridge_deg, sea_deg)
    )

    us_hw, _us_set = s.make_us()
    ussr_hw, _ussr_set = s.make_ussr()
    wlr_search_hw = wlr_hw()
    us_rmin = us_hw.min_detectable_range_m()
    ussr_rmin = ussr_hw.min_detectable_range_m()
    wlr_rmin = wlr_search_hw.min_detectable_range_m()

    asl_grid = np.linspace(0.0, 700.0, 141)
    rhi_step = 20.0

    rhi_jobs = [
        (
            "gbrs_rhi_us_ridge.png",
            us_hw,
            us_rmin,
            7000.0,
            ridge_deg,
            "US PD search RHI — ridge %.0f deg  (green=detectable, red=pulse sphere, grey=out of beam)"
            % ridge_deg,
        ),
        (
            "gbrs_rhi_ussr_ridge.png",
            ussr_hw,
            ussr_rmin,
            10000.0,
            ridge_deg,
            "USSR P-18 search RHI — same ridge  (1 km pulse sphere + hill shadow)",
        ),
        (
            "gbrs_rhi_us_sea.png",
            us_hw,
            us_rmin,
            7000.0,
            sea_deg,
            "US PD search RHI — sea/low bearing %.0f deg" % sea_deg,
        ),
        (
            "gbrs_rhi_wlr_ridge.png",
            wlr_search_hw,
            wlr_rmin,
            8000.0,
            ridge_deg,
            "US WLR RHI — looks up (15–55 deg). Ground vehicles sit in the grey lobe.",
        ),
    ]

    print("\n--- RHI (range vs ASL) ---")
    for fname, hw, rmin, rmax, bearing, title in rhi_jobs:
        ranges, terrain_asl, _water = sample_profile(dem, origin, bearing, rmax, rhi_step)
        codes = rhi_codes(ranges, terrain_asl, origin[1], asl_grid, rmin, hw, rmax)
        path = out_dir / fname
        save_rhi(
            path,
            ranges,
            asl_grid,
            codes,
            terrain_asl,
            origin[1],
            rmin,
            hw,
            rmax,
            title,
        )
        print("  wrote %s" % path.name)

    print("\n--- 3D volume (vertical exaggeration %.0fx) ---" % VERT_EXAG)
    half_m = 3500.0
    n = args.grid
    xx, zz, terrain, land = window_grid(dem, origin, half_m, n)
    dx = (2.0 * half_m) / max(1, n - 1)
    cell_area = dx * dx
    los_step = max(dem.cell_m * 4.0, 16.0)

    vis_by_agl: dict[float, np.ndarray] = {}
    layer_stats = {}
    for agl in AGL_LAYERS_M:
        mask = np.zeros(terrain.shape, dtype=np.bool_)
        h, w = terrain.shape
        for iz in range(h):
            for ix in range(w):
                t_asl = float(terrain[iz, ix])
                code = classify_point(
                    dem,
                    origin,
                    float(xx[iz, ix]),
                    float(zz[iz, ix]),
                    t_asl + agl,
                    t_asl,
                    us_rmin,
                    7000.0,
                    us_hw,
                    los_step,
                )
                if code == CODE_VISIBLE:
                    mask[iz, ix] = True
        vis_by_agl[agl] = mask
        stats = land_visibility_stats(land, mask, cell_area)
        layer_stats[str(int(agl))] = stats
        print(
            "  US search AGL %6.0f m  land visible %.1f%%  (%.2f km^2)"
            % (agl, stats["visible_pct"], stats["visible_km2"])
        )

    save_agl_layers_3d(
        out_dir / "gbrs_volume_3d_us_agl_layers.png",
        xx,
        zz,
        terrain,
        origin,
        vis_by_agl,
        "US PD search 7 km — same DEM, four target heights (ASL exaggerated %.0fx)"
        % VERT_EXAG,
    )
    print("  wrote gbrs_volume_3d_us_agl_layers.png")

    agl_candidates = (2.0, 5.0, 10.0, 20.0, 40.0, 80.0, 150.0, 300.0, 500.0)
    min_agl = min_visible_agl(
        dem,
        origin,
        xx,
        zz,
        terrain,
        us_rmin,
        7000.0,
        us_hw,
        los_step,
        agl_candidates,
    )
    save_min_agl_3d(
        out_dir / "gbrs_volume_3d_min_agl.png",
        xx,
        zz,
        terrain,
        min_agl,
        origin,
        us_rmin,
        "US PD — lowest AGL that is detectable (cool = must fly higher to clear the ridge)",
    )
    print("  wrote gbrs_volume_3d_min_agl.png")

    report = {
        "station_xyz": list(xyz),
        "radar_origin": list(origin),
        "ridge_bearing_deg": ridge_deg,
        "sea_bearing_deg": sea_deg,
        "vertical_exaggeration_3d": VERT_EXAG,
        "note": "Top-down maps used 80 m AGL only. RHI uses slant-range Rmin (sphere). 3D height is exaggerated.",
        "us_rmin_m": us_rmin,
        "ussr_rmin_m": ussr_rmin,
        "wlr_rmin_m": wlr_rmin,
        "us_search_land_visible_by_agl": layer_stats,
        "maps": [
            "gbrs_rhi_us_ridge.png",
            "gbrs_rhi_ussr_ridge.png",
            "gbrs_rhi_us_sea.png",
            "gbrs_rhi_wlr_ridge.png",
            "gbrs_volume_3d_us_agl_layers.png",
            "gbrs_volume_3d_min_agl.png",
        ],
    }
    report_path = out_dir / "gbrs_coverage_3d_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("Wrote %s" % report_path)
    return 0


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="GBRS RHI + 3D DEM coverage")
    parser.add_argument("--out-dir", default=str(OUT_DIR))
    parser.add_argument("--dem-root", default=str(PROFILE_EDEN))
    parser.add_argument(
        "--grid",
        type=int,
        default=56,
        help="3D window samples along each axis",
    )
    return parser


if __name__ == "__main__":
    sys.exit(run(build_arg_parser().parse_args()))
