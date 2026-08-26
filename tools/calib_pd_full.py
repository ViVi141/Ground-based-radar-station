"""Calibrate GBRS DemClutterScale under full RDF PD stack.

Models PhysicalDetect SNR with:
  - MTD bank (body-only mti_gain≈1, heli-tip mti_gain≈0.09)
  - coherent integration
  - DEM clutter × leakage/floor
  - CFAR thermal pedestal (approx)

Writes calib/pd_full_report.json and prints recommended scales.
"""

from __future__ import annotations

import json
import math
import os
import random
import sys

import simulate_clutter_cover as s

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "calib")
C_LIGHT = 299792458.0


def mtd_clutter_bin_gain(bin_index: int, floor: float, leak: float) -> float:
    if bin_index == 0:
        return max(1e-6, floor)
    return max(1e-9, leak)


def suggest_mti_floor(sigma_vr: float, wavelength: float, prf: float, order: int = 1) -> float:
    # Match RDF_RadarHwCalib.SuggestMtiClutterFloor (RDF 1.1.6): residue ≈
    # (2π·σ_f/PRF)^(2N). Missing 2π underestimates N=1 by ~40×.
    sigma_fd = 2.0 * max(0.05, sigma_vr) / wavelength
    x = abs(2.0 * math.pi * sigma_fd / prf)
    residue = x ** (2 * order)
    return float(max(1e-6, min(0.5, residue)))


def suggest_mtd_leak(sigma_vr: float, wavelength: float, prf: float) -> float:
    # Match RDF_RadarHwCalib.SuggestMtdLeakage clamps (hi by sigma).
    sigma_fd = 2.0 * max(1e-6, sigma_vr) / wavelength
    fd_max = 0.5 * prf
    n = 129
    inv = 1.0 / 1.3862943611198906
    peak = 0.0
    for i in range(n):
        t = i / (n - 1)
        fd = -fd_max + t * (2.0 * fd_max)
        x = fd / sigma_fd
        p = 0.5 ** ((x * x) * inv)
        peak = max(peak, p)
    if peak < 1e-7:
        peak = 1.0
    dc = side = 0.0
    for j in range(n):
        t2 = j / (n - 1)
        fd2 = -fd_max + t2 * (2.0 * fd_max)
        x2 = fd2 / sigma_fd
        p2 = (0.5 ** ((x2 * x2) * inv)) / peak
        if abs(fd2 / prf) < 0.08:
            dc += p2
        else:
            side += p2
    total = dc + side
    if total <= 0:
        return 1e-6
    leak = side / total
    hi = 0.05
    if sigma_vr >= 2.0:
        hi = 0.25
    elif sigma_vr >= 1.0:
        hi = 0.10
    return float(max(1e-9, min(hi, leak)))


def snr_at_range(
    hw: s.Hardware,
    settings: s.Settings,
    range_m: float,
    radial_ms: float,
    mti_gain: float,
    clutter_mti: float,
    yaw_deg: float = 0.0,
    seed: int = 0,
) -> s.DetectResult:
    """PhysicalDetect-like with injected MTD mti/clutter gains."""
    rng = random.Random(seed)
    los_el = math.degrees(math.atan2(s.TARGET_AGL_M - s.RADAR_AGL_M, max(1.0, range_m)))
    rcs = s.instant_rcs_uh1(yaw_deg, 0.0, los_el, True, seed)

    # Monkey-patch: compute using modified mti path by cloning detect internals.
    elevation_deg = los_el
    pattern_gain, beam_name = s.strongest_beam_gain(hw, 0.0, elevation_deg)
    pr = s.received_power_w(hw, rcs, range_m, pattern_gain)
    if settings.enable_atmospheric_loss:
        atm_db = settings.atm_loss_db_per_km_one_way
        if atm_db < 0.0:
            atm_db = s.atmospheric_one_way_db_per_km(hw.frequency_hz)
        latm = s.atmospheric_loss_linear(
            range_m, atm_db, settings.rain_loss_db_per_km_one_way
        )
        if latm > 1.0:
            pr = pr / latm

    gproc = hw.processing_gain()
    processed = pr * gproc * max(1e-6, mti_gain)

    # DEM clutter with MTD bin gain (replace TwoPulse floor-only path).
    clutter_base = s.dem_clutter_processed_w(
        hw,
        settings,
        range_m,
        pattern_gain,
        gproc,
        s.RADAR_AGL_M,
        surface_class=2,
        cell_size_m=s.CELL_SIZE_M,
    )
    # dem_clutter_processed_w already multiplies mti_clutter_floor when enable_mti.
    # Undo floor and apply MTD clutter_mti instead.
    floor = max(1e-6, hw.mti_clutter_floor)
    if hw.enable_mti and floor > 0.0:
        clutter = clutter_base / floor * clutter_mti
    else:
        clutter = clutter_base * clutter_mti

    thermal = hw.noise_power_w() * gproc
    noise = thermal + settings.additional_noise_power_w + clutter
    snr_db = s.lin_to_db(processed / max(1e-30, noise))
    snr_hit = snr_db >= settings.detection_snr_db
    cfar_hit = False
    cfar_thr = 0.0
    if snr_hit:
        cfar_hit, cfar_thr = s.apply_cfar_single_target(
            settings, hw, processed, range_m, rng
        )
    return s.DetectResult(
        snr_hit,
        cfar_hit and snr_hit,
        snr_db,
        pr,
        processed,
        clutter,
        thermal,
        noise,
        pattern_gain,
        mti_gain,
        1.0,
        rcs,
        beam_name,
        0.0,
        elevation_deg,
        s.doppler_hz(radial_ms, hw.wavelength_m()),
        cfar_thr,
    )


