#!/usr/bin/env python3
"""Full factorial sweep of GBRS/RDF realism flags on the offline PD chain.

Enumerates every on/off combination of the PhysicalDetect / CFAR channel
flags that actually change SNR or Pd. ESM, dwell scheduler, clutter-map
persistence, and SitePathLut are cost-only (they do not enter this chain).

PRF range fold is reported as a geometric no-op at GBRS ranges (Ru >> 10 km).
4/3 refraction is likewise a no-op inside ~12 km radio horizon.

Balance score prefers:
  - US 7 km head-on scan Pd in 0.40-0.85 (not vacuum, not dead)
  - US 4 km head-on Pd >= 0.70
  - US 7 km tangential Pd in 0.08-0.55 (rotor sidebands, not free-space)
  - USSR 10 km head-on Pd in 0.25-0.75
  - terrain-blocked Pd <= 0.35
  - CFAR false-alarm on empty cells < 0.08
  - lower CPU cost among similar detection scores

Usage:
  python sweep_fidelity_flags.py
  python sweep_fidelity_flags.py --trials 40
"""

from __future__ import annotations

import argparse
import itertools
import json
import math
import random
from dataclasses import replace
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
import sys

if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import simulate_clutter_cover as s

FLAG_NAMES = (
    "dem_clutter",
    "atm_loss",
    "weather_rain",
    "two_ray",
    "refraction",
    "nlos",
    "knife",
    "cfar_gate",
    "cfar_thermal_fill",
)

# Relative scan CPU. Knife/NLOS only fire on blocked LOS; cost still counts
# because the game still samples the path to decide blocked vs clear.
COST_WEIGHT = {
    "dem_clutter": 2.0,
    "atm_loss": 0.2,
    "weather_rain": 0.3,
    "two_ray": 0.8,
    "refraction": 0.3,
    "nlos": 1.5,
    "knife": 3.0,
    "cfar_gate": 1.0,
    "cfar_thermal_fill": 0.4,
}

# Named packs that are not in the Pd factorial.
COST_ONLY = {
    "clutter_map": 1.2,
    "dem_span_occlusion": 4.0,
    "esm_receive": 2.0,
    "dwell_scheduler": 3.0,
    "prf_folds": 0.2,
    "measurement_noise": 0.2,
}

US_RAIN_DB_PER_KM = 0.20
USSR_RAIN_DB_PER_KM = 0.02


def apply_flags(base: s.Settings, bits: dict, rain_db: float) -> s.Settings:
    rain_loss = 0.0
    if bits["weather_rain"]:
        rain_loss = rain_db
    return replace(
        base,
        enable_dem_clutter=bits["dem_clutter"],
        enable_atmospheric_loss=bits["atm_loss"],
        enable_weather_rain=bits["weather_rain"],
        rain_loss_db_per_km_one_way=rain_loss,
        enable_los_two_ray=bits["two_ray"],
        enable_atmospheric_refraction=bits["refraction"],
        enable_nlos_multipath=bits["nlos"],
        enable_knife_edge=bits["knife"],
        enable_cfar_gate=bits["cfar_gate"],
        enable_cfar_thermal_fill=bits["cfar_thermal_fill"],
    )


def combo_cost(bits: dict) -> float:
    total = 0.0
    for name in FLAG_NAMES:
        if bits[name]:
            total = total + COST_WEIGHT[name]
    return total


def bits_from_tuple(values: tuple) -> dict:
    out = {}
    i = 0
    while i < len(FLAG_NAMES):
        out[FLAG_NAMES[i]] = bool(values[i])
        i = i + 1
    return out


def pack_key(bits: dict) -> str:
    parts = []
    for name in FLAG_NAMES:
        if bits[name]:
            parts.append(name)
    if not parts:
        return "none"
    return "+".join(parts)


def band_score(pd: float, lo: float, hi: float, weight: float) -> float:
    if pd < lo:
        return -weight * (lo - pd) * 2.0
    if pd > hi:
        return -weight * (pd - hi)
    span = hi - lo
    if span <= 1e-6:
        return weight
    mid = 0.5 * (lo + hi)
    return weight * (1.0 - abs(pd - mid) / span)


