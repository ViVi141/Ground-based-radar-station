#!/usr/bin/env python3
"""1:1 offline port of GBRS → RDF_RadarPhysicalDetect + CFAR + scan dwell.

Mirrors Enforce sources (do not "improve" formulas):
  RDF_RadarClutterModel.c
  RDF_RadarPhysicalDetect.c
  RDF_RadarCfarGate.c / RDF_RadarCfarProcessor.c
  RDF_RadarHardware.c / RDF_RadarRcsModel.c
  RDF_RadarSurfaceTable builtins (RDF 1.1.0 per-band σ⁰ via gbrs_rdf_band.py)
  RDF_RadarRcsModel.AspectRcsFromObb / AspectRcsFromExtents3D
  GBRS_RadarStationConfig.c (US / USSR search presets)

Previous sim assumed boresight patternGain=1, fixed RCS=12, fixed radial=50,
and SNR-only gate. That is an optimistic upper bound, not the game loop.
This script runs the same chain as the game and reports hard scenarios.
"""

from __future__ import annotations

import json
import math
import random
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path

# --- constants (RDF_RadarClutterModel / PhysicalDetect) ---
C_LIGHT = 299792458.0
FOUR_PI = 12.5663706144
BOLTZMANN = 1.380649e-23
T0_K = 290.0
DEG_TO_RAD = 0.0174532925199
CELL_SIZE_M = 30.0  # fallback when DEM absent; live DEM uses dem.cell_m

from gbrs_rdf_band import (
    SURF_NAMES,
    aspect_rcs_from_extents_3d,
    band_for_frequency,
    sigma0_linear,
    surface_atten_db_per_km,
)

# UH-1H from RDF Signatures/rdf_radar_signatures.conf
# Prefab: Prefabs/Vehicles/Helicopters/UH1H/UH1H_base.et (and armed_gunship_HE)
UH1_SIG_KEY = "{D03C0F6044DB5208}Prefabs/Vehicles/Helicopters/UH1H/UH1H_base.et"
UH1_SIZE_X = 2.86572  # beam
UH1_SIZE_Y = 5.06917  # height
UH1_SIZE_Z = 12.9736  # length
UH1_MEAN_RCS_M2 = 16.4414
UH1_SWERLING = 1
UH1_RCS_SEED = 424242
UH1_SCATTERER_ID = 1
TARGET_AGL_M = 80.0
RADAR_AGL_M = 8.0  # mast fallback; DEM mode uses live Y - terrainY

RANGES_M = tuple(float(r) for r in range(500, 7501, 250))
SCALES = (0.25, 0.5, 0.75, 1.0, 1.5, 2.0)
RADIAL_SPEEDS_MS = (0.0, 5.0, 20.0, 40.0, 50.0, 60.0, 80.0)
MC_TRIALS = 400
RNG_SEED = 42

# Import local Eden DEM helper (profile bake).
_GBRS_DIR = Path(__file__).resolve().parent
if str(_GBRS_DIR) not in sys.path:
    sys.path.insert(0, str(_GBRS_DIR))
try:
    from gbrs_eden_dem import (
        DEFAULT_RADAR_XYZ,
        EdenDemCrop,
        find_land_bearing,
        load_eden_crop,
        los_probe,
        pick_target_on_bearing,
    )
except ImportError:
    DEFAULT_RADAR_XYZ = (4771.01, 27.8448, 11214.7)
    EdenDemCrop = None  # type: ignore
    find_land_bearing = None  # type: ignore
    load_eden_crop = None  # type: ignore
    los_probe = None  # type: ignore
    pick_target_on_bearing = None  # type: ignore


def db_to_lin(db: float) -> float:
    return 10.0 ** (db / 10.0)


def lin_to_db(linear: float) -> float:
    if linear <= 1e-30:
        return -300.0
    return 10.0 * math.log10(linear)


@dataclass
class ElevationBeam:
    name: str
    boresight_deg: float
    beamwidth_deg: float
    relative_gain_db: float = 0.0


@dataclass
class Hardware:
    """Matches RDF_RadarHardware fields used by PhysicalDetect."""

    name: str
    frequency_hz: float
    peak_power_w: float
    antenna_gain_dbi: float
    az_beamwidth_deg: float
    system_loss_db: float
    noise_figure_db: float
    pulse_width_s: float
    bandwidth_hz: float
    pulses_integrated: int
    coherent_integration: bool
    enable_mti: bool
    mti_clutter_floor: float
    prf_hz: float
    scan_rpm: float
    elevation_beams: list[ElevationBeam] = field(default_factory=list)
    receiver_recovery_s: float = 0.0
    # "two_pulse" matches legacy PhysicalDetect; "mtd_bank" matches
    # GBRS ApplyPulseDopplerHardware (RDF_MTI_MTD_BANK).
    mti_mode: str = "two_pulse"
    mtd_clutter_leakage: float = 1.0e-6

    def wavelength_m(self) -> float:
        if self.frequency_hz <= 0.0:
            return 1.0
        return C_LIGHT / self.frequency_hz

    def tx_blanking_time_s(self) -> float:
        blank = self.pulse_width_s
        if blank < 0.0:
            blank = 0.0
        if self.receiver_recovery_s > 0.0:
            blank = blank + self.receiver_recovery_s
        return blank

    def min_detectable_range_m(self) -> float:
        blank = self.tx_blanking_time_s()
        if blank <= 0.0:
            return 0.0
        return C_LIGHT * blank * 0.5

    def processing_gain(self) -> float:
        # RDF_RadarHardware.GetProcessingGain
        pulse_compression = self.bandwidth_hz * self.pulse_width_s
        if pulse_compression < 1.0:
            pulse_compression = 1.0
        pulse_count = max(1, self.pulses_integrated)
        if self.coherent_integration:
            integration = float(pulse_count)
        else:
            integration = math.sqrt(float(pulse_count))
        return pulse_compression * integration

    def noise_power_w(self) -> float:
        bandwidth = max(1.0, self.bandwidth_hz)
        return BOLTZMANN * T0_K * bandwidth * db_to_lin(self.noise_figure_db)

    def scan_period_s(self) -> float:
        if self.scan_rpm <= 0.0:
            return 1.0e9
        return 60.0 / self.scan_rpm


@dataclass
class Settings:
    """Subset of RDF_RadarSettings used by PhysicalDetect / CFAR / GBRS."""

    range_m: float
    update_interval_s: float
    detection_snr_db: float
    dem_clutter_scale: float
    enable_dem_clutter: bool = True
    enable_atmospheric_loss: bool = True
    atm_loss_db_per_km_one_way: float = -1.0  # <0 → auto
    rain_loss_db_per_km_one_way: float = 0.0
    enable_cfar_gate: bool = True
    enable_cfar_thermal_fill: bool = True
    cfar_guard_cells: int = 2
    cfar_training_cells: int = 8
    cfar_pfa: float = 1.0e-6
    cfar_mode: str = "ca"  # ca / go / so
    range_bin_count: int = 64
    additional_noise_power_w: float = 0.0
    nlos_reflection_abs: float = 0.55
    nlos_min_factor: float = 0.008
    nlos_max_target_agl_m: float = 800.0
    enable_nlos_multipath: bool = True
    enable_knife_edge: bool = True
    knife_edge_clearance_slack_m: float = 2.0
    min_distance_m: float = 40.0
    # RDF opt-in channel extras (ApplyRealisticChannelOptIn). Default off so
    # existing PD validation scripts keep their published numbers.
    enable_los_two_ray: bool = False
    los_two_ray_reflection_coeff: float = -0.5
    los_two_ray_max_target_agl_m: float = 600.0
    los_two_ray_min_factor: float = 0.08
    los_two_ray_max_factor: float = 4.0
    enable_atmospheric_refraction: bool = False
    earth_radius_factor: float = 1.3333333
    enable_weather_rain: bool = False
    enable_range_ambiguity_fold: bool = False
    enable_doppler_ambiguity_fold: bool = False


