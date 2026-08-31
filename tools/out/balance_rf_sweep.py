#!/usr/bin/env python3
"""One-shot RF rebalance sweep for GBRS US WLR + USSR PD SEARCH."""

from __future__ import annotations

import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import calib_pd_full as c
import simulate_wlr_projectile as w

US_BEAMS = [
    ("flat", 5.0, 16.0, 0.0),
    ("mortar_low", 15.0, 28.0, 0.0),
    ("mortar_mid", 35.0, 30.0, 0.0),
    ("mortar_high", 55.0, 28.0, -0.5),
]


def sweep_us_wlr() -> None:
    print("=== US WLR 8 km center SNR (RCS 0.01) ===")
    print(f"{'pw_kW':>8} {'bw':>4} {'N':>3} {'SNR':>7}  {'@4dB':>5} {'@5dB':>5}")
    for pw in (
        23000,
        50000,
        100000,
        150000,
        200000,
        250000,
        350000,
        500000,
    ):
        cfg = w.ScanConfig(
            beamwidth_deg=24.0,
            rpm=6.0,
            snr_gate_db=4.0,
            update_interval_s=0.08,
            elevation_beams=US_BEAMS,
        )
        hw = w.build_hw(cfg, "US")
        hw.peak_power_w = float(pw)
        settings = w.build_settings(cfg, 8000.0, hw)
        res = w.snr_at(
            hw, settings, 8000.0, w.PROJECTILE_RCS_M2, 180.0, 15.0, 0.0, 1
        )
        ok4 = "PASS" if res.snr_db >= 4.0 else "FAIL"
        ok5 = "PASS" if res.snr_db >= 5.0 else "FAIL"
        print(
            f"{pw / 1000.0:>8.0f} {24:>4} {32:>3} "
            f"{res.snr_db:>7.1f}  {ok4:>5} {ok5:>5}"
        )


def sweep_ussr_pd() -> None:
    print()
    print("=== USSR PD SEARCH MTD_BANK (peak 250 vs 350) ===")
    for peak in (250000.0, 350000.0):
        hw, settings, meta = c.make_full_pd_hw(False)
        hw.peak_power_w = peak
        settings.range_m = 16000.0
        settings.detection_snr_db = 5.0
        settings.dem_clutter_scale = 0.50
        settings.min_distance_m = hw.min_detectable_range_m()
        leak = meta["mtd_clutter_leakage"]
        cases = (
            ("16km head-on", 16000.0, 50.0, 1.0, leak),
            ("12km head-on", 12000.0, 50.0, 1.0, leak),
            ("10km head-on", 10000.0, 50.0, 1.0, leak),
            ("16km slow heli tip", 16000.0, 5.0, 0.09, leak),
        )
        for label, rng, radial, mti_gain, clutter_mti in cases:
            trials = 200
            snr_hits = 0
            cfar_hits = 0
            snr_sum = 0.0
            for t in range(trials):
                res = c.snr_at_range(
                    hw, settings, rng, radial, mti_gain, clutter_mti, seed=t + 7
                )
                snr_sum += res.snr_db
                if res.detected_snr:
                    snr_hits += 1
                if res.detected_cfar:
                    cfar_hits += 1
            print(
                f"  peak={peak / 1000.0:.0f}kW {label}: "
                f"Pd_snr={snr_hits / trials * 100:.0f}% "
                f"Pd_cfar={cfar_hits / trials * 100:.0f}% "
                f"mean={snr_sum / trials:.1f} dB"
            )


def candidate_matrix() -> None:
    print()
    print("=== US WLR candidates ===")
    for pw, gate in (
        (200000, 4.0),
        (250000, 4.0),
        (250000, 5.0),
        (500000, 4.0),
    ):
        cfg = w.ScanConfig(
            beamwidth_deg=24.0,
            rpm=6.0,
            snr_gate_db=gate,
            update_interval_s=0.08,
            elevation_beams=US_BEAMS,
        )
        hw = w.build_hw(cfg, "US")
        hw.peak_power_w = float(pw)
        settings = w.build_settings(cfg, 8000.0, hw)
        center = w.snr_at(
            hw, settings, 8000.0, w.PROJECTILE_RCS_M2, 180.0, 15.0, 0.0, 1
        )
        edge = w.snr_at(
            hw, settings, 8000.0, w.PROJECTILE_RCS_M2, 180.0, 15.0, 10.0, 1
        )
        margin = center.snr_db - gate
        print(
            f"  {pw / 1000.0:.0f}kW gate={gate:.0f}: "
            f"center {center.snr_db:.1f} dB (margin {margin:+.1f}), "
            f"az+10 {edge.snr_db:.1f} dB"
        )


if __name__ == "__main__":
    sweep_us_wlr()
    sweep_ussr_pd()
    candidate_matrix()
