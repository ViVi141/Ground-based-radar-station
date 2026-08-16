#!/usr/bin/env python3
"""Offline WLR tracker validation: detect tracker fragmentation and PPI clutter.

Why this exists:
  The older WLR offline scripts validated SNR and illumination windows only.
  They did NOT simulate RDF_RadarProjectileTracker, measurement noise, track
  association, miss counting, or track pruning.  In-game WLR therefore looked
  like "天女散花": one shell split into many confirmed tracks.

This script simulates:
  - 82 mm HE ballistic arcs (AirDrag + gravity, 3D)
  - RDF-style physical detection via simulate_clutter_cover.snr_at
  - RDF measurement noise (range/az/el CRLB model)
  - GNN tracker association / confirm / miss / coast / prune
  - track fragmentation metrics per physical shell
  - PPI clutter proxy (number of alive confirmed tracks over time)

It is intentionally a model, not a 1:1 game-loop port, but it is close enough
to catch the fragmentation failure mode before entering Workbench.
"""

from __future__ import annotations

import argparse
import json
import math
import random
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import simulate_clutter_cover as s
from simulate_wlr_projectile import ScanConfig, build_hw, build_settings, snr_at

# ---------------------------------------------------------------------------
# Physical / scenario constants
# ---------------------------------------------------------------------------
C_LIGHT = 299792458.0
GRAVITY_M_S2 = -9.81
AIR_DRAG_SHELL_82MM_HE = 0.000615
DT_S = 0.05
PROJECTILE_RCS_M2 = 0.01
SHELL_V0_MS = 210.0
SHELL_ELEV_DEG = 55.0
RADAR_AGL_M = 8.0
LAUNCH_RANGE_M = 80.0
LAUNCH_HEIGHT_M = 8.0
FIRE_INTERVAL_S = 6.0
SIM_DURATION_S = 30.0
UPDATE_INTERVAL_S = 0.05
WLR_MIN_HITS = 3
WLR_MIN_SPAN_S = 0.8

# Current GBRS WLR US geometry.
US_CFG = ScanConfig(
    beamwidth_deg=25.0,
    rpm=10.0,
    snr_gate_db=4.0,
    update_interval_s=0.05,
    elevation_beams=[
        ("mortar_horizon", 8.0, 16.0, 0.0),
        ("mortar_low", 18.0, 22.0, 0.0),
        ("mortar_mid", 35.0, 26.0, 0.0),
        ("mortar_high", 55.0, 26.0, -0.5),
    ],
)

USSR_CFG = ScanConfig(
    beamwidth_deg=30.0,
    rpm=6.0,
    snr_gate_db=5.0,
    update_interval_s=0.05,
    elevation_beams=[
        ("mortar_horizon", 8.0, 16.0, 0.0),
        ("mortar_low", 18.0, 22.0, 0.0),
        ("mortar_mid", 35.0, 26.0, 0.0),
        ("mortar_high", 55.0, 26.0, -0.5),
    ],
)

# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------
@dataclass
class Shell:
    shell_id: int
    launch_az_deg: float
    samples: list  # list[(t, pos, vel)]

@dataclass
class Plot:
    shell_id: int
    time_s: float
    pos: tuple
    range_m: float
    az_deg: float
    el_deg: float
    snr_db: float

class Track:
    __slots__ = (
        "track_id", "shell_id", "last_pos", "last_vel", "last_time",
        "hit_count", "miss_count", "coast_elapsed", "confirmed",
        "first_time", "last_hit_time", "ever_hits", "last_snr_db",
    )

    def __init__(self, track_id: int, shell_id: int, plot: Plot):
        self.track_id = track_id
        self.shell_id = shell_id
        self.last_pos = plot.pos
        self.last_vel = (0.0, 0.0, 0.0)
        self.last_time = plot.time_s
        self.hit_count = 1
        self.miss_count = 0
        self.coast_elapsed = 0.0
        self.confirmed = False
        self.first_time = plot.time_s
        self.last_hit_time = plot.time_s
        self.ever_hits = 1
        self.last_snr_db = plot.snr_db