@dataclass
class DetectResult:
    detected_snr: bool
    detected_cfar: bool
    snr_db: float
    received_power_w: float
    processed_power_w: float
    clutter_power_w: float
    thermal_proc_w: float
    noise_power_w: float
    pattern_gain: float
    mti_gain: float
    multipath_factor: float
    rcs_m2: float
    beam_name: str
    azimuth_offset_deg: float
    elevation_deg: float
    doppler_hz: float
    cfar_threshold_w: float


def make_us() -> tuple[Hardware, Settings]:
    hw = Hardware(
        name="US_SHORAD_PD",
        frequency_hz=9.0e9,
        peak_power_w=120000.0,
        antenna_gain_dbi=32.0,
        az_beamwidth_deg=2.5,
        system_loss_db=6.0,
        noise_figure_db=5.0,
        pulse_width_s=5.0e-7,
        bandwidth_hz=4.0e6,
        pulses_integrated=32,
        coherent_integration=True,
        enable_mti=True,
        mti_clutter_floor=1.0e-4,
        prf_hz=4000.0,
        scan_rpm=10.0,
        elevation_beams=[
            ElevationBeam("low", 2.0, 16.0, 0.0),
            ElevationBeam("mid", 18.0, 24.0, 0.0),
            ElevationBeam("high", 40.0, 30.0, -1.0),
        ],
        receiver_recovery_s=0.2e-6,
    )
    settings = Settings(
        range_m=7000.0,
        update_interval_s=0.02,
        detection_snr_db=8.0,
        dem_clutter_scale=1.0,
        min_distance_m=hw.min_detectable_range_m(),
    )
    return hw, settings


def make_ussr() -> tuple[Hardware, Settings]:
    hw = Hardware(
        name="USSR_P18_PD",
        frequency_hz=1.6e8,
        peak_power_w=250000.0,
        antenna_gain_dbi=20.0,
        az_beamwidth_deg=6.0,
        system_loss_db=8.0,
        noise_figure_db=6.0,
        pulse_width_s=6.0e-6,
        bandwidth_hz=166666.0,
        pulses_integrated=12,
        coherent_integration=True,
        enable_mti=True,
        mti_clutter_floor=0.01,
        prf_hz=200.0,
        scan_rpm=6.0,
        elevation_beams=[
            ElevationBeam("low", 2.0, 16.0, 0.0),
            ElevationBeam("mid", 18.0, 24.0, 0.0),
            ElevationBeam("high", 42.0, 30.0, -1.0),
        ],
        receiver_recovery_s=1.0e-6,
    )
    settings = Settings(
        range_m=10000.0,
        update_interval_s=0.04,
        detection_snr_db=5.0,
        dem_clutter_scale=0.5,
        min_distance_m=hw.min_detectable_range_m(),
    )
    return hw, settings


# --- RDF_RadarRcsModel (UH-1 signature path) ---


def wrap_delta_deg(delta_deg: float) -> float:
    rel = delta_deg
    while rel > 180.0:
        rel = rel - 360.0
    while rel < -180.0:
        rel = rel + 360.0
    return rel


def hash_unit(seed: int, id_a: int, id_b: int, channel: int) -> float:
    """RDF_RadarRcsModel.HashUnit — deterministic u in (eps, 1]."""
    x = int(seed)
    x = x * 374761393 + id_a * 668265263
    x = x * 2246822519 + id_b * 3266489917
    x = x * 668265263 + channel * 1013904223
    # Keep in 32-bit-ish signed range like Enforce int overflow isn't exact;
    # fold with unsigned mask for stable Python ints.
    x = x & 0x7FFFFFFF
    rem = x % 1000000
    u = rem / 1000000.0
    if u < 0.000001:
        u = 0.000001
    if u > 1.0:
        u = 1.0
    return u


def sample_swerling(
    mean_rcs_m2: float,
    model: int,
    seed: int,
    scan_number: int,
    scatterer_id: int,
) -> float:
    if mean_rcs_m2 <= 0.0:
        return 0.0
    if model <= 0:
        return mean_rcs_m2
    u1 = hash_unit(seed, scatterer_id, scan_number, 1)
    u2 = hash_unit(seed, scatterer_id, scan_number, 2)
    if model == 2 or model == 4:
        u1 = hash_unit(seed, scatterer_id, scan_number * 131 + 17, 3)
        u2 = hash_unit(seed, scatterer_id, scan_number * 131 + 17, 4)
    if model == 3 or model == 4:
        return -0.5 * mean_rcs_m2 * (math.log(u1) + math.log(u2))
    return -mean_rcs_m2 * math.log(u1)


def evaluate_instant_rcs_uh1(
    yaw_deg: float,
    pitch_deg: float,
    los_azimuth_deg: float,
    los_elevation_deg: float,
    scan_number: int,
    use_swerling: bool,
) -> float:
    """Matches RDF_RadarScattererRegistry.EvaluateInstantRcsM2 for UH1H."""
    aspect = aspect_rcs_from_extents_3d(
        UH1_MEAN_RCS_M2,
        UH1_SIZE_X,
        UH1_SIZE_Y,
        UH1_SIZE_Z,
        yaw_deg,
        pitch_deg,
        los_azimuth_deg,
        los_elevation_deg,
    )
    if not use_swerling:
        return aspect
    return sample_swerling(
        aspect, UH1_SWERLING, UH1_RCS_SEED, scan_number, UH1_SCATTERER_ID
    )


def estimate_rcs_from_extents(size_x: float, size_y: float, size_z: float) -> float:
    fallback = 5.0  # vehicle default
    width = abs(size_x)
    height = abs(size_y)
    length = abs(size_z)
    projected = max(width * height, length * height)
    if projected <= 0.01:
        return fallback
    estimate = projected * 0.25
    lo = fallback * 0.25
    hi = 1000.0
    if estimate < lo:
        return lo
    if estimate > hi:
        return hi
    return estimate


def swerling1_draw(mean_rcs: float, rng: random.Random) -> float:
    # Kept for legacy flat scenarios; prefer evaluate_instant_rcs_uh1.
    if mean_rcs <= 0.0:
        return 0.0
    u = rng.random()
    if u < 1e-12:
        u = 1e-12
    return -mean_rcs * math.log(u)


# --- RDF_RadarClutterModel ---


def gaussian_beam_gain(offset_deg: float, beamwidth_deg: float) -> float:
    if beamwidth_deg <= 1e-6:
        return 1.0
    half_width = beamwidth_deg * 0.5
    ratio = offset_deg / half_width
    return 0.5 ** (ratio * ratio)


def strongest_beam_gain(
    hw: Hardware, azimuth_offset_deg: float, elevation_deg: float
) -> tuple[float, str]:
    if not hw.elevation_beams:
        return 0.0, ""
    az_gain = gaussian_beam_gain(azimuth_offset_deg, hw.az_beamwidth_deg)
    strongest = 0.0
    beam_name = ""
    for beam in hw.elevation_beams:
        el_gain = gaussian_beam_gain(
            elevation_deg - beam.boresight_deg, beam.beamwidth_deg
        )
        relative = db_to_lin(beam.relative_gain_db)
        one_way = az_gain * el_gain * relative
        two_way = one_way * one_way
        if two_way > strongest:
            strongest = two_way
            beam_name = beam.name
    return strongest, beam_name


