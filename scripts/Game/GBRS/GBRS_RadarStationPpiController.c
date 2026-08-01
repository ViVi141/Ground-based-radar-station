// Local PPI session for a powered GBRS radar station.
// Feeds RDF_RadarHUD from the bound station sensor; closes on E / Esc /
// power-off / destruction / delete.
// Holds phosphor-style afterglow so plots survive between RDF dwell clears.
class GBRS_RadarStationPpiController
{
    protected static const int FEED_INTERVAL_MS = 50;
    protected static const int PERSIST_MAX_BLIPS = 512;
    protected static const float PERSIST_SEC_MIN = 2.5;
    protected static const float PERSIST_SEC_MAX = 12.0;
    // UH-1 airframe + rotor parts sit tens of meters apart; merge for PPI.
    protected static const float DISPLAY_CLUSTER_M = 120.0;
    // Default PC binding: E = CharacterLeanRight.
    protected static const string CLOSE_ACTION_E = "CharacterLeanRight";
    protected static const string CLOSE_ACTION_BACK = "MenuBack";

    protected static ref GBRS_RadarStationPpiController s_Instance;

    protected GBRS_RadarStationComponent m_Station;
    protected bool m_bActive;
    protected bool m_bListening;
    protected bool m_bFeedScheduled;
    protected ref array<ref RDF_RadarTarget> m_PersistPlots;
    protected ref array<ref RDF_RadarTarget> m_DisplayPlots;

    static GBRS_RadarStationPpiController GetInstance()
    {
        if (!s_Instance)
            s_Instance = new GBRS_RadarStationPpiController();

        return s_Instance;
    }

    static void OpenFor(GBRS_RadarStationComponent station)
    {
        GetInstance().Open(station);
    }

    static void CloseIfBound(GBRS_RadarStationComponent station)
    {
        GetInstance().CloseBound(station);
    }

    static bool IsOpenFor(GBRS_RadarStationComponent station)
    {
        return GetInstance().IsBoundTo(station);
    }

    bool IsBoundTo(GBRS_RadarStationComponent station)
    {
        if (!m_bActive)
            return false;

        return m_Station == station;
    }

    void Open(GBRS_RadarStationComponent station)
    {
        if (!station)
            return;

        if (!station.IsPowered())
            return;

        if (m_bActive && m_Station == station)
        {
            FeedOnce();
            return;
        }

        CloseInternal();

        m_Station = station;
        m_bActive = true;
        EnsurePersistBuffer();
        ClearPersist();

        RDF_RadarHUD.Show();
        RDF_RadarHUD.SetMode("PD | E");

        StartListeners();
        StartFeed();
        FeedOnce();
    }

    void CloseBound(GBRS_RadarStationComponent station)
    {
        if (!m_bActive)
            return;

        if (m_Station != station)
            return;

        CloseInternal();
    }

    void Close()
    {
        CloseInternal();
    }

    protected void CloseInternal()
    {
        StopListeners();
        StopFeed();
        ClearPersist();

        if (RDF_RadarHUD.IsVisible())
            RDF_RadarHUD.Hide();

        m_Station = null;
        m_bActive = false;
    }

    protected void OnCloseInput()
    {
        CloseInternal();
    }

    protected void StartListeners()
    {
        if (m_bListening)
            return;

        InputManager inputManager = GetGame().GetInputManager();
        if (!inputManager)
            return;

        inputManager.AddActionListener(CLOSE_ACTION_E, EActionTrigger.DOWN, OnCloseInput);
        inputManager.AddActionListener(CLOSE_ACTION_BACK, EActionTrigger.DOWN, OnCloseInput);
        m_bListening = true;
    }

    protected void StopListeners()
    {
        if (!m_bListening)
            return;

        InputManager inputManager = GetGame().GetInputManager();
        if (inputManager)
        {
            inputManager.RemoveActionListener(CLOSE_ACTION_E, EActionTrigger.DOWN, OnCloseInput);
            inputManager.RemoveActionListener(CLOSE_ACTION_BACK, EActionTrigger.DOWN, OnCloseInput);
        }

        m_bListening = false;
    }

    protected void StartFeed()
    {
        if (m_bFeedScheduled)
            return;

        GetGame().GetCallqueue().CallLater(TickFeed, FEED_INTERVAL_MS, true);
        m_bFeedScheduled = true;
    }

    protected void StopFeed()
    {
        if (!m_bFeedScheduled)
            return;

        GetGame().GetCallqueue().Remove(TickFeed);
        m_bFeedScheduled = false;
    }

    protected void TickFeed()
    {
        if (!m_bActive)
        {
            StopFeed();
            return;
        }

        if (!CanKeepOpen())
        {
            CloseInternal();
            return;
        }

        FeedOnce();
    }