# ---------------------------------------------------------------------------
# Ballistics / geometry
# ---------------------------------------------------------------------------
def build_shell_trajectory(
    shell_id: int,
    launch_az_deg: float,
    radar_origin: tuple,
    v0_ms: float = SHELL_V0_MS,
    elev_deg: float = SHELL_ELEV_DEG,
    t_max_s: float = 40.0,
) -> Shell:
    az_r = math.radians(launch_az_deg)
    el_r = math.radians(elev_deg)
    dir_x = math.cos(az_r)
    dir_z = math.sin(az_r)
    launch_pos = (
        radar_origin[0] + LAUNCH_RANGE_M * dir_x,
        radar_origin[1] + LAUNCH_HEIGHT_M,
        radar_origin[2] + LAUNCH_RANGE_M * dir_z,
    )
    vx0 = v0_ms * math.cos(el_r) * dir_x
    vy0 = v0_ms * math.sin(el_r)
    vz0 = v0_ms * math.cos(el_r) * dir_z

    x, y, z = launch_pos
    vx, vy, vz = vx0, vy0, vz0
    samples = []
    t = 0.0
    while t <= t_max_s and y >= launch_pos[1] - 0.5:
        speed = math.sqrt(vx * vx + vy * vy + vz * vz)
        ax = -AIR_DRAG_SHELL_82MM_HE * speed * vx
        ay = GRAVITY_M_S2 - AIR_DRAG_SHELL_82MM_HE * speed * vy
        az = -AIR_DRAG_SHELL_82MM_HE * speed * vz
        vx += ax * DT_S
        vy += ay * DT_S
        vz += az * DT_S
        x += vx * DT_S
        y += vy * DT_S
        z += vz * DT_S
        t += DT_S
        if y >= launch_pos[1] - 0.5:
            samples.append((t, (x, y, z), (vx, vy, vz)))
    return Shell(shell_id, launch_az_deg, samples)

def sample_at(shell: Shell, t: float):
    best = None
    for item in shell.samples:
        if item[0] <= t + 1e-9:
            best = item
        else:
            break
    return best

def polar(radar_origin: tuple, pos: tuple):
    dx = pos[0] - radar_origin[0]
    dy = pos[1] - radar_origin[1]
    dz = pos[2] - radar_origin[2]
    rng = math.sqrt(dx * dx + dy * dy + dz * dz)
    if rng < 1e-6:
        return 0.0, 0.0, 0.0, (0.0, 0.0, 0.0)
    az = math.degrees(math.atan2(dz, dx))
    el = math.degrees(math.atan2(dy, math.sqrt(dx * dx + dz * dz)))
    return rng, az, el, (dx, dy, dz)

def angle_delta_deg(a: float, b: float) -> float:
    d = (a - b) % 360.0
    if d > 180.0:
        d -= 360.0
    return abs(d)

# ---------------------------------------------------------------------------
# RDF measurement noise (simplified CRLB model)
# ---------------------------------------------------------------------------
def range_bin_m(hw: s.Hardware) -> float:
    if hw.bandwidth_hz > 1.0:
        return max(1.0, C_LIGHT / (2.0 * hw.bandwidth_hz))
    return max(1.0, C_LIGHT * hw.pulse_width_s * 0.5)

def apply_measurement_noise(
    hw: s.Hardware,
    radar_origin: tuple,
    rng_m: float,
    az_deg: float,
    el_deg: float,
    snr_db: float,
    noise_scale: float,
    range_bias_m: float,
    az_bias_deg: float,
    el_bias_deg: float,
    rng: random.Random,
):
    snr_lin = 10.0 ** (snr_db / 10.0)
    snr_eff = max(snr_lin, 1.0)
    denom = 1.6 * math.sqrt(2.0 * snr_eff)
    if denom < 0.001:
        denom = 0.001

    rb = range_bin_m(hw)
    range_sigma = (rb / denom) * noise_scale
    az_sigma = (hw.az_beamwidth_deg / denom) * noise_scale
    el_sigma = (20.0 / denom) * noise_scale  # typical WLR elevation beamwidth proxy

    mr = rng_m + range_bias_m
    if range_sigma > 0.0:
        mr += rng.gauss(0.0, range_sigma)
    ma = az_deg + az_bias_deg
    if az_sigma > 0.0:
        ma += rng.gauss(0.0, az_sigma)
    me = el_deg + el_bias_deg
    if el_sigma > 0.0:
        me += rng.gauss(0.0, el_sigma)

    az_r = math.radians(ma)
    el_r = math.radians(me)
    dx = mr * math.cos(el_r) * math.cos(az_r)
    dy = mr * math.sin(el_r)
    dz = mr * math.cos(el_r) * math.sin(az_r)
    pos = (radar_origin[0] + dx, radar_origin[1] + dy, radar_origin[2] + dz)
    return mr, ma, me, pos