def received_power_w(
    hw: Hardware, sigma_m2: float, range_m: float, pattern_two_way: float
) -> float:
    if range_m < 1.0:
        range_m = 1.0
    if sigma_m2 <= 0.0 or pattern_two_way <= 0.0 or hw.frequency_hz <= 0.0:
        return 0.0
    wavelength = hw.wavelength_m()
    gain_lin = db_to_lin(hw.antenna_gain_dbi)
    loss_lin = db_to_lin(hw.system_loss_db)
    denom = FOUR_PI * FOUR_PI * FOUR_PI * loss_lin
    radar_const = hw.peak_power_w * gain_lin * gain_lin * wavelength * wavelength / denom
    return radar_const * pattern_two_way * sigma_m2 / (range_m**4)


def atmospheric_one_way_db_per_km(frequency_hz: float) -> float:
    f_ghz = frequency_hz / 1.0e9
    if f_ghz < 1.0:
        return 0.003
    if f_ghz < 4.0:
        return 0.007
    if f_ghz < 8.0:
        return 0.012
    if f_ghz < 12.0:
        return 0.02
    return 0.04


def two_ray_multipath_factor(
    wavelength_m: float,
    range_m: float,
    radar_agl_m: float,
    target_agl_m: float,
    reflection_coeff: float,
    max_height_m: float,
    min_factor: float,
    max_factor: float,
) -> float:
    """RDF_RadarClutterModel.TwoRayMultipathFactor (clear LOS)."""
    if wavelength_m <= 0.0:
        return 1.0
    if range_m < 1.0:
        return 1.0
    if radar_agl_m < 1.0:
        return 1.0
    if target_agl_m < 1.0:
        return 1.0
    if max_height_m > 0.0:
        if target_agl_m > max_height_m:
            return 1.0

    delta = 2.0 * radar_agl_m * target_agl_m / range_m
    phase = 2.0 * math.pi * delta / wavelength_m
    real = 1.0 + reflection_coeff * math.cos(phase)
    imag = reflection_coeff * math.sin(phase)
    factor = real * real + imag * imag

    lo = min_factor
    if lo < 0.01:
        lo = 0.01
    hi = max_factor
    if hi < lo:
        hi = lo
    if factor < lo:
        factor = lo
    if factor > hi:
        factor = hi
    return factor


def radio_horizon_range_m(
    radar_agl_m: float, target_agl_m: float, earth_radius_factor: float
) -> float:
    """RDF_RadarClutterModel.RadioHorizonRangeM (k-Earth)."""
    re = 6371000.0 * earth_radius_factor
    hr = radar_agl_m
    if hr < 0.0:
        hr = 0.0
    ht = target_agl_m
    if ht < 0.0:
        ht = 0.0
    return math.sqrt(2.0 * re * hr) + math.sqrt(2.0 * re * ht)


def horizon_soft_factor(range_m: float, horizon_m: float) -> float:
    """RDF_RadarClutterModel.HorizonSoftFactor."""
    if horizon_m <= 1.0:
        return 1.0
    if range_m <= horizon_m:
        return 1.0
    over = (range_m - horizon_m) / horizon_m
    if over < 0.0:
        over = 0.0
    denom = 1.0 + 4.0 * over * over
    factor = 1.0 / denom
    if factor < 0.02:
        factor = 0.02
    return factor


def unambiguous_range_m(prf_hz: float) -> float:
    if prf_hz <= 0.0:
        return 1.0e12
    return C_LIGHT / (2.0 * prf_hz)


def unambiguous_velocity_ms(wavelength_m: float, prf_hz: float) -> float:
    if prf_hz <= 0.0:
        return 1.0e12
    if wavelength_m <= 0.0:
        return 1.0e12
    return wavelength_m * prf_hz * 0.25


def atmospheric_loss_linear(
    range_m: float, atm_db_per_km_one_way: float, rain_db_per_km_one_way: float
) -> float:
    if range_m < 0.0:
        range_m = 0.0
    alpha = atm_db_per_km_one_way + rain_db_per_km_one_way
    if alpha <= 0.0:
        return 1.0
    loss_db = 2.0 * alpha * (range_m / 1000.0)
    factor = db_to_lin(loss_db)
    if factor < 1.0:
        return 1.0
    return factor


def mti_two_pulse_gain(doppler_hz: float, prf_hz: float) -> float:
    if prf_hz <= 0.0:
        return 1.0
    s = math.sin(math.pi * doppler_hz / prf_hz)
    return s * s


def mtd_bank_gains(hw: Hardware, radial_ms: float) -> tuple[float, float]:
    """MTD_BANK target / clutter gains used by GBRS PD SEARCH.

    Moving body (>= 3 m/s radial) sits in a clear Doppler bin: mti_gain 1,
    clutter is leakage. Hover / slow body keeps a rotor-sideband fraction
    so VHF EW still paints a helicopter, matching calib_pd_full.py.
    """
    leak = hw.mtd_clutter_leakage
    if leak < 1e-9:
        leak = 1e-9
    abs_vr = radial_ms
    if abs_vr < 0.0:
        abs_vr = -abs_vr
    if abs_vr >= 3.0:
        return 1.0, leak
    return 0.09, leak


def doppler_hz(radial_ms: float, wavelength_m: float) -> float:
    if wavelength_m <= 0.0:
        return 0.0
    return 2.0 * radial_ms / wavelength_m


def estimate_range_resolution_m(hw: Hardware) -> float:
    # RDF_RadarPhysicalDetect.EstimateRangeResolutionM (min 0.5)
    by_bw = 0.0
    if hw.bandwidth_hz > 0.0:
        by_bw = C_LIGHT / (2.0 * hw.bandwidth_hz)
    by_pw = 0.0
    if hw.pulse_width_s > 0.0:
        by_pw = C_LIGHT * hw.pulse_width_s * 0.5
    if by_bw > 0.0:
        if by_bw < 0.5:
            return 0.5
        return by_bw
    if by_pw > 0.0:
        if by_pw < 0.5:
            return 0.5
        return by_pw
    return 1.0


def dem_clutter_processed_w(
    hw: Hardware,
    settings: Settings,
    range_m: float,
    pattern_gain: float,
    processing_gain: float,
    radar_agl_m: float,
    surface_class: int,
    cell_size_m: float,
    clutter_mti_override: float = -1.0,
) -> float:
    if not settings.enable_dem_clutter:
        return 0.0
    if settings.dem_clutter_scale <= 0.0 or range_m <= 0.001:
        return 0.0
    grazing = math.atan2(abs(radar_agl_m), max(1.0, range_m))
    band = band_for_frequency(hw.frequency_hz)
    sigma0 = sigma0_linear(surface_class, grazing, band) * settings.dem_clutter_scale
    if sigma0 <= 0.0:
        return 0.0
    cell = cell_size_m
    if cell < 1.0:
        cell = CELL_SIZE_M
    az_width_m = abs(range_m * (hw.az_beamwidth_deg * DEG_TO_RAD))
    if az_width_m < cell:
        az_width_m = cell
    range_res = estimate_range_resolution_m(hw)
    area_m2 = range_res * az_width_m
    min_area = cell * cell
    if area_m2 < min_area:
        area_m2 = min_area
    received = received_power_w(hw, sigma0 * area_m2, range_m, pattern_gain)
    atten = surface_atten_db_per_km(surface_class, band)
    if atten > 0.0:
        surface_loss = atmospheric_loss_linear(range_m, atten, 0.0)
        if surface_loss > 1.0:
            received = received / surface_loss
    if received <= 0.0:
        return 0.0
    if clutter_mti_override >= 0.0:
        clutter_mti = clutter_mti_override
    else:
        clutter_mti = 1.0
        if hw.enable_mti:
            clutter_mti = hw.mti_clutter_floor
    if clutter_mti < 1e-6:
        clutter_mti = 1e-6
    return received * processing_gain * clutter_mti