    protected bool CanKeepOpen()
    {
        if (!m_Station)
            return false;

        if (!m_Station.IsPowered())
            return false;

        if (m_Station.IsDestroyedForPpi())
            return false;

        RDF_RadarComponent radar = m_Station.GetRadarComponent();
        if (!radar)
            return false;

        if (!radar.IsEnabled())
            return false;

        return true;
    }

    protected void EnsurePersistBuffer()
    {
        if (!m_PersistPlots)
            m_PersistPlots = new array<ref RDF_RadarTarget>();
        if (!m_DisplayPlots)
            m_DisplayPlots = new array<ref RDF_RadarTarget>();
    }

    protected void ClearPersist()
    {
        if (m_PersistPlots)
            m_PersistPlots.Clear();
        if (m_DisplayPlots)
            m_DisplayPlots.Clear();
    }

    protected float GetPersistLifeS()
    {
        float rpm = 15.0;
        if (m_Station)
            rpm = m_Station.GetScanRpm();

        float life = 3.0;
        if (rpm > 0.0)
            life = 60.0 / rpm;

        // Hold a little longer than one revolution so the PPI looks continuous.
        life = life * 1.15;
        if (life < PERSIST_SEC_MIN)
            life = PERSIST_SEC_MIN;
        if (life > PERSIST_SEC_MAX)
            life = PERSIST_SEC_MAX;
        return life;
    }

    protected void IngestLivePlots(array<ref RDF_RadarTarget> live, vector origin, float nowS)
    {
        EnsurePersistBuffer();
        if (!live)
            return;

        int i = 0;
        while (i < live.Count())
        {
            RDF_RadarTarget src = live.Get(i);
            i = i + 1;
            if (!src)
                continue;
            if (!src.m_Detected)
                continue;

            RDF_RadarTarget existing = FindPersistMatch(src);
            if (existing)
            {
                CopyPlot(src, existing, nowS);
                continue;
            }

            if (m_PersistPlots.Count() >= PERSIST_MAX_BLIPS)
                RemoveOldestPersist();

            RDF_RadarTarget created = new RDF_RadarTarget();
            CopyPlot(src, created, nowS);
            m_PersistPlots.Insert(created);
        }
    }

    protected RDF_RadarTarget FindPersistMatch(RDF_RadarTarget src)
    {
        if (!src || !m_PersistPlots)
            return null;

        int i = 0;
        while (i < m_PersistPlots.Count())
        {
            RDF_RadarTarget t = m_PersistPlots.Get(i);
            i = i + 1;
            if (!t)
                continue;

            if (src.m_ScattererId > 0 && t.m_ScattererId == src.m_ScattererId)
                return t;

            vector d = t.m_Position - src.m_Position;
            if (d.LengthSq() < 4.0)
                return t;
        }

        return null;
    }

    protected void RemoveOldestPersist()
    {
        if (!m_PersistPlots || m_PersistPlots.Count() == 0)
            return;

        int oldest = 0;
        float oldestTime = 1.0e30;
        int i = 0;
        while (i < m_PersistPlots.Count())
        {
            RDF_RadarTarget t = m_PersistPlots.Get(i);
            if (t && t.m_Time < oldestTime)
            {
                oldestTime = t.m_Time;
                oldest = i;
            }
            i = i + 1;
        }

        m_PersistPlots.Remove(oldest);
    }

    protected void CopyPlot(RDF_RadarTarget src, RDF_RadarTarget dst, float nowS)
    {
        if (!src || !dst)
            return;

        dst.m_Entity = src.m_Entity;
        dst.m_ScattererId = src.m_ScattererId;
        dst.m_Position = src.m_Position;
        dst.m_Distance = src.m_Distance;
        dst.m_Velocity = src.m_Velocity;
        dst.m_Type = src.m_Type;
        dst.m_Time = nowS;
        dst.m_AzimuthDeg = src.m_AzimuthDeg;
        dst.m_ElevationDeg = src.m_ElevationDeg;
        dst.m_RadialSpeedMs = src.m_RadialSpeedMs;
        dst.m_RcsM2 = src.m_RcsM2;
        dst.m_MeanRcsM2 = src.m_MeanRcsM2;
        dst.m_SwerlingModel = src.m_SwerlingModel;
        dst.m_AglM = src.m_AglM;
        dst.m_DemTerrainY = src.m_DemTerrainY;
        dst.m_ReceivedPowerW = src.m_ReceivedPowerW;
        dst.m_ProcessedPowerW = src.m_ProcessedPowerW;
        dst.m_DopplerHz = src.m_DopplerHz;
        dst.m_MtiGain = src.m_MtiGain;
        dst.m_DopplerBin = src.m_DopplerBin;
        dst.m_PrfIndex = src.m_PrfIndex;
        dst.m_RotorTipSpeedMs = src.m_RotorTipSpeedMs;
        dst.m_BladeCount = src.m_BladeCount;
        dst.m_RotorRcsFraction = src.m_RotorRcsFraction;
        dst.m_HubWidthMs = src.m_HubWidthMs;
        dst.m_RotorSidebandUsed = src.m_RotorSidebandUsed;
        dst.m_DemSurfaceClass = src.m_DemSurfaceClass;
        dst.m_DemSampleValid = src.m_DemSampleValid;
        dst.m_ClutterPowerW = src.m_ClutterPowerW;
        dst.m_ClutterToNoiseDb = src.m_ClutterToNoiseDb;
        dst.m_SnrDb = src.m_SnrDb;
        dst.m_Detected = true;
        dst.m_IsAnonymous = src.m_IsAnonymous;
        dst.m_IsFalsePlot = src.m_IsFalsePlot;
        dst.m_CfarPowerW = src.m_CfarPowerW;
        dst.m_LosBlocked = src.m_LosBlocked;
        dst.m_LosHitFraction = src.m_LosHitFraction;
        dst.m_MultipathFactor = src.m_MultipathFactor;
        dst.m_EmitFrequencyHz = src.m_EmitFrequencyHz;
        dst.m_EmitPeakPowerW = src.m_EmitPeakPowerW;
        dst.m_EmitAntennaGainDbi = src.m_EmitAntennaGainDbi;
        dst.m_EmitStrength = src.m_EmitStrength;
        dst.m_BeamName = src.m_BeamName;
        dst.m_ScanNumber = src.m_ScanNumber;
    }