# ---------------------------------------------------------------------------
# Simple GNN tracker model
# ---------------------------------------------------------------------------
def predicted_polar(track: Track, radar_origin: tuple, time_s: float):
    dt = max(0.0, time_s - track.last_time)
    pos = (
        track.last_pos[0] + track.last_vel[0] * dt,
        track.last_pos[1] + track.last_vel[1] * dt,
        track.last_pos[2] + track.last_vel[2] * dt,
    )
    return polar(radar_origin, pos)

def update_tracker(
    tracks: list,
    next_track_id: list,
    plots: list,
    radar_origin: tuple,
    time_s: float,
    gate_range_m: float,
    gate_az_deg: float,
    confirm_hits: int,
    max_misses: int,
    coast_max_s: float,
):
    assigned_track = [-1] * len(tracks)
    used_plot = [False] * len(plots)

    # Greedy nearest-neighbour association.
    pairs = []
    for ti, tr in enumerate(tracks):
        pr, pa, pe, _ = predicted_polar(tr, radar_origin, time_s)
        for pi, pl in enumerate(plots):
            d_rng = abs(pl.range_m - pr)
            d_az = angle_delta_deg(pl.az_deg, pa)
            if d_rng <= gate_range_m and d_az <= gate_az_deg:
                cost = d_rng / max(gate_range_m, 1.0) + d_az / max(gate_az_deg, 0.1)
                pairs.append((cost, ti, pi))
    pairs.sort(key=lambda x: x[0])

    for cost, ti, pi in pairs:
        if assigned_track[ti] >= 0 or used_plot[pi]:
            continue
        assigned_track[ti] = pi
        used_plot[pi] = True
        tr = tracks[ti]
        pl = plots[pi]
        dt = max(0.001, pl.time_s - tr.last_time)
        pred = (
            tr.last_pos[0] + tr.last_vel[0] * dt,
            tr.last_pos[1] + tr.last_vel[1] * dt,
            tr.last_pos[2] + tr.last_vel[2] * dt,
        )
        alpha = 0.5
        beta = 0.2
        new_pos = (
            pred[0] + (pl.pos[0] - pred[0]) * alpha,
            pred[1] + (pl.pos[1] - pred[1]) * alpha,
            pred[2] + (pl.pos[2] - pred[2]) * alpha,
        )
        new_vel = (
            tr.last_vel[0] + (pl.pos[0] - pred[0]) * (beta / dt),
            tr.last_vel[1] + (pl.pos[1] - pred[1]) * (beta / dt),
            tr.last_vel[2] + (pl.pos[2] - pred[2]) * (beta / dt),
        )
        tr.last_pos = new_pos
        tr.last_vel = new_vel
        tr.last_time = pl.time_s
        tr.hit_count += 1
        tr.ever_hits += 1
        tr.miss_count = 0
        tr.coast_elapsed = 0.0
        tr.last_hit_time = pl.time_s
        tr.last_snr_db = pl.snr_db
        if tr.hit_count >= confirm_hits:
            tr.confirmed = True

    # Miss / coast for unassigned tracks.
    for ti, tr in enumerate(tracks):
        if assigned_track[ti] >= 0:
            continue
        dt = max(0.0, time_s - tr.last_time)
        tr.miss_count += 1
        tr.coast_elapsed += dt
        tr.last_pos = (
            tr.last_pos[0] + tr.last_vel[0] * dt,
            tr.last_pos[1] + tr.last_vel[1] * dt,
            tr.last_pos[2] + tr.last_vel[2] * dt,
        )
        tr.last_time = time_s

    # Birth new tracks from unassigned plots.
    for pi, pl in enumerate(plots):
        if used_plot[pi]:
            continue
        tr = Track(next_track_id[0], pl.shell_id, pl)
        next_track_id[0] += 1
        tracks.append(tr)

    # Prune.
    i = len(tracks) - 1
    while i >= 0:
        tr = tracks[i]
        drop = False
        if tr.miss_count > max_misses:
            drop = True
        if coast_max_s > 0.0 and tr.coast_elapsed > coast_max_s:
            drop = True
        if drop:
            tracks.pop(i)
        i -= 1