def floor_score(pd: float, minimum: float, weight: float) -> float:
    if pd >= minimum:
        return weight
    return -weight * (minimum - pd) * 2.0


def cap_score(pd: float, maximum: float, weight: float) -> float:
    if pd <= maximum:
        return weight * 0.25
    return -weight * (pd - maximum) * 2.0


def far_score(far: float, maximum: float, weight: float) -> float:
    if far <= maximum:
        return weight
    return -weight * (far - maximum) * 8.0


def run_scan(
    hw: s.Hardware,
    settings: s.Settings,
    range_m: float,
    radial_ms: float,
    yaw_deg: float,
    los_blocked: bool,
    trials: int,
    seed: int,
) -> dict:
    return s.run_scenario_pd(
        hw,
        settings,
        range_m,
        radial_ms,
        yaw_deg,
        los_blocked,
        0.55,
        18.0,
        0.42,
        True,
        False,
        True,
        trials,
        seed,
    )


def empty_cfar_far(
    hw: s.Hardware, settings: s.Settings, trials: int, seed: int
) -> float:
    if not settings.enable_cfar_gate:
        return 0.0
    rng = random.Random(seed)
    hits = 0
    i = 0
    while i < trials:
        hit, _thr = s.apply_cfar_single_target(
            settings, hw, 0.0, settings.range_m * 0.5, rng
        )
        if hit:
            hits = hits + 1
        i = i + 1
    return hits / float(trials)


def evaluate_faction(
    hw: s.Hardware,
    settings: s.Settings,
    max_range_m: float,
    trials: int,
) -> dict:
    head7 = run_scan(hw, settings, max_range_m, 50.0, 0.0, False, trials, 11)
    tang7 = run_scan(hw, settings, max_range_m, 0.0, 0.0, False, trials, 13)
    mid = run_scan(hw, settings, 4000.0, 50.0, 0.0, False, trials, 17)
    blocked = run_scan(hw, settings, 4000.0, 50.0, 0.0, True, trials, 19)
    far = empty_cfar_far(hw, settings, trials, 23)
    return {
        "head_max": head7,
        "tang_max": tang7,
        "head_4km": mid,
        "blocked_4km": blocked,
        "cfar_far": far,
    }


def score_card(us: dict, ussr: dict) -> float:
    total = 0.0
    total = total + band_score(us["head_max"]["pd_cfar"], 0.40, 0.85, 3.0)
    total = total + floor_score(us["head_4km"]["pd_cfar"], 0.70, 2.5)
    total = total + band_score(us["tang_max"]["pd_cfar"], 0.08, 0.55, 2.0)
    total = total + band_score(ussr["head_max"]["pd_cfar"], 0.25, 0.75, 3.0)
    total = total + floor_score(ussr["head_4km"]["pd_cfar"], 0.45, 1.5)
    total = total + cap_score(us["blocked_4km"]["pd_cfar"], 0.35, 1.5)
    total = total + cap_score(ussr["blocked_4km"]["pd_cfar"], 0.40, 1.0)
    total = total + far_score(us["cfar_far"], 0.08, 2.0)
    total = total + far_score(ussr["cfar_far"], 0.08, 1.0)
    return total


def named_bits(name: str) -> dict:
    off = {}
    for flag in FLAG_NAMES:
        off[flag] = False
    if name == "vacuum":
        return off
    if name == "rdf_product_defaults":
        off["dem_clutter"] = True
        off["nlos"] = True
        off["knife"] = True
        off["cfar_gate"] = True
        return off
    if name == "gbrs_full":
        for flag in FLAG_NAMES:
            off[flag] = True
        off["weather_rain"] = False
        return off
    if name == "gbrs_full_rain":
        for flag in FLAG_NAMES:
            off[flag] = True
        return off
    if name == "rdf_airtest":
        return off
    raise ValueError(name)