def pd_curve(
    hw: s.Hardware,
    settings: s.Settings,
    ranges: list[float],
    radial_ms: float,
    mti_gain: float,
    clutter_mti: float,
    trials: int = 40,
) -> list[dict]:
    rows = []
    for r in ranges:
        snr_n = 0
        cfar_n = 0
        snr_sum = 0.0
        for t in range(trials):
            res = snr_at_range(
                hw, settings, r, radial_ms, mti_gain, clutter_mti, seed=t + int(r)
            )
            snr_sum += res.snr_db
            if res.detected_snr:
                snr_n += 1
            if res.detected_cfar:
                cfar_n += 1
        rows.append(
            {
                "range_m": r,
                "pd_snr": snr_n / trials,
                "pd_cfar": cfar_n / trials,
                "mean_snr_db": snr_sum / trials,
            }
        )
    return rows


def r_at_pd(rows: list[dict], key: str, threshold: float) -> float:
    last_ok = 0.0
    for row in rows:
        if row[key] >= threshold:
            last_ok = row["range_m"]
        else:
            break
    return last_ok


def make_full_pd_hw(us: bool) -> tuple[s.Hardware, s.Settings, dict]:
    if us:
        hw, settings = s.make_us()
        sigma_vr = 0.5
    else:
        hw, settings = s.make_ussr()
        sigma_vr = 0.5

    hw.coherent_integration = True
    hw.enable_mti = True
    lam = hw.wavelength_m()
    leak = suggest_mtd_leak(sigma_vr, lam, hw.prf_hz)
    floor = suggest_mti_floor(sigma_vr, lam, hw.prf_hz, order=1)
    # Keep SHORAD design floor if derive suggests tinier than prior gameplay floor.
    if us:
        floor = max(floor, 1e-4)
    else:
        floor = max(floor, 0.01)
    hw.mti_clutter_floor = floor

    meta = {
        "sigma_vr_m_s": sigma_vr,
        "mtd_clutter_leakage": leak,
        "mti_clutter_floor": floor,
        "coherent": True,
        "mti_mode": "mtd_bank",
        "prf_stagger_ratio": 1.2,
        "doppler_bin_count": 16,
        "clutter_map_alpha": 0.10,
    }
    return hw, settings, meta


