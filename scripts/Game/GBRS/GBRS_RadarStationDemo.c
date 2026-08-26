// In-mission GBRS workstation demo (Script Debugger, Play mode).
// Mirrors RDF_RadarManualDemo / RDF_RadarPlayAutoTest: spawn or reuse a
// station, force-power it, open the GBRS PPI (not the RDF AutoRunner HUD),
// and keep a live target in the picture until Stop().
//
// Production RF only — no ideal-channel overlay, no scatterer pre-seed.
//
// Usage:
//   GBRS_RadarStationDemo.Start();      // US AN/TPN-19, PD SEARCH, radial Mi-8
//   GBRS_RadarStationDemo.StartUssr();  // USSR RPL-5, PD SEARCH
//   GBRS_RadarStationDemo.StartWlr();   // US station, WLR, periodic 82 mm shells
//   GBRS_RadarStationDemo.StartLock();  // reserved (LOCK / fire-control)
//   GBRS_RadarStationDemo.Probe();      // dump station sensor plots
//   GBRS_RadarStationDemo.Stop();
class GBRS_RadarStationDemo
{
    protected static ref GBRS_RadarStationDemo s_Instance;
    protected static bool s_TickRegistered;
    protected static GBRS_RadarStationComponent s_QueryFoundStation;
    protected static float s_QueryBestDistSq;

    // Demo-only world-space debug visuals (scan frustum, shell spheres, air
    // target marker, map overlay). Keep off for clean PPI-only testing.
    protected static bool DEBUG_VISUALS = false;

    protected static const ResourceName AIR_TARGET_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";
    protected static const ResourceName SHELL_PREFAB =
        "{98EC9C526AFBA282}Prefabs/Weapons/Ammo/Ammo_Shell_82mm_HE_O832DU.et";

    protected static const int TICK_MS = 200;
    protected static const int OPEN_PPI_DELAY_MS = 350;
    // Radial in/out, not a circle around the dish: PD MTI needs |vr| >= 3 m/s
    // to paint, and a centered orbit is essentially CPA the whole time.
    protected static const float AIR_ALTITUDE_M = 180.0;
    protected static const float AIR_LEG_S = 24.0;
    protected static const float US_AIR_NEAR_M = 1600.0;
    protected static const float US_AIR_FAR_M = 4500.0;
    protected static const float USSR_AIR_NEAR_M = 2000.0;
    protected static const float USSR_AIR_FAR_M = 7000.0;
    protected static const float SHELL_SPEED_COEF = 1.736;
    protected static const float SHELL_ELEVATION_DEG = 55.0;
    // Close enough to paint on the Demo PPI (zoomed ~2.5 km) but far enough
    // that launch≠radar. Must land on dry ground — see BuildFireAzimuth().
    protected static const float SHELL_LAUNCH_RANGE_M = 700.0;
    protected static const float SHELL_MIN_SURFACE_Y = 2.0;
    protected static const float SHELL_LAUNCH_HEIGHT_M = 8.0;
    protected static const float SHELL_FIRE_INTERVAL_S = 8.0;
    // Spread successive Demo rounds a few degrees so they do not sit inside
    // one another's association gate on the same radial.
    protected static const float SHELL_AZ_STAGGER_DEG = 3.0;
    protected static const int SHELL_KEEP_MAX = 4;
    // 8 km WLR PPI hides a 2 km mortar track against the core. Demo zooms in.
    protected static const float SHELL_PPI_VIEW_M = 2500.0;
    protected static const float STATUS_PRINT_S = 5.0;

    protected bool m_Running;
    protected bool m_SpawnedStation;
    protected bool m_WantAirTarget;
    protected bool m_WantShells;
    protected string m_WantMode;
    protected EGBRS_RadarFactionPreset m_WantFaction;

    protected float m_StartWallS;
    protected float m_LastStatusWallS;
    protected float m_LastFireWallS;
    protected int m_LastScanSerial;

    protected IEntity m_Subject;
    protected IEntity m_AirTarget;
    protected IEntity m_StationEntity;
    protected GBRS_RadarStationComponent m_Station;
    protected vector m_RadarOrigin;
    protected vector m_FireAzimuthFlat;
    protected float m_AirRangeNearM;
    protected float m_AirRangeFarM;
    protected bool m_PrevScanVisual;
    protected bool m_PrevStare;
    protected float m_PrevStareAzDeg;
    protected bool m_DemoOwnsStare;

    protected int m_ScanCount;
    protected int m_PlotCount;
    protected int m_DetectedCount;
    protected int m_IntervalPeakPlots;
    protected int m_IntervalPeakDet;
    protected int m_IntervalPpiEligible;
    protected int m_IntervalAirHits;
    protected int m_TrackCount;
    protected int m_TentCount;
    protected int m_IntervalPeakTrk;
    protected int m_IntervalPeakTent;
    protected int m_ShellsFired;
    protected int m_RespawnCount;
    protected float m_MaxSnrDb;
    protected float m_AirRangeRateMs;
    // Latest detected projectile (shell) plot's measurement Doppler / radial
    // speed. Reported as trackVr=/doppler= so the debugger can audit RDF's real
    // per-scan measurement chain (distinct from the air-target fallback rate).
    protected float m_ShellRangeRateMs;
    protected float m_ShellDopplerHz;
    protected int m_ShellLastScanSerial = -1;
    protected float m_ShellLastPlotTime = -1.0;