    protected void PrunePersist(float nowS, float lifeS)
    {
        if (!m_PersistPlots)
            return;

        int i = m_PersistPlots.Count() - 1;
        while (i >= 0)
        {
            RDF_RadarTarget t = m_PersistPlots.Get(i);
            if (!t || (nowS - t.m_Time) > lifeS)
                m_PersistPlots.Remove(i);
            i = i - 1;
        }
    }

    // Collapse airframe + attached parts into one PPI blip (120 m gate).
    protected void BuildClusteredDisplayPlots()
    {
        EnsurePersistBuffer();
        m_DisplayPlots.Clear();
        if (!m_PersistPlots || m_PersistPlots.Count() == 0)
            return;

        float gateSq = DISPLAY_CLUSTER_M * DISPLAY_CLUSTER_M;
        int i = 0;
        while (i < m_PersistPlots.Count())
        {
            RDF_RadarTarget src = m_PersistPlots.Get(i);
            i = i + 1;
            if (!src)
                continue;

            int match = -1;
            int j = 0;
            while (j < m_DisplayPlots.Count())
            {
                RDF_RadarTarget kept = m_DisplayPlots.Get(j);
                if (kept)
                {
                    vector d = kept.m_Position - src.m_Position;
                    if (d.LengthSq() <= gateSq)
                    {
                        match = j;
                        break;
                    }
                }
                j = j + 1;
            }

            if (match < 0)
            {
                RDF_RadarTarget created = new RDF_RadarTarget();
                CopyPlot(src, created, src.m_Time);
                m_DisplayPlots.Insert(created);
                continue;
            }

            RDF_RadarTarget winner = m_DisplayPlots.Get(match);
            if (!winner)
                continue;

            if (src.m_SnrDb > winner.m_SnrDb)
                CopyPlot(src, winner, src.m_Time);
        }
    }

    protected void FeedOnce()
    {
        if (!m_Station)
            return;

        RDF_RadarComponent radar = m_Station.GetRadarComponent();
        if (!radar)
            return;

        RDF_RadarSensor sensor = radar.GetSensor();
        if (!sensor)
            return;

        if (!RDF_RadarHUD.IsVisible())
            RDF_RadarHUD.Show();

        float nowS = System.GetTickCount() * 0.001;
        float lifeS = GetPersistLifeS();

        vector hudOrigin = m_Station.GetScanOriginWorld();
        // Continuous sweep — not the last dwell forward from RDF context.
        vector hudForward = m_Station.GetScanForwardWorld();

        RDF_RadarScanContext ctx = sensor.GetScanContext();
        RDF_RadarSettings settings = sensor.GetSettings();

        float hudRange = 2000.0;
        if (settings)
            hudRange = settings.m_Range;

        if (ctx)
        {
            if (ctx.m_Origin.LengthSq() > 0.0001)
                hudOrigin = ctx.m_Origin;
            if (ctx.m_RangeM > 0.0)
                hudRange = ctx.m_RangeM;
        }

        IngestLivePlots(sensor.GetPlots(), hudOrigin, nowS);
        PrunePersist(nowS, lifeS);
        BuildClusteredDisplayPlots();

        RDF_RadarHUD.SetDisplayRange(hudRange);
        RDF_RadarHUD.SetMode("PD | E");
        RDF_RadarHUD.FeedScan(
            m_DisplayPlots,
            hudOrigin,
            hudForward,
            hudRange,
            sensor.GetTracker());
    }
}