# ---------------------------------------------------------------------------
# Simulation
# ---------------------------------------------------------------------------
def simulate(
    faction: str,
    cfg: ScanConfig,
    range_m: float,
    noise_scale: float,
    range_bias_m: float,
    az_bias_deg: float,
    el_bias_deg: float,
    gate_range_m: float,
    gate_az_deg: float,
    max_misses: int,
    coast_max_s: float,
    duration_s: float = SIM_DURATION_S,
    fire_interval_s: float = FIRE_INTERVAL_S,
    seed: int = 12345,
):
    hw = build_hw(cfg, faction)
    settings = build_settings(cfg, range_m)
    rng = random.Random(seed)
    radar_origin = (0.0, RADAR_AGL_M, 0.0)
    launch_az = 80.0  # demo fires from a fixed bearing near the radar

    tracks: list = []
    next_track_id = [1]
    shells: list = []
    next_shell_id = 1
    next_fire_s = 1.0

    max_alive = 0
    max_confirmed_alive = 0
    alive_hist = []
    confirmed_hist = []
    track_shells: dict[int, set] = {}

    t = 0.0
    while t <= duration_s + 1e-9:
        while next_fire_s <= t + 1e-9:
            shells.append(build_shell_trajectory(next_shell_id, launch_az, radar_origin))
            next_shell_id += 1
            next_fire_s += fire_interval_s

        # Advance shells and collect detected plots.
        plots = []
        for shell in shells:
            sample = sample_at(shell, t)
            if sample is None:
                continue
            st, pos, vel = sample
            rng_m, az_deg, el_deg, delta = polar(radar_origin, pos)
            los_len = math.sqrt(delta[0] ** 2 + delta[1] ** 2 + delta[2] ** 2)
            if los_len < 1e-6:
                continue
            los = (delta[0] / los_len, delta[1] / los_len, delta[2] / los_len)
            radial_ms = abs(vel[0] * los[0] + vel[1] * los[1] + vel[2] * los[2])
            az_off = angle_delta_deg(az_deg, launch_az)  # stare at launch bearing
            res = snr_at(
                hw,
                settings,
                rng_m,
                PROJECTILE_RCS_M2,
                radial_ms,
                el_deg,
                az_off,
                seed=(shell.shell_id * 100000 + int(t * 1000)),
            )
            if res.detected_snr:
                mr, ma, me, mpos = apply_measurement_noise(
                    hw,
                    radar_origin,
                    rng_m,
                    az_deg,
                    el_deg,
                    res.snr_db,
                    noise_scale,
                    range_bias_m,
                    az_bias_deg,
                    el_bias_deg,
                    rng,
                )
                plots.append(Plot(shell.shell_id, t, mpos, mr, ma, me, res.snr_db))

        update_tracker(
            tracks,
            next_track_id,
            plots,
            radar_origin,
            t,
            gate_range_m,
            gate_az_deg,
            confirm_hits=2,
            max_misses=max_misses,
            coast_max_s=coast_max_s,
        )

        alive = len(tracks)
        confirmed = sum(1 for tr in tracks if tr.confirmed)
        max_alive = max(max_alive, alive)
        max_confirmed_alive = max(max_confirmed_alive, confirmed)
        alive_hist.append(alive)
        confirmed_hist.append(confirmed)

        for tr in tracks:
            if tr.shell_id not in track_shells:
                track_shells[tr.shell_id] = set()
            track_shells[tr.shell_id].add(tr.track_id)

        t += UPDATE_INTERVAL_S

    total_shells = next_shell_id - 1
    total_tracks_ever = next_track_id[0] - 1
    tracks_per_shell = {
        sid: len(track_shells.get(sid, set())) for sid in range(1, total_shells + 1)
    }
    fragmented_shells = sum(1 for v in tracks_per_shell.values() if v > 1)

    stable_shells = 0
    for sid in range(1, total_shells + 1):
        ok = False
        for tr in tracks:
            if tr.shell_id == sid and tr.confirmed and tr.ever_hits >= WLR_MIN_HITS:
                if (tr.last_hit_time - tr.first_time) >= WLR_MIN_SPAN_S:
                    ok = True
                    break
        if ok:
            stable_shells += 1

    mean_alive = sum(alive_hist) / len(alive_hist) if alive_hist else 0.0
    mean_confirmed = sum(confirmed_hist) / len(confirmed_hist) if confirmed_hist else 0.0

    return {
        "faction": faction,
        "range_m": range_m,
        "noise_scale": noise_scale,
        "gate_az_deg": gate_az_deg,
        "gate_range_m": gate_range_m,
        "max_misses": max_misses,
        "coast_max_s": coast_max_s,
        "total_shells": total_shells,
        "total_tracks_ever": total_tracks_ever,
        "tracks_per_shell": tracks_per_shell,
        "fragmented_shells": fragmented_shells,
        "stable_shells": stable_shells,
        "max_alive": max_alive,
        "mean_alive": round(mean_alive, 2),
        "max_confirmed_alive": max_confirmed_alive,
        "mean_confirmed_alive": round(mean_confirmed, 2),
        "fragmentation_ratio": round(total_tracks_ever / max(1, total_shells), 2),
        "verdict": "PASS" if fragmented_shells == 0 and stable_shells == total_shells else "FAIL",
    }

