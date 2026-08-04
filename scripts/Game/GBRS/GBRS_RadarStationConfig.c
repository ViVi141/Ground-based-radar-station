// Faction station presets: thin wrappers around RDF product modes
// RDF_RADAR_MODE_PULSE_DOPPLER (air search / lock) and RDF_RADAR_MODE_WLR.
//
// Do not invent a second detection channel. Station code only sets geometry,
// range, and include filters. Channel physics (MTD, PRF stagger, coast,
// HwCalib) stays with RDF.
//
// Balance intent:
//   US  = precise SHORAD (7 km PD / 8 km WLR, 10 RPM search, 2.5 deg beam).
//   USSR = early-warning (10 km PD/WLR, 6 RPM search, 6 deg beam, VHF DEM 0.25).
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

    // Enables RDF channel and environment effects appropriate for air search.
    static void ApplyFullFidelity(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        settings.ApplyRealisticChannel();
        settings.m_EnableDemClutter = true;
        settings.m_EnableDemSpanOcclusion = true;
        settings.m_EnableCoarseRd = true;
    }

    // Operator console readout: keep class tags on the PPI/list and soften the
    // stock realistic-channel measurement noise (3.5x + 5 m bias is too coarse
    // for a manned workstation). Channel physics stay on; only identity strip
    // and noise scale are tuned for playable precision.
    static void ApplyWorkstationReadout(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        settings.m_KeepEntityTruth = true;
        settings.m_MeasNoiseScale = 1.25;
        settings.m_MeasRangeBiasM = 1.0;
        settings.m_MeasAzimuthBiasDeg = 0.05;
        settings.m_MeasElevationBiasDeg = 0.05;
    }

    // US RPL-5: SHORAD pulse-Doppler search (~7 km).
    static RDF_RadarSettings CreateUsSearch()
    {
        RDF_RadarSettings settings = RDF_RadarSensor.CreatePulseDopplerSettings(64);
        settings.m_Range = 7000.0;
        // At 10 RPM the stock 2.5-degree azimuth beam moves 4.8 degrees in
        // 80 ms, leaving permanent scan gaps. 20 ms dwells move 1.2 degrees.
        settings.m_UpdateInterval = 0.02;
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

        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_ScanRpm = 10.0;
            settings.m_Hardware.ClearElevationBeams();
            settings.m_Hardware.AddElevationBeam("low", 2.0, 16.0, 0.0);
            settings.m_Hardware.AddElevationBeam("mid", 18.0, 24.0, 0.0);
            settings.m_Hardware.AddElevationBeam("high", 40.0, 30.0, -1.0);
            settings.m_Hardware.Validate();
        }
        ApplyFullFidelity(settings);
        ApplyWorkstationReadout(settings);
        settings.Validate();
        return settings;
    }

    // USSR TPN-19 / P-18-like: VHF early-warning pulse-Doppler (~10 km).
    static RDF_RadarSettings CreateUssrSearch()
    {
        RDF_RadarSettings settings = RDF_RadarSensor.CreatePulseDopplerSettings(96);
        settings.m_Range = 10000.0;
        // At 6 RPM the P-18-like 6-degree azimuth beam moves 1.44 degrees in
        // 40 ms, so dwells stay continuous across the beam.
        settings.m_UpdateInterval = 0.04;
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
        // Wider EW beam + VHF clutter: slightly softer gate than US SHORAD.
        settings.m_DetectionSnrDb = 5.0;
        settings.m_KeepUndetected = false;

        // P-18 RF front-end, then re-apply stock PD MTI (CreateP18Like is TwoPulse).
        RDF_RadarHardware hw = RDF_RadarHardware.CreateP18Like();
        hw.m_ScanRpm = 6.0;
        // Mild early-warning RF uplift; Pd remains clutter-limited on Eden DEM.
        hw.m_PeakPowerW = 350000.0;
        hw.m_AntennaGainDbi = 20.0;
        hw.m_PulsesIntegrated = 12;
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

        ApplyFullFidelity(settings);
        // VHF surface returns are weaker than X-band SHORAD clutter cells.
        settings.m_DemClutterScale = 0.25;
        ApplyWorkstationReadout(settings);
        settings.Validate();
        return settings;
    }

    // Shared WLR geometry: rotating projectile search with mortar elevation beams.
    // Do not ApplyFullFidelity — DEM clutter would bury shell returns.
    static void ApplyWlrProductFlags(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        settings.m_SectorHalfAngleDeg = 180.0;
        settings.m_UpdateInterval = 0.15;
        settings.m_IncludeVehicles = false;
        settings.m_IncludeRadarEmitters = false;
        settings.m_IncludeProjectiles = true;
        // Keep mechanical scan on so the station antenna / PPI sweep continue
        // after switching from PD SEARCH (stock RDF WLR is stare / ScanRpm=0).
        settings.m_EnableMechanicalScan = true;
        settings.m_EnableBallisticPrediction = true;
        settings.m_EnableWeaponLocate = true;
        settings.m_EnableDemGroundForWlr = true;
        settings.m_WeaponLocateMinHits = 5;
        settings.m_WeaponLocateMinSpanS = 1.0;
        settings.m_TrackConfirmHits = 2;
        settings.m_UseBoundsCenter = false;
        settings.m_UseLocalOffset = false;
        settings.m_OriginOffset = "0 0 0";
        settings.m_MinDistance = 40.0;
        settings.m_EnablePhysicalDetection = true;
        settings.m_KeepUndetected = false;
        settings.m_EnableDemClutter = false;
        settings.m_EnableDemSpanOcclusion = false;
        settings.m_EnableCoarseRd = false;

        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_EnableMti = false;
            settings.m_Hardware.ClearElevationBeams();
            settings.m_Hardware.AddElevationBeam("mortar_low", 15.0, 28.0, 0.0);
            settings.m_Hardware.AddElevationBeam("mortar_mid", 35.0, 30.0, 0.0);
            settings.m_Hardware.AddElevationBeam("mortar_high", 55.0, 28.0, -0.5);
            settings.m_Hardware.Validate();
        }
    }

    // US counter-battery WLR (~8 km rotating search).
    static RDF_RadarSettings CreateUsWlr()
    {
        RDF_RadarSettings settings = RDF_RadarSensor.CreateWlrSettings(128);
        settings.m_Range = 8000.0;
        settings.m_MaxLosTracesPerScan = 96;
        settings.m_FreshUpdateBudgetMin = 48;
        settings.m_FreshUpdateBudgetMax = 96;
        settings.m_ScattererDiscoveryIntervalS = 0.25;
        settings.m_ScattererDiscoveryRangeScale = 1.25;
        settings.m_ScattererClassifyPerTick = 128;
        settings.m_ScattererRefreshPerTick = 256;
        settings.m_ScattererMaxEntries = 1024;
        settings.m_DetectionSnrDb = 6.0;
        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_AzimuthBeamwidthDeg = 25.0;
            settings.m_Hardware.m_ScanRpm = 10.0;
        }
        ApplyWlrProductFlags(settings);
        ApplyWorkstationReadout(settings);
        settings.Validate();
        return settings;
    }

    // USSR counter-battery WLR (~10 km rotating search, wider beam).
    static RDF_RadarSettings CreateUssrWlr()
    {
        RDF_RadarSettings settings = RDF_RadarSensor.CreateWlrSettings(128);
        settings.m_Range = 10000.0;
        settings.m_MaxLosTracesPerScan = 96;
        settings.m_FreshUpdateBudgetMin = 48;
        settings.m_FreshUpdateBudgetMax = 96;
        settings.m_ScattererDiscoveryIntervalS = 0.25;
        settings.m_ScattererDiscoveryRangeScale = 1.25;
        settings.m_ScattererClassifyPerTick = 128;
        settings.m_ScattererRefreshPerTick = 256;
        settings.m_ScattererMaxEntries = 1024;
        settings.m_DetectionSnrDb = 5.0;
        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_AzimuthBeamwidthDeg = 30.0;
            settings.m_Hardware.m_ScanRpm = 6.0;
        }
        ApplyWlrProductFlags(settings);
        ApplyWorkstationReadout(settings);
        settings.Validate();
        return settings;
    }
}