    protected ref array<IEntity> m_LiveShells;

    //------------------------------------------------------------------------------------------------
    static GBRS_RadarStationDemo GetInstance()
    {
        if (!s_Instance)
            s_Instance = new GBRS_RadarStationDemo();
        return s_Instance;
    }

    static void Start()
    {
        GetInstance().StartInternal(
            EGBRS_RadarFactionPreset.US,
            GBRS_RadarStationConstants.MODE_PD_SEARCH,
            true,
            false);
    }

    static void StartUssr()
    {
        GetInstance().StartInternal(
            EGBRS_RadarFactionPreset.USSR,
            GBRS_RadarStationConstants.MODE_PD_SEARCH,
            true,
            false);
    }

    static void StartWlr()
    {
        GetInstance().StartInternal(
            EGBRS_RadarFactionPreset.US,
            GBRS_RadarStationConstants.MODE_WLR,
            false,
            true);
    }

    static void StartLock()
    {
        // Reserved: no matching fire-control addon yet.
        // GetInstance().StartInternal(
        //     EGBRS_RadarFactionPreset.US,
        //     GBRS_RadarStationConstants.MODE_LOCK,
        //     true,
        //     false);
        Print("[GBRS Demo] StartLock() reserved.", LogLevel.WARNING);
    }

    static void Stop()
    {
        GBRS_RadarStationDemo inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal();
        Print("[GBRS Demo] stopped.");
    }

    static bool IsRunning()
    {
        GBRS_RadarStationDemo inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    static void Probe()
    {
        GetInstance().ProbeInternal();
    }

    protected static void StaticTick()
    {
        GBRS_RadarStationDemo inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected static void StaticOpenPpi()
    {
        GBRS_RadarStationDemo inst = GetInstance();
        if (!inst)
            return;
        inst.OpenPpi();
    }

    //------------------------------------------------------------------------------------------------
    protected void StartInternal(
        EGBRS_RadarFactionPreset faction,
        string mode,
        bool wantAir,
        bool wantShells)
    {
        if (m_Running)
            StopInternal();

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[GBRS Demo] no world.", LogLevel.WARNING);
            return;
        }

        if (!GetGame().InPlayMode())
        {
            Print("[GBRS Demo] start from Workbench Play, not the editor pause.", LogLevel.WARNING);
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[GBRS Demo] no local subject. Close GM free-cam or possess a character.", LogLevel.WARNING);
            return;
        }

        m_WantFaction = faction;
        m_WantMode = mode;
        m_WantAirTarget = wantAir;
        m_WantShells = wantShells;
        m_SpawnedStation = false;
        m_Station = null;
        m_StationEntity = null;
        m_AirTarget = null;
        m_LiveShells = new array<IEntity>();
        m_ScanCount = 0;
        m_PlotCount = 0;
        m_DetectedCount = 0;
        m_IntervalPeakPlots = 0;
        m_IntervalPeakDet = 0;
        m_IntervalPpiEligible = 0;
        m_IntervalAirHits = 0;
        m_TrackCount = 0;
        m_TentCount = 0;
        m_IntervalPeakTrk = 0;
        m_IntervalPeakTent = 0;
        m_ShellsFired = 0;
        m_RespawnCount = 0;
        m_MaxSnrDb = -300.0;
        m_AirRangeRateMs = 0.0;
        m_ShellRangeRateMs = 0.0;
        m_ShellDopplerHz = 0.0;
        m_ShellLastScanSerial = -1;
        m_ShellLastPlotTime = -1.0;
        m_LastScanSerial = -1;

        if (!ResolveOrSpawnStation())
        {
            Print("[GBRS Demo] failed to resolve/spawn GBRS station.", LogLevel.ERROR);
            return;
        }

        m_RadarOrigin = m_Station.GetScanOriginWorld();
        if (m_RadarOrigin.LengthSq() < 0.01)
            m_RadarOrigin = m_StationEntity.GetOrigin();

        ApplyAirPathForStation();
        BuildFireAzimuth();

        m_PrevScanVisual = m_Station.IsScanVisualEnabled();
        m_PrevStare = m_Station.IsAntennaStare();
        m_PrevStareAzDeg = m_Station.GetAntennaStareAzDeg();
        m_DemoOwnsStare = false;
        m_Station.SetScanVisualEnabled(DEBUG_VISUALS);
        if (!m_Station.SetDesiredWorkstationMode(m_WantMode))
            Print("[GBRS Demo] SetDesiredWorkstationMode failed.", LogLevel.WARNING);
        m_Station.SetPoweredForAutoTest(true);
        if (!m_Station.IsPowered())
        {
            Print("[GBRS Demo] station failed to power on.", LogLevel.ERROR);
            CleanupSpawnedStation();
            return;
        }

        RDF_RadarSensor sensor = GetStationSensor();
        if (sensor)
            sensor.SetForceLocalScan(true);

        if (!m_Station.ApplyWorkstationMode(m_WantMode))
            Print("[GBRS Demo] ApplyWorkstationMode failed; leaving current mode.", LogLevel.WARNING);

        m_StartWallS = System.GetTickCount() * 0.001;
        m_LastStatusWallS = m_StartWallS;
        m_LastFireWallS = m_StartWallS - SHELL_FIRE_INTERVAL_S;

        if (m_WantAirTarget)
        {
            if (!SpawnAirTarget())
            {
                Print("[GBRS Demo] failed to spawn air target.", LogLevel.ERROR);
                StopInternal();
                return;
            }

            if (m_Station.SetAntennaStare(true, AirTargetAzimuthDeg()))
            {
                m_DemoOwnsStare = true;
                Print("[GBRS Demo] antenna stared RDF az="
                    + AirTargetAzimuthDeg().ToString()
                    + " deg (0=east, 90=north) on the Mi-8 radial.");
            }
            else
            {
                Print("[GBRS Demo] stare failed; mechanical scan will only paint for a few tens of ms per revolution.", LogLevel.WARNING);
            }
        }
        else if (m_WantShells)
        {
            // WLR sweeps back and forth within a narrow sector (cold-war
            // counter-battery behavior). Centre that sector on the mortar line.
            float fireAz = FireAzimuthDeg();
            m_Station.SetWlrSectorCenterDeg(fireAz);
            Print("[GBRS Demo] WLR sector-sweep centred at az="
                + fireAz.ToString()
                + " deg (0=east, 90=north) on the mortar line.");
        }

        m_Running = true;
        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, TICK_MS, true);
        }

