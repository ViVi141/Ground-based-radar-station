#!/usr/bin/env python3
"""Offline validation + tuning for GBRS WLR (weapon-locating) projectile search.

Validates the two things the GBRS WLR config depends on:
  1. SNR feasibility: can a 0.01 m2 projectile be detected at 8 km (US) /
     10 km (USSR) with the configured hardware + elevation beams + SNR gate?
  2. Rotating-scan hit budget: with mechanical scan at 10/6 RPM and a
     25/30 deg azimuth beam, how many illumination windows (>= the
     WeaponLocateMinHits=5 gate) fit inside a mortar shell flight?

Then sweeps beamwidth / RPM / SNR gate to recommend tuning if the current
config cannot meet both goals.

Reuses the 1:1 offline port chain from simulate_clutter_cover.py.
"""

from __future__ import annotations

import math
import random
import sys
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import simulate_clutter_cover as s

# ---------------------------------------------------------------------------
# Projectile model: 82mm HE mortar, RDF default AirDrag, gravity integration.
# ---------------------------------------------------------------------------
AIR_DRAG_SHELL_82MM_HE = 0.000615
GRAVITY_M_S2 = -9.81
DT_S = 0.05
PROJECTILE_RCS_M2 = 0.01

# WLR quality gates (mirror RDF tracker defaults / GBRS config).
WLR_MIN_HITS = 5
WLR_MIN_SPAN_S = 1.0


def integrate_ballistic(
    v0_ms: float,
    elevation_deg: float,
    origin_y: float,
    t_max_s: float,
) -> list[tuple[float, float, float]]:
    """Integrate a simple ballistic arc, returns (t, y, vx, vz-ish) samples.

    Simplified 2D: x downrange, y up. Returns [(t, y, speed_ms, elev_deg)].
    Wind ignored (matches RDF SolveWeaponLocate vacuum-ish fit assumption for
    the geometry check; RDF uses AirDrag+wind for prediction, close enough).
    """
    el = math.radians(elevation_deg)
    vx = v0_ms * math.cos(el)
    vy = v0_ms * math.sin(el)
    x = 0.0
    y = origin_y
    samples: list[tuple[float, float, float, float]] = []
    t = 0.0
    while t <= t_max_s and y >= origin_y - 0.5:
        # AirDrag: a = -drag * v * |v| (RDF AccelWithDrag, no wind)
        v = math.hypot(vx, vy)
        if v > 1e-6:
            ax = -AIR_DRAG_SHELL_82MM_HE * vx * v
            ay = GRAVITY_M_S2 - AIR_DRAG_SHELL_82MM_HE * vy * v
        else:
            ax = 0.0
            ay = GRAVITY_M_S2
        vx += ax * DT_S
        vy += ay * DT_S
        x += vx * DT_S
        y += vy * DT_S
        t += DT_S
        if y >= origin_y:
            elev_now = math.degrees(math.atan2(vy, max(1.0, vx)))
            speed_now = math.hypot(vx, vy)
            samples.append((t, y - origin_y, speed_now, elev_now))
    return samples


def flight_time_for_range(
    range_m: float,
    v0_ms: float,
    origin_y: float = 0.0,
    max_t: float = 90.0,
) -> float:
    """Find the ballistic arc (fixed elevation 45 deg-ish) whose downrange at
    impact equals range_m. Rough binary search on elevation."""
    lo_el = 30.0
    hi_el = 80.0
    best_t = max_t
    for _ in range(40):
        mid = (lo_el + hi_el) * 0.5
        samples = integrate_ballistic(v0_ms, mid, origin_y, max_t)
        if not samples:
            return best_t
        impact_x = 0.0
        impact_t = max_t
        for (t, dy, _, _) in samples:
            # track x implicitly: recompute via full integration is heavy;
            # approximate x by integrating vx along the arc.
            pass
        # Binary search on elevation to land exactly at range: use a direct
        # integrate-with-x version.
        break
    return _flight_time_exact(range_m, v0_ms, origin_y, max_t)


