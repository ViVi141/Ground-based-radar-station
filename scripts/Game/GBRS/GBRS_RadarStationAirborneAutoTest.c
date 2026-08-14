// GBRS airborne auto-test (Script Debugger):
// 1) find nearby GBRS station or spawn US RPL-5 composition,
// 2) force-power the station (supply gate bypassed),
// 3) spawn Mi-8 on a circular orbit (no Register() seed),
// 4) verify station RDF sensor sees / detects the airframe.
//
// Usage:
//   GBRS_RadarStationAirborneAutoTest.Start();
//   GBRS_RadarStationAirborneAutoTest.StartIdeal();  // relaxed SNR/CFAR for diagnosis
//   GBRS_RadarStationAirborneAutoTest.StartKeepTarget();
//   GBRS_RadarStationAirborneAutoTest.Stop();
//
// Production path: RDF_RADAR_MODE_PULSE_DOPPLER + ScattererRegistry only.
class GBRS_RadarStationAirborneAutoTest
{
    protected static ref GBRS_RadarStationAirborneAutoTest s_Instance;
    protected static bool s_TickRegistered;
    protected static bool s_KeepSpawnedTargetAfterTest;
    protected static bool s_UseIdealChannel;
    protected static GBRS_RadarStationComponent s_QueryFoundStation;
    protected static float s_QueryBestDistSq;

    protected static const ResourceName AIR_TARGET_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";
    protected static const ResourceName US_STATION_PREFAB =
        "{69FCEDCEA0010001}Prefabs/Compositions/Misc/FreeRoamBuilding/RadarStation_S_US_01.et";

    protected bool m_Running;
    protected bool m_SpawnedStation;
    protected bool m_IdealChannel;
    protected float m_StartWallS;
    protected float m_LastProgressWallS;
    protected float m_LastDebugPrintWallS;
    // Mechanical scan + cold registry need more wall time than RDF AutoRunner air test.
    protected float m_DurationS = 50.0;
    protected int m_LastScanSerial = -1;

    protected IEntity m_Subject;
    protected IEntity m_AirTarget;
    protected IEntity m_StationEntity;
    protected GBRS_RadarStationComponent m_Station;
    protected vector m_RadarOrigin;

    protected float m_OrbitRadiusM = 320.0;
    protected float m_OrbitAltitudeM = 120.0;
    protected float m_OrbitRateRadS = 0.18;
    protected float m_OrbitPhaseRad = 0.0;

    protected int m_ScanCount;
    protected int m_TargetSeenCount;
    protected int m_TargetDetectedCount;
    protected int m_TargetSeenAsVehicle;
    protected int m_TargetSeenAsEmitter;
    protected int m_TargetSeenAsAnonymous;
    protected float m_MaxTargetSnrDb = -300.0;
    protected bool m_TargetDiscovered;
    protected int m_RespawnCount;

    protected ref RDF_RadarSettings m_PrevSettings;
    protected bool m_PrevScanVisual;