def nlos_bounce_factor(
    settings: Settings,
    range_m: float,
    radar_agl_m: float,
    target_agl_m: float,
    hit_fraction: float,
) -> float:
    if not settings.enable_nlos_multipath:
        return 0.0
    if range_m < 1.0:
        return 0.0
    hr = radar_agl_m
    ht = target_agl_m
    if hr < 0.5:
        hr = 0.5
    if ht < 0.5:
        ht = 0.5
    if ht > settings.nlos_max_target_agl_m:
        return 0.0
    sum_h = hr + ht
    bounce_range = math.sqrt(range_m * range_m + sum_h * sum_h)
    if bounce_range < 1.0:
        return 0.0
    ratio = range_m / bounce_range
    geom = ratio**4
    gamma = settings.nlos_reflection_abs
    factor = gamma * gamma * geom
    depth = hit_fraction
    if depth < 0.2:
        depth = 0.2
    if depth > 1.0:
        depth = 1.0
    factor = factor * depth
    if factor < settings.nlos_min_factor:
        return 0.0
    if factor > 1.0:
        factor = 1.0
    return factor


def knife_edge_linear_factor(nu: float) -> float:
    if nu <= -0.78:
        return 1.0
    t = nu - 0.1
    inner = math.sqrt(t * t + 1.0) + t
    if inner < 0.001:
        inner = 0.001
    loss_db = 6.9 + 20.0 * math.log10(inner)
    if loss_db < 0.0:
        loss_db = 0.0
    factor = db_to_lin(-loss_db)
    if factor > 1.0:
        factor = 1.0
    if factor < 0.0:
        factor = 0.0
    return factor


def knife_edge_factor(
    settings: Settings, range_m: float, h_obs_m: float, u_edge: float, wavelength_m: float
) -> float:
    if not settings.enable_knife_edge:
        return 0.0
    if h_obs_m <= 0.0 or range_m < 1.0:
        return 0.0
    u = u_edge
    if u < 0.05:
        u = 0.05
    if u > 0.95:
        u = 0.95
    d1 = u * range_m
    d2 = range_m - d1
    if d1 < 1.0:
        d1 = 1.0
    if d2 < 1.0:
        d2 = 1.0
    lam = wavelength_m
    if lam < 0.001:
        lam = 0.001
    nu = h_obs_m * math.sqrt(2.0 * range_m / (lam * d1 * d2))
    factor = knife_edge_linear_factor(nu)
    if factor < settings.nlos_min_factor:
        return 0.0
    return factor


# --- CFAR (RDF_RadarCfarGate) ---


def cfar_cell_detected(
    row_powers: list[float],
    bin_idx: int,
    noise_floor_w: float,
    guard_cells: int,
    training_cells: int,
    pfa: float,
    mode: str,
) -> tuple[bool, float]:
    nbin = len(row_powers)
    if nbin <= 0 or bin_idx < 0 or bin_idx >= nbin:
        return False, 0.0
    n_train = training_cells
    if n_train < 2:
        n_train = 2
    alpha = n_train * (pfa ** (-1.0 / n_train) - 1.0)
    if alpha < 1.0:
        alpha = 1.0
    half = n_train // 2
    sum_left = 0.0
    count_left = 0
    sum_right = 0.0
    count_right = 0
    left0 = bin_idx - guard_cells - half
    left1 = bin_idx - guard_cells - 1
    for i in range(left0, left1 + 1):
        if i < 0 or i >= nbin:
            continue
        sum_left += row_powers[i]
        count_left += 1
    right0 = bin_idx + guard_cells + 1
    right1 = bin_idx + guard_cells + half
    for j in range(right0, right1 + 1):
        if j < 0 or j >= nbin:
            continue
        sum_right += row_powers[j]
        count_right += 1
    mean_left = noise_floor_w
    if count_left > 0:
        mean_left = sum_left / count_left
    mean_right = noise_floor_w
    if count_right > 0:
        mean_right = sum_right / count_right
    local_noise = noise_floor_w
    if mode == "go":
        if mean_left > mean_right:
            local_noise = mean_left
        else:
            local_noise = mean_right
    elif mode == "so":
        if count_left <= 0 and count_right <= 0:
            local_noise = noise_floor_w
        elif count_left <= 0:
            local_noise = mean_right
        elif count_right <= 0:
            local_noise = mean_left
        elif mean_left < mean_right:
            local_noise = mean_left
        else:
            local_noise = mean_right
    else:
        count = count_left + count_right
        total = sum_left + sum_right
        if count > 0:
            local_noise = total / count
    if local_noise < noise_floor_w:
        local_noise = noise_floor_w
    threshold = local_noise * alpha
    return row_powers[bin_idx] > threshold, threshold


def apply_cfar_single_target(
    settings: Settings,
    hw: Hardware,
    processed_power_w: float,
    range_m: float,
    rng: random.Random,
) -> tuple[bool, float]:
    if not settings.enable_cfar_gate:
        return True, 0.0
    n_bins = settings.range_bin_count
    if n_bins < 8:
        n_bins = 8
    max_range = max(1.0, settings.range_m)
    dist = range_m
    if dist > max_range:
        dist = max_range
    range_norm = dist / max_range
    if range_norm < 0.0:
        range_norm = 0.0
    if range_norm > 0.999999:
        range_norm = 0.999999
    bin_idx = int(math.floor(range_norm * n_bins))
    if bin_idx < 0:
        bin_idx = 0
    if bin_idx >= n_bins:
        bin_idx = n_bins - 1

    processing_gain = hw.processing_gain()
    noise_floor = hw.noise_power_w() * processing_gain + settings.additional_noise_power_w
    if noise_floor < 1e-30:
        noise_floor = 1e-30

    row = [0.0] * n_bins
    row[bin_idx] = processed_power_w
    if settings.enable_cfar_thermal_fill:
        # RDF FillEmptyCellsWithThermalNoise: Gauss(sigma=floor, mean=floor)
        # Approximated with clipped normal samples.
        sigma = noise_floor
        min_sample = noise_floor * 0.01
        for i in range(n_bins):
            if row[i] > 0.0:
                continue
            sample = rng.gauss(noise_floor, sigma)
            if sample < min_sample:
                sample = min_sample
            row[i] = sample
    hit, thr = cfar_cell_detected(
        row,
        bin_idx,
        noise_floor,
        settings.cfar_guard_cells,
        settings.cfar_training_cells,
        settings.cfar_pfa,
        settings.cfar_mode,
    )
    return hit, thr


# --- PhysicalDetect.Process (active radar path) ---


def effective_min_distance_m(hw: Hardware, settings: Settings) -> float:
    pulse_min = hw.min_detectable_range_m()
    floor = settings.min_distance_m
    if pulse_min > floor:
        return pulse_min
    return floor