def fold_notes(hw: s.Hardware, range_m: float) -> dict:
    ru = s.unambiguous_range_m(hw.prf_hz)
    vu = s.unambiguous_velocity_ms(hw.wavelength_m(), hw.prf_hz)
    range_folds = False
    if range_m > ru:
        range_folds = True
    speed_folds = False
    if 50.0 > vu:
        speed_folds = True
    horizon = s.radio_horizon_range_m(s.RADAR_AGL_M, s.TARGET_AGL_M, 1.3333333)
    return {
        "unambiguous_range_m": ru,
        "unambiguous_velocity_ms": vu,
        "range_fold_at_max": range_folds,
        "doppler_fold_at_50ms": speed_folds,
        "radio_horizon_m": horizon,
        "inside_horizon_at_max": range_m <= horizon,
    }


def pareto_front(rows: list) -> list:
    ordered = sorted(rows, key=lambda r: (-r["score"], r["cost"]))
    front = []
    best_cost = 1.0e9
    for row in ordered:
        if row["cost"] < best_cost - 1e-9:
            front.append(row)
            best_cost = row["cost"]
    return front


def compact_result(res: dict) -> dict:
    return {
        "pd_cfar": round(res["pd_cfar"], 4),
        "pd_snr": round(res["pd_snr"], 4),
        "mean_snr_db": round(res["mean_snr_db"], 2),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--trials", type=int, default=80)
    args = parser.parse_args()
    trials = args.trials
    if trials < 16:
        trials = 16

    us_hw, us_base = s.make_us()
    ussr_hw, ussr_base = s.make_ussr()
    us_base = replace(us_base, update_interval_s=0.04)
    ussr_base = replace(ussr_base, update_interval_s=0.04)

    rows = []
    combos = list(itertools.product([False, True], repeat=len(FLAG_NAMES)))
    print(
        "Sweeping %d flag combinations x 2 factions x %d trials..."
        % (len(combos), trials)
    )

    i = 0
    while i < len(combos):
        bits = bits_from_tuple(combos[i])
        us_set = apply_flags(us_base, bits, US_RAIN_DB_PER_KM)
        ussr_set = apply_flags(ussr_base, bits, USSR_RAIN_DB_PER_KM)
        us = evaluate_faction(us_hw, us_set, 7000.0, trials)
        ussr = evaluate_faction(ussr_hw, ussr_set, 10000.0, trials)
        score = score_card(us, ussr)
        rows.append(
            {
                "pack": pack_key(bits),
                "flags": bits,
                "cost": round(combo_cost(bits), 3),
                "score": round(score, 4),
                "us": {
                    "head_7km": compact_result(us["head_max"]),
                    "tang_7km": compact_result(us["tang_max"]),
                    "head_4km": compact_result(us["head_4km"]),
                    "blocked_4km": compact_result(us["blocked_4km"]),
                    "cfar_far": round(us["cfar_far"], 4),
                },
                "ussr": {
                    "head_10km": compact_result(ussr["head_max"]),
                    "tang_10km": compact_result(ussr["tang_max"]),
                    "head_4km": compact_result(ussr["head_4km"]),
                    "blocked_4km": compact_result(ussr["blocked_4km"]),
                    "cfar_far": round(ussr["cfar_far"], 4),
                },
            }
        )
        i = i + 1
        if i % 64 == 0:
            print("  %d / %d" % (i, len(combos)))

    rows_by_score = sorted(rows, key=lambda r: (-r["score"], r["cost"]))
    front = pareto_front(rows)

    named = {}
    for label in (
        "vacuum",
        "rdf_airtest",
        "rdf_product_defaults",
        "gbrs_full",
        "gbrs_full_rain",
    ):
        bits = named_bits(label)
        key = pack_key(bits)
        found = None
        for row in rows:
            if row["pack"] == key:
                found = row
                break
        named[label] = found

    report = {
        "trials": trials,
        "flag_names": list(FLAG_NAMES),
        "cost_weight": COST_WEIGHT,
        "cost_only_not_in_pd_chain": COST_ONLY,
        "geometry": {
            "us": fold_notes(us_hw, 7000.0),
            "ussr": fold_notes(ussr_hw, 10000.0),
        },
        "named_packs": named,
        "top15_by_score": rows_by_score[:15],
        "pareto_score_then_cost": front,
        "gbrs_full_rank": None,
        "rdf_defaults_rank": None,
    }

    def rank_of(label: str):
        pack = named[label]
        if not pack:
            return None
        r = 1
        for row in rows_by_score:
            if row["pack"] == pack["pack"]:
                return r
            r = r + 1
        return None

    report["gbrs_full_rank"] = rank_of("gbrs_full")
    report["rdf_defaults_rank"] = rank_of("rdf_product_defaults")

    out_dir = TOOLS / "out"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "fidelity_flag_sweep.json"
    with out_path.open("w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)

    print()
    print("=" * 78)
    print("GBRS realism-flag factorial (%d combos, %d trials)" % (len(rows), trials))
    print("=" * 78)
    print(
        "US Ru=%.0f m  Vu=%.1f m/s  radio horizon=%.0f m"
        % (
            report["geometry"]["us"]["unambiguous_range_m"],
            report["geometry"]["us"]["unambiguous_velocity_ms"],
            report["geometry"]["us"]["radio_horizon_m"],
        )
    )
    print(
        "USSR Ru=%.0f m  Vu=%.1f m/s  radio horizon=%.0f m"
        % (
            report["geometry"]["ussr"]["unambiguous_range_m"],
            report["geometry"]["ussr"]["unambiguous_velocity_ms"],
            report["geometry"]["ussr"]["radio_horizon_m"],
        )
    )
    print()
    print("Named packs:")
    for label in (
        "rdf_airtest",
        "rdf_product_defaults",
        "gbrs_full",
        "gbrs_full_rain",
    ):
        row = named[label]
        if not row:
            continue
        print(
            "  %-22s score=%7.3f cost=%4.1f  US7=%.0f%% tang=%.0f%%  "
            "USSR10=%.0f%% blk=%.0f%% far=%.1f%%  [%s]"
            % (
                label,
                row["score"],
                row["cost"],
                row["us"]["head_7km"]["pd_cfar"] * 100.0,
                row["us"]["tang_7km"]["pd_cfar"] * 100.0,
                row["ussr"]["head_10km"]["pd_cfar"] * 100.0,
                row["us"]["blocked_4km"]["pd_cfar"] * 100.0,
                row["us"]["cfar_far"] * 100.0,
                row["pack"],
            )
        )

    print()
    print("Top 10 by balance score (then cheaper):")
    k = 0
    while k < 10 and k < len(rows_by_score):
        row = rows_by_score[k]
        print(
            "  %2d score=%7.3f cost=%4.1f  US7=%.0f%% U4=%.0f%% tang=%.0f%%  "
            "SU10=%.0f%% blk=%.0f%%  %s"
            % (
                k + 1,
                row["score"],
                row["cost"],
                row["us"]["head_7km"]["pd_cfar"] * 100.0,
                row["us"]["head_4km"]["pd_cfar"] * 100.0,
                row["us"]["tang_7km"]["pd_cfar"] * 100.0,
                row["ussr"]["head_10km"]["pd_cfar"] * 100.0,
                row["us"]["blocked_4km"]["pd_cfar"] * 100.0,
                row["pack"],
            )
        )
        k = k + 1

    print()
    print("Pareto (best score at each cheaper cost):")
    for row in front:
        print(
            "  score=%7.3f cost=%4.1f  US7=%.0f%% tang=%.0f%% SU10=%.0f%%  %s"
            % (
                row["score"],
                row["cost"],
                row["us"]["head_7km"]["pd_cfar"] * 100.0,
                row["us"]["tang_7km"]["pd_cfar"] * 100.0,
                row["ussr"]["head_10km"]["pd_cfar"] * 100.0,
                row["pack"],
            )
        )

    print()
    print("Wrote %s" % out_path)
    print(
        "GBRS full rank %s / %d   RDF product-defaults rank %s / %d"
        % (
            report["gbrs_full_rank"],
            len(rows),
            report["rdf_defaults_rank"],
            len(rows),
        )
    )
    print(
        "Not in Pd chain (cost only): %s"
        % ", ".join(COST_ONLY.keys())
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
