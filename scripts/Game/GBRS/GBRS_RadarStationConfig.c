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
    protected static const float MTI_DISPLAY_MIN_RADIAL_SPEED_MS = 3.0;

    // Display-side gate for PPI / detect visuals.
    // WLR is projectile-only: never paint vehicles, infantry, or anonymous clutter.
    // PD SEARCH keeps the MTI radial-speed gate so slow ground clutter stays off-scope.
    static bool ShouldDisplayPlot(RDF_RadarTarget target, RDF_RadarSettings settings)
    {
        if (!target || !target.m_Detected)
            return false;

        if (target.m_Entity && ChimeraCharacter.Cast(target.m_Entity))
            return false;

        // Counter-battery product: IncludeProjectiles on, vehicles off.
        if (settings && settings.m_IncludeProjectiles && !settings.m_IncludeVehicles)
        {
            if (target.m_Type != ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
                return false;

            return true;
        }

        if (!settings || !settings.m_Hardware || !settings.m_Hardware.m_EnableMti)
            return true;

        if (target.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return true;
        if (target.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return true;

        // A hovering helicopter remains detectable through rotor sidebands even
        // when its body radial velocity is near zero.
        if (target.m_RotorSidebandUsed)
            return true;

        float radialSpeed = target.m_RadialSpeedMs;
        if (radialSpeed < 0.0)
            radialSpeed = -radialSpeed;

        return radialSpeed >= MTI_DISPLAY_MIN_RADIAL_SPEED_MS;
    }

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

    // Enables RDF channel / environment / track features for air search.
    static void ApplyFullFidelity(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        settings.ApplyRealisticChannel();
        settings.m_EnableDemClutter = true;
        settings.m_EnableDemSpanOcclusion = true;
        settings.m_EnableCoarseRd = true;
        settings.m_RdCellsPerScan = 32;
        settings.m_RdMapAlpha = 0.2;
        settings.m_RdDecayPerScan = 0.97;
        settings.m_RdClutterBlend = 0.35;
        settings.m_EnableClutterMap = true;
        settings.m_ClutterMapAlpha = 0.15;
        settings.m_EnableNlosMultipath = true;
        settings.m_EnableKnifeEdgeDiffraction = true;
        settings.m_EnableEsmReceive = true;
        settings.m_EnableRwrReporting = true;
        settings.m_EnableMeasurementSynthesis = true;
        settings.m_EnableCfarGate = true;
        settings.m_EnableCfarThermalFill = true;
        settings.m_EnableAtmosphericLoss = true;
        settings.m_EnableWeatherDrivenRainLoss = true;
        settings.m_EnableWlrHudAlerts = true;
        settings.m_FairScanCursor = true;
        settings.m_TrackCoastOnMiss = true;
        settings.m_TrackCoastOnDopplerNull = true;
        settings.m_TrackCoastGateGrowPerMiss = 0.25;
        settings.m_TrackCoastMaxSec = 8.0;
        settings.m_TrackMaxMisses = 6;
        if (!settings.m_MeasurementModel)
            settings.m_MeasurementModel = new RDF_RadarDefaultMeasurementModel();

        ApplyEwStack(settings);
    }

    // WLR keeps a live clutter channel (DEM + clutter map + span occlusion).
    // Shells are not protected by turning clutter off — elevation beams look
    // above the ground ring, CFAR gates clutter edges, include-filters are
    // projectile-only, and ShouldDisplayPlot drops non-shell plots.
    static void ApplyWlrFidelity(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        settings.ApplyRealisticChannel();
        settings.m_EnableDemClutter = true;
        settings.m_EnableDemSpanOcclusion = true;
        settings.m_EnableCoarseRd = true;
        settings.m_RdCellsPerScan = 24;
        settings.m_RdMapAlpha = 0.15;
        settings.m_RdDecayPerScan = 0.97;
        settings.m_RdClutterBlend = 0.3;
        settings.m_EnableClutterMap = true;
        settings.m_ClutterMapAlpha = 0.12;
        settings.m_EnableNlosMultipath = true;
        settings.m_EnableKnifeEdgeDiffraction = true;
        settings.m_EnableEsmReceive = false;
        settings.m_EnableRwrReporting = true;
        settings.m_EnableMeasurementSynthesis = true;
        settings.m_EnableCfarGate = true;
        settings.m_EnableCfarThermalFill = true;
        settings.m_EnableAtmosphericLoss = true;
        settings.m_EnableWeatherDrivenRainLoss = true;
        settings.m_EnableBallisticPrediction = true;
        settings.m_EnableWeaponLocate = true;
        settings.m_EnableDemGroundForWlr = true;
        settings.m_EnableWlrHudAlerts = true;
        settings.m_FairScanCursor = true;
        settings.m_TrackCoastOnMiss = true;
        settings.m_TrackCoastOnDopplerNull = false;
        settings.m_TrackCoastMaxSec = 4.0;
        // Greater-of CFAR: better at clutter ridge / land-sea edges for WLR.
        settings.m_CfarMode = ERDF_CfarMode.RDF_CFAR_GO;
        if (!settings.m_MeasurementModel)
            settings.m_MeasurementModel = new RDF_RadarDefaultMeasurementModel();

        ApplyEwStack(settings);
    }

    // Receiver-side EW uses only live registered emitters. Static deception
    // effects must not be attached permanently: doing so creates fake plots
    // even when no jammer exists, including the observed 1–2 m/s returns.
    static void ApplyEwStack(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        if (!settings.m_EwStack)
            settings.m_EwStack = new RDF_RadarEwStack();

        settings.m_EwStack.Clear();

        GBRS_RadarEmitterNoiseBridge emitterNoise = new GBRS_RadarEmitterNoiseBridge();
        emitterNoise.m_MaxRangeM = settings.m_Range * 1.5;
        if (emitterNoise.m_MaxRangeM < 5000.0)
            emitterNoise.m_MaxRangeM = 5000.0;
        emitterNoise.m_UseSearchAvg = true;
        emitterNoise.m_SidelobeLevelDb = -40.0;
        emitterNoise.m_CouplingGain = 1.0;
        settings.m_EwStack.Add(emitterNoise);

        // Floor for receiver noise injection path (stack adds on top).
        settings.m_AdditionalNoisePowerW = 0.0;
    }

    // Operator console readout: strip entity identity from published plots and
    // keep measurement noise in the RDF realistic-channel band. SHORAD (US)
    // is slightly tighter than stock; early-warning (USSR) stays coarse.
    // Channel physics (MTI/CFAR/DEM) are untouched — only PPI identity and
    // kinematic synthesis fidelity.
    static void ApplyWorkstationReadout(RDF_RadarSettings settings, bool precisionShored)
    {
        if (!settings)
            return;

        settings.m_KeepEntityTruth = false;
        settings.m_EnableMeasurementSynthesis = true;

        if (precisionShored)
        {
            // US RPL-5: manned SHORAD — better than stock 3.5x, not truth.
            settings.m_MeasNoiseScale = 2.5;
            settings.m_MeasRangeBiasM = 3.0;
            settings.m_MeasAzimuthBiasDeg = 0.12;
            settings.m_MeasElevationBiasDeg = 0.1;
        }
        else
        {
            // USSR TPN-19 / P-18-like: VHF EW — stock realistic or slightly worse.
            settings.m_MeasNoiseScale = 3.5;
            settings.m_MeasRangeBiasM = 6.0;
            settings.m_MeasAzimuthBiasDeg = 0.3;
            settings.m_MeasElevationBiasDeg = 0.22;
        }
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
            ApplyPulseDopplerHardware(settings.m_Hardware);
            settings.m_Hardware.m_ScanRpm = 10.0;
            settings.m_Hardware.ClearElevationBeams();
            settings.m_Hardware.AddElevationBeam("low", 2.0, 16.0, 0.0);
            settings.m_Hardware.AddElevationBeam("mid", 18.0, 24.0, 0.0);
            settings.m_Hardware.AddElevationBeam("high", 40.0, 30.0, -1.0);
            settings.m_Hardware.Validate();
        }
        ApplyFullFidelity(settings);
        settings.m_CfarMode = ERDF_CfarMode.RDF_CFAR_CA;
        ApplyWorkstationReadout(settings, true);
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
        // Greater-of CFAR for clutter-edge VHF EW scenes.
        settings.m_CfarMode = ERDF_CfarMode.RDF_CFAR_GO;
        ApplyWorkstationReadout(settings, false);
        settings.Validate();
        return settings;
    }

    // Shared WLR geometry: rotating projectile search with mortar elevation beams.
    // Clutter stays in the channel; high-look beams + CFAR + include/display
    // filters keep ground returns from dominating the operator picture.
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
        // Infantry must never enter WLR discovery/display — only shells/rockets.
        settings.m_EnableMechanicalScan = true;
        settings.m_WeaponLocateMinHits = 5;
        settings.m_WeaponLocateMinSpanS = 1.0;
        settings.m_TrackConfirmHits = 2;
        settings.m_UseBoundsCenter = false;
        settings.m_UseLocalOffset = false;
        settings.m_OriginOffset = "0 0 0";
        settings.m_MinDistance = 40.0;
        settings.m_EnablePhysicalDetection = true;
        settings.m_KeepUndetected = false;

        if (settings.m_Hardware)
        {
            // No MTI: ballistic Doppler is not a slow-clutter notch problem.
            // Ground clutter is handled by elevation look-up + CFAR instead.
            settings.m_Hardware.m_EnableMti = false;
            settings.m_Hardware.ClearElevationBeams();
            // Boresights sit well above the horizon so main-beam DEM clutter
            // is mostly sidelobe / lower-skirt energy, not boresight terrain.
            settings.m_Hardware.AddElevationBeam("mortar_low", 18.0, 22.0, 0.0);
            settings.m_Hardware.AddElevationBeam("mortar_mid", 35.0, 26.0, 0.0);
            settings.m_Hardware.AddElevationBeam("mortar_high", 55.0, 26.0, -0.5);
            settings.m_Hardware.Validate();
        }

        ApplyWlrFidelity(settings);
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
        ApplyWorkstationReadout(settings, true);
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
        // Same VHF surface-scale relief as USSR search; clutter stays enabled.
        settings.m_DemClutterScale = 0.25;
        ApplyWorkstationReadout(settings, false);
        settings.Validate();
        return settings;
    }
}