def _flight_time_exact(
    range_m: float,
    v0_ms: float,
    origin_y: float,
    max_t: float,
) -> float:
    """Integrate arcs over elevation, pick the one that lands at range_m."""
    best_t = max_t
    best_err = 1e18
    el = 30.0
    while el <= 80.0:
        el_rad = math.radians(el)
        vx0 = v0_ms * math.cos(el_rad)
        vy0 = v0_ms * math.sin(el_rad)
        x = 0.0
        y = origin_y
        vx = vx0
        vy = vy0
        t = 0.0
        while t <= max_t and y >= origin_y - 0.5:
            v = math.hypot(vx, vy)
            if v > 1e-6:
                ax = -AIR_DRAG_SHELL_82MM_HE * vx * v
                ay = GRAVITY_M_S2 - AIR_DRAG_SHELL_82MM_HE * vy * v
            else:
                ax = 0.0
                ay = GRAVITY_M_S2
            vx += ax * DT_S
            vy += ay * DT_S
            x += vx * DT_S
            y += vy * DT_S
            t += DT_S
            if y < origin_y and y + vy * DT_S >= origin_y:
                err = abs(x - range_m)
                if err < best_err:
                    best_err = err
                    best_t = t
                break
        el += 1.0
    return best_t


@dataclass
class ScanConfig:
    beamwidth_deg: float
    rpm: float
    snr_gate_db: float
    update_interval_s: float
    elevation_beams: list[tuple[str, float, float, float]] = field(
        default_factory=list
    )


def build_hw(cfg: ScanConfig, faction: str) -> s.Hardware:
    hw = s.Hardware(
        name=f"WLR_{faction}",
        frequency_hz=9.0e9,
        peak_power_w=120000.0,
        antenna_gain_dbi=32.0,
        az_beamwidth_deg=cfg.beamwidth_deg,
        system_loss_db=6.0,
        noise_figure_db=5.0,
        pulse_width_s=5.0e-7,
        bandwidth_hz=4.0e6,
        pulses_integrated=32,
        coherent_integration=True,
        enable_mti=False,
        mti_clutter_floor=1.0e-4,
        prf_hz=4000.0,
        scan_rpm=cfg.rpm,
        elevation_beams=[
            s.ElevationBeam(n, b, w, r) for (n, b, w, r) in cfg.elevation_beams
        ],
    )
    return hw


def build_settings(cfg: ScanConfig, range_m: float) -> s.Settings:
    return s.Settings(
        range_m=range_m,
        update_interval_s=cfg.update_interval_s,
        detection_snr_db=cfg.snr_gate_db,
        dem_clutter_scale=0.0,
        enable_dem_clutter=False,
        enable_atmospheric_loss=True,
        enable_cfar_gate=False,
        min_distance_m=40.0,
    )


def snr_at(hw, settings, range_m, rcs, radial_ms, elev_deg, az_off_deg, seed):
    rng = random.Random(seed)
    target_agl = settings.min_distance_m + range_m * math.tan(
        math.radians(elev_deg)
    )
    # LOS clear, flat terrain (WLR beams look up; clutter handled separately).
    res = s.physical_detect(
        hw,
        settings,
        range_m,
        rcs,
        radial_ms,
        az_off_deg,
        target_agl,
        8.0,
        False,
        1.0,
        0.0,
        0.0,
        rng,
        surface_class=2,
        cell_size_m=s.CELL_SIZE_M,
    )
    return res


def illumination_windows(
    rpm: float,
    beamwidth_deg: float,
    flight_time_s: float,
    update_interval_s: float,
    start_phase_deg: float = 0.0,
) -> int:
    """Count how many dwell updates a projectile gets while the rotating beam
    points at its azimuth (worst case: it sits at one bearing)."""
    period_s = 60.0 / rpm if rpm > 0 else 1e9
    beam_half = beamwidth_deg * 0.5
    # The projectile flies along a bearing that sweeps in azimuth too; worst
    # case for a counter-battery solve is it stays near one bearing (mortar
    # lobbed at the radar). Count dwells where |scan_az - target_az| < half.
    hits = 0
    t = 0.0
    while t <= flight_time_s:
        scan_deg = (start_phase_deg + t * 360.0 / period_s) % 360.0
        delta = abs(scan_deg - 0.0)
        if delta > 180.0:
            delta = 360.0 - delta
        if delta <= beam_half:
            hits += 1
        t += update_interval_s
    return hits