        GetGame().GetCallqueue().CallLater(StaticOpenPpi, OPEN_PPI_DELAY_MS, false);

        if (DEBUG_VISUALS)
        {
            RDF_RadarAutoTestMapOverlay.Start();
            if (m_AirTarget)
                RDF_RadarAutoTestMapOverlay.SetAircraft(m_AirTarget, "GBRS Demo");
        }

        Print(string.Format(
            "[GBRS Demo] started faction=%1 mode=%2 origin=%3 stationSpawned=%4",
            FactionLabel(m_WantFaction),
            m_WantMode,
            m_RadarOrigin.ToString(),
            m_SpawnedStation.ToString()));
        Print("[GBRS Demo] PPI opens on the station HUD. Up/Dn zooms display range. Probe() / Stop() from the debugger.");
        if (m_WantAirTarget)
            Print("[GBRS Demo] Mi-8 flies radial in/out so PD MTI can paint |vr|>=3 m/s.");
        if (m_WantShells)
        {
            Print("[GBRS Demo] 82 mm O832DU every "
                + SHELL_FIRE_INTERVAL_S.ToString()
                + " s, "
                + SHELL_LAUNCH_RANGE_M.ToString()
                + " m out. Ammo mesh is tiny; orange debug spheres mark each round.");
            // Do not Launch from the debugger Start() frame. The first
            // StaticTick (~TICK_MS) fires because m_LastFireWallS is already
            // armed to SHELL_FIRE_INTERVAL_S in the past.
        }
    }

    protected void StopInternal()
    {
        m_Running = false;
        RDF_RadarAutoTestMapOverlay.ClearAircraft();
        RDF_RadarAutoTestMapOverlay.Stop();

        if (m_Station)
            GBRS_RadarStationMenu.CloseIfBound(m_Station);

        if (m_AirTarget)
        {
            RDF_RadarScattererRegistry.Unregister(m_AirTarget);
            SCR_EntityHelper.DeleteEntityAndChildren(m_AirTarget);
            m_AirTarget = null;
        }

        DeleteLiveShells();

        if (m_Station)
        {
            m_Station.SetScanVisualEnabled(m_PrevScanVisual);
            if (m_DemoOwnsStare)
            {
                if (m_PrevStare)
                    m_Station.SetAntennaStare(true, m_PrevStareAzDeg);
                else
                    m_Station.SetAntennaStare(false, 0.0);
                m_DemoOwnsStare = false;
            }
            if (m_SpawnedStation)
            {
                m_Station.SetPoweredForAutoTest(false);
                CleanupSpawnedStation();
            }
        }

        m_Station = null;
        m_StationEntity = null;
        m_Subject = null;
    }

    protected void OpenPpi()
    {
        if (!m_Running)
            return;
        if (!m_Station)
            return;
        if (!m_Station.IsPowered())
        {
            Print("[GBRS Demo] PPI skipped: station not powered.", LogLevel.WARNING);
            return;
        }

        GBRS_RadarStationMenu.OpenFor(m_Station);
        if (m_WantShells)
            GBRS_RadarStationMenu.SetOpenMenuPpiViewRange(SHELL_PPI_VIEW_M);
        Print("[GBRS Demo] opened GBRS PPI.");
    }

    //------------------------------------------------------------------------------------------------
    protected void OnTick()
    {
        if (!m_Running)
            return;

        float nowS = System.GetTickCount() * 0.001;
        if (m_Station)
            m_RadarOrigin = m_Station.GetScanOriginWorld();

        if (m_WantAirTarget)
            UpdateAirTargetMotion();

        if (m_DemoOwnsStare && m_Station && m_AirTarget)
            KeepStareOnAirTarget();

        if (m_WantShells)
        {
            if ((nowS - m_LastFireWallS) >= SHELL_FIRE_INTERVAL_S)
            {
                if (TryFireShell())
                    m_LastFireWallS = nowS;
            }
            PruneLiveShells();
            if (DEBUG_VISUALS)
                DrawLiveShellMarkers();
        }

        AccumulateLatestScan();

        if (DEBUG_VISUALS && m_AirTarget)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_AirTarget, "GBRS Demo");

        if ((nowS - m_LastStatusWallS) >= STATUS_PRINT_S)
        {
            m_LastStatusWallS = nowS;
            PrintStatus();
        }
    }

    protected void AccumulateLatestScan()
    {
        RDF_RadarSensor sensor = GetStationSensor();
        if (!sensor)
            return;

        int serial = sensor.GetScanSerial();
        if (serial == m_LastScanSerial)
            return;

        m_LastScanSerial = serial;
        m_ScanCount = m_ScanCount + 1;

        array<ref RDF_RadarTarget> plots = sensor.GetPlots();
        m_PlotCount = 0;
        m_DetectedCount = 0;
        if (plots)
        {
            m_PlotCount = plots.Count();
            if (m_PlotCount > m_IntervalPeakPlots)
                m_IntervalPeakPlots = m_PlotCount;

            RDF_RadarSettings settings = sensor.GetSettings();
            foreach (RDF_RadarTarget t : plots)
            {
                if (!t)
                    continue;
                if (t.m_SnrDb > m_MaxSnrDb)
                    m_MaxSnrDb = t.m_SnrDb;
                if (t.m_Detected)
                    m_DetectedCount = m_DetectedCount + 1;
                if (GBRS_RadarStationConfig.ShouldDisplayPlot(t, settings))
                    m_IntervalPpiEligible = m_IntervalPpiEligible + 1;
                if (m_AirTarget && IsPlotNearEntity(t, m_AirTarget, m_RadarOrigin, 350.0))
                    m_IntervalAirHits = m_IntervalAirHits + 1;

                // Audit the REAL per-scan measurement Doppler / radial speed for
                // the latest projectile (shell) plot. RDF_RadarTrack does not
                // retain m_DopplerHz, so we sample it here from the scan's plots
                // (distinct from the air-target fallback `airVr`).
                if (t.m_Detected
                    && t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
                {
                    if (m_ShellLastScanSerial != serial || t.m_Time > m_ShellLastPlotTime)
                    {
                        m_ShellLastScanSerial = serial;
                        m_ShellLastPlotTime = t.m_Time;
                        m_ShellRangeRateMs = t.m_RadialSpeedMs;
                        m_ShellDopplerHz = t.m_DopplerHz;
                    }
                }
            }

            if (m_DetectedCount > m_IntervalPeakDet)
                m_IntervalPeakDet = m_DetectedCount;
        }

        CountLiveTracks(sensor);
    }

    protected void CountLiveTracks(RDF_RadarSensor sensor)
    {
        m_TrackCount = 0;
        m_TentCount = 0;
        if (!sensor)
            return;

        RDF_RadarProjectileTracker tracker = sensor.GetTracker();
        if (!tracker)
            return;

        array<ref RDF_RadarTrack> tracks = tracker.GetAllTracks();
        if (!tracks)
            return;

        int i = 0;
        while (i < tracks.Count())
        {
            RDF_RadarTrack tr = tracks.Get(i);
            i = i + 1;
            if (!tr)
                continue;
            if (tr.m_Confirmed)
                m_TrackCount = m_TrackCount + 1;
            else
                m_TentCount = m_TentCount + 1;
        }

        if (m_TrackCount > m_IntervalPeakTrk)
            m_IntervalPeakTrk = m_TrackCount;
        if (m_TentCount > m_IntervalPeakTent)
            m_IntervalPeakTent = m_TentCount;
    }

    protected void PrintStatus()
    {
        string airLabel = "none";
        if (m_AirTarget)
        {
            vector pos = m_AirTarget.GetOrigin();
            float dx = pos[0] - m_RadarOrigin[0];
            float dy = pos[1] - m_RadarOrigin[1];
            float dz = pos[2] - m_RadarOrigin[2];
            float dist = Math.Sqrt(dx * dx + dy * dy + dz * dz);
            airLabel = dist.ToString();
        }

        float heliAz = AirTargetAzimuthDeg();
        float scanAz = 0.0;
        if (m_Station)
            scanAz = m_Station.GetLiveScanAngleDeg();
        float dAz = heliAz - scanAz;
        while (dAz > 180.0)
            dAz = dAz - 360.0;
        while (dAz < -180.0)
            dAz = dAz + 360.0;
        string stareFlag = "0";
        if (m_Station && m_Station.IsAntennaStare())
            stareFlag = "1";

        Print("[GBRS Demo] scans=" + m_ScanCount.ToString()
            + " nowPlots=" + m_PlotCount.ToString()
            + " peakPlots=" + m_IntervalPeakPlots.ToString()
            + " peakDet=" + m_IntervalPeakDet.ToString()
            + " ppi=" + m_IntervalPpiEligible.ToString()
            + " airHits=" + m_IntervalAirHits.ToString()
            + " maxSnr=" + m_MaxSnrDb.ToString()
            + " airRange=" + airLabel
            + " airVr=" + m_AirRangeRateMs.ToString()
            + " trackVr=" + m_ShellRangeRateMs.ToString()
            + " doppler=" + m_ShellDopplerHz.ToString()
            + " scanAz=" + scanAz.ToString()
            + " heliAz=" + heliAz.ToString()
            + " dAz=" + dAz.ToString()
            + " stare=" + stareFlag
            + " trk=" + m_TrackCount.ToString()
            + " tent=" + m_TentCount.ToString()
            + " peakTrk=" + m_IntervalPeakTrk.ToString()
            + " shells=" + m_ShellsFired.ToString()
            + " " + RDF_RadarScattererRegistry.GetStatsLine());

        m_IntervalPeakPlots = 0;
        m_IntervalPeakDet = 0;
        m_IntervalPpiEligible = 0;
        m_IntervalAirHits = 0;
        m_IntervalPeakTrk = 0;
        m_IntervalPeakTent = 0;
    }

    protected void ProbeInternal()
    {
        if (!m_Station)
        {
            Print("[GBRS Demo] Probe FAIL: demo not started.", LogLevel.WARNING);
            return;
        }

        RDF_RadarSensor sensor = GetStationSensor();
        if (!sensor)
        {
            Print("[GBRS Demo] Probe FAIL: no station sensor.", LogLevel.WARNING);
            return;
        }

        IEntity owner = m_Station.GetOwner();
        BaseWorld world = GetGame().GetWorld();
        float worldTimeS = 0.0;
        if (world)
            worldTimeS = world.GetWorldTime() * 0.001;

        sensor.SetForceLocalScan(true);
        sensor.SetEnabled(true);
        if (owner)
            sensor.ScanOnce(owner, null, worldTimeS);

        array<ref RDF_RadarTarget> plots = sensor.GetPlots();
        int plotN = 0;
        int detN = sensor.CountDetectedPlots();
        float maxSnr = -300.0;
        if (plots)
        {
            plotN = plots.Count();
            foreach (RDF_RadarTarget t : plots)
            {
                if (!t)
                    continue;
                if (t.m_SnrDb > maxSnr)
                    maxSnr = t.m_SnrDb;
            }
        }

        string mode = m_Station.GetWorkstationMode();
        Print(string.Format(
            "[GBRS Demo] Probe mode=%1 powered=%2 plots=%3 det=%4 maxSnr=%5 serial=%6 status=%7",
            mode,
            m_Station.IsPowered().ToString(),
            plotN.ToString(),
            detN.ToString(),
            maxSnr.ToString(),
            sensor.GetScanSerial().ToString(),
            sensor.GetStatusShort()));

        if (!plots)
            return;

        int printed = 0;
        foreach (RDF_RadarTarget t : plots)
        {
            if (!t)
                continue;
            if (printed >= 8)
                break;
            Print(string.Format(
                "[GBRS Demo] plot det=%1 snr=%2 dist=%3 type=%4 los=%5 beam=%6",
                t.m_Detected.ToString(),
                t.m_SnrDb.ToString(),
                t.m_Distance.ToString(),
                t.m_Type.ToString(),
                t.m_LosBlocked.ToString(),
                t.m_BeamName));
            printed = printed + 1;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected RDF_RadarSensor GetStationSensor()
    {
        if (!m_Station)
            return null;
        RDF_RadarComponent radar = m_Station.GetRadarComponent();
        if (!radar)
            return null;
        return radar.GetSensor();
    }

    protected string FactionLabel(EGBRS_RadarFactionPreset faction)
    {
        if (faction == EGBRS_RadarFactionPreset.USSR)
            return "USSR";
        return "US";
    }

    protected void ApplyAirPathForStation()
    {
        m_AirRangeNearM = US_AIR_NEAR_M;
        m_AirRangeFarM = US_AIR_FAR_M;
        if (!m_Station)
            return;
        if (m_Station.GetFactionPreset() == EGBRS_RadarFactionPreset.USSR)
        {
            m_AirRangeNearM = USSR_AIR_NEAR_M;
            m_AirRangeFarM = USSR_AIR_FAR_M;
        }
    }

    protected void BuildFireAzimuth()
    {
        // Default north (+Z). Subject-relative aim often pointed into the sea
        // when the GM/editor camera sat north of the station (launch Y≈-2).
        m_FireAzimuthFlat = Vector(0.0, 0.0, 1.0);

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;

        // Prefer: away from the local subject (mortar "behind" the player),
        // then compass probes until the launch pad is dry land.
        array<vector> candidates = new array<vector>();
        if (m_Subject)
        {
            vector delta = m_RadarOrigin - m_Subject.GetOrigin();
            delta[1] = 0.0;
            float len = delta.Length();
            if (len >= 1.0)
                candidates.Insert(delta * (1.0 / len));
        }

        candidates.Insert(Vector(0.0, 0.0, 1.0));   // north
        candidates.Insert(Vector(0.707, 0.0, 0.707)); // NE
        candidates.Insert(Vector(-0.707, 0.0, 0.707)); // NW
        candidates.Insert(Vector(1.0, 0.0, 0.0));    // east
        candidates.Insert(Vector(-1.0, 0.0, 0.0));   // west
        candidates.Insert(Vector(0.707, 0.0, -0.707)); // SE
        candidates.Insert(Vector(-0.707, 0.0, -0.707)); // SW
        candidates.Insert(Vector(0.0, 0.0, -1.0));  // south

        int i = 0;
        while (i < candidates.Count())
        {
            vector dir = candidates.Get(i);
            i = i + 1;
            if (IsDryFireCorridor(world, dir))
            {
                m_FireAzimuthFlat = dir;
                float sy = SampleLaunchSurfaceY(world, dir);
                Print("[GBRS Demo] mortar line az="
                    + DirToRdfAzDeg(dir).ToString()
                    + " deg launchSurfY=" + sy.ToString());
                return;
            }
        }

        Print("[GBRS Demo] no dry mortar corridor found; using north (shells may ditch).", LogLevel.WARNING);
    }

    protected float DirToRdfAzDeg(vector dir)
    {
        float az = Math.Atan2(dir[2], dir[0]) * Math.RAD2DEG;
        if (az < 0.0)
            az = az + 360.0;
        while (az >= 360.0)
            az = az - 360.0;
        return az;
    }

    protected float SampleLaunchSurfaceY(BaseWorld world, vector dirFlat)
    {
        if (!world)
            return -1000.0;

        vector p = m_RadarOrigin
            + Vector(dirFlat[0] * SHELL_LAUNCH_RANGE_M, 0.0, dirFlat[2] * SHELL_LAUNCH_RANGE_M);
        return world.GetSurfaceY(p[0], p[2]);
    }

    // Launch pad + downrange mid-point must be above sea / void so the 82 mm
    // round actually flies in the WLR beam instead of spawning underwater.
    protected bool IsDryFireCorridor(BaseWorld world, vector dirFlat)
    {
        if (!world)
            return false;

        float launchY = SampleLaunchSurfaceY(world, dirFlat);
        if (launchY < SHELL_MIN_SURFACE_Y)
            return false;

        float radarY = m_RadarOrigin[1];
        if (launchY < radarY - 40.0)
            return false;

        float midRange = SHELL_LAUNCH_RANGE_M + 900.0;
        vector mid = m_RadarOrigin
            + Vector(dirFlat[0] * midRange, 0.0, dirFlat[2] * midRange);
        float midY = world.GetSurfaceY(mid[0], mid[2]);
        if (midY < SHELL_MIN_SURFACE_Y)
            return false;

        return true;
    }

    protected float FireAzimuthDeg()
    {
        return DirToRdfAzDeg(m_FireAzimuthFlat);
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
            Print("[GBRS Demo] using existing station at "
                + m_StationEntity.GetOrigin().ToString());
            s_QueryFoundStation = null;
            return true;
        }

        return SpawnStationNearSubject();
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
        GBRS_RadarStationDemo inst = s_Instance;
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

    protected bool SpawnStationNearSubject()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world || !m_Subject)
            return false;

        ResourceName prefab = GBRS_RadarStationConstants.PREFAB_ROOT_US;
        if (m_WantFaction == EGBRS_RadarFactionPreset.USSR)
            prefab = GBRS_RadarStationConstants.PREFAB_ROOT_USSR;

        Resource prefabRes = Resource.Load(prefab);
        if (!prefabRes)
        {
            Print("[GBRS Demo] failed to load station prefab.", LogLevel.ERROR);
            return false;
        }

        vector spawnPos = ChooseFlatSpawnNear(m_Subject.GetOrigin(), world);
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
            Print("[GBRS Demo] spawned prefab missing GBRS_RadarStationComponent.", LogLevel.ERROR);
            SCR_EntityHelper.DeleteEntityAndChildren(m_StationEntity);
            m_StationEntity = null;
            return false;
        }

        m_SpawnedStation = true;
        Print("[GBRS Demo] spawned station at " + spawnPos.ToString());
        return true;
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

    protected vector ChooseFlatSpawnNear(vector center, BaseWorld world)
    {
        vector best = Vector(center[0] + 40.0, center[1], center[2] + 40.0);
        float bestScore = 9999999.0;
        float searchRadius = 80.0;
        float sampleOffset = 12.0;

        int i = 0;
        while (i < 12)
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
            i = i + 1;
        }

        return best;
    }

    //------------------------------------------------------------------------------------------------
    protected float GetElapsedS()
    {
        if (m_StartWallS <= 0.0)
            return 0.0;
        float elapsed = System.GetTickCount() * 0.001 - m_StartWallS;
        if (elapsed < 0.0)
            return 0.0;
        return elapsed;
    }

    protected void GetAirRangeState(float elapsedS, out float rangeM, out float vrMs)
    {
        float nearM = m_AirRangeNearM;
        float farM = m_AirRangeFarM;
        if (farM <= nearM)
            farM = nearM + 1000.0;

        float cycle = AIR_LEG_S * 2.0;
        float t = elapsedS;
        while (t >= cycle)
            t = t - cycle;

        float span = farM - nearM;
        float speed = span / AIR_LEG_S;
        if (t < AIR_LEG_S)
        {
            rangeM = nearM + span * (t / AIR_LEG_S);
            vrMs = speed;
            return;
        }

        float back = t - AIR_LEG_S;
        rangeM = farM - span * (back / AIR_LEG_S);
        vrMs = -speed;
    }

    // RDF mechanical scan angle: GetScanForward = (cos(az), 0, sin(az)).
    // 0 deg = +X east, 90 deg = +Z north. PPI north-up is the other convention.
    protected float AirTargetAzimuthDeg()
    {
        if (!m_AirTarget)
            return 90.0;

        vector delta = m_AirTarget.GetOrigin() - m_RadarOrigin;
        float az = Math.Atan2(delta[2], delta[0]) * Math.RAD2DEG;
        if (az < 0.0)
            az = az + 360.0;
        while (az >= 360.0)
            az = az - 360.0;
        return az;
    }

    protected void KeepStareOnAirTarget()
    {
        float az = AirTargetAzimuthDeg();
        float cur = m_Station.GetAntennaStareAzDeg();
        float d = az - cur;
        while (d > 180.0)
            d = d - 360.0;
        while (d < -180.0)
            d = d + 360.0;
        if (d < 0.0)
            d = -d;
        if (d < 1.0)
            return;

        m_Station.SetAntennaStare(true, az);
    }

    protected vector ComputeAirPos(float elapsedS)
    {
        float rangeM;
        float vrMs;
        GetAirRangeState(elapsedS, rangeM, vrMs);
        m_AirRangeRateMs = vrMs;
        return Vector(
            m_RadarOrigin[0],
            m_RadarOrigin[1] + AIR_ALTITUDE_M,
            m_RadarOrigin[2] + rangeM);
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
        return true;
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
        vector spawnPos = ComputeAirPos(GetElapsedS());
        spawnParams.Transform[3] = spawnPos;

        m_AirTarget = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!m_AirTarget)
            return false;

        Print("[GBRS Demo] spawned Mi-8 at " + spawnPos.ToString());
        return true;
    }

    protected void UpdateAirTargetMotion()
    {
        if (!m_AirTarget)
        {
            if (SpawnAirTarget())
            {
                m_RespawnCount = m_RespawnCount + 1;
                Print("[GBRS Demo] air target respawned count=" + m_RespawnCount.ToString());
            }
            return;
        }

        float elapsedS = GetElapsedS();
        float rangeM;
        float vrMs;
        GetAirRangeState(elapsedS, rangeM, vrMs);
        m_AirRangeRateMs = vrMs;
        vector pos = ComputeAirPos(elapsedS);
        m_AirTarget.SetOrigin(pos);

        Physics physics = m_AirTarget.GetPhysics();
        if (physics)
            physics.SetVelocity(Vector(0.0, 0.0, vrMs));

        if (DEBUG_VISUALS)
        {
            int markerFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
            Shape.CreateSphere(ARGBF(1, 0.2, 1, 0.2), markerFlags, pos, 6.0);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected vector BuildLaunchDirection()
    {
        vector flat = FireAzimuthFlatForShot(m_ShellsFired);
        float elevRad = SHELL_ELEVATION_DEG * Math.DEG2RAD;
        float c = Math.Cos(elevRad);
        float s = Math.Sin(elevRad);
        return Vector(
            flat[0] * c,
            s,
            flat[2] * c);
    }

    // Rotate the mortar line by ±stagger so barrage rounds are not clones on
    // one RDF az radial (which the tracker would merge).
    protected vector FireAzimuthFlatForShot(int shellIndex)
    {
        float stagger = 0.0;
        if (SHELL_AZ_STAGGER_DEG > 0.0)
        {
            int slot = shellIndex % 5;
            stagger = (slot - 2) * SHELL_AZ_STAGGER_DEG;
        }

        float baseAz = DirToRdfAzDeg(m_FireAzimuthFlat);
        float azRad = (baseAz + stagger) * Math.DEG2RAD;
        return Vector(Math.Cos(azRad), 0.0, Math.Sin(azRad));
    }

    protected bool TryFireShell()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        Resource prefabRes = Resource.Load(SHELL_PREFAB);
        if (!prefabRes || !prefabRes.IsValid())
        {
            Print("[GBRS Demo] failed to load shell prefab.", LogLevel.ERROR);
            return false;
        }

        vector flat = FireAzimuthFlatForShot(m_ShellsFired);
        vector launchPos = m_RadarOrigin
            + Vector(
                flat[0] * SHELL_LAUNCH_RANGE_M,
                0.0,
                flat[2] * SHELL_LAUNCH_RANGE_M);
        float surfaceY = world.GetSurfaceY(launchPos[0], launchPos[2]);
        launchPos[1] = surfaceY + SHELL_LAUNCH_HEIGHT_M;

        if (surfaceY < SHELL_MIN_SURFACE_Y)
        {
            Print("[GBRS Demo] refuse shell: launch surfY="
                + surfaceY.ToString()
                + " (sea/void). Rebuild mortar azimuth.", LogLevel.WARNING);
            BuildFireAzimuth();
            return false;
        }

        vector dir = BuildLaunchDirection();
        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = launchPos;

        IEntity shell = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!shell)
        {
            Print("[GBRS Demo] SpawnEntityPrefab failed for 82 mm shell.", LogLevel.ERROR);
            return false;
        }

        vector basis[4];
        Math3D.DirectionAndUpMatrix(dir, vector.Up, basis);
        basis[3] = launchPos;
        shell.SetTransform(basis);

        ProjectileMoveComponent move = ProjectileMoveComponent.Cast(
            shell.FindComponent(ProjectileMoveComponent));
        if (!move)
        {
            Print("[GBRS Demo] shell missing ProjectileMoveComponent.", LogLevel.ERROR);
            SCR_EntityHelper.DeleteEntityAndChildren(shell);
            return false;
        }

        // Cartridge shells stay inert until simulation is enabled. Match
        // RDF_RadarShellFireAutoTest: Launch gunner must be null. Passing the
        // GM / editor subject as instigator native-crashes on the next Frame.
        move.EnableSimulation(shell);
        move.SetBulletCoef(SHELL_SPEED_COEF);
        move.Launch(dir, vector.Zero, 1.0, shell, null, null, null, null);
        RDF_RadarScattererRegistry.Unignore(shell);

        if (!m_LiveShells)
            m_LiveShells = new array<IEntity>();
        m_LiveShells.Insert(shell);
        m_ShellsFired = m_ShellsFired + 1;
        Print("[GBRS Demo] fired 82 mm shell #" + m_ShellsFired.ToString()
            + " rdfAz=" + DirToRdfAzDeg(flat).ToString()
            + " from " + launchPos.ToString());
        return true;
    }

    protected void DrawLiveShellMarkers()
    {
        if (!m_LiveShells)
            return;

        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
        int i = 0;
        while (i < m_LiveShells.Count())
        {
            IEntity shell = m_LiveShells.Get(i);
            i = i + 1;
            if (!shell)
                continue;
            Shape.CreateSphere(ARGBF(1, 1, 0.35, 0.08), flags, shell.GetOrigin(), 3.0);
        }
    }

    protected void PruneLiveShells()
    {
        if (!m_LiveShells)
            return;

        int i = m_LiveShells.Count() - 1;
        while (i >= 0)
        {
            IEntity shell = m_LiveShells.Get(i);
            if (!shell)
                m_LiveShells.Remove(i);
            i = i - 1;
        }

        while (m_LiveShells.Count() > SHELL_KEEP_MAX)
        {
            IEntity oldShell = m_LiveShells.Get(0);
            m_LiveShells.Remove(0);
            if (oldShell)
            {
                RDF_RadarScattererRegistry.Unregister(oldShell);
                SCR_EntityHelper.DeleteEntityAndChildren(oldShell);
            }
        }
    }

    protected void DeleteLiveShells()
    {
        if (!m_LiveShells)
            return;

        int i = 0;
        while (i < m_LiveShells.Count())
        {
            IEntity shell = m_LiveShells.Get(i);
            i = i + 1;
            if (!shell)
                continue;
            RDF_RadarScattererRegistry.Unregister(shell);
            SCR_EntityHelper.DeleteEntityAndChildren(shell);
        }
        m_LiveShells.Clear();
    }
}
