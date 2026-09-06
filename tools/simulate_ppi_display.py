#!/usr/bin/env python3
"""GBRS PPI display-path offline validation.

Models the GBRS workstation PPI stack on top of simulate_pd_tracker:
  - plot afterglow (frozen last hit, PpiDisplayBaker persist)
  - TWS track squares (FilteredPosition / measured anchor)
  - dual-layer separation (afterglow dot vs track square)
  - display clustering (CollectDisplayTracks / PpiDisplayBaker)

Phase gate metrics:
  - fragmentation (tracks ever per aircraft)
  - display_symbols_per_aircraft (what the operator sees after clustering)
  - max_dual_layer_sep_m (afterglow vs track square, same contact)
  - max_display_jump_m (frame-to-frame primary symbol motion)
  - filter_vs_truth_m (FilteredPosition error)

Run:
  python3 tools/simulate_ppi_display.py
  python3 tools/simulate_ppi_display.py --policy playable
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import simulate_pd_tracker as pd

PolicyName = Literal["baseline", "playable"]

# GBRS PpiDisplayBaker / Hud constants (mirrored).
PLOT_AFTERGLOW_S = 0.45
DISPLAY_CLUSTER_M = 120.0
TRACK_CLUSTER_RANGE_M = 350.0
TRACK_CLUSTER_AZ_DEG = 5.0
WORKSTATION_NOISE_US = 2.5
WORKSTATION_RANGE_BIAS_US = 3.0
WORKSTATION_AZ_BIAS_US = 0.12


@dataclass
class DisplayTrack:
    track_id: int
    aircraft_id: int
    scatterer_id: int
    filtered_pos: tuple
    anchor_pos: tuple
    confirmed: bool
    last_snr_db: float
    hit_count: int
    coasting: bool


@dataclass
class AfterglowPlot:
    aircraft_id: int
    scatterer_id: int
    pos: tuple
    snr_db: float
    last_fresh_s: float


@dataclass
class DisplayFrame:
    time_s: float
    afterglows: list[tuple]
    track_squares: list[tuple]
    truth: dict[int, tuple]


class ExtendedTrack(pd.Track):
    __slots__ = pd.Track.__slots__ + (
        "scatterer_id",
        "last_plot_pos",
        "position_history",
        "filtered_pos",
    )

    def __init__(self, track_id: int, aircraft_id: int, plot: pd.Plot):
        super().__init__(track_id, aircraft_id, plot)
        self.scatterer_id = aircraft_id
        self.last_plot_pos = plot.pos
        self.position_history = [plot.pos]
        self.filtered_pos = plot.pos


def update_tracker_extended(
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
    merge_birth_by_scatterer: bool,
):
    assigned_track = [-1] * len(tracks)
    used_plot = [False] * len(plots)

    pairs = []
    for ti, tr in enumerate(tracks):
        pr, pa, pe, _ = pd.predicted_polar(tr, radar_origin, time_s)
        for pi, pl in enumerate(plots):
            d_rng = abs(pl.range_m - pr)
            d_az = pd.angle_delta_deg(pl.az_deg, pa)
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
        tr.filtered_pos = new_pos
        tr.last_vel = new_vel
        tr.last_time = pl.time_s
        tr.hit_count += 1
        tr.ever_hits += 1
        tr.miss_count = 0
        tr.coast_elapsed = 0.0
        tr.last_hit_time = pl.time_s
        tr.last_snr_db = pl.snr_db
        tr.last_plot_pos = pl.pos
        tr.position_history.append(pl.pos)
        if len(tr.position_history) > 32:
            tr.position_history.pop(0)
        if tr.hit_count >= confirm_hits:
            tr.confirmed = True

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
        tr.filtered_pos = tr.last_pos
        tr.last_time = time_s

    for pi, pl in enumerate(plots):
        if used_plot[pi]:
            continue
        if merge_birth_by_scatterer:
            merged = False
            for tr in tracks:
                if tr.scatterer_id != pl.aircraft_id:
                    continue
                d_rng = abs(pl.range_m - pd.polar(radar_origin, tr.last_plot_pos)[0])
                d_az = pd.angle_delta_deg(
                    pl.az_deg, pd.polar(radar_origin, tr.last_plot_pos)[1]
                )
                if d_rng <= gate_range_m and d_az <= gate_az_deg:
                    tr.last_plot_pos = pl.pos
                    tr.position_history.append(pl.pos)
                    tr.last_pos = pl.pos
                    tr.filtered_pos = pl.pos
                    tr.last_time = pl.time_s
                    tr.hit_count += 1
                    tr.miss_count = 0
                    tr.coast_elapsed = 0.0
                    tr.last_snr_db = pl.snr_db
                    if tr.hit_count >= confirm_hits:
                        tr.confirmed = True
                    merged = True
                    break
            if merged:
                continue
        tr = ExtendedTrack(next_track_id[0], pl.aircraft_id, pl)
        next_track_id[0] += 1
        tracks.append(tr)

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


def horiz_dist(a: tuple, b: tuple) -> float:
    return math.hypot(a[0] - b[0], a[2] - b[2])


def track_draw_pos(tr: ExtendedTrack, use_measured_anchor: bool) -> tuple:
    if use_measured_anchor and tr.last_plot_pos:
        return tr.last_plot_pos
    return tr.filtered_pos


def collect_display_tracks(tracks: list, origin: tuple) -> list[DisplayTrack]:
    result: list[DisplayTrack] = []
    gate_sq = TRACK_CLUSTER_RANGE_M * TRACK_CLUSTER_RANGE_M
    for tr in tracks:
        if not isinstance(tr, ExtendedTrack):
            continue
        match = -1
        for j, kept in enumerate(result):
            if tr.scatterer_id > 0 and kept.scatterer_id == tr.scatterer_id:
                match = j
                break
            if horiz_dist(tr.filtered_pos, kept.filtered_pos) <= math.sqrt(gate_sq):
                match = j
                break
        row = DisplayTrack(
            track_id=tr.track_id,
            aircraft_id=tr.aircraft_id,
            scatterer_id=tr.scatterer_id,
            filtered_pos=tr.filtered_pos,
            anchor_pos=tr.last_plot_pos,
            confirmed=tr.confirmed,
            last_snr_db=tr.last_snr_db,
            hit_count=tr.hit_count,
            coasting=tr.miss_count > 0,
        )
        if match < 0:
            result.append(row)
            continue
        kept = result[match]
        if tr.confirmed and not kept.confirmed:
            result[match] = row
        elif tr.last_snr_db > kept.last_snr_db:
            result[match] = row
    return result


def plot_covered_by_track(
    plot_pos: tuple,
    scatterer_id: int,
    display_tracks: list[DisplayTrack],
    origin: tuple,
    use_measured_anchor: bool,
) -> bool:
    for tr in display_tracks:
        if scatterer_id > 0 and tr.scatterer_id == scatterer_id:
            return True
        draw = tr.anchor_pos if use_measured_anchor else tr.filtered_pos
        if horiz_dist(plot_pos, draw) <= TRACK_CLUSTER_RANGE_M:
            return True
    return False


def ingest_afterglow(
    persist: list[AfterglowPlot],
    plots: list[pd.Plot],
    now_s: float,
) -> None:
    for pl in plots:
        match = None
        for row in persist:
            if row.scatterer_id == pl.aircraft_id:
                match = row
                break
        if match:
            match.pos = pl.pos
            match.snr_db = pl.snr_db
            match.last_fresh_s = now_s
        else:
            persist.append(
                AfterglowPlot(
                    aircraft_id=pl.aircraft_id,
                    scatterer_id=pl.aircraft_id,
                    pos=pl.pos,
                    snr_db=pl.snr_db,
                    last_fresh_s=now_s,
                )
            )


def prune_afterglow(persist: list[AfterglowPlot], now_s: float, life_s: float) -> None:
    i = len(persist) - 1
    while i >= 0:
        if (now_s - persist[i].last_fresh_s) > life_s:
            persist.pop(i)
        i -= 1


def simulate_display(
    faction: str,
    policy: PolicyName,
    duration_s: float = pd.SIM_DURATION_S,
    seed: int = 12345,
) -> dict:
    use_measured_anchor = policy == "playable"
    single_layer_pd = policy == "playable"
    merge_birth = policy == "playable"

    noise_scale = 0.0
    range_bias_m = 0.0
    az_bias_deg = 0.0
    el_bias_deg = 0.0
    if faction == "US" and policy == "baseline":
        noise_scale = WORKSTATION_NOISE_US
        range_bias_m = WORKSTATION_RANGE_BIAS_US
        az_bias_deg = WORKSTATION_AZ_BIAS_US

    gate_range_m = 1200.0
    gate_az_deg = 10.0
    coast_max_s = 16.0 if faction == "US" else 12.0

    hw, settings = pd.make_pd(faction)
    rng = __import__("random").Random(seed)
    radar_origin = (0.0, pd.RADAR_AGL_M, 0.0)
    update_interval_s = settings.update_interval_s
    scan_rpm = hw.scan_rpm

    aircraft_list = [
        pd.build_aircraft_trajectory(1, 6000.0, 0.0, radar_origin),
        pd.build_aircraft_trajectory(2, 6500.0, 15.0, radar_origin),
        pd.build_aircraft_trajectory(3, 7000.0, 30.0, radar_origin),
    ]

    tracks: list = []
    next_track_id = [1]
    persist: list[AfterglowPlot] = []
    track_aircraft: dict[int, set] = {}

    max_dual_layer_sep = 0.0
    max_intra_scan_jump = 0.0
    max_inter_scan_jump = 0.0
    prev_primary: dict[int, tuple | None] = {a.aircraft_id: None for a in aircraft_list}
    prev_had_plot: dict[int, bool] = {a.aircraft_id: False for a in aircraft_list}
    filter_errors: list[float] = []
    anchor_errors: list[float] = []
    max_display_symbols = 0

    t = 0.0
    while t <= duration_s + 1e-9:
        period_s = 60.0 / scan_rpm if scan_rpm > 0.0 else 1e9
        scan_az = (t * 360.0 / period_s) % 360.0 if scan_rpm > 0.0 else 0.0

        plots: list[pd.Plot] = []
        truth: dict[int, tuple] = {}
        for aircraft in aircraft_list:
            sample = pd.sample_at(aircraft, t)
            if sample is None:
                continue
            st, pos, vel = sample
            truth[aircraft.aircraft_id] = pos
            rng_m, az_deg, el_deg, delta = pd.polar(radar_origin, pos)
            los_len = math.sqrt(delta[0] ** 2 + delta[1] ** 2 + delta[2] ** 2)
            if los_len < 1e-6:
                continue
            los = (delta[0] / los_len, delta[1] / los_len, delta[2] / los_len)
            radial_ms = abs(vel[0] * los[0] + vel[1] * los[1] + vel[2] * los[2])
            az_off = pd.angle_delta_deg(az_deg, scan_az)
            res = __import__("simulate_clutter_cover", fromlist=["physical_detect"]).physical_detect(
                hw,
                settings,
                rng_m,
                pd.UH1_MEAN_RCS_M2,
                radial_ms,
                az_off,
                pd.TARGET_AGL_M,
                pd.RADAR_AGL_M,
                False,
                1.0,
                0.0,
                0.0,
                __import__("random").Random(aircraft.aircraft_id * 100000 + int(t * 1000)),
            )
            if res.detected_snr:
                mr, ma, me, mpos = pd.apply_measurement_noise(
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
                plots.append(pd.Plot(aircraft.aircraft_id, t, mpos, mr, ma, me, res.snr_db))

        update_tracker_extended(
            tracks,
            next_track_id,
            plots,
            radar_origin,
            t,
            gate_range_m,
            gate_az_deg,
            confirm_hits=2,
            max_misses=600,
            coast_max_s=coast_max_s,
            merge_birth_by_scatterer=merge_birth,
        )

        for tr in tracks:
            if tr.aircraft_id not in track_aircraft:
                track_aircraft[tr.aircraft_id] = set()
            track_aircraft[tr.aircraft_id].add(tr.track_id)

        ingest_afterglow(persist, plots, t)
        prune_afterglow(persist, t, PLOT_AFTERGLOW_S)
        display_tracks = collect_display_tracks(tracks, radar_origin)

        afterglow_draw: list[tuple] = []
        for row in persist:
            if single_layer_pd and plot_covered_by_track(
                row.pos, row.scatterer_id, display_tracks, radar_origin, use_measured_anchor
            ):
                continue
            afterglow_draw.append(row.pos)

        track_draw: list[tuple] = []
        for tr in display_tracks:
            if not tr.confirmed:
                continue
            pos = tr.anchor_pos if use_measured_anchor else tr.filtered_pos
            track_draw.append(pos)

        for tr in tracks:
            if isinstance(tr, ExtendedTrack) and tr.aircraft_id in truth:
                tp = truth[tr.aircraft_id]
                filter_errors.append(horiz_dist(tr.filtered_pos, tp))
                anchor_errors.append(horiz_dist(tr.last_plot_pos, tp))

        plot_this_frame = {pl.aircraft_id for pl in plots}

        for aid, tpos in truth.items():
            primary = None
            for tr in display_tracks:
                if tr.aircraft_id != aid or not tr.confirmed:
                    continue
                primary = tr.anchor_pos if use_measured_anchor else tr.filtered_pos
                break
            if primary is None:
                for row in persist:
                    if row.aircraft_id == aid:
                        primary = row.pos
                        break
            if primary is not None:
                prev = prev_primary.get(aid)
                if prev is not None:
                    jump = horiz_dist(prev, primary)
                    if aid in plot_this_frame:
                        if prev_had_plot.get(aid):
                            max_inter_scan_jump = max(max_inter_scan_jump, jump)
                    else:
                        max_intra_scan_jump = max(max_intra_scan_jump, jump)
                prev_primary[aid] = primary
            prev_had_plot[aid] = aid in plot_this_frame

        for ag in afterglow_draw:
            for tg in track_draw:
                sep = horiz_dist(ag, tg)
                if sep > max_dual_layer_sep:
                    max_dual_layer_sep = sep

        sym_count = len(afterglow_draw) + len(track_draw)
        max_display_symbols = max(max_display_symbols, sym_count)

        t += update_interval_s

    tracks_per_aircraft = {
        sid: len(track_aircraft.get(sid, set())) for sid in range(1, len(aircraft_list) + 1)
    }
    fragmented = sum(1 for v in tracks_per_aircraft.values() if v > 1)

    final_display_tracks = collect_display_tracks(tracks, radar_origin)
    confirmed_display = [tr for tr in final_display_tracks if tr.confirmed]

    med_filter = sorted(filter_errors)[len(filter_errors) // 2] if filter_errors else 0.0
    med_anchor = sorted(anchor_errors)[len(anchor_errors) // 2] if anchor_errors else 0.0

    # Intra-scan jumps catch FilteredPosition coasting between mechanical hits
    # (the "flung contact" symptom). Inter-scan jumps are expected when the
    # anchor snaps to a new measurement each beam pass.
    playable = (
        fragmented == 0
        and max_intra_scan_jump < 5.0
        and max_dual_layer_sep < 80.0
        and len(confirmed_display) <= len(aircraft_list)
    )

    return {
        "policy": policy,
        "faction": faction,
        "gate_az_deg": gate_az_deg,
        "gate_range_m": gate_range_m,
        "noise_scale": noise_scale,
        "use_measured_anchor": use_measured_anchor,
        "single_layer_pd": single_layer_pd,
        "merge_birth_by_scatterer": merge_birth,
        "tracks_per_aircraft": tracks_per_aircraft,
        "fragmented_aircraft": fragmented,
        "fragmentation_ratio": round((next_track_id[0] - 1) / max(1, len(aircraft_list)), 2),
        "max_dual_layer_sep_m": round(max_dual_layer_sep, 1),
        "median_filter_error_m": round(med_filter, 1),
        "median_anchor_error_m": round(med_anchor, 1),
        "max_intra_scan_jump_m": round(max_intra_scan_jump, 1),
        "max_inter_scan_jump_m": round(max_inter_scan_jump, 1),
        "max_display_symbols_alive": max_display_symbols,
        "confirmed_display_tracks": len(confirmed_display),
        "verdict": "PASS" if playable else "FAIL",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="GBRS PPI display-path validation")
    parser.add_argument(
        "--policy",
        choices=["baseline", "playable", "both"],
        default="both",
    )
    parser.add_argument(
        "--out",
        default=str(TOOLS / "out" / "ppi_display_validation.json"),
    )
    args = parser.parse_args()

    policies: list[PolicyName]
    if args.policy == "both":
        policies = ["baseline", "playable"]
    else:
        policies = [args.policy]  # type: ignore

    report = {
        "script": "simulate_ppi_display.py",
        "description": "GBRS PPI display-path: dual-layer, anchor vs filter, clustering",
        "results": {},
    }

    print("=" * 72)
    print("GBRS PPI display-path offline validation")
    print("=" * 72)

    for policy in policies:
        us = simulate_display("US", policy)
        ussr = simulate_display("USSR", policy)
        report["results"][policy] = {"US": us, "USSR": ussr}
        for faction, r in (("US", us), ("USSR", ussr)):
            print(f"\n[{faction}] policy={policy}")
            print(f"  fragmented={r['fragmented_aircraft']} tracks/aircraft={r['tracks_per_aircraft']}")
            print(
                f"  dual_layer_sep={r['max_dual_layer_sep_m']}m "
                f"intra_jump={r['max_intra_scan_jump_m']}m "
                f"inter_jump={r['max_inter_scan_jump_m']}m"
            )
            print(f"  filter_err_med={r['median_filter_error_m']}m anchor_err_med={r['median_anchor_error_m']}m")
            print(f"  verdict={r['verdict']}")

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"\nWrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