    static GBRS_RadarStationAirborneAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new GBRS_RadarStationAirborneAutoTest();
        return s_Instance;
    }

    // Production GBRS preset (registry only, mechanical scan, CFAR/DEM as shipped).
    static void Start()
    {
        s_KeepSpawnedTargetAfterTest = false;
        s_UseIdealChannel = false;
        GetInstance().StartInternal();
    }

    // Same setup with ideal channel + CFAR off for pipeline diagnosis.
    static void StartIdeal()
    {
        s_KeepSpawnedTargetAfterTest = false;
        s_UseIdealChannel = true;
        GetInstance().StartInternal();
    }

    static void StartKeepTarget()
    {
        s_KeepSpawnedTargetAfterTest = true;
        s_UseIdealChannel = false;
        GetInstance().StartInternal();
    }

    static void StartIdealKeepTarget()
    {
        s_KeepSpawnedTargetAfterTest = true;
        s_UseIdealChannel = true;
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        GBRS_RadarStationAirborneAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[GBRS AirTest] stopped by user.");
    }

    static bool IsRunning()
    {
        GBRS_RadarStationAirborneAutoTest inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    protected static void StaticTick()
    {
        GBRS_RadarStationAirborneAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[GBRS AirTest] already running.");
            return;
        }

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[GBRS AirTest] no world.", LogLevel.WARNING);
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[GBRS AirTest] no local subject.", LogLevel.WARNING);
            return;
        }

        m_IdealChannel = s_UseIdealChannel;
        m_SpawnedStation = false;
        m_Station = null;
        m_StationEntity = null;
        m_PrevSettings = null;

        if (!ResolveOrSpawnStation())
        {
            Print("[GBRS AirTest] failed to resolve/spawn GBRS station.", LogLevel.ERROR);
            return;
        }

        m_RadarOrigin = m_Station.GetScanOriginWorld();
        if (m_RadarOrigin.LengthSq() < 0.01)
            m_RadarOrigin = m_StationEntity.GetOrigin();

        m_PrevScanVisual = m_Station.IsScanVisualEnabled();
        m_Station.SetScanVisualEnabled(true);
        m_Station.SetPoweredForAutoTest(true);
        if (!m_Station.IsPowered())
        {
            Print("[GBRS AirTest] station failed to power on.", LogLevel.ERROR);
            CleanupSpawnedStation();
            return;
        }

        if (m_IdealChannel)
            ApplyIdealOverlay();
        else
            EnsureForceLocalScan();

        m_StartWallS = System.GetTickCount() * 0.001;

        if (!SpawnAirTarget())
        {
            Print("[GBRS AirTest] failed to spawn air target.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        m_ScanCount = 0;
        m_TargetSeenCount = 0;
        m_TargetDetectedCount = 0;
        m_TargetSeenAsVehicle = 0;
        m_TargetSeenAsEmitter = 0;
        m_TargetSeenAsAnonymous = 0;
        m_MaxTargetSnrDb = -300.0;
        m_TargetDiscovered = false;
        m_RespawnCount = 0;

        RDF_RadarSensor sensor = GetStationSensor();
        if (sensor)
            m_LastScanSerial = sensor.GetScanSerial();
        else
            m_LastScanSerial = -1;

        m_LastProgressWallS = m_StartWallS;
        m_LastDebugPrintWallS = m_StartWallS;
        m_Running = true;

        RDF_RadarAutoTestMapOverlay.Start();
        if (m_AirTarget)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_AirTarget, "GBRS Air");

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
        }

        string modeLabel = "production";
        if (m_IdealChannel)
            modeLabel = "ideal";

        Print(string.Format(
            "[GBRS AirTest] started mode=%1 origin=%2 stationSpawned=%3",
            modeLabel,
            m_RadarOrigin.ToString(),
            m_SpawnedStation.ToString()));
        Print("[GBRS AirTest] open map (M) for aircraft marker; Stop() to abort.");
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestMapOverlay.ClearAircraft();
        RDF_RadarAutoTestMapOverlay.Stop();

        if (m_AirTarget && !s_KeepSpawnedTargetAfterTest)
        {
            RDF_RadarScattererRegistry.Unregister(m_AirTarget);
            SCR_EntityHelper.DeleteEntityAndChildren(m_AirTarget);
            m_AirTarget = null;
        }

        if (!restore)
            return;

        if (m_Station)
        {
            if (m_PrevSettings)
            {
                RDF_RadarSensor sensor = GetStationSensor();
                if (sensor)
                {
                    sensor.SetForceLocalScan(true);
                    sensor.Configure(m_PrevSettings);
                }
                m_PrevSettings = null;
            }

            m_Station.SetScanVisualEnabled(m_PrevScanVisual);

            if (m_SpawnedStation)
            {
                m_Station.SetPoweredForAutoTest(false);
                CleanupSpawnedStation();
            }
            else
            {
                // Leave pre-placed stations powered so the operator can inspect PPI.
            }
        }

        m_Station = null;
        m_StationEntity = null;
    }

    protected void CleanupSpawnedStation()
    {
        if (!m_SpawnedStation)
            return;
        if (!m_StationEntity)
            return;

        SCR_EntityHelper.DeleteEntityAndChildren(m_StationEntity);
        m_StationEntity = null;
        m_Station = null;
        m_SpawnedStation = false;
    }

    protected RDF_RadarSensor GetStationSensor()
    {
        if (!m_Station)
            return null;
        RDF_RadarComponent radar = m_Station.GetRadarComponent();
        if (!radar)
            return null;
        return radar.GetSensor();
    }

    protected void EnsureForceLocalScan()
    {
        RDF_RadarSensor sensor = GetStationSensor();
        if (!sensor)
            return;
        sensor.SetForceLocalScan(true);
    }

    protected void ApplyIdealOverlay()
    {
        RDF_RadarSensor sensor = GetStationSensor();
        if (!sensor)
            return;

        // Hold the live settings object; Configure() swaps in a new one so restore
        // can put the original back.
        m_PrevSettings = sensor.GetSettings();

        RDF_RadarSettings cfg = GBRS_RadarStationConfig.CreateUsSearch();
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = false;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_KeepUndetected = true;
        cfg.m_DetectionSnrDb = -40.0;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableClutterMap = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_KeepEntityTruth = true;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_ScattererMaxEntries = 2048;
        cfg.m_ScattererDiscoveryIntervalS = 0.25;
        // Ideal overlay: keep PD mode stamp but open the gate (no MTI / clutter map).
        if (cfg.m_Hardware)
            cfg.m_Hardware.m_EnableMti = false;

        // RDF 1.0.0 removed ApplyIdealChannel; StabilizeForRegression clears the
        // same optional fidelity extras (noise / thermal fill / atmosphere /
        // two-ray / refraction / PRF folds). CFAR gate is handled explicitly below.
        cfg.StabilizeForRegression();
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableCfarGate = false;
        cfg.Validate();

        sensor.SetForceLocalScan(true);
        sensor.SetMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_PULSE_DOPPLER);
        sensor.Configure(cfg);
        Print("[GBRS AirTest] applied ideal overlay (PD mode, CFAR/DEM/MTI off, SNR=-40).");
    }

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float nowS = System.GetTickCount() * 0.001;
        UpdateAirTargetMotion(nowS);
        if (m_AirTarget)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_AirTarget, "GBRS Air");
        AccumulateLatestScan(nowS);

        if (nowS - m_StartWallS < m_DurationS)
            return;

        FinalizeAndReport();
        StopInternal(true);
    }

    protected bool ResolveOrSpawnStation()
    {
        s_QueryFoundStation = null;
        s_QueryBestDistSq = 999999999.0;
        BaseWorld world = GetGame().GetWorld();
        if (!world || !m_Subject)
            return false;

        world.QueryEntitiesBySphere(
            m_Subject.GetOrigin(),
            2500.0,
            OnStationQueryEntity,
            null,
            EQueryEntitiesFlags.ALL);

        if (s_QueryFoundStation)
        {
            m_Station = s_QueryFoundStation;
            m_StationEntity = m_Station.GetOwner();
            m_SpawnedStation = false;
            Print("[GBRS AirTest] using existing station at "
                + m_StationEntity.GetOrigin().ToString());
            s_QueryFoundStation = null;
            return true;
        }

        return SpawnUsStationNearSubject();
    }

    protected static bool OnStationQueryEntity(IEntity entity)
    {
        if (!entity)
            return true;

        GBRS_RadarStationComponent station =
            GBRS_RadarStationComponent.Cast(entity.FindComponent(GBRS_RadarStationComponent));
        if (!station)
            return true;

        IEntity subject = null;
        GBRS_RadarStationAirborneAutoTest inst = s_Instance;
        if (inst)
            subject = inst.m_Subject;
        if (!subject)
        {
            if (!s_QueryFoundStation)
                s_QueryFoundStation = station;
            return true;
        }

        vector delta = entity.GetOrigin() - subject.GetOrigin();
        float distSq = delta.LengthSq();
        if (distSq < s_QueryBestDistSq)
        {
            s_QueryBestDistSq = distSq;
            s_QueryFoundStation = station;
        }
        return true;
    }

    protected bool SpawnUsStationNearSubject()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world || !m_Subject)
            return false;

        Resource prefabRes = Resource.Load(US_STATION_PREFAB);
        if (!prefabRes)
        {
            Print("[GBRS AirTest] failed to load US station prefab.", LogLevel.ERROR);
            return false;
        }

        vector mat[4];
        m_Subject.GetWorldTransform(mat);
        vector center = mat[3];
        vector spawnPos = ChooseFlatSpawnNear(center, world);

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = spawnPos;

        m_StationEntity = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!m_StationEntity)
            return false;

        m_Station = GBRS_RadarStationComponent.Cast(
            m_StationEntity.FindComponent(GBRS_RadarStationComponent));
        if (!m_Station)
        {
            Print("[GBRS AirTest] spawned prefab missing GBRS_RadarStationComponent.",
                LogLevel.ERROR);
            SCR_EntityHelper.DeleteEntityAndChildren(m_StationEntity);
            m_StationEntity = null;
            return false;
        }

        m_SpawnedStation = true;
        Print("[GBRS AirTest] spawned US station at " + spawnPos.ToString());
        return true;
    }

    protected vector ChooseFlatSpawnNear(vector center, BaseWorld world)
    {
        vector best = Vector(center[0] + 40.0, center[1], center[2] + 40.0);
        float bestScore = 9999999.0;
        float searchRadius = 80.0;
        float sampleOffset = 12.0;

        for (int i = 0; i < 12; i++)
        {
            float angle = i * Math.PI * 2.0 / 12.0;
            float x = center[0] + Math.Cos(angle) * searchRadius;
            float z = center[2] + Math.Sin(angle) * searchRadius;
            float y = world.GetSurfaceY(x, z);

            float hx1 = world.GetSurfaceY(x + sampleOffset, z);
            float hx2 = world.GetSurfaceY(x - sampleOffset, z);
            float hz1 = world.GetSurfaceY(x, z + sampleOffset);
            float hz2 = world.GetSurfaceY(x, z - sampleOffset);
            float score = Math.AbsFloat(hx1 - hx2) + Math.AbsFloat(hz1 - hz2);
            if (score < bestScore)
            {
                bestScore = score;
                best = Vector(x, y + 0.5, z);
            }
        }

        return best;
    }

    protected vector ComputeOrbitPos(float elapsedS)
    {
        float phase = elapsedS * m_OrbitRateRadS;
        return Vector(
            m_RadarOrigin[0] + Math.Cos(phase) * m_OrbitRadiusM,
            m_RadarOrigin[1] + m_OrbitAltitudeM + 15.0 * Math.Sin(elapsedS * 0.07),
            m_RadarOrigin[2] + Math.Sin(phase) * m_OrbitRadiusM);
    }

    protected float GetElapsedS()
    {
        if (m_StartWallS <= 0.0)
            return 0.0;
        float elapsed = System.GetTickCount() * 0.001 - m_StartWallS;
        if (elapsed < 0.0)
            return 0.0;
        return elapsed;
    }

    protected bool SpawnAirTarget()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        Resource prefabRes = Resource.Load(AIR_TARGET_PREFAB);
        if (!prefabRes)
            return false;

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        vector spawnPos = ComputeOrbitPos(GetElapsedS());
        spawnParams.Transform[3] = spawnPos;

        m_AirTarget = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!m_AirTarget)
            return false;

        // Skin RCS only: no emitter flag, no registry pre-seeding.
        Print("[GBRS AirTest] spawned Mi-8 at " + spawnPos.ToString());
        return true;
    }

    protected void UpdateAirTargetMotion(float nowS)
    {
        if (!m_AirTarget)
        {
            if (SpawnAirTarget())
            {
                m_RespawnCount = m_RespawnCount + 1;
                Print("[GBRS AirTest] target respawned count=" + m_RespawnCount.ToString());
            }
            return;
        }

        float elapsedS = GetElapsedS();
        m_OrbitPhaseRad = elapsedS * m_OrbitRateRadS;
        vector pos = ComputeOrbitPos(elapsedS);
        m_AirTarget.SetOrigin(pos);

        float vx = -Math.Sin(m_OrbitPhaseRad) * m_OrbitRadiusM * m_OrbitRateRadS;
        float vz = Math.Cos(m_OrbitPhaseRad) * m_OrbitRadiusM * m_OrbitRateRadS;
        float vy = 15.0 * 0.07 * Math.Cos(elapsedS * 0.07);
        Physics physics = m_AirTarget.GetPhysics();
        if (physics)
            physics.SetVelocity(Vector(vx, vy, vz));

        int markerFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
        Shape.CreateSphere(ARGBF(1, 0.2, 1, 0.2), markerFlags, pos, 6.0);
        vector markerLine[2];
        markerLine[0] = pos;
        markerLine[1] = pos + "0 25 0";
        Shape.CreateLines(ARGBF(1, 0.2, 1, 0.2), markerFlags, markerLine, 2);

        if (nowS - m_LastDebugPrintWallS > 3.0)
        {
            m_LastDebugPrintWallS = nowS;
            float dx = pos[0] - m_RadarOrigin[0];
            float dy = pos[1] - m_RadarOrigin[1];
            float dz = pos[2] - m_RadarOrigin[2];
            float dist = Math.Sqrt(dx * dx + dy * dy + dz * dz);
            Print("[GBRS AirTest] marker range=" + dist.ToString()
                + " scans=" + m_ScanCount.ToString()
                + " seen=" + m_TargetSeenCount.ToString()
                + " det=" + m_TargetDetectedCount.ToString()
                + " inTable=" + m_TargetDiscovered.ToString()
                + " " + RDF_RadarScattererRegistry.GetStatsLine());
        }
    }

    protected void AccumulateLatestScan(float nowS)
    {
        RDF_RadarSensor sensor = GetStationSensor();
        if (!sensor)
            return;

        // Keep origin synced to live antenna bind (US bone / USSR yaw).
        if (m_Station)
            m_RadarOrigin = m_Station.GetScanOriginWorld();

        int serial = sensor.GetScanSerial();
        if (serial == m_LastScanSerial)
        {
            if (nowS - m_LastProgressWallS > 5.0)
            {
                m_LastProgressWallS = nowS;
                Print("[GBRS AirTest] waiting for scan progress; ensure Play mode + station powered.");
            }
            return;
        }

        m_LastScanSerial = serial;
        m_LastProgressWallS = nowS;
        m_ScanCount = m_ScanCount + 1;
        if (m_AirTarget && RDF_RadarScattererRegistry.Find(m_AirTarget))
            m_TargetDiscovered = true;

        array<ref RDF_RadarTarget> targets = sensor.GetPlots();
        if (!targets || !m_AirTarget)
            return;

        foreach (RDF_RadarTarget t : targets)
        {
            if (!t)
                continue;

            float matchGateM = 250.0;
            if (!IsPlotNearEntity(t, m_AirTarget, m_RadarOrigin, matchGateM))
                continue;

            m_TargetSeenCount = m_TargetSeenCount + 1;
            if (!t.m_Detected)
                continue;

            m_TargetDetectedCount = m_TargetDetectedCount + 1;
            if (t.m_SnrDb > m_MaxTargetSnrDb)
                m_MaxTargetSnrDb = t.m_SnrDb;
            if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE)
                m_TargetSeenAsVehicle = m_TargetSeenAsVehicle + 1;
            if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
                m_TargetSeenAsEmitter = m_TargetSeenAsEmitter + 1;
            if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS)
                m_TargetSeenAsAnonymous = m_TargetSeenAsAnonymous + 1;
        }
    }

    protected bool IsPlotNearEntity(
        RDF_RadarTarget plot,
        IEntity entity,
        vector radarOrigin,
        float gateM)
    {
        if (!plot || !entity)
            return false;
        if (plot.m_Entity == entity)
            return true;

        vector truthPos = entity.GetOrigin();
        vector truthDelta = truthPos - radarOrigin;
        float truthRange = truthDelta.Length();
        float dRange = plot.m_Distance - truthRange;
        if (dRange < 0.0)
            dRange = -dRange;
        if (dRange > gateM)
            return false;

        float truthAz = Math.Atan2(truthDelta[2], truthDelta[0]) * 57.2957795;
        float dAz = plot.m_AzimuthDeg - truthAz;
        while (dAz > 180.0)
            dAz = dAz - 360.0;
        while (dAz < -180.0)
            dAz = dAz + 360.0;
        if (dAz < 0.0)
            dAz = -dAz;
        if (dAz > 8.0)
            return false;
        return true;
    }

    protected void FinalizeAndReport()
    {
        bool passScans = m_ScanCount > 8;
        bool passSeen = m_TargetSeenCount > 0;
        bool passDetected = m_TargetDetectedCount > 0;
        bool passDiscovery = m_TargetDiscovered;
        // Production may synthesize ANONYMOUS; ideal overlay keeps vehicle truth.
        bool passClassified = false;
        if (m_IdealChannel)
            passClassified = m_TargetSeenAsVehicle > 0;
        else
        {
            if (m_TargetSeenAsVehicle > 0)
                passClassified = true;
            else if (m_TargetSeenAsAnonymous > 0)
                passClassified = true;
        }
        bool passNoEmitterAid = m_TargetSeenAsEmitter == 0;

        bool allPass = passScans && passSeen && passDetected && passClassified
            && passNoEmitterAid && passDiscovery;

        string modeLabel = "production";
        if (m_IdealChannel)
            modeLabel = "ideal";

        array<string> lines = new array<string>();
        lines.Insert("GBRS Radar Station Airborne Auto Test");
        lines.Insert("mode " + modeLabel);
        lines.Insert("prefab " + AIR_TARGET_PREFAB);
        lines.Insert("result " + BoolLabel(allPass));
        lines.Insert("");
        lines.Insert("checks:");
        lines.Insert("  scans_present " + BoolLabel(passScans));
        lines.Insert("  target_seen " + BoolLabel(passSeen));
        lines.Insert("  target_detected " + BoolLabel(passDetected));
        lines.Insert("  target_classified " + BoolLabel(passClassified));
        lines.Insert("  no_emitter_aid " + BoolLabel(passNoEmitterAid));
        lines.Insert("  discovered_unaided " + BoolLabel(passDiscovery));
        lines.Insert("");
        lines.Insert("metrics:");
        lines.Insert("  scan_count " + m_ScanCount.ToString());
        lines.Insert("  target_seen_count " + m_TargetSeenCount.ToString());
        lines.Insert("  target_detected_count " + m_TargetDetectedCount.ToString());
        lines.Insert("  seen_as_vehicle " + m_TargetSeenAsVehicle.ToString());
        lines.Insert("  seen_as_anonymous " + m_TargetSeenAsAnonymous.ToString());
        lines.Insert("  seen_as_emitter " + m_TargetSeenAsEmitter.ToString());
        lines.Insert("  max_snr_db " + m_MaxTargetSnrDb.ToString());
        lines.Insert("  respawn_count " + m_RespawnCount.ToString());
        lines.Insert("  station_spawned " + m_SpawnedStation.ToString());
        lines.Insert("  " + RDF_RadarScattererRegistry.GetStatsLine());

        foreach (string line : lines)
        {
            if (allPass)
                Print("[GBRS AirTest] " + line);
            else
                Print("[GBRS AirTest] " + line, LogLevel.WARNING);
        }
    }

    protected string BoolLabel(bool value)
    {
        if (value)
            return "PASS";
        return "FAIL";
    }
}
