#!/usr/bin/env python3
"""RDF 1.1.0 band-aware surface clutter tables (parity with tools/dem/rdf_radar_materials.py).

Keep in sync with RDF_RadarSurfaceTable.c / RDF_RadarHardware.GetBand().
"""

from __future__ import annotations

import math

# ERDF_DemSurfaceClass — matches RDF_DemMaterialTable.c
SURF_UNKNOWN = 0
SURF_WATER = 1
SURF_VEGETATION = 2
SURF_SOIL = 3
SURF_SAND = 4
SURF_GRAVEL = 5
SURF_ASPHALT = 6
SURF_HARD = 7
SURF_WOOD = 8
SURF_METAL = 9
SURF_SNOW_ICE = 10
SURF_FABRIC = 11
SURF_COUNT = 12

SURF_NAMES = [
    "unknown",
    "water",
    "vegetation",
    "soil",
    "sand",
    "gravel",
    "asphalt",
    "hard",
    "wood",
    "metal",
    "snow_ice",
    "fabric",
]

# σ⁰_ref [dB] at 30° grazing — RDF 1.1.0 _BAND_SIGMA0_DB
_BAND_SIGMA0_DB: dict[str, dict[str, float]] = {
    "X": {
        "unknown": -18.0,
        "water": -22.0,
        "vegetation": -14.0,
        "soil": -18.0,
        "sand": -20.0,
        "gravel": -16.0,
        "asphalt": -12.0,
        "hard": -10.0,
        "wood": -18.0,
        "metal": -5.0,
        "snow_ice": -20.0,
        "fabric": -24.0,
    },
    "C": {
        "unknown": -19.0,
        "water": -24.0,
        "vegetation": -15.0,
        "soil": -19.0,
        "sand": -21.0,
        "gravel": -17.0,
        "asphalt": -13.0,
        "hard": -11.0,
        "wood": -19.0,
        "metal": -5.5,
        "snow_ice": -21.0,
        "fabric": -25.0,
    },
    "S": {
        "unknown": -20.0,
        "water": -26.0,
        "vegetation": -16.0,
        "soil": -20.0,
        "sand": -22.0,
        "gravel": -18.0,
        "asphalt": -14.0,
        "hard": -12.0,
        "wood": -20.0,
        "metal": -6.0,
        "snow_ice": -22.0,
        "fabric": -26.0,
    },
    "L": {
        "unknown": -22.0,
        "water": -28.0,
        "vegetation": -12.0,
        "soil": -22.0,
        "sand": -24.0,
        "gravel": -20.0,
        "asphalt": -16.0,
        "hard": -14.0,
        "wood": -22.0,
        "metal": -7.0,
        "snow_ice": -18.0,
        "fabric": -28.0,
    },
    "VHF": {
        "unknown": -24.0,
        "water": -30.0,
        "vegetation": -10.0,
        "soil": -24.0,
        "sand": -26.0,
        "gravel": -22.0,
        "asphalt": -18.0,
        "hard": -16.0,
        "wood": -14.0,
        "metal": -8.0,
        "snow_ice": -20.0,
        "fabric": -28.0,
    },
}

_DEFAULT_EXPONENT: dict[str, float] = {
    "unknown": 1.0,
    "water": 1.4,
    "vegetation": 0.8,
    "soil": 1.0,
    "sand": 1.1,
    "gravel": 1.0,
    "asphalt": 1.0,
    "hard": 1.0,
    "wood": 1.0,
    "metal": 0.5,
    "snow_ice": 1.0,
    "fabric": 1.0,
}

# Base attenuation_db_per_km at X-band authorship (RDF InstallBuiltins overlay).
_SURFACE_ATTEN_DB_PER_KM: dict[int, float] = {
    SURF_UNKNOWN: 0.0,
    SURF_WATER: 0.0,
    SURF_VEGETATION: 0.5,
    SURF_SOIL: 0.0,
    SURF_SAND: 0.0,
    SURF_GRAVEL: 0.0,
    SURF_ASPHALT: 0.0,
    SURF_HARD: 0.0,
    SURF_WOOD: 0.2,
    SURF_METAL: 0.0,
    SURF_SNOW_ICE: 0.0,
    SURF_FABRIC: 0.0,
}