def undetected_result(
    rcs_m2: float,
    azimuth_offset_deg: float,
    elevation_deg: float,
) -> DetectResult:
    return DetectResult(
        False,
        False,
        -300.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        rcs_m2,
        "",
        azimuth_offset_deg,
        elevation_deg,
        0.0,
        0.0,
    )


def physical_detect(
    hw: Hardware,
    settings: Settings,
    range_m: float,
    rcs_m2: float,
    radial_ms: float,
    azimuth_offset_deg: float,
    target_agl_m: float,
    radar_agl_m: float,
    los_blocked: bool,
    hit_fraction: float,
    obstacle_height_m: float,
    obstacle_u: float,
    rng: random.Random,
    surface_class: int = 2,
    cell_size_m: float = CELL_SIZE_M,
) -> DetectResult:
    elevation_deg = math.degrees(math.atan2(target_agl_m - radar_agl_m, max(1.0, range_m)))
    min_d = effective_min_distance_m(hw, settings)
    if range_m <= min_d:
        return undetected_result(rcs_m2, azimuth_offset_deg, elevation_deg)

    multipath = 1.0
    used_knife = False
    if los_blocked:
        bounce = nlos_bounce_factor(
            settings, range_m, radar_agl_m, target_agl_m, hit_fraction
        )
        knife = knife_edge_factor(
            settings, range_m, obstacle_height_m, obstacle_u, hw.wavelength_m()
        )
        multipath = bounce
        if knife > multipath:
            multipath = knife
            used_knife = True
        if multipath <= 0.0:
            return DetectResult(
                False,
                False,
                -300.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                rcs_m2,
                "",
                azimuth_offset_deg,
                elevation_deg,
                0.0,
                0.0,
            )
    elif settings.enable_los_two_ray:
        multipath = two_ray_multipath_factor(
            hw.wavelength_m(),
            range_m,
            radar_agl_m,
            target_agl_m,
            settings.los_two_ray_reflection_coeff,
            settings.los_two_ray_max_target_agl_m,
            settings.los_two_ray_min_factor,
            settings.los_two_ray_max_factor,
        )
    if settings.enable_atmospheric_refraction:
        horizon_m = radio_horizon_range_m(
            radar_agl_m, target_agl_m, settings.earth_radius_factor
        )
        multipath = multipath * horizon_soft_factor(range_m, horizon_m)

    pattern_gain, beam_name = strongest_beam_gain(hw, azimuth_offset_deg, elevation_deg)
    if los_blocked:
        if used_knife:
            beam_name = beam_name + "/diff"
        else:
            beam_name = beam_name + "/nlos"

    pr = received_power_w(hw, rcs_m2, range_m, pattern_gain)
    pr = pr * multipath
    if settings.enable_atmospheric_loss:
        atm_db = settings.atm_loss_db_per_km_one_way
        if atm_db < 0.0:
            atm_db = atmospheric_one_way_db_per_km(hw.frequency_hz)
        rain_db = 0.0
        if settings.enable_weather_rain:
            rain_db = settings.rain_loss_db_per_km_one_way
        latm = atmospheric_loss_linear(range_m, atm_db, rain_db)
        if latm > 1.0:
            pr = pr / latm

    fd = doppler_hz(radial_ms, hw.wavelength_m())
    mti_gain = 1.0
    clutter_mti = 1.0
    if hw.enable_mti:
        if hw.mti_mode == "mtd_bank":
            mti_gain, clutter_mti = mtd_bank_gains(hw, radial_ms)
        else:
            mti_gain = mti_two_pulse_gain(fd, hw.prf_hz)
            if mti_gain < 1e-6:
                mti_gain = 1e-6
            clutter_mti = hw.mti_clutter_floor
            if clutter_mti < 1e-6:
                clutter_mti = 1e-6

    gproc = hw.processing_gain()
    processed = pr * gproc * mti_gain
    clutter_override = -1.0
    if hw.enable_mti:
        clutter_override = clutter_mti
    clutter = dem_clutter_processed_w(
        hw,
        settings,
        range_m,
        pattern_gain,
        gproc,
        radar_agl_m,
        surface_class,
        cell_size_m,
        clutter_override,
    )
    thermal = hw.noise_power_w() * gproc
    noise = thermal + settings.additional_noise_power_w + clutter
    snr_db = lin_to_db(processed / max(1e-30, noise))
    snr_hit = snr_db >= settings.detection_snr_db
    cfar_hit = False
    cfar_thr = 0.0
    if snr_hit:
        cfar_hit, cfar_thr = apply_cfar_single_target(
            settings, hw, processed, range_m, rng
        )
    return DetectResult(
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
        multipath,
        rcs_m2,
        beam_name,
        azimuth_offset_deg,
        elevation_deg,
        fd,
        cfar_thr,
    )


def mean_rcs_uh1(
    yaw_deg: float = 0.0,
    los_az_deg: float = 0.0,
    los_el_deg: float = 0.0,
    pitch_deg: float = 0.0,
) -> float:
    """Aspect-only UH-1 RCS (no Swerling), baked mean 16.4414 m2."""
    return evaluate_instant_rcs_uh1(
        yaw_deg, pitch_deg, los_az_deg, los_el_deg, 0, False
    )


def instant_rcs_uh1(
    yaw_deg: float,
    los_az_deg: float,
    los_el_deg: float,
    use_swerling: bool,
    trial_index: int,
) -> float:
    return evaluate_instant_rcs_uh1(
        yaw_deg, 0.0, los_az_deg, los_el_deg, trial_index, use_swerling
    )


def scan_azimuth_offset_for_illumination(hw: Hardware, rng: random.Random) -> float:
    """One mechanical-scan paint: Enforce gates candidates to ±az_bw/2.

    RDF_RadarScanner: halfAngleRad = AzimuthBeamwidthDeg * 0.5 when
    m_EnableMechanicalScan. Outside that cone the target is not processed.
    """
    half = 0.5 * hw.az_beamwidth_deg
    return rng.uniform(-half, half)


def random_update_sees_target(hw: Hardware, rng: random.Random) -> bool:
    """Probability a random update tick paints the target ≈ az_bw / 360."""
    return rng.random() < (hw.az_beamwidth_deg / 360.0)


def run_scenario_pd(
    hw: Hardware,
    settings: Settings,
    range_m: float,
    radial_ms: float,
    yaw_deg: float,
    los_blocked: bool,
    hit_fraction: float,
    obstacle_h: float,
    obstacle_u: float,
    use_scan_illumination: bool,
    use_random_update_miss: bool,
    use_swerling: bool,
    trials: int,
    seed: int,
) -> dict:
    """use_scan_illumination: az uniform in ±bw/2 (one paint per revolution).
    use_random_update_miss: most ticks miss the beam (bw/360), else one paint.
    """
    rng = random.Random(seed)
    snr_hits = 0
    cfar_hits = 0
    snr_sum = 0.0
    for i in range(trials):
        if use_random_update_miss:
            if not random_update_sees_target(hw, rng):
                snr_sum += -300.0
                continue
            az = scan_azimuth_offset_for_illumination(hw, rng)
        elif use_scan_illumination:
            az = scan_azimuth_offset_for_illumination(hw, rng)
        else:
            az = 0.0
        los_el = math.degrees(
            math.atan2(TARGET_AGL_M - RADAR_AGL_M, max(1.0, range_m))
        )
        rcs = instant_rcs_uh1(yaw_deg, 0.0, los_el, use_swerling, i)
        res = physical_detect(
            hw,
            settings,
            range_m,
            rcs,
            radial_ms,
            az,
            TARGET_AGL_M,
            RADAR_AGL_M,
            los_blocked,
            hit_fraction,
            obstacle_h,
            obstacle_u,
            rng,
            surface_class=2,
            cell_size_m=CELL_SIZE_M,
        )
        snr_sum += res.snr_db
        if res.detected_snr:
            snr_hits += 1
        if res.detected_cfar:
            cfar_hits += 1
    return {
        "pd_snr": snr_hits / trials,
        "pd_cfar": cfar_hits / trials,
        "mean_snr_db": snr_sum / trials,
        "trials": trials,
    }