def validate_faction(
    faction: str,
    range_m: float,
    v0_ms: float,
    cfg: ScanConfig,
    radial_ms: float = 180.0,
) -> dict:
    hw = build_hw(cfg, faction)
    settings = build_settings(cfg, range_m)

    flight_s = _flight_time_exact(range_m, v0_ms, 0.0, 90.0)

    # Sample SNR along the arc: worst / mid / best elevation inside beams.
    snr_samples = []
    for elev in (15.0, 25.0, 40.0, 55.0):
        res = snr_at(
            hw,
            settings,
            range_m,
            PROJECTILE_RCS_M2,
            radial_ms,
            elev,
            0.0,
            1,
        )
        snr_samples.append(
            {
                "elev_deg": elev,
                "snr_db": res.snr_db,
                "beam": res.beam_name,
                "detected_snr": res.detected_snr,
            }
        )
    max_snr = max(x["snr_db"] for x in snr_samples)

    # Rotating-scan hit budget at the projectile's bearing.
    windows = illumination_windows(
        cfg.rpm, cfg.beamwidth_deg, flight_s, cfg.update_interval_s
    )
    hit_ok = windows >= WLR_MIN_HITS
    snr_ok = max_snr >= cfg.snr_gate_db

    return {
        "faction": faction,
        "range_m": range_m,
        "flight_s": round(flight_s, 1),
        "snr_samples": snr_samples,
        "max_snr_db": round(max_snr, 1),
        "snr_gate_db": cfg.snr_gate_db,
        "snr_ok": snr_ok,
        "beamwidth_deg": cfg.beamwidth_deg,
        "rpm": cfg.rpm,
        "illumination_windows": windows,
        "min_hits_needed": WLR_MIN_HITS,
        "hit_ok": hit_ok,
        "pass": snr_ok and hit_ok,
    }