# clutter_scale per surface (X-band builtins)
_SURFACE_CLUTTER_SCALE: dict[int, float] = {
    SURF_UNKNOWN: 1.0,
    SURF_WATER: 1.0,
    SURF_VEGETATION: 1.0,
    SURF_SOIL: 1.0,
    SURF_SAND: 1.0,
    SURF_GRAVEL: 1.0,
    SURF_ASPHALT: 1.0,
    SURF_HARD: 1.0,
    SURF_WOOD: 1.0,
    SURF_METAL: 1.0,
    SURF_SNOW_ICE: 1.0,
    SURF_FABRIC: 1.0,
}

BAND_ATTENUATION_SCALE: dict[str, float] = {
    "VHF": 0.20,
    "L": 0.40,
    "S": 0.55,
    "C": 0.75,
    "X": 1.0,
}

_SEA_STATE_WATER_DB: dict[int, float] = {
    0: -8.0,
    1: -4.0,
    2: -1.0,
    3: 0.0,
    4: 2.0,
    5: 4.0,
    6: 6.0,
}

THETA_REF_RAD = math.radians(30.0)
MIN_GRAZING_RAD = math.radians(0.5)
MAX_SIGMA0 = 10.0


def db_to_lin(db: float) -> float:
    return 10.0 ** (db / 10.0)


def band_for_frequency(frequency_hz: float) -> str:
    """Mirror RDF_RadarHardware.GetBand / rdf_radar_channel.band_for_frequency."""
    f_ghz = frequency_hz / 1.0e9
    if f_ghz < 0.3:
        return "VHF"
    if f_ghz < 1.0:
        return "L"
    if f_ghz < 2.0:
        return "L"
    if f_ghz < 4.0:
        return "S"
    if f_ghz < 8.0:
        return "C"
    return "X"


def attenuation_scale_for_band(band: str) -> float:
    key = str(band).upper()
    if key not in BAND_ATTENUATION_SCALE:
        return 1.0
    return float(BAND_ATTENUATION_SCALE[key])


def sigma0_ref_db(surface_class: int, band: str, sea_state: int = 3) -> float:
    band_key = str(band).upper()
    if band_key not in _BAND_SIGMA0_DB:
        band_key = "X"
    table = _BAND_SIGMA0_DB[band_key]
    if surface_class < 0 or surface_class >= SURF_COUNT:
        name = "unknown"
    else:
        name = SURF_NAMES[surface_class]
    db = float(table[name])
    if surface_class == SURF_WATER:
        sea = int(sea_state)
        if sea < 0:
            sea = 0
        if sea > 6:
            sea = 6
        db = db + _SEA_STATE_WATER_DB[sea]
    return db


def gamma_k(surface_class: int) -> float:
    if surface_class < 0 or surface_class >= SURF_COUNT:
        return 1.0
    return float(_DEFAULT_EXPONENT[SURF_NAMES[surface_class]])


def surface_clutter_scale(surface_class: int) -> float:
    if surface_class < 0 or surface_class >= SURF_COUNT:
        return 1.0
    return float(_SURFACE_CLUTTER_SCALE.get(surface_class, 1.0))


def surface_atten_db_per_km(surface_class: int, band: str) -> float:
    if surface_class < 0 or surface_class >= SURF_COUNT:
        surface_class = SURF_UNKNOWN
    base = float(_SURFACE_ATTEN_DB_PER_KM.get(surface_class, 0.0))
    return base * attenuation_scale_for_band(band)


def sigma0_linear(
    surface_class: int,
    grazing_rad: float,
    band: str,
    sea_state: int = 3,
) -> float:
    theta = grazing_rad
    if theta < MIN_GRAZING_RAD:
        theta = MIN_GRAZING_RAD
    if theta > math.pi * 0.5:
        theta = math.pi * 0.5

    ref_value = db_to_lin(sigma0_ref_db(surface_class, band, sea_state))
    gamma = gamma_k(surface_class)
    clutter_scale = surface_clutter_scale(surface_class)
    ratio = math.sin(theta) / math.sin(THETA_REF_RAD)
    if ratio < 1e-6:
        ratio = 1e-6
    value = ref_value * (ratio ** gamma) * clutter_scale
    if value > MAX_SIGMA0:
        value = MAX_SIGMA0
    return value


