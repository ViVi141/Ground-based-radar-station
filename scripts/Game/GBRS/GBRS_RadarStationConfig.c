// Faction-specific RDF_RadarSettings for ground radar stations.
// Scan path: scatterer registry only (no per-scan UseSphereQuery).
//
// RDF mechanical gate is a 3D cone about horizontal scanForward:
//   dot(forward, toTarget) >= cos(AzimuthBeamwidthDeg / 2)
//
// Projectiles OFF: mounted Hydra carry ProjectileMoveComponent and RDF
// classifies them as separate PROJECTILE scatterers (not the airframe).
class GBRS_RadarStationConfig
{
    // US RPL-5: ~7 km SHORAD / low-air search.
    static RDF_RadarSettings CreateUsSearch()
    {
        RDF_RadarSettings settings = RDF_RadarDemoConfig.CreateDefault(64);
        settings.m_Range = 7000.0;
        // Dense enough that elevated air targets get >=2 samples per pass.
        settings.m_UpdateInterval = 0.08;
        settings.m_SectorHalfAngleDeg = 180.0;
        settings.m_EnableMechanicalScan = true;
        settings.m_UseBoundsCenter = false;
        settings.m_UseLocalOffset = false;
        settings.m_OriginOffset = "0 0 0";
        // Hard LOS: TraceMove must clear (or hit the target). Default RDF
        // EnableNlosMultipath=true still plots over hills via bounce/knife-edge.
        settings.m_EnableNlosMultipath = false;
        settings.m_EnableKnifeEdgeDiffraction = false;
        // Reuse without a fresh Trace keeps last-detected plots while the
        // aircraft ducks behind a ridge. Force full updates every dwell.
        settings.m_PriorityBand1IntervalS = 0.0;
        settings.m_PriorityBand2IntervalS = 0.0;
        settings.m_LosCacheMaxAgeS = 0.05;
        settings.m_TargetReuseMaxAgeS = 0.05;
        settings.m_PhysicalReuseMaxAgeS = 0.05;
        settings.m_MaxLosTracesPerScan = 128;
        settings.m_FreshUpdateBudgetMin = 64;
        settings.m_FreshUpdateBudgetMax = 128;
        // Fit from 1.log UH-1 revs SNR=[5.4,0.2,-2.9,9.8,12.0]:
        // gate<=-3 clears 5/5; -4 keeps ~1 dB margin (simulate_snr_gates.py).
        settings.m_DetectionSnrDb = -4.0;
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
        // Lone air targets in empty range cells get CFAR-killed otherwise.
        settings.m_EnableCfarGate = false;
        settings.m_EnableDemClutter = true;
        settings.m_DemClutterScale = 0.10;
        settings.m_KeepUndetected = true;
        settings.m_MinDistance = 40.0;
        // Default MeasNoiseScale=1 with 60° beam yields tens–hundreds of meters
        // of azimuth jitter. Search PPI wants geometry near entity truth.
        settings.m_EnableMeasurementSynthesis = false;
        settings.m_KeepEntityTruth = true;
        settings.m_MeasNoiseScale = 0.0;
        settings.m_MeasRangeBiasM = 0.0;
        settings.m_MeasAzimuthBiasDeg = 0.0;
        settings.m_MeasElevationBiasDeg = 0.0;
        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_ScanRpm = 10.0;
            settings.m_Hardware.m_AzimuthBeamwidthDeg = 60.0;
            settings.m_Hardware.m_EnableMti = false;
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
        // Hard LOS (same as US search): no NLOS multipath / knife-edge plots.
        settings.m_EnableNlosMultipath = false;
        settings.m_EnableKnifeEdgeDiffraction = false;
        settings.m_PriorityBand1IntervalS = 0.0;
        settings.m_PriorityBand2IntervalS = 0.0;
        settings.m_LosCacheMaxAgeS = 0.05;
        settings.m_TargetReuseMaxAgeS = 0.05;
        settings.m_PhysicalReuseMaxAgeS = 0.05;
        settings.m_MaxLosTracesPerScan = 96;
        settings.m_FreshUpdateBudgetMin = 48;
        settings.m_FreshUpdateBudgetMax = 96;
        // Same UH-1 log fit as US search (see simulate_snr_gates.py).
        settings.m_DetectionSnrDb = -4.0;
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
        settings.m_EnableCfarGate = false;
        settings.m_EnableDemClutter = true;
        settings.m_DemClutterScale = 0.10;
        settings.m_KeepUndetected = true;
        settings.m_MinDistance = 40.0;
        settings.m_EnableMeasurementSynthesis = false;
        settings.m_KeepEntityTruth = true;
        settings.m_MeasNoiseScale = 0.0;
        settings.m_MeasRangeBiasM = 0.0;
        settings.m_MeasAzimuthBiasDeg = 0.0;
        settings.m_MeasElevationBiasDeg = 0.0;
        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_ScanRpm = 6.0;
            settings.m_Hardware.m_AzimuthBeamwidthDeg = 60.0;
            settings.m_Hardware.m_EnableMti = false;
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
