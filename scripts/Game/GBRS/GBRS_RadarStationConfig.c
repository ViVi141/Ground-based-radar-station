// Faction-specific RDF_RadarSettings for ground radar stations.
// Scan path: scatterer registry only (no per-scan UseSphereQuery).
//
// Fidelity: ApplyRealisticChannel + MTI + DEM clutter + NLOS multipath.
// Projectiles OFF: mounted Hydra carry ProjectileMoveComponent and RDF
// classifies them as separate PROJECTILE scatterers (not the airframe).
class GBRS_RadarStationConfig
{
    // US RPL-5: ~7 km SHORAD / low-air search.
    static RDF_RadarSettings CreateUsSearch()
    {
        RDF_RadarSettings settings = RDF_RadarDemoConfig.CreateDefault(64);
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
        settings.m_UseScattererRegistry = true;
        settings.m_UseSphereQuery = false;
        settings.m_ScattererDiscoveryIntervalS = 0.25;
        settings.m_ScattererDiscoveryRangeScale = 1.25;
        settings.m_ScattererClassifyPerTick = 96;
        settings.m_ScattererRefreshPerTick = 128;
        settings.m_ScattererMaxEntries = 512;
        settings.m_IncludeVehicles = true;
        settings.m_IncludeProjectiles = false;
        settings.m_IncludeRadarEmitters = true;
        settings.m_MinDistance = 40.0;

        // Physical / channel fidelity (RDF gameplay profile).
        settings.ApplyRealisticChannel();
        settings.m_DetectionSnrDb = 8.0;
        settings.m_KeepUndetected = false;
        settings.m_KeepEntityTruth = false;
        settings.m_EnableDemClutter = true;
        // X-band + 2.5 deg + MTI: full scale=1 keeps Pd~100% to 7.5 km
        // at 50 m/s radial (simulate_clutter_cover.py MTI profile).
        settings.m_DemClutterScale = 1.0;
        settings.m_EnableNlosMultipath = true;
        settings.m_EnableKnifeEdgeDiffraction = true;

        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_ScanRpm = 10.0;
            // Shorad default az (~2.5 deg); do not widen to showcase cone.
            settings.m_Hardware.m_EnableMti = true;
            settings.m_Hardware.ClearElevationBeams();
            settings.m_Hardware.AddElevationBeam("low", 2.0, 16.0, 0.0);
            settings.m_Hardware.AddElevationBeam("mid", 18.0, 24.0, 0.0);
            settings.m_Hardware.AddElevationBeam("high", 40.0, 30.0, -1.0);
            settings.m_Hardware.Validate();
        }
        settings.Validate();
        return settings;
    }

    // USSR TPN-19 / P-18-like: ~10 km early warning.
    static RDF_RadarSettings CreateUssrSearch()
    {
        RDF_RadarSettings settings = RDF_RadarDemoConfig.CreateP18Like(96);
        settings.m_Range = 10000.0;
        settings.m_UpdateInterval = 0.12;
        settings.m_EnableMechanicalScan = true;
        settings.m_UseBoundsCenter = false;
        settings.m_UseLocalOffset = false;
        settings.m_OriginOffset = "0 0 0";
        settings.m_MaxLosTracesPerScan = 96;
        settings.m_FreshUpdateBudgetMin = 48;
        settings.m_FreshUpdateBudgetMax = 96;
        settings.m_UseScattererRegistry = true;
        settings.m_UseSphereQuery = false;
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
        // VHF + 6 deg + MTI floor 0.01: full scale=1 buries past ~1.25 km
        // (simulate_clutter_cover.py MTI profile). 0.25 keeps Rmax~7.5 km.
        settings.m_DemClutterScale = 0.25;
        settings.m_EnableNlosMultipath = true;
        settings.m_EnableKnifeEdgeDiffraction = true;

        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_ScanRpm = 6.0;
            // P18-like default az (~6 deg).
            settings.m_Hardware.m_EnableMti = true;
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