# ---------------------------------------------------------------------------
# CLI / report
# ---------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(description="WLR tracker fragmentation offline validation")
    parser.add_argument("--faction", choices=["US", "USSR", "both"], default="both")
    parser.add_argument("--out", default=str(TOOLS / "out" / "wlr_tracker_validation.json"))
    args = parser.parse_args()

    factions = ["US", "USSR"] if args.faction == "both" else [args.faction]
    report = {
        "script": "simulate_wlr_tracker.py",
        "description": "Offline WLR tracker fragmentation / PPI clutter validation",
        "factions": {},
    }

    print("=" * 72)
    print("GBRS WLR tracker fragmentation offline validation")
    print("=" * 72)

    for faction in factions:
        cfg = US_CFG if faction == "US" else USSR_CFG
        range_m = 8000.0 if faction == "US" else 10000.0

        # Current GBRS WLR settings.
        current = simulate(
            faction,
            cfg,
            range_m,
            noise_scale=0.0,
            range_bias_m=0.0,
            az_bias_deg=0.0,
            el_bias_deg=0.0,
            gate_range_m=600.0,
            gate_az_deg=8.0,
            max_misses=96,
            coast_max_s=12.0,
        )

        # Legacy noisy settings for comparison.
        legacy = simulate(
            faction,
            cfg,
            range_m,
            noise_scale=3.5,
            range_bias_m=5.0,
            az_bias_deg=0.3,
            el_bias_deg=0.22,
            gate_range_m=400.0,
            gate_az_deg=4.0,
            max_misses=6,
            coast_max_s=4.0,
        )

        report["factions"][faction] = {
            "current_gbrs": current,
            "legacy": legacy,
            "summary": {
                "current_verdict": current["verdict"],
                "legacy_verdict": legacy["verdict"],
                "current_tracks_per_shell": current["tracks_per_shell"],
                "legacy_tracks_per_shell": legacy["tracks_per_shell"],
            },
        }

        for name, r in (("CURRENT (noise off, gates 8/600, maxMiss 96)", current),
                        ("LEGACY (noise 3.5, gates 4/400, maxMiss 6)", legacy)):
            print(f"\n[{faction}] {name}")
            print(f"  shells={r['total_shells']} tracksEver={r['total_tracks_ever']}")
            print(f"  tracks_per_shell={r['tracks_per_shell']}")
            print(f"  fragmented_shells={r['fragmented_shells']} stable_shells={r['stable_shells']}")
            print(f"  max_alive={r['max_alive']} mean_alive={r['mean_alive']} "
                  f"max_confirmed_alive={r['max_confirmed_alive']} mean_confirmed_alive={r['mean_confirmed_alive']}")
            print(f"  fragmentation_ratio={r['fragmentation_ratio']} verdict={r['verdict']}")

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"\nWrote {out_path}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
