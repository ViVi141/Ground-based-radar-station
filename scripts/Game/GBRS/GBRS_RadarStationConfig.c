// Faction station presets: thin wrappers around RDF product mode
// RDF_RADAR_MODE_PULSE_DOPPLER / CreatePulseDopplerSettings.
//
// Do not invent a second detection channel. Station code only sets geometry,
// range, and include filters. Channel physics (MTD, PRF stagger, coast,
// HwCalib) stays with RDF.
//
// DEM clutter stays off for mechanical-scan SHORAD / EW — same as RDF
// SamEngage / ManualDemo PD (wide beams bury skin returns in DEM cells).
class GBRS_RadarStationConfig
{
    // Re-stamp PD MTI after swapping Hardware (e.g. P-18 RF front-end).
    // Mirrors RDF_RadarSensor.CreatePulseDopplerSettings hardware block.
    static void ApplyPulseDopplerHardware(RDF_RadarHardware hw)
    {
        if (!hw)
            return;

        hw.m_EnableMti = true;
        hw.m_MtiMode = ERDF_MtiMode.RDF_MTI_MTD_BANK;
        hw.m_DopplerBinCount = 16;
        hw.m_MtiClutterFloor = 0.0001;
        hw.m_MtdClutterLeakage = 0.000001;
        hw.m_ClutterSigmaVrMs = 0.5;
        hw.m_DeriveMtdLeakageFromSigmaVr = true;
        hw.m_LoadHwCalibFromProfile = true;
        hw.m_PrfStaggerRatio = 1.2;
        hw.m_MtiStaggerDeblind = true;
        hw.m_CoherentIntegration = true;
        hw.Validate();
    }

    // US RPL-5: SHORAD pulse-Doppler search (~7 km).
    static RDF_RadarSettings CreateUsSearch()
    {
        RDF_RadarSettings settings = RDF_RadarSensor.CreatePulseDopplerSettings(64);
        settings.m_Range = 7000.0;
        settings.m_UpdateInterval = 0.08;
        settings.m_SectorHalfAngleDeg = 180.0;
        settings.m_EnableMechanicalScan = true;
        settings.m_UseBoundsCenter = false;
        settings.m_UseLocalOffset = false;
        settings.m_OriginOffset = "0 0 0";
        settings.m_MaxLosTracesPerScan = 128;
        settings.m_FreshUpdateBudgetMin = 64;
        settings.m_FreshUpdateBudgetMax = 128;
        settings.m_ScattererDiscoveryIntervalS = 0.25;
        settings.m_ScattererDiscoveryRangeScale = 1.25;
        settings.m_ScattererClassifyPerTick = 96;
        settings.m_ScattererRefreshPerTick = 128;
        settings.m_ScattererMaxEntries = 512;
        settings.m_IncludeVehicles = true;
        settings.m_IncludeProjectiles = false;
        settings.m_IncludeRadarEmitters = true;
        settings.m_MinDistance = 40.0;
        settings.m_EnablePhysicalDetection = true;
        settings.m_DetectionSnrDb = 8.0;
        settings.m_KeepUndetected = false;
        settings.m_KeepEntityTruth = false;
        // RDF SamEngage / ManualDemo PD: DEM clutter off for wide-beam PPI.
        settings.m_EnableDemClutter = false;

        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_ScanRpm = 10.0;
            settings.m_Hardware.ClearElevationBeams();
            settings.m_Hardware.AddElevationBeam("low", 2.0, 16.0, 0.0);
            settings.m_Hardware.AddElevationBeam("mid", 18.0, 24.0, 0.0);
            settings.m_Hardware.AddElevationBeam("high", 40.0, 30.0, -1.0);
            settings.m_Hardware.Validate();
        }
        settings.Validate();
        return settings;
    }

    // USSR TPN-19 / P-18-like: VHF early-warning pulse-Doppler (~10 km).
    static RDF_RadarSettings CreateUssrSearch()
    {
        RDF_RadarSettings settings = RDF_RadarSensor.CreatePulseDopplerSettings(96);
        settings.m_Range = 10000.0;
        settings.m_UpdateInterval = 0.12;
        settings.m_SectorHalfAngleDeg = 180.0;
        settings.m_EnableMechanicalScan = true;
        settings.m_UseBoundsCenter = false;
        settings.m_UseLocalOffset = false;
        settings.m_OriginOffset = "0 0 0";
        settings.m_MaxLosTracesPerScan = 96;
        settings.m_FreshUpdateBudgetMin = 48;
        settings.m_FreshUpdateBudgetMax = 96;
        settings.m_ScattererDiscoveryIntervalS = 0.25;
        settings.m_ScattererDiscoveryRangeScale = 1.25;
        settings.m_ScattererClassifyPerTick = 96;
        settings.m_ScattererRefreshPerTick = 128;
        settings.m_ScattererMaxEntries = 512;
        settings.m_IncludeVehicles = true;
        settings.m_IncludeProjectiles = false;
        settings.m_IncludeRadarEmitters = true;
        settings.m_MinDistance = 40.0;
        settings.m_EnablePhysicalDetection = true;
        settings.m_DetectionSnrDb = 6.0;
        settings.m_KeepUndetected = false;
        settings.m_KeepEntityTruth = false;
        settings.m_EnableDemClutter = false;

        // P-18 RF front-end, then re-apply stock PD MTI (CreateP18Like is TwoPulse).
        RDF_RadarHardware hw = RDF_RadarHardware.CreateP18Like();
        hw.m_ScanRpm = 6.0;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("low", 2.0, 16.0, 0.0);
        hw.AddElevationBeam("mid", 18.0, 24.0, 0.0);
        hw.AddElevationBeam("high", 42.0, 30.0, -1.0);
        ApplyPulseDopplerHardware(hw);
        // Do not load SHORAD profile HwCalib over VHF RF; keep P-18 bin-0 floor.
        hw.m_LoadHwCalibFromProfile = false;
        hw.m_MtiClutterFloor = 0.01;
        hw.Validate();
        settings.m_Hardware = hw;

        settings.Validate();
        return settings;
    }
}