def sweep_scale(
    us: bool,
    scales: list[float],
    ranges: list[float],
) -> dict:
    hw, settings, meta = make_full_pd_hw(us)
    leak = meta["mtd_clutter_leakage"]
    floor = meta["mti_clutter_floor"]

    # Moving radial: clear Doppler bin.
    body_mti = 1.0
    body_clutter = leak
    # Hovering heli tip-bin (worst MaxMtdSpectrumGain fraction ~0.09).
    heli_mti = 0.09
    heli_clutter = leak
    # Slow body in bin0.
    slow_mti = 1.0
    slow_clutter = floor

    results = []
    for scale in scales:
        settings.dem_clutter_scale = scale
        body = pd_curve(hw, settings, ranges, 50.0, body_mti, body_clutter)
        heli = pd_curve(hw, settings, ranges, 5.0, heli_mti, heli_clutter)
        slow = pd_curve(hw, settings, ranges, 0.0, slow_mti, slow_clutter)
        results.append(
            {
                "dem_clutter_scale": scale,
                "body_r80_cfar_m": r_at_pd(body, "pd_cfar", 0.8),
                "body_r50_cfar_m": r_at_pd(body, "pd_cfar", 0.5),
                "heli_r50_cfar_m": r_at_pd(heli, "pd_cfar", 0.5),
                "heli_r80_cfar_m": r_at_pd(heli, "pd_cfar", 0.8),
                "slow_r50_cfar_m": r_at_pd(slow, "pd_cfar", 0.5),
                "body_curve": body,
                "heli_curve": heli,
            }
        )
    return {"meta": meta, "faction": "US" if us else "USSR", "sweeps": results}


def pick_scale(sweeps: list[dict], body_r50_min: float, heli_r50_min: float) -> float:
    """Largest scale that still meets range floors (more clutter = more realistic)."""
    best = sweeps[0]["dem_clutter_scale"]
    for row in sweeps:
        if row["body_r50_cfar_m"] >= body_r50_min and row["heli_r50_cfar_m"] >= heli_r50_min:
            best = row["dem_clutter_scale"]
    return best


def main() -> None:
    ranges = [200, 500, 1000, 1500, 2000, 2500, 3000, 4000, 5000, 6000, 7000, 8000, 10000]
    us_scales = [0.05, 0.10, 0.15, 0.20, 0.25, 0.35, 0.50, 0.75, 1.0]
    ussr_scales = [0.05, 0.10, 0.15, 0.20, 0.25, 0.35, 0.50]

    us = sweep_scale(True, us_scales, ranges)
    ussr = sweep_scale(False, ussr_scales, ranges)

    # Goals: multi-km search under full PD; heli tip case still paints past 2 km.
    us_scale = pick_scale(us["sweeps"], body_r50_min=5000.0, heli_r50_min=2500.0)
    ussr_scale = pick_scale(ussr["sweeps"], body_r50_min=6000.0, heli_r50_min=2500.0)

    report = {
        "goals": {
            "us_body_r50_min_m": 5000,
            "us_heli_r50_min_m": 2500,
            "ussr_body_r50_min_m": 6000,
            "ussr_heli_r50_min_m": 2500,
            "notes": "Full PD: MTD+coherent+derive floors/leak+clutter-map alpha 0.10",
        },
        "recommended": {
            "US": {
                "dem_clutter_scale": us_scale,
                "detection_snr_db": 8.0,
                **us["meta"],
            },
            "USSR": {
                "dem_clutter_scale": ussr_scale,
                "detection_snr_db": 6.0,
                **ussr["meta"],
            },
        },
        "us_sweep": us,
        "ussr_sweep": ussr,
    }

    os.makedirs(OUT_DIR, exist_ok=True)
    out_path = os.path.join(OUT_DIR, "pd_full_report.json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
        f.write("\n")

    print("=== GBRS full-PD recalibration ===")
    print("US recommended dem_clutter_scale=", us_scale, "leak=", us["meta"]["mtd_clutter_leakage"])
    for row in us["sweeps"]:
        if abs(row["dem_clutter_scale"] - us_scale) < 1e-9:
            print(
                "  body_r50=%.0f heli_r50=%.0f"
                % (row["body_r50_cfar_m"], row["heli_r50_cfar_m"])
            )
    print("USSR recommended dem_clutter_scale=", ussr_scale, "leak=", ussr["meta"]["mtd_clutter_leakage"])
    for row in ussr["sweeps"]:
        if abs(row["dem_clutter_scale"] - ussr_scale) < 1e-9:
            print(
                "  body_r50=%.0f heli_r50=%.0f"
                % (row["body_r50_cfar_m"], row["heli_r50_cfar_m"])
            )
    print("wrote", out_path)


if __name__ == "__main__":
    main()