def axes_from_yaw_pitch(yaw_deg: float, pitch_deg: float) -> tuple[
    tuple[float, float, float],
    tuple[float, float, float],
    tuple[float, float, float],
]:
    """Zero-roll body axes — RDF_RadarRcsModel / rdf_radar_channel."""
    yaw = math.radians(float(yaw_deg))
    pitch = math.radians(float(pitch_deg))
    cy = math.cos(yaw)
    sy = math.sin(yaw)
    cp = math.cos(pitch)
    sp = math.sin(pitch)
    forward = (cp * cy, sp, cp * sy)
    right = (-sy, 0.0, cy)
    up = (
        right[1] * forward[2] - right[2] * forward[1],
        right[2] * forward[0] - right[0] * forward[2],
        right[0] * forward[1] - right[1] * forward[0],
    )

    def _norm(
        vec: tuple[float, float, float],
        fallback: tuple[float, float, float],
    ) -> tuple[float, float, float]:
        length = math.sqrt(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2])
        if length < 1e-3:
            return fallback
        inv = 1.0 / length
        return (vec[0] * inv, vec[1] * inv, vec[2] * inv)

    return (
        _norm(right, (0.0, 0.0, 1.0)),
        _norm(up, (0.0, 1.0, 0.0)),
        _norm(forward, (1.0, 0.0, 0.0)),
    )


def los_unit_from_az_el(azimuth_deg: float, elevation_deg: float) -> tuple[float, float, float]:
    az = math.radians(float(azimuth_deg))
    el = math.radians(float(elevation_deg))
    ce = math.cos(el)
    return (ce * math.cos(az), math.sin(el), ce * math.sin(az))


def aspect_rcs_from_obb(
    mean_rcs_m2: float,
    size_x: float,
    size_y: float,
    size_z: float,
    axis_right: tuple[float, float, float],
    axis_up: tuple[float, float, float],
    axis_forward: tuple[float, float, float],
    los_azimuth_deg: float,
    los_elevation_deg: float,
) -> float:
    """Mirror RDF_RadarRcsModel.AspectRcsFromObb."""
    fallback = float(mean_rcs_m2)
    if fallback <= 0.0:
        fallback = 1.0
    if size_x <= 0.01 and size_y <= 0.01 and size_z <= 0.01:
        return fallback

    height = size_y
    if height < 0.1:
        height = 0.1
    length = size_z
    if length < 0.1:
        length = 0.1
    beam = size_x
    if beam < 0.1:
        beam = 0.1

    los = los_unit_from_az_el(los_azimuth_deg, los_elevation_deg)

    def _abs_dot(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
        return abs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2])

    projected = (
        _abs_dot(los, axis_forward) * beam * height
        + _abs_dot(los, axis_right) * length * height
        + _abs_dot(los, axis_up) * beam * length
    )
    estimate = projected * 0.25
    lo = fallback * 0.2
    hi = fallback * 4.0
    if estimate < lo:
        estimate = lo
    if estimate > hi:
        estimate = hi
    return estimate


def aspect_rcs_from_extents_3d(
    mean_rcs_m2: float,
    size_x: float,
    size_y: float,
    size_z: float,
    yaw_deg: float,
    pitch_deg: float,
    los_azimuth_deg: float,
    los_elevation_deg: float,
) -> float:
    """Mirror RDF_RadarRcsModel.AspectRcsFromExtents3D (zero-roll OBB path)."""
    fallback = float(mean_rcs_m2)
    if fallback <= 0.0:
        fallback = 1.0
    if size_x <= 0.01 and size_y <= 0.01 and size_z <= 0.01:
        return fallback

    axis_right, axis_up, axis_forward = axes_from_yaw_pitch(yaw_deg, pitch_deg)
    return aspect_rcs_from_obb(
        mean_rcs_m2,
        size_x,
        size_y,
        size_z,
        axis_right,
        axis_up,
        axis_forward,
        los_azimuth_deg,
        los_elevation_deg,
    )
