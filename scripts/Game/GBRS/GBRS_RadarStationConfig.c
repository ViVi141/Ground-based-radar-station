// Faction station presets: thin wrappers around RDF product modes
// RDF_RADAR_MODE_PULSE_DOPPLER (air search / lock) and RDF_RADAR_MODE_WLR.
//
// Do not invent a second detection channel. Station code only sets geometry,
// range, and include filters. Channel physics (MTD, PRF stagger, coast,
// HwCalib) stays with RDF.
//
// Balance intent:
//   US  = precise SHORAD (7 km PD / 8 km WLR, 10 RPM search, 2.5 deg beam).
//   USSR = early-warning (10 km PD/WLR, 6 RPM search, 6 deg beam, VHF DEM 0.50).
class GBRS_RadarStationConfig
{
    protected static const float MTI_DISPLAY_MIN_RADIAL_SPEED_MS = 3.0;
    // X-band ferrite circulator recovery on the SHORAD search pulse.
    protected static const float US_SEARCH_RECEIVER_RECOVERY_S = 0.0000002;
    // VHF TR-tube recovery on the P-18-like long pulse.
    protected static const float USSR_SEARCH_RECEIVER_RECOVERY_S = 0.000001;
    // Counter-battery locating pulse (not the EW search pulse).
    protected static const float WLR_PULSE_WIDTH_S = 0.000001;
    protected static const float WLR_RECEIVER_RECOVERY_S = 0.0000005;

