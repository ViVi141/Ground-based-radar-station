// Faction-specific RDF_RadarSettings for ground radar stations.
// Product mode: RDF_RADAR_MODE_PULSE_DOPPLER (MTD + rotor sidebands + PRF
// stagger + track coast). ScattererRegistry is the only candidate source.
//
// Fidelity: ApplyRealisticChannel + DEM clutter + NLOS multipath + knife-edge.
// Projectiles OFF: mounted Hydra carry ProjectileMoveComponent and RDF
// classifies them as separate PROJECTILE scatterers (not the airframe).
class GBRS_RadarStationConfig
{
    // Shared PD channel knobs (matches RDF_RadarSensor.CreatePulseDopplerSettings).
    // Call after replacing Hardware so P18 / SHORAD both keep MTD behaviour.
    static void ApplyPulseDopplerChannel(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_EnableMti = true;
            settings.m_Hardware.m_MtiMode = ERDF_MtiMode.RDF_MTI_MTD_BANK;
            settings.m_Hardware.m_DopplerBinCount = 16;
            settings.m_Hardware.m_MtiClutterFloor = 0.0001;
            settings.m_Hardware.m_MtdClutterLeakage = 0.000001;
            settings.m_Hardware.m_ClutterSigmaVrMs = 0.5;
            settings.m_Hardware.m_DeriveMtdLeakageFromSigmaVr = true;
            settings.m_Hardware.m_LoadHwCalibFromProfile = true;
            settings.m_Hardware.m_PrfStaggerRatio = 1.2;
            settings.m_Hardware.m_MtiStaggerDeblind = true;
            settings.m_Hardware.m_CoherentIntegration = true;
        }

        settings.m_EnableClutterMap = true;
        settings.m_ClutterMapAlpha = 0.15;
        settings.m_EnableCoarseRd = false;
        settings.m_TrackCoastOnMiss = true;
        settings.m_TrackCoastOnDopplerNull = true;
        settings.m_TrackCoastGateGrowPerMiss = 0.25;
        settings.m_TrackCoastMaxSec = 8.0;
        settings.m_TrackMaxMisses = 6;
        settings.m_EnableEsmReceive = true;
    }

    // US RPL-5: ~7 km SHORAD / low-air pulse-Doppler search.
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

        settings.ApplyRealisticChannel();
        settings.m_DetectionSnrDb = 8.0;
        settings.m_KeepUndetected = false;
        settings.m_KeepEntityTruth = false;
        settings.m_EnableDemClutter = true;
        // X-band + 2.5 deg + MTD: full scale=1 keeps Pd high to ~7.5 km
        // for radial movers; coast covers CPA / vr≈0.
        settings.m_DemClutterScale = 1.0;
        settings.m_EnableNlosMultipath = true;
        settings.m_EnableKnifeEdgeDiffraction = true;

        ApplyPulseDopplerChannel(settings);

        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_ScanRpm = 10.0;
            // Shorad default az (~2.5 deg); do not widen to showcase cone.
            settings.m_Hardware.ClearElevationBeams();
            settings.m_Hardware.AddElevationBeam("low", 2.0, 16.0, 0.0);
            settings.m_Hardware.AddElevationBeam("mid", 18.0, 24.0, 0.0);
            settings.m_Hardware.AddElevationBeam("high", 40.0, 30.0, -1.0);
            settings.m_Hardware.Validate();
        }
        settings.Validate();
        return settings;
    }

    // USSR TPN-19 / P-18-like: ~10 km VHF early-warning pulse-Doppler search.
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

        settings.ApplyRealisticChannel();
        settings.m_DetectionSnrDb = 6.0;
        settings.m_KeepUndetected = false;
        settings.m_KeepEntityTruth = false;
        settings.m_EnableDemClutter = true;
        // VHF + 6 deg: scale 0.25 keeps Rmax usable under DEM clutter.
        settings.m_DemClutterScale = 0.25;
        settings.m_EnableNlosMultipath = true;
        settings.m_EnableKnifeEdgeDiffraction = true;

        // Keep P-18 RF front-end, then re-apply PD / MTD knobs on top.
        RDF_RadarHardware hw = RDF_RadarHardware.CreateP18Like();
        settings.m_Hardware = hw;
        ApplyPulseDopplerChannel(settings);

        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_ScanRpm = 6.0;
            // P18-like default az (~6 deg); keep GBRS three-beam elevation stack.
            settings.m_Hardware.ClearElevationBeams();
            settings.m_Hardware.AddElevationBeam("low", 2.0, 16.0, 0.0);
            settings.m_Hardware.AddElevationBeam("mid", 18.0, 24.0, 0.0);
            settings.m_Hardware.AddElevationBeam("high", 42.0, 30.0, -1.0);
            settings.m_Hardware.Validate();
        }
        settings.Validate();
        return settings;
    }
}