def run_scenario_pd_dem(
    hw: Hardware,
    settings: Settings,
    dem: EdenDemCrop,
    radar_xyz: tuple[float, float, float],
    radar_agl_m: float,
    bearing_deg: float,
    range_m: float,
    radial_ms: float,
    yaw_deg: float,
    use_scan_illumination: bool,
    use_random_update_miss: bool,
    use_swerling: bool,
    force_los_clear: bool,
    trials: int,
    seed: int,
) -> dict:
    """Place UH-1 on Eden DEM along bearing; LOS/knife/surface from crop."""
    rng = random.Random(seed)
    placed = pick_target_on_bearing(
        dem, radar_xyz, range_m, bearing_deg, TARGET_AGL_M
    )
    if placed is None:
        return {
            "pd_snr": 0.0,
            "pd_cfar": 0.0,
            "mean_snr_db": -300.0,
            "trials": 0,
            "los_clear": False,
            "surface_class": -1,
            "error": "target_out_of_dem",
        }
    tx, ty, tz, _ok, _ty0, surf = placed
    rx, ry, rz = radar_xyz
    clear, hit_u, max_h, max_u = los_probe(dem, rx, ry, rz, tx, ty, tz)
    if force_los_clear:
        clear = True
        hit_u = 1.0
        max_h = 0.0
        max_u = 0.5
    los_blocked = not clear
    slant = math.sqrt((tx - rx) ** 2 + (ty - ry) ** 2 + (tz - rz) ** 2)
    target_agl = ty - _ty0
    snr_hits = 0
    cfar_hits = 0
    snr_sum = 0.0
    for i in range(trials):
        if use_random_update_miss:
            if not random_update_sees_target(hw, rng):
                snr_sum += -300.0
                continue
            az = scan_azimuth_offset_for_illumination(hw, rng)
        elif use_scan_illumination:
            az = scan_azimuth_offset_for_illumination(hw, rng)
        else:
            az = 0.0
        dx = tx - rx
        dy = ty - ry
        dz = tz - rz
        horiz = math.sqrt(dx * dx + dz * dz)
        los_el = math.degrees(math.atan2(dy, max(0.001, horiz)))
        # LOS azimuth world ≈ bearing when target placed on bearing.
        rcs = instant_rcs_uh1(yaw_deg, bearing_deg, los_el, use_swerling, i)
        res = physical_detect(
            hw,
            settings,
            slant,
            rcs,
            radial_ms,
            az,
            target_agl,
            radar_agl_m,
            los_blocked,
            hit_u,
            max_h,
            max_u,
            rng,
            surface_class=surf,
            cell_size_m=dem.get_cell_size_m(),
        )
        snr_sum += res.snr_db
        if res.detected_snr:
            snr_hits += 1
        if res.detected_cfar:
            cfar_hits += 1
    return {
        "pd_snr": snr_hits / trials,
        "pd_cfar": cfar_hits / trials,
        "mean_snr_db": snr_sum / trials,
        "trials": trials,
        "los_clear": clear,
        "los_blocked": los_blocked,
        "hit_fraction": round(hit_u, 4),
        "max_h_obs_m": round(max_h, 2),
        "surface_class": int(surf),
        "surface_name": SURF_NAMES[int(surf)] if 0 <= int(surf) < len(SURF_NAMES) else "unknown",
        "slant_m": round(slant, 1),
        "target_xyz": [round(tx, 1), round(ty, 1), round(tz, 1)],
    }


def faction_report_dem(
    hw: Hardware,
    settings: Settings,
    dem: EdenDemCrop,
    radar_xyz: tuple[float, float, float],
    radar_agl_m: float,
    bearing_deg: float,
) -> dict:
    base = faction_report(hw, settings)
    dem_scenarios = {}
    defs = (
        ("DEM_paint_radial50", 50.0, 0.0, True, False, True, False, bearing_deg),
        ("DEM_paint_force_clear", 50.0, 0.0, True, False, True, True, bearing_deg),
        ("DEM_tick", 50.0, 0.0, False, True, True, False, bearing_deg),
        ("DEM_tangential", 0.0, 0.0, True, False, True, False, bearing_deg),
        ("DEM_broadside", 50.0, 90.0, True, False, True, False, bearing_deg),
        ("DEM_blocked_brg310", 50.0, 0.0, True, False, True, False, 310.0),
    )
    for name, radial, yaw, illum, tick, sw, force_clear, brg in defs:
        dem_scenarios[name] = run_scenario_pd_dem(
            hw,
            settings,
            dem,
            radar_xyz,
            radar_agl_m,
            brg,
            4000.0,
            radial,
            yaw,
            illum,
            tick,
            sw,
            force_clear,
            MC_TRIALS,
            RNG_SEED,
        )

    pd_vs_range = []
    for r in RANGES_M:
        if r > settings.range_m + 1.0:
            continue
        row = run_scenario_pd_dem(
            hw,
            settings,
            dem,
            radar_xyz,
            radar_agl_m,
            bearing_deg,
            r,
            50.0,
            0.0,
            True,
            False,
            True,
            False,
            MC_TRIALS,
            RNG_SEED + int(r),
        )
        pd_vs_range.append(
            {
                "range_km": round(r / 1000.0, 3),
                "pd_snr": round(row["pd_snr"], 3),
                "pd_cfar": round(row["pd_cfar"], 3),
                "mean_snr_db": round(row["mean_snr_db"], 2),
                "los_clear": row.get("los_clear"),
                "surface": row.get("surface_name"),
                "slant_m": row.get("slant_m"),
            }
        )

    base["dem_eden"] = {
        "bearing_deg": bearing_deg,
        "radar_xyz": list(radar_xyz),
        "radar_agl_m": round(radar_agl_m, 2),
        "scenarios_4km": dem_scenarios,
        "pd_vs_range_paint_swerling_cfar": pd_vs_range,
    }
    return base


def build_snr_curve(
    hw: Hardware,
    settings: Settings,
    radial_ms: float,
    yaw_deg: float,
    az_offset_deg: float,
    los_blocked: bool,
    seed: int,
) -> list[dict]:
    rng = random.Random(seed)
    rows = []
    for r in RANGES_M:
        rcs = mean_rcs_uh1(yaw_deg, 0.0, 0.0)
        res = physical_detect(
            hw,
            settings,
            r,
            rcs,
            radial_ms,
            az_offset_deg,
            TARGET_AGL_M,
            RADAR_AGL_M,
            los_blocked,
            0.5,
            20.0,
            0.5,
            rng,
        )
        rows.append(
            {
                "range_km": round(r / 1000.0, 3),
                "snr_db": round(res.snr_db, 2),
                "pattern_gain": round(res.pattern_gain, 4),
                "mti_gain": round(res.mti_gain, 4),
                "multipath": round(res.multipath_factor, 4),
                "rcs_m2": round(res.rcs_m2, 2),
                "beam": res.beam_name,
                "detected_snr": res.detected_snr,
                "detected_cfar": res.detected_cfar,
            }
        )
    return rows