def main() -> None:
    mortar_v0 = 210.0  # 82mm HE typical muzzle velocity m/s

    print("=" * 72)
    print("GBRS WLR offline validation (projectile counter-battery)")
    print("=" * 72)

    # Current GBRS config.
    us_cfg = ScanConfig(
        beamwidth_deg=25.0,
        rpm=10.0,
        snr_gate_db=6.0,
        update_interval_s=0.15,
        elevation_beams=[
            ("mortar_low", 18.0, 22.0, 0.0),
            ("mortar_mid", 35.0, 26.0, 0.0),
            ("mortar_high", 55.0, 26.0, -0.5),
        ],
    )
    ussr_cfg = ScanConfig(
        beamwidth_deg=30.0,
        rpm=6.0,
        snr_gate_db=5.0,
        update_interval_s=0.15,
        elevation_beams=[
            ("mortar_low", 18.0, 22.0, 0.0),
            ("mortar_mid", 35.0, 26.0, 0.0),
            ("mortar_high", 55.0, 26.0, -0.5),
        ],
    )

    print("\n--- Current config ---")
    for faction, cfg, rng_m in (
        ("US", us_cfg, 8000.0),
        ("USSR", ussr_cfg, 10000.0),
    ):
        r = validate_faction(faction, rng_m, mortar_v0, cfg)
        print(f"\n[{faction}] range={r['range_m']}m flight={r['flight_s']}s")
        print(f"  max SNR = {r['max_snr_db']} dB (gate {r['snr_gate_db']} dB) "
              f"{'PASS' if r['snr_ok'] else 'FAIL'}")
        for x in r["snr_samples"]:
            print(f"    elev {x['elev_deg']:>5.1f} deg -> {x['snr_db']:>6.1f} dB "
                  f"[{x['beam']}] {'DET' if x['detected_snr'] else 'miss'}")
        print(f"  illumination windows = {r['illumination_windows']} "
              f"(need {r['min_hits_needed']}) "
              f"{'PASS' if r['hit_ok'] else 'FAIL'}")
        print(f"  overall: {'PASS' if r['pass'] else 'FAIL'}")

    print("\n--- Beamwidth / RPM sweep (US 8 km, 5 hit gate) ---")
    print(f"{'bw':>5} {'rpm':>5} {'gate':>5} {'maxSNR':>7} {'wins':>5}  verdict")
    best = None
    for bw in (15.0, 20.0, 25.0, 30.0, 40.0):
        for rpm in (6.0, 10.0, 15.0):
            cfg = ScanConfig(
                beamwidth_deg=bw,
                rpm=rpm,
                snr_gate_db=6.0,
                update_interval_s=0.15,
                elevation_beams=us_cfg.elevation_beams,
            )
            r = validate_faction("US", 8000.0, mortar_v0, cfg)
            verdict = "PASS" if r["pass"] else "FAIL"
            print(
                f"{bw:>5.0f} {rpm:>5.0f} {6.0:>5.1f} {r['max_snr_db']:>7.1f} "
                f"{r['illumination_windows']:>5}  {verdict}"
            )
            if r["pass"]:
                score = r["illumination_windows"] + r["max_snr_db"] * 0.1
                if best is None or score > best[0]:
                    best = (score, bw, rpm, r)

    print("\n--- USSR 10 km beamwidth sweep (fixed rpm 6, gate 5) ---")
    for bw in (20.0, 25.0, 30.0, 40.0):
        cfg = ScanConfig(
            beamwidth_deg=bw,
            rpm=6.0,
            snr_gate_db=5.0,
            update_interval_s=0.15,
            elevation_beams=us_cfg.elevation_beams,
        )
        r = validate_faction("USSR", 10000.0, mortar_v0, cfg)
        print(
            f"{bw:>5.0f} {6.0:>5.0f} {5.0:>5.1f} {r['max_snr_db']:>7.1f} "
            f"{r['illumination_windows']:>5}  "
            f"{'PASS' if r['pass'] else 'FAIL'}"
        )

    print("\n--- Azimuth offset impact (beam center vs edge, US 8 km) ---")
    hw_az = build_hw(us_cfg, "US")
    settings_az = build_settings(us_cfg, 8000.0)
    for off in (0.0, 5.0, 10.0, 12.0):
        res = snr_at(hw_az, settings_az, 8000.0, PROJECTILE_RCS_M2, 180.0, 15.0, off, 1)
        print(f"  az_off {off:>4.0f} deg -> {res.snr_db:>6.1f} dB "
              f"{'DET' if res.detected_snr else 'miss'}")

    print("\n--- Joint power x beamwidth (US 8 km, gate 6 dB) ---")
    print(f"{'power':>8} {'bw':>5} {'maxSNR':>7}  verdict")
    for pw in (120000.0, 250000.0, 500000.0):
        for bw in (10.0, 15.0, 20.0, 25.0):
            cfg_j = ScanConfig(
                beamwidth_deg=bw, rpm=10.0, snr_gate_db=6.0,
                update_interval_s=0.15, elevation_beams=us_cfg.elevation_beams,
            )
            hw_j = build_hw(cfg_j, "US")
            hw_j.peak_power_w = pw
            settings_j = build_settings(cfg_j, 8000.0)
            res = snr_at(hw_j, settings_j, 8000.0, PROJECTILE_RCS_M2, 180.0, 15.0, 0.0, 1)
            ok = res.snr_db >= 6.0
            print(f"{pw:>8.0f} {bw:>5.0f} {res.snr_db:>7.1f}  "
                  f"{'PASS' if ok else 'FAIL'}")

    print("\n--- Beamwidth impact on illumination windows (10 RPM, 8 km flight) ---")
    for bw in (10.0, 15.0, 20.0, 25.0, 30.0):
        wins = illumination_windows(10.0, bw, 25.0, 0.15)
        print(f"  bw {bw:>4.0f} deg -> {wins} windows in 25 s flight")

    print("\n--- SNR gate sweep (US 8 km, current hw) ---")
    print(f"{'gate':>5} {'maxSNR':>7}  verdict @beam-center")
    hw = build_hw(us_cfg, "US")
    for gate in (-2.0, 0.0, 2.0, 4.0, 6.0):
        settings = build_settings(ScanConfig(25.0, 10.0, gate, 0.15, us_cfg.elevation_beams), 8000.0)
        res = snr_at(hw, settings, 8000.0, PROJECTILE_RCS_M2, 180.0, 15.0, 0.0, 1)
        ok = res.snr_db >= gate
        print(f"{gate:>5.1f} {res.snr_db:>7.1f}  {'PASS' if ok else 'FAIL'}")

    print("\n--- Peak power sweep (US 8 km, gate 6 dB) ---")
    print(f"{'power':>8} {'maxSNR':>7}  verdict")
    for pw in (120000.0, 250000.0, 500000.0, 1000000.0):
        hw_p = build_hw(us_cfg, "US")
        hw_p.peak_power_w = pw
        settings_p = build_settings(us_cfg, 8000.0)
        res = snr_at(hw_p, settings_p, 8000.0, PROJECTILE_RCS_M2, 180.0, 15.0, 0.0, 1)
        ok = res.snr_db >= 6.0
        print(f"{pw:>8.0f} {res.snr_db:>7.1f}  {'PASS' if ok else 'FAIL'}")

    if best:
        _, bw, rpm, r = best
        print(f"\nRecommended US: beamwidth={bw:.0f} deg, rpm={rpm:.0f}, "
              f"gate={r['snr_gate_db']} dB -> "
              f"{r['illumination_windows']} windows, {r['max_snr_db']} dB")


if __name__ == "__main__":
    main()