    // Display-side gate for PPI / detect visuals.
    // WLR is projectile-only: never paint vehicles, infantry, or anonymous clutter.
    // PD SEARCH keeps the MTI radial-speed gate so slow ground clutter stays off-scope.
    static bool ShouldDisplayPlot(RDF_RadarTarget target, RDF_RadarSettings settings)
    {
        if (!target || !target.m_Detected)
            return false;

        if (target.m_Entity && ChimeraCharacter.Cast(target.m_Entity))
            return false;

        if (settings)
        {
            if (target.m_Distance <= settings.GetEffectiveMinDistance())
                return false;
        }

        // Counter-battery product: IncludeProjectiles on, vehicles off.
        // Workstation readout sets KeepEntityTruth=false, so RDF measurement
        // synthesis strips type to ANONYMOUS. Discovery is already
        // projectile-only — dropping anything that is not PROJECTILE made the
        // PPI empty at every range.
        if (settings && settings.m_IncludeProjectiles && !settings.m_IncludeVehicles)
        {
            if (target.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
                return true;
            if (target.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS)
                return true;
            if (target.m_IsAnonymous)
                return true;
            return false;
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

    // TX blanking Rmin = c·(τ + recovery)/2. Syncs m_MinDistance so HUD/debug
    // match RDF GetEffectiveMinDistance (pulse eclipsing, not a 40 m floor).
    static void ApplySearchPulseBlindZone(RDF_RadarHardware hw, bool usFaction)
    {
        if (!hw)
            return;

        if (usFaction)
            hw.m_ReceiverRecoveryS = US_SEARCH_RECEIVER_RECOVERY_S;
        else
            hw.m_ReceiverRecoveryS = USSR_SEARCH_RECEIVER_RECOVERY_S;
        hw.Validate();
    }

    static void ApplyWlrPulseBlindZone(RDF_RadarHardware hw)
    {
        if (!hw)
            return;

        hw.m_PulseWidthS = WLR_PULSE_WIDTH_S;
        hw.m_ReceiverRecoveryS = WLR_RECEIVER_RECOVERY_S;
        hw.Validate();
    }

    static void SyncMinDistanceToPulseBlind(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        settings.m_EnablePulseBlindZone = true;
        if (!settings.m_Hardware)
            return;

        float rmin = settings.m_Hardware.GetMinDetectableRangeM();
        settings.m_MinDistance = rmin;
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

    // Channel opt-in kept after tools/sweep_fidelity_flags.py (512 combos).
    // DEM clutter + CFAR + thermal fill + two-ray move Pd; 4/3 refraction,
    // clear-air / rain loss, and PRF range fold are no-ops inside 7–10 km.
    static void ApplyRealisticChannelOptIn(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        settings.SetMeasurementNoise(3.5, 5.0, 0.2, 0.15);
        settings.EnableCfarThermalFill(true);
        settings.EnableLosTwoRayMultipath();
        settings.DisableAtmosphericPathLoss();
        settings.m_EnableAtmosphericRefraction = false;
        settings.m_EnableRangeAmbiguityFold = false;
        settings.m_EnableDopplerAmbiguityFold = false;
    }

    // Dwell scheduler adds extra ScanOnce slices for LOCK/TWS. SEARCH rotation
    // does not use them; keep off unless a dedicated fire-control load returns.
    static void ApplySystemLayers(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        settings.m_EnableDwellScheduler = false;
    }

    // RDF 1.0.2 LOS queue + BudgetGovernor default to 4–5 Hz SEARCH (16 traces
    // per Tick, scan ≤ ~30% of one server frame, LOS cap 20). GBRS mechanical
    // dwells are 40–50 ms (stare 120 ms) so those defaults starve the current
    // beam. Keep the queue (flatten TraceMove spikes) but pin the per-tick
    // slice and disable the governor.
    static void ApplyMechanicalScanBudget(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        settings.m_EnableLosFrameQueue = true;
        settings.m_LosTracesPerTick = 12;
        settings.m_LosQueueMax = 128;
        settings.m_EnableAdaptiveBudget = false;
    }

    // RDF ProcessPending only runs when ScattererRegistry.Tick runs, and
    // ScanOnce is the only RDF caller. Stare (120 ms) then classifies at
    // ~8 Hz, so a 15k Eden dump sits in front of the in-beam helicopter.
    // Pin the RDF-max classify slice; GBRS_RadarStationComponent also Ticks
    // the registry every powered frame so the queue is not scan-gated.
    static void ApplyScattererDiscoveryBudget(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        settings.m_ScattererDiscoveryIntervalS =
            GBRS_RadarStationConstants.SCATTERER_DISCOVERY_INTERVAL_S;
        settings.m_ScattererDiscoveryRangeScale =
            GBRS_RadarStationConstants.SCATTERER_DISCOVERY_RANGE_SCALE;
        settings.m_ScattererClassifyPerTick =
            GBRS_RadarStationConstants.SCATTERER_CLASSIFY_PER_TICK;
        settings.m_ScattererRefreshPerTick =
            GBRS_RadarStationConstants.SCATTERER_REFRESH_PER_TICK;
        settings.m_ScattererMaxEntries =
            GBRS_RadarStationConstants.SCATTERER_MAX_ENTRIES;
    }

    // Enables RDF channel / environment / track features for air search.
    static void ApplyFullFidelity(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        ApplyRealisticChannelOptIn(settings);
        settings.m_EnableDemClutter = true;
        // Span occlusion needs extra DEM column samples; SURF packs rarely
        // carry building spans, so this is cost without PPI change.
        settings.m_EnableDemSpanOcclusion = false;
        // Coarse RD is off in RDF product defaults; the GBRS PPI does not
        // draw it, and 20–50 ms dwells would pay for it every scan.
        settings.m_EnableCoarseRd = false;
        settings.m_RdCellsPerScan = 32;
        settings.m_RdMapAlpha = 0.2;
        settings.m_RdDecayPerScan = 0.97;
        settings.m_RdClutterBlend = 0.35;
        settings.m_EnableClutterMap = false;
        settings.m_ClutterMapAlpha = 0.15;
        settings.m_EnableNlosMultipath = true;
        settings.m_EnableKnifeEdgeDiffraction = true;
        settings.m_EnableEsmReceive = false;
        settings.m_EnableRwrReporting = true;
        settings.m_EnableMeasurementSynthesis = true;
        settings.m_EnableCfarGate = true;
        settings.m_EnableCfarThermalFill = true;
        settings.m_EnableAtmosphericLoss = false;
        settings.m_EnableWeatherDrivenRainLoss = false;
        settings.m_EnableWlrHudAlerts = true;
        settings.m_FairScanCursor = true;
        settings.m_TrackCoastOnMiss = true;
        settings.m_TrackCoastOnDopplerNull = true;
        settings.m_TrackCoastGateGrowPerMiss = 0.25;
        settings.m_TrackCoastMaxSec = 12.0;
        // RDF clamps TrackMaxMisses to 32. For a rotating narrow beam that is
        // still too few misses to coast between beam passes at fast update
        // intervals; GBRS_RadarProjectileTrackerFix raises the tracker's own
        // miss allowance when mechanical scan is enabled.
        settings.m_TrackMaxMisses = 32;
        // RDF 1.0.2 FindGatingTrack already drops leftover plots inside an
        // existing track gate. 8° / 600 m is wide enough for mechanical-scan
        // measurement noise without merging a formation.
        settings.m_TrackGateAzimuthDeg = 8.0;
        settings.m_TrackGateRangeM = 600.0;
        if (!settings.m_MeasurementModel)
            settings.m_MeasurementModel = new RDF_RadarDefaultMeasurementModel();

        ApplyEwStack(settings);
        ApplySystemLayers(settings);
        ApplyMechanicalScanBudget(settings);
        ApplyScattererDiscoveryBudget(settings);
    }

    // WLR fidelity: projectile-only counter-battery search.
    // DEM clutter is DISABLED: offline validation (simulate_wlr_projectile)
    // showed the full DEM clutter floor swamps 0.01 m2 projectiles (-35 dB,
    // clutter-limited) so nothing is ever detected. Real counter-battery radars
    // look up at the ballistic mid-course with high elevation beams - the
    // main-beam ground return is negligible there, so thermal-noise-limited
    // detection is the correct model. Launch/impact solving uses DEM ground
    // (m_EnableDemGroundForWlr) for the terrain surface-intersection fit.
    //
    // RDF_RadarScanner drops a candidate when LOS is blocked and NLOS is off.
    // Scatterer-table hits (GetStatsLine hit=) are signature lookups, not
    // published plots — StartWlr() showed scat/hit climbing with GetPlots()
    // empty after NLOS was disabled for perf. WLR must keep NLOS on.
    // DEM LOS precheck stays off: high-look mortar beams plus Eden HEIGHT
    // liveY=0 otherwise veto ballistic paths before PhysicalDetect runs.
    // EW / RDF WLR HUD stay off — they do not create those plots.
    static void ApplyWlrFidelity(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        ApplyRealisticChannelOptIn(settings);
        // WLR tracker stability matters more than operator-facing noise. Clear
        // measurement noise so low-SNR shell plots do not jump outside the
        // association gates and fragment one shell into many tracks.
        settings.ClearMeasurementNoise();
        settings.m_EnableDemClutter = false;
        settings.m_EnableDemSpanOcclusion = false;
        settings.m_EnableDemLosPrecheck = false;
        settings.m_EnableCoarseRd = false;
        settings.m_RdCellsPerScan = 24;
        settings.m_RdMapAlpha = 0.15;
        settings.m_RdDecayPerScan = 0.97;
        settings.m_RdClutterBlend = 0.3;
        settings.m_EnableClutterMap = false;
        settings.m_EnableNlosMultipath = true;
        settings.m_EnableKnifeEdgeDiffraction = true;
        settings.m_EnableEsmReceive = false;
        settings.m_EnableRwrReporting = false;
        settings.m_EnableMeasurementSynthesis = true;
        // WLR projectile detection is intentionally thermal-noise-limited.
        // CFAR was enabled by ApplyRealisticChannelOptIn, but offline validation
        // and RDF's own ShellFire test both run without CFAR; in-game CFAR made
        // small shell returns intermittent, which fragmented the tracker.
        settings.m_EnableCfarGate = false;
        settings.m_EnableCfarThermalFill = false;
        settings.m_EnableAtmosphericLoss = false;
        settings.m_EnableWeatherDrivenRainLoss = false;
        settings.m_EnableBallisticPrediction = true;
        settings.m_EnableWeaponLocate = true;
        // DEM ground fit for WLR launch/impact stays ON (high-fidelity terrain
        // impact). RDF's weapon-locate DEM sampling is left at its stock behavior.
        settings.m_EnableDemGroundForWlr = true;
        // GBRS PPI draws launch/impact; RDF's own WLR HUD overlay is extra work.
        settings.m_EnableWlrHudAlerts = false;
        settings.m_FairScanCursor = true;
        settings.m_TrackCoastOnMiss = true;
        settings.m_TrackCoastOnDopplerNull = false;
        settings.m_TrackCoastMaxSec = 12.0;
        if (!settings.m_MeasurementModel)
            settings.m_MeasurementModel = new RDF_RadarDefaultMeasurementModel();

        ApplySystemLayers(settings);
        ApplyMechanicalScanBudget(settings);
        ApplyScattererDiscoveryBudget(settings);
        // RDF 1.0.2+ default is 2 solves/scan (queue the rest). GBRS PPI launch
        // / impact / ETA need the fix in the same barrage, so take the governor
        // max and keep the overflow queue. HUD reads track.m_LastWlrFix only —
        // do not run a second ballistic solver on the feed tick.
        settings.m_WeaponLocateSolvesPerScan = 6;
        settings.m_WeaponLocateQueueMax = 16;

        if (settings.m_EwStack)
            settings.m_EwStack.Clear();
        settings.m_EnableEccmDecision = false;
        settings.m_AdditionalNoisePowerW = 0.0;
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

        // RDF 1.0.0 ECCM decision layer (layered, opt-in). The bridge now
        // inherits RDF_RadarNoiseJammerEffect so the Sensor's RunEccmDecision
        // can read GetMainlobeFraction and drive SLB / PRF agility / burn-
        // through against live jammers. JN gate 6 dB is enough to catch a
        // dedicated jammer while ignoring scatterer-table noise floor.
        settings.m_EnableEccmDecision = true;
        settings.m_EccmJnOnDb = 6.0;
        settings.m_EccmJnHysteresisDb = 2.0;
        settings.m_EccmSidelobeCouplingOn = 0.3;
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
            // US AN/TPN-19 mesh: manned SHORAD readout — better than stock 3.5x, not truth.
            settings.m_MeasNoiseScale = 2.5;
            settings.m_MeasRangeBiasM = 3.0;
            settings.m_MeasAzimuthBiasDeg = 0.12;
            settings.m_MeasElevationBiasDeg = 0.1;
        }
        else
        {
            // USSR Tesla RPL-5 mesh / P-18-like VHF EW — stock realistic or slightly worse.
            settings.m_MeasNoiseScale = 3.5;
            settings.m_MeasRangeBiasM = 6.0;
            settings.m_MeasAzimuthBiasDeg = 0.3;
            settings.m_MeasElevationBiasDeg = 0.22;
        }
    }

    // US AN/TPN-19 visual, SHORAD pulse-Doppler search (~7 km). Not GCA handbook RF.
    static RDF_RadarSettings CreateUsSearch()
    {
        RDF_RadarSettings settings = RDF_RadarSensor.CreatePulseDopplerSettings(64);
        settings.m_Range = 7000.0;
        // 10 RPM × 2.5° beam ≈ 42 ms on target. 40 ms keeps overlap without
        // running ScanOnce on almost every game frame (20 ms was hitching).
        settings.m_UpdateInterval = 0.04;
        settings.m_SectorHalfAngleDeg = 180.0;
        settings.m_EnableMechanicalScan = true;
        settings.m_UseBoundsCenter = false;
        settings.m_UseLocalOffset = false;
        settings.m_OriginOffset = "0 0 0";
        settings.m_MaxLosTracesPerScan = 128;
        settings.m_FreshUpdateBudgetMin = 64;
        settings.m_FreshUpdateBudgetMax = 128;
        ApplyScattererDiscoveryBudget(settings);
        settings.m_IncludeVehicles = true;
        settings.m_IncludeProjectiles = false;
        settings.m_IncludeRadarEmitters = true;
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
            ApplySearchPulseBlindZone(settings.m_Hardware, true);
        }
        ApplyFullFidelity(settings);
        settings.m_CfarMode = ERDF_CfarMode.RDF_CFAR_CA;
        ApplyWorkstationReadout(settings, true);
        // RDF 1.0.2 FindGatingTrack drops leftover plots already inside an
        // existing track gate, so the old 14° / 900 m US gate would merge a
        // formation. Keep the shared 8° / 600 m fidelity gates.
        settings.m_TrackCoastMaxSec = 16.0;
        SyncMinDistanceToPulseBlind(settings);
        settings.Validate();
        return settings;
    }

    // USSR Tesla RPL-5 visual, P-18-like VHF early-warning pulse-Doppler (~10 km).
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
        ApplyScattererDiscoveryBudget(settings);
        settings.m_IncludeVehicles = true;
        settings.m_IncludeProjectiles = false;
        settings.m_IncludeRadarEmitters = true;
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
        ApplySearchPulseBlindZone(hw, false);
        settings.m_Hardware = hw;

        ApplyFullFidelity(settings);
        // RDF 1.1.0 per-band sigma0: VHF vegetation is stronger than legacy X-table.
        // Offline recal (calib_pd_full.py) -> 0.50 for R50 >= 10 km UH-1 (MTD_BANK).
        settings.m_DemClutterScale = 0.50;
        // Greater-of CFAR for clutter-edge VHF EW scenes.
        settings.m_CfarMode = ERDF_CfarMode.RDF_CFAR_GO;
        ApplyWorkstationReadout(settings, false);
        SyncMinDistanceToPulseBlind(settings);
        settings.Validate();
        return settings;
    }

    // Shared WLR geometry: projectile-only counter-battery search on a slow
    // all-around mechanical rotation (see WLR_SCAN_RPM). Infantry must never
    // enter WLR discovery/display — only shells/rockets.
    static void ApplyWlrProductFlags(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        settings.m_SectorHalfAngleDeg = 180.0;
        settings.m_UpdateInterval = 0.15;
        settings.m_IncludeVehicles = false;
        settings.m_IncludeRadarEmitters = false;
        settings.m_IncludeProjectiles = true;
        // Counter-battery WLR sweeps all-around on a slow mechanical rotation
        // (not a parked stare), so it does not need the operator/Demo to aim it.
        settings.m_EnableMechanicalScan = true;
        settings.m_WeaponLocateMinHits = 2;
        settings.m_WeaponLocateMinSpanS = 0.8;
        settings.m_TrackConfirmHits = 2;
        // Slightly wider association gates than RDF defaults so measurement
        // noise does not split one shell into several tracker files.
        settings.m_TrackGateAzimuthDeg = 8.0;
        settings.m_TrackGateRangeM = 600.0;
        settings.m_UseBoundsCenter = false;
        settings.m_UseLocalOffset = false;
        settings.m_OriginOffset = "0 0 0";
        settings.m_EnablePhysicalDetection = true;
        settings.m_KeepUndetected = false;

        if (settings.m_Hardware)
        {
            // No MTI: ballistic Doppler is not a slow-clutter notch problem.
            settings.m_Hardware.m_EnableMti = false;
            settings.m_Hardware.m_ScanRpm = GBRS_RadarStationConstants.WLR_SCAN_RPM;
            settings.m_Hardware.ClearElevationBeams();
            settings.m_Hardware.AddElevationBeam("mortar_low", 15.0, 28.0, 0.0);
            settings.m_Hardware.AddElevationBeam("mortar_mid", 35.0, 30.0, 0.0);
            settings.m_Hardware.AddElevationBeam("mortar_high", 55.0, 28.0, -0.5);
            ApplyWlrPulseBlindZone(settings.m_Hardware);
        }

        ApplyWlrFidelity(settings);
        SyncMinDistanceToPulseBlind(settings);
    }

    // US counter-battery WLR (~8 km slow all-around rotation).
    // Offline-tuned (tools/simulate_wlr_projectile.py + WLR_VALIDATION.md):
    // 500 kW gives ~8.6 dB at beam center and ~3.0 dB at 12 deg offset.
    // Gate 4 dB keeps center detections and drops the cheap offset lobe,
    // closer to USSR WLR 5 dB instead of the previous 2 dB US advantage.
    // NOTE: the offline chain models clutter-limited SNR but its CFAR gates
    // on thermal noise only (RDF uses adaptive clutter CFAR), so projectile
    // detection inside clutter needs in-game verification.
    static RDF_RadarSettings CreateUsWlr()
    {
        RDF_RadarSettings settings = RDF_RadarSensor.CreateWlrSettings(128);
        settings.m_Range = 8000.0;
        settings.m_MaxLosTracesPerScan = 96;
        settings.m_FreshUpdateBudgetMin = 48;
        settings.m_FreshUpdateBudgetMax = 96;
        ApplyScattererDiscoveryBudget(settings);
        settings.m_DetectionSnrDb = 4.0;
        if (settings.m_Hardware)
        {
            // Wide fan for the slow all-around rotation: a wider azimuth beam
            // illuminates a broader band each sweep, so a shell accumulates
            // enough detections to reach a fire solution. Boresight peak gain
            // is unchanged (Gaussian pattern peaks at 1.0 at any width).
            settings.m_Hardware.m_AzimuthBeamwidthDeg = 45.0;
            settings.m_Hardware.m_PeakPowerW = 500000.0;
        }
        ApplyWlrProductFlags(settings);
        ApplyWorkstationReadout(settings, true);
        // Keep WLR measurement clean: workstation readout re-adds noise, which
        // fragments fast-moving projectile tracks. RDF's own WLR tests clear
        // measurement noise for the same reason.
        settings.ClearMeasurementNoise();
        // WLR ballistic fit requires projectile-typed tracks. Workstation
        // readout strips type to ANONYMOUS, which makes SolveWeaponLocate
        // no-op and hides launch/impact/ETA. Keep class, not vehicle names.
        settings.m_KeepEntityTruth = true;
        settings.Validate();
        return settings;
    }

    // USSR counter-battery WLR (~10 km slow all-around rotation, wider beam).
    // VHF hardware (P-18-like) gives a large lambda^2 advantage at 10 km:
    // offline chain (no clutter) shows ~30 dB center SNR — far above the gate,
    // so peak power stays at the P-18 default (250 kW, CreateP18Like). The
    // offline CFAR model cannot reliably resolve clutter-limited projectile
    // detection (it gates on thermal noise, not RDF's adaptive clutter CFAR),
    // so clutter behavior needs in-game verification.
    static RDF_RadarSettings CreateUssrWlr()
    {
        RDF_RadarSettings settings = RDF_RadarSensor.CreateWlrSettings(128);
        settings.m_Range = 10000.0;
        settings.m_MaxLosTracesPerScan = 96;
        settings.m_FreshUpdateBudgetMin = 48;
        settings.m_FreshUpdateBudgetMax = 96;
        ApplyScattererDiscoveryBudget(settings);
        settings.m_DetectionSnrDb = 5.0;
        if (settings.m_Hardware)
        {
            // Wider fan than US for the slow all-around rotation (see US WLR
            // note); boresight peak gain is unchanged by the wider beam.
            settings.m_Hardware.m_AzimuthBeamwidthDeg = 55.0;
        }
        ApplyWlrProductFlags(settings);
        // Same VHF surface-scale relief as USSR search; clutter stays enabled.
        settings.m_DemClutterScale = 0.10;
        ApplyWorkstationReadout(settings, false);
        // Keep WLR measurement clean; see US WLR comment.
        settings.ClearMeasurementNoise();
        settings.m_KeepEntityTruth = true;
        settings.Validate();
        return settings;
    }
}