def try_write_png(report: dict, out_dir: Path) -> list[str]:
    paths: list[str] = []
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception:
        return paths

    for faction, block in report["factions"].items():
        if "US" in faction:
            hw, settings = make_us()
        else:
            hw, settings = make_ussr()
        settings.dem_clutter_scale = block["config"]["dem_clutter_scale"]
        gate = settings.detection_snr_db

        fig, axes = plt.subplots(2, 2, figsize=(12, 8.5))

        # 1) SNR vs range: boresight radial vs tangential vs broadside
        ax = axes[0][0]
        for label, radial, yaw in (
            ("radial 50 nose", 50.0, 0.0),
            ("radial 0 (MTI null)", 0.0, 0.0),
            ("radial 50 broadside", 50.0, 90.0),
        ):
            xs = []
            ys = []
            for row in build_snr_curve(hw, settings, radial, yaw, 0.0, False, 1):
                xs.append(row["range_km"])
                ys.append(row["snr_db"])
            ax.plot(xs, ys, label=label, linewidth=1.4)
        ax.axhline(gate, color="0.35", linestyle="--", linewidth=1.0, label="SNR gate")
        ax.set_xlabel("Range (km)")
        ax.set_ylabel("SNR (dB)")
        ax.set_title("%s PhysicalDetect SNR (boresight)" % faction)
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=7)

        # 2) Pd vs range: illumination paint vs random update tick
        ax = axes[0][1]
        curves = (
            ("paint+Swerling+CFAR", 50.0, 0.0, True, False, True),
            ("tick miss (bw/360)", 50.0, 0.0, False, True, True),
            ("tangential paint", 0.0, 0.0, True, False, True),
        )
        for label, radial, yaw, illuminate, tick_miss, sw in curves:
            xs = []
            ys = []
            for r in (500.0, 1500.0, 3000.0, 4500.0, 6000.0, 7500.0):
                if r > settings.range_m + 1.0:
                    continue
                pd = run_scenario_pd(
                    hw,
                    settings,
                    r,
                    radial,
                    yaw,
                    False,
                    0.5,
                    0.0,
                    0.5,
                    illuminate,
                    tick_miss,
                    sw,
                    MC_TRIALS,
                    RNG_SEED + int(r),
                )
                xs.append(r / 1000.0)
                ys.append(pd["pd_cfar"] * 100.0)
            ax.plot(xs, ys, marker="o", label=label, linewidth=1.4)
        ax.set_xlabel("Range (km)")
        ax.set_ylabel("Pd CFAR (%)")
        ax.set_ylim(-5, 105)
        ax.set_title("%s Monte-Carlo Pd (game chain)" % faction)
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=7)

        # 3) Pattern gain vs az offset at 4 km
        ax = axes[1][0]
        elev = math.degrees(math.atan2(TARGET_AGL_M - RADAR_AGL_M, 4000.0))
        xs = list(range(-30, 31))
        ys = []
        for az in xs:
            g, _ = strongest_beam_gain(hw, float(az), elev)
            ys.append(lin_to_db(max(g, 1e-12)))
        ax.plot(xs, ys, color="C0", linewidth=1.5)
        ax.axvline(-hw.az_beamwidth_deg * 0.5, color="0.5", linestyle=":")
        ax.axvline(hw.az_beamwidth_deg * 0.5, color="0.5", linestyle=":")
        ax.set_xlabel("Azimuth offset (deg)")
        ax.set_ylabel("Two-way pattern (dB)")
        ax.set_title("Beam pattern @ 4 km elev=%.1f deg" % elev)
        ax.grid(True, alpha=0.3)

        # 4) Scenario bar at 4 km (prefer Eden DEM rows when present)
        ax = axes[1][1]
        if "dem_eden" in block:
            scenarios = block["dem_eden"]["scenarios_4km"]
            title = "Eden DEM scenarios @ 4 km"
        else:
            scenarios = block["scenarios_4km"]
            title = "Hard scenarios (config scale)"
        names = list(scenarios.keys())
        pds = [scenarios[n]["pd_cfar"] * 100.0 for n in names]
        ax.barh(names, pds, color="steelblue")
        ax.set_xlim(0, 105)
        ax.set_xlabel("Pd CFAR (%) @ 4 km")
        ax.set_title(title)
        ax.grid(True, axis="x", alpha=0.3)

        fig.suptitle(
            "GBRS RDF 1:1 + DEM  scale=%.2f gate=%.1f dB"
            % (settings.dem_clutter_scale, gate),
            fontsize=11,
        )
        fig.tight_layout()
        path = out_dir / ("clutter_mti_%s.png" % faction.lower())
        fig.savefig(path, dpi=140)
        plt.close(fig)
        paths.append(str(path))
    return paths


def faction_report(hw: Hardware, settings: Settings) -> dict:
    base_rcs = UH1_MEAN_RCS_M2
    nose = mean_rcs_uh1(0.0, 0.0, 0.0)
    broad = mean_rcs_uh1(90.0, 0.0, 0.0)
    scenarios_4km = {}
    # name, radial, yaw, illuminate, tick_miss, swerling, los_blocked, hit_f, h_obs
    scenario_defs = (
        ("A_boresight_snr_only", 50.0, 0.0, False, False, False, False, 0.5, 0.0),
        ("B_boresight_cfar_swerling", 50.0, 0.0, False, False, True, False, 0.5, 0.0),
        ("C_paint_in_beam", 50.0, 0.0, True, False, True, False, 0.5, 0.0),
        ("D_random_update_tick", 50.0, 0.0, False, True, True, False, 0.5, 0.0),
        ("E_tangential_v0", 0.0, 0.0, False, False, True, False, 0.5, 0.0),
        ("F_broadside_aspect", 50.0, 90.0, False, False, True, False, 0.5, 0.0),
        ("G_nlos_bounce", 50.0, 0.0, False, False, True, True, 0.4, 0.0),
        ("H_nlos_knife20m", 50.0, 0.0, False, False, True, True, 0.5, 20.0),
    )
    for (
        name,
        radial,
        yaw,
        illuminate,
        tick_miss,
        sw,
        los_blocked,
        hit_f,
        h_obs,
    ) in scenario_defs:
        scenarios_4km[name] = run_scenario_pd(
            hw,
            settings,
            4000.0,
            radial,
            yaw,
            los_blocked,
            hit_f,
            h_obs,
            0.5,
            illuminate,
            tick_miss,
            sw,
            MC_TRIALS,
            RNG_SEED,
        )

    # Pd vs range: one paint per revolution (in-beam) + Swerling + CFAR
    pd_vs_range = []
    for r in RANGES_M:
        if r > settings.range_m + 1.0:
            continue
        pd = run_scenario_pd(
            hw,
            settings,
            r,
            50.0,
            0.0,
            False,
            0.5,
            0.0,
            0.5,
            True,
            False,
            True,
            MC_TRIALS,
            RNG_SEED + int(r),
        )
        pd_vs_range.append(
            {
                "range_km": round(r / 1000.0, 3),
                "pd_snr": round(pd["pd_snr"], 3),
                "pd_cfar": round(pd["pd_cfar"], 3),
                "mean_snr_db": round(pd["mean_snr_db"], 2),
            }
        )

    snr_boresight = build_snr_curve(hw, settings, 50.0, 0.0, 0.0, False, 7)
    snr_tangential = build_snr_curve(hw, settings, 0.0, 0.0, 0.0, False, 8)

    mti_table = []
    for v in RADIAL_SPEEDS_MS:
        fd = doppler_hz(v, hw.wavelength_m())
        g = mti_two_pulse_gain(fd, hw.prf_hz)
        mti_table.append(
            {
                "radial_ms": v,
                "doppler_hz": round(fd, 1),
                "mti_gain": round(g, 4),
                "mti_gain_db": round(lin_to_db(max(g, 1e-12)), 2),
            }
        )

    beam_duty = hw.az_beamwidth_deg / 360.0
    return {
        "config": {
            "dem_clutter_scale": settings.dem_clutter_scale,
            "detection_snr_db": settings.detection_snr_db,
            "range_m": settings.range_m,
            "update_interval_s": settings.update_interval_s,
            "scan_rpm": hw.scan_rpm,
            "beam_duty_cycle": round(beam_duty, 5),
            "enable_cfar": settings.enable_cfar_gate,
            "enable_atm": settings.enable_atmospheric_loss,
        },
        "uh1_signature_key": UH1_SIG_KEY,
        "uh1_baked_mean_rcs_m2": UH1_MEAN_RCS_M2,
        "uh1_aspect_rcs_nose_m2": round(nose, 2),
        "uh1_aspect_rcs_broadside_m2": round(broad, 2),
        "uh1_size_m": [UH1_SIZE_X, UH1_SIZE_Y, UH1_SIZE_Z],
        "uh1_swerling": UH1_SWERLING,
        "mti_vs_speed": mti_table,
        "scenarios_4km": scenarios_4km,
        "pd_vs_range_scan_swerling_cfar": pd_vs_range,
        "snr_vs_range_boresight_radial50": snr_boresight,
        "snr_vs_range_boresight_radial0": snr_tangential,
        "hardware": {
            "name": hw.name,
            "frequency_hz": hw.frequency_hz,
            "az_beamwidth_deg": hw.az_beamwidth_deg,
            "prf_hz": hw.prf_hz,
            "processing_gain": round(hw.processing_gain(), 3),
            "mti_clutter_floor": hw.mti_clutter_floor,
        },
    }


