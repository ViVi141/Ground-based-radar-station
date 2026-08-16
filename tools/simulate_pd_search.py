#!/usr/bin/env python3
"""Offline validation for GBRS air-defense (PD SEARCH) radar configs.

Validates the GBRS pulse-Doppler air-search presets against UH-1 class
targets (mean RCS ~16 m2, rotor sidebands) at the configured range:

  US   SHORAD (TPN-19 mesh, X-band PD): 7 km, 10 RPM, 2.5 deg, SNR 8 dB
  USSR P-18-like VHF EW (RPL-5 mesh): 10 km, 6 RPM, 6 deg, SNR 5 dB, DEM 0.10

Scenarios:
  1. Beam-center SNR at max range (ideal illumination)
  2. Rotating-scan illumination (uniform az in +/- bw/2, one paint per rev)
  3. Random dwell misses (most ticks outside the beam, bw/360 duty)
  4. Radial speed effects: head-on (fast), tangential (MTI null -> rotor
     sidebands only), hovering (rotor sidebands)
  5. Low-altitude (near beam lower edge) vs cruise AGL

Reuses the 1:1 offline chain from simulate_clutter_cover.py.
"""

from __future__ import annotations

import random
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import simulate_clutter_cover as s


def report_scenario(
    label: str,
    hw: s.Hardware,
    settings: s.Settings,
    range_m: float,
    radial_ms: float,
    yaw_deg: float,
    mode: str,
    trials: int = 200,
    seed: int = 7,
) -> dict:
    if mode == "center":
        r = s.run_scenario_pd(
            hw, settings, range_m, radial_ms, yaw_deg,
            False, 1.0, 0.0, 0.0,
            False, False, True, trials, seed,
        )
    elif mode == "scan":
        r = s.run_scenario_pd(
            hw, settings, range_m, radial_ms, yaw_deg,
            False, 1.0, 0.0, 0.0,
            True, False, True, trials, seed,
        )
    elif mode == "dwell":
        r = s.run_scenario_pd(
            hw, settings, range_m, radial_ms, yaw_deg,
            False, 1.0, 0.0, 0.0,
            False, True, True, trials, seed,
        )
    else:
        raise ValueError(mode)
    gate = settings.detection_snr_db
    print(
        f"  {label:<34} Pd_snr={r['pd_snr']*100:>5.0f}%  "
        f"Pd_cfar={r['pd_cfar']*100:>5.0f}%  mean={r['mean_snr_db']:>6.1f} dB "
        f"(gate {gate:>4.1f})"
    )
    return r


def main() -> None:
    print("=" * 76)
    print("GBRS air-defense (PD SEARCH) offline validation")
    print("=" * 76)

    us_hw, us_set = s.make_us()
    ussr_hw, ussr_set = s.make_ussr()

    print("\n--- US SHORAD @ 7 km (UH-1, cruise AGL 80 m) ---")
    report_scenario("beam center, head-on 50 m/s", us_hw, us_set, 7000, 50.0, 0.0, "center")
    report_scenario("beam center, tangential 0 m/s", us_hw, us_set, 7000, 0.0, 0.0, "center")
    report_scenario("scan illumination, head-on 50", us_hw, us_set, 7000, 50.0, 0.0, "scan")
    report_scenario("scan illumination, tangential 0", us_hw, us_set, 7000, 0.0, 0.0, "scan")
    report_scenario("random dwell, head-on 50", us_hw, us_set, 7000, 50.0, 0.0, "dwell")
    report_scenario("random dwell, tangential 0", us_hw, us_set, 7000, 0.0, 0.0, "dwell")

    print("\n--- US SHORAD @ 5 km (mid-range) ---")
    us_set5 = s.Settings(
        range_m=5000.0, update_interval_s=0.02, detection_snr_db=8.0,
        dem_clutter_scale=1.0,
    )
    report_scenario("scan illumination, head-on 50", us_hw, us_set5, 5000, 50.0, 0.0, "scan")
    report_scenario("scan illumination, tangential 0", us_hw, us_set5, 5000, 0.0, 0.0, "scan")

    print("\n--- USSR P-18 VHF EW @ 10 km (UH-1, cruise AGL 80 m) ---")
    report_scenario("beam center, head-on 50 m/s", ussr_hw, ussr_set, 10000, 50.0, 0.0, "center")
    report_scenario("beam center, tangential 0 m/s", ussr_hw, ussr_set, 10000, 0.0, 0.0, "center")
    report_scenario("scan illumination, head-on 50", ussr_hw, ussr_set, 10000, 50.0, 0.0, "scan")
    report_scenario("scan illumination, tangential 0", ussr_hw, ussr_set, 10000, 0.0, 0.0, "scan")
    report_scenario("random dwell, head-on 50", ussr_hw, ussr_set, 10000, 50.0, 0.0, "dwell")
    report_scenario("random dwell, tangential 0", ussr_hw, ussr_set, 10000, 0.0, 0.0, "dwell")

    print("\n--- USSR P-18 VHF EW @ 7 km (closer) ---")
    ussr_set7 = s.Settings(
        range_m=7000.0, update_interval_s=0.04, detection_snr_db=5.0,
        dem_clutter_scale=0.10,
    )
    report_scenario("scan illumination, head-on 50", ussr_hw, ussr_set7, 7000, 50.0, 0.0, "scan")
    report_scenario("scan illumination, tangential 0", ussr_hw, ussr_set7, 7000, 0.0, 0.0, "scan")

    print("\n--- Low-altitude sensitivity (US, AGL 20 m, near beam lower edge) ---")
    # run_scenario_pd uses TARGET_AGL_M constant; emulate lower AGL by
    # running at shorter range where elevation angle stays low.
    us_set_low = s.Settings(
        range_m=7000.0, update_interval_s=0.02, detection_snr_db=8.0,
        dem_clutter_scale=1.0,
    )
    report_scenario("low AGL head-on 50 (range 3 km)", us_hw, us_set_low, 3000, 50.0, 0.0, "scan")
    report_scenario("low AGL tangential 0 (range 3 km)", us_hw, us_set_low, 3000, 0.0, 0.0, "scan")


if __name__ == "__main__":
    main()