def main() -> int:
    out_dir = Path(__file__).resolve().parent / "out"
    out_dir.mkdir(parents=True, exist_ok=True)

    radar_xyz = DEFAULT_RADAR_XYZ
    dem = None
    bearing = 0.0
    radar_agl = RADAR_AGL_M
    dem_meta = {"loaded": False}
    if load_eden_crop is not None:
        cache = out_dir / "eden_crop_radar.npz"
        print("Loading Eden DEM crop from profile RDF DemData...")
        dem = load_eden_crop(
            radar_xyz=radar_xyz,
            radius_m=9000.0,
            cache_path=cache,
        )
        ok, ty, surf = dem.sample(radar_xyz[0], radar_xyz[2])
        if ok:
            radar_agl = max(0.5, radar_xyz[1] - ty)
        bearing = find_land_bearing(dem, radar_xyz, 4000.0, TARGET_AGL_M)
        dem_meta = {
            "loaded": True,
            "source": dem.source,
            "shape": [dem.height, dem.width],
            "cell_m": dem.cell_m,
            "radar_terrain_y": round(ty, 2) if ok else None,
            "radar_surface": int(surf) if ok else None,
            "radar_agl_m": round(radar_agl, 2),
            "land_bearing_deg": bearing,
            "cache": str(cache),
        }
        print(
            "DEM ok shape=%sx%s cell=%.1f radarAGL=%.1f bearing=%.0f"
            % (dem.height, dem.width, dem.cell_m, radar_agl, bearing)
        )
    else:
        print("WARNING: gbrs_eden_dem not available; flat-earth fallback")

    report: dict = {
        "profile": "rdf_physical_detect_1to1_eden_dem",
        "notes": (
            "Ports RDF_RadarPhysicalDetect + CFAR + scan gate + Swerling. "
            "When profile DemData/GM_Eden is present, clutter/LOS/knife use "
            "real terrain_y + surface_class around the spawned radar site."
        ),
        "target_agl_m": TARGET_AGL_M,
        "radar_agl_m": radar_agl,
        "mc_trials": MC_TRIALS,
        "dem": dem_meta,
        "uh1": {
            "key": UH1_SIG_KEY,
            "mean_rcs_m2": UH1_MEAN_RCS_M2,
            "size_xyz_m": [UH1_SIZE_X, UH1_SIZE_Y, UH1_SIZE_Z],
            "swerling": UH1_SWERLING,
            "aspect_nose_m2": round(mean_rcs_uh1(0.0, 0.0, 0.0), 3),
            "aspect_broadside_m2": round(mean_rcs_uh1(90.0, 0.0, 0.0), 3),
        },
        "factions": {},
    }

    print("=== GBRS / RDF PhysicalDetect 1:1 + Eden DEM ===")
    print(
        "UH-1H signature meanRCS=%.4f m2 size=%.2fx%.2fx%.2f Swerling=%d"
        % (
            UH1_MEAN_RCS_M2,
            UH1_SIZE_X,
            UH1_SIZE_Y,
            UH1_SIZE_Z,
            UH1_SWERLING,
        )
    )
    print(
        "aspect RCS nose=%.2f broadside=%.2f m2 (EvaluateInstantRcsM2 path)"
        % (mean_rcs_uh1(0.0, 0.0, 0.0), mean_rcs_uh1(90.0, 0.0, 0.0))
    )
    print()

    for factory in (make_us, make_ussr):
        hw, settings = factory()
        if dem is not None:
            block = faction_report_dem(
                hw, settings, dem, radar_xyz, radar_agl, bearing
            )
        else:
            block = faction_report(hw, settings)
        report["factions"][hw.name] = block
        print(
            "--- %s scale=%.2f gate=%.1f ---"
            % (hw.name, settings.dem_clutter_scale, settings.detection_snr_db)
        )
        print(
            "Beam duty ~ %.2f%% of circle"
            % (block["config"]["beam_duty_cycle"] * 100.0)
        )
        if "dem_eden" in block:
            print("Eden DEM scenarios @ ~4 km ground range (Pd CFAR):")
            for name, row in block["dem_eden"]["scenarios_4km"].items():
                print(
                    "  %-28s  Pd_cfar=%5.1f%%  meanSNR=%6.1f  los=%s surf=%s"
                    % (
                        name,
                        row["pd_cfar"] * 100.0,
                        row["mean_snr_db"],
                        "clear" if row.get("los_clear") else "BLOCK",
                        row.get("surface_name"),
                    )
                )
        else:
            print("Scenarios @ 4 km (Pd CFAR):")
            for name, row in block["scenarios_4km"].items():
                print(
                    "  %-28s  Pd_snr=%5.1f%%  Pd_cfar=%5.1f%%  meanSNR=%6.1f dB"
                    % (
                        name,
                        row["pd_snr"] * 100.0,
                        row["pd_cfar"] * 100.0,
                        row["mean_snr_db"],
                    )
                )
        print()

    pngs = try_write_png(report, out_dir)
    report["png_files"] = pngs
    out_path = out_dir / "clutter_mti_report.json"
    out_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("Wrote %s" % out_path)
    for p in pngs:
        print("Wrote %s" % p)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
