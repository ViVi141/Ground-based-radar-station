//------------------------------------------------------------------------------------------------
//! Local operator session: bind PPI / CONTACT / OPTICS CRTs, subscribe snapshot, replace Menu path.
class GBRS_ConsoleSession
{
    static const ResourceName LAYOUT_PPI =
        "{69FCEDCEA0080001}UI/layouts/GBRS/PpiScreen.layout";
    static const ResourceName LAYOUT_CONTACT =
        "{69FCEDCEA0080002}UI/layouts/GBRS/ContactScreen.layout";

    static const int PPI_W = 288;
    static const int PPI_H = 256;
    static const int CONTACT_W = 288;
    static const int CONTACT_H = 256;
    static const int UPDATE_MS = 66;
    static const float LEAVE_DISTANCE_M = 4.5;
    static const float MODE_NAV_COOLDOWN_S = 0.22;
    static const float PPI_RANGE_MIN_M = 2000.0;
    static const int PPI_RANGE_STEP_COUNT = 6;

    static const string ACTION_TAB_PREV = "MenuTabLeft";
    static const string ACTION_TAB_NEXT = "MenuTabRight";
    static const string ACTION_ZOOM_OUT = "MenuUp";
    static const string ACTION_ZOOM_IN = "MenuDown";
    static const string ACTION_SELECT = "MenuSelect";
    static const string ACTION_BACK = "MenuBack";

    protected static ref GBRS_ConsoleSession s_Active;

    protected GBRS_RadarStationComponent m_Station;
    protected GBRS_ConsoleComponent m_Console;
    protected IEntity m_User;
    protected ref GBRS_WorldScreen m_PpiScreen;
    protected ref GBRS_WorldScreen m_ContactScreen;
    protected ref GBRS_OpticsScreen m_OpticsScreen;
    protected ref GBRS_PpiPanel m_PpiPanel;
    protected ref GBRS_ContactListPanel m_ContactPanel;
    protected bool m_bOpticsWanted;
    protected bool m_bInputsBound;
    protected bool m_bHasSnapshot;
    protected int m_iLastSnapshotSeq;
    protected vector m_SnapOrigin;
    protected float m_SnapScanAzDeg;
    protected float m_SnapRangeM;
    protected string m_SnapEccm;
    protected int m_SnapDetectedTotal;
    protected int m_SnapNetOnline;
    protected int m_SnapLockedTrackId;
    protected ref array<ref RDF_RadarTarget> m_SnapPlots;
    protected ref array<ref RDF_RadarTrack> m_SnapTracks;
    protected ref array<ref RDF_RadarFusedTrack> m_SnapFused;
    protected ref array<ref GBRS_WlrPersistDisplay> m_SnapWlr;
    protected float m_PpiViewRangeM;
    protected bool m_bPpiZoomManual;
    protected float m_fLastModeNavS;
    protected int m_iFocusedModeTab;

    //------------------------------------------------------------------------------------------------
    static GBRS_ConsoleSession GetActive()
    {
        return s_Active;
    }

    //------------------------------------------------------------------------------------------------
    static bool IsActiveFor(GBRS_RadarStationComponent station)
    {
        if (!s_Active || !station)
            return false;
        return s_Active.m_Station == station;
    }

    //------------------------------------------------------------------------------------------------
    static void StartFor(GBRS_RadarStationComponent station, IEntity user)
    {
        if (!station)
            return;
        if (!station.IsPowered())
            return;

        if (s_Active)
        {
            if (s_Active.m_Station == station)
            {
                s_Active.FeedOnce();
                return;
            }
            s_Active.Stop();
        }

        GBRS_ConsoleSession session = new GBRS_ConsoleSession();
        if (!session.Begin(station, user))
            return;

        s_Active = session;
    }

    //------------------------------------------------------------------------------------------------
    static void StopIfBound(GBRS_RadarStationComponent station)
    {
        if (!s_Active)
            return;
        if (station && s_Active.m_Station != station)
            return;
        s_Active.Stop();
    }

    //------------------------------------------------------------------------------------------------
    static void ApplyReplicatedSnapshot(
        RplId stationId,
        vector origin,
        float scanAzDeg,
        float rangeM,
        string eccm,
        int snapshotSeq,
        array<int> packedInts,
        array<float> packedFloats)
    {
        if (!s_Active)
            return;
        s_Active.StoreReplicatedSnapshot(
            stationId, origin, scanAzDeg, rangeM, eccm, snapshotSeq, packedInts, packedFloats);
    }

    //------------------------------------------------------------------------------------------------
    static void SetActivePpiViewRange(float rangeM)
    {
        if (!s_Active)
            return;
        if (rangeM <= 0.0)
            return;
        s_Active.m_PpiViewRangeM = rangeM;
        s_Active.m_bPpiZoomManual = true;
        if (s_Active.m_PpiPanel)
            s_Active.m_PpiPanel.SetDisplayRange(rangeM);
    }

    //------------------------------------------------------------------------------------------------
    protected bool Begin(GBRS_RadarStationComponent station, IEntity user)
    {
        m_Station = station;
        m_User = user;
        m_bOpticsWanted = false;
        m_bHasSnapshot = false;
        m_iLastSnapshotSeq = 0;
        m_PpiViewRangeM = 0.0;
        m_bPpiZoomManual = false;
        m_fLastModeNavS = 0.0;
        m_iFocusedModeTab = ModeToTab(station.GetWorkstationMode());

        IEntity stationRoot = station.GetOwner();
        m_Console = GBRS_ConsoleComponent.FindOnStation(stationRoot);
        if (!m_Console)
        {
            Print("[GBRS Console] no GBRS_ConsoleComponent under station", LogLevel.WARNING);
            return false;
        }

        IEntity ppiTagged = m_Console.FindScreenMesh(EGBRS_ScreenKind.PPI);
        IEntity contactTagged = m_Console.FindScreenMesh(EGBRS_ScreenKind.CONTACT);
        if (!ppiTagged || !contactTagged)
        {
            Print("[GBRS Console] PPI/CONTACT screen meshes missing", LogLevel.WARNING);
            return false;
        }

        // RDF order: LOD0 + remap, then EnableScreen (SetRenderTarget only).
        IEntity ppiMesh = GBRS_WorldScreen.PrepareScreenMesh(ppiTagged);
        IEntity contactMesh = GBRS_WorldScreen.PrepareScreenMesh(contactTagged);

        m_PpiScreen = new GBRS_WorldScreen(LAYOUT_PPI, PPI_W, PPI_H, "PpiCanvas");
        if (!m_PpiScreen.EnableScreen(ppiMesh))
        {
            CleanupScreens();
            return false;
        }

        m_ContactScreen = new GBRS_WorldScreen(LAYOUT_CONTACT, CONTACT_W, CONTACT_H, string.Empty);
        if (!m_ContactScreen.EnableScreen(contactMesh))
        {
            CleanupScreens();
            return false;
        }

        m_PpiPanel = new GBRS_PpiPanel();
        m_PpiPanel.Bind(m_PpiScreen.GetCanvas(), m_PpiScreen.GetStatus());
        m_PpiPanel.DrawIdle(station.GetWorkstationMode());

        TextWidget body = TextWidget.Cast(m_ContactScreen.GetRoot().FindAnyWidget("ContactBody"));
        TextWidget footer = TextWidget.Cast(m_ContactScreen.GetRoot().FindAnyWidget("ContactFooter"));
        m_ContactPanel = new GBRS_ContactListPanel();
        m_ContactPanel.Bind(m_ContactScreen.GetStatus(), body, footer);
        m_ContactPanel.DrawIdle(station.GetWorkstationMode());

        GBRS_PlayerControllerNet.RequestSubscribePpi(station);
        BindInputs();
        StartUpdate();
        FeedOnce();
        Print("[GBRS Console] operate session started", LogLevel.NORMAL);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    void Stop()
    {
        StopUpdate();
        UnbindInputs();
        if (m_Station)
            GBRS_PlayerControllerNet.RequestUnsubscribePpi(m_Station);
        CleanupScreens();
        ClearSnapshot();
        m_Station = null;
        m_Console = null;
        m_User = null;
        if (s_Active == this)
            s_Active = null;
        Print("[GBRS Console] operate session stopped", LogLevel.NORMAL);
    }

    //------------------------------------------------------------------------------------------------
    protected void CleanupScreens()
    {
        if (m_OpticsScreen)
        {
            m_OpticsScreen.DisableScreen();
            m_OpticsScreen = null;
        }
        if (m_PpiPanel)
        {
            m_PpiPanel.Destroy();
            m_PpiPanel = null;
        }
        if (m_ContactPanel)
        {
            m_ContactPanel.Destroy();
            m_ContactPanel = null;
        }
        if (m_PpiScreen)
        {
            m_PpiScreen.DisableScreen();
            m_PpiScreen = null;
        }
        if (m_ContactScreen)
        {
            m_ContactScreen.DisableScreen();
            m_ContactScreen = null;
        }
        m_bOpticsWanted = false;
    }

    //------------------------------------------------------------------------------------------------
    protected void StartUpdate()
    {
        ScriptCallQueue queue = GetGame().GetCallqueue();
        if (!queue)
            return;
        queue.CallLater(OnUpdate, UPDATE_MS, true);
    }

    //------------------------------------------------------------------------------------------------
    protected void StopUpdate()
    {
        ScriptCallQueue queue = GetGame().GetCallqueue();
        if (!queue)
            return;
        queue.Remove(OnUpdate);
    }

    //------------------------------------------------------------------------------------------------
    protected void OnUpdate()
    {
        if (!m_Station)
        {
            Stop();
            return;
        }
        if (!m_Station.IsPowered() || m_Station.IsDestroyed())
        {
            Stop();
            return;
        }
        if (!IsUserNearConsole())
        {
            Stop();
            return;
        }

        FeedOnce();
        if (m_OpticsScreen && m_OpticsScreen.IsEnabled())
            m_OpticsScreen.UpdatePose();
    }

    //------------------------------------------------------------------------------------------------
    protected bool IsUserNearConsole()
    {
        if (!m_Console)
            return false;

        IEntity controlled = null;
        PlayerController pc = GetGame().GetPlayerController();
        if (pc)
            controlled = pc.GetControlledEntity();
        if (!controlled)
            controlled = m_User;

        // Demo / script path may start without a user entity — keep session alive.
        if (!controlled)
            return true;

        IEntity consoleEnt = m_Console.GetOwner();
        if (!consoleEnt)
            return false;

        float dist = vector.Distance(controlled.GetOrigin(), consoleEnt.GetOrigin());
        if (dist > LEAVE_DISTANCE_M)
            return false;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    void FeedOnce()
    {
        if (!m_Station || !m_PpiPanel || !m_ContactPanel)
            return;

        string mode = m_Station.GetWorkstationMode();
        m_iFocusedModeTab = ModeToTab(mode);

        vector origin = m_Station.GetScanOriginWorld();
        vector forward = m_Station.GetScanForwardWorld();
        float rfRange = m_SnapRangeM;
        if (rfRange <= 0.0)
            rfRange = ResolveRfRange();
        float viewRange = ResolveViewRange(rfRange);

        array<ref RDF_RadarTarget> plots = m_SnapPlots;
        array<ref RDF_RadarTrack> tracks = m_SnapTracks;
        array<ref RDF_RadarFusedTrack> fused = m_SnapFused;
        array<ref GBRS_WlrPersistDisplay> wlr = m_SnapWlr;
        int detected = m_SnapDetectedTotal;
        int netOnline = m_SnapNetOnline;
        int lockedId = m_SnapLockedTrackId;
        string eccm = m_SnapEccm;

        if (m_bHasSnapshot)
        {
            origin = m_SnapOrigin;
            float azRad = m_SnapScanAzDeg * Math.DEG2RAD;
            forward = Vector(Math.Sin(azRad), 0.0, Math.Cos(azRad));
        }

        if (lockedId <= 0)
            lockedId = m_Station.ResolveLockedTrackId();

        m_PpiPanel.SetMode(mode);
        m_PpiPanel.DrawFrame(
            plots,
            tracks,
            fused,
            wlr,
            origin,
            forward,
            viewRange,
            mode,
            eccm,
            lockedId,
            m_iLastSnapshotSeq,
            false);

        m_ContactPanel.DrawFrame(
            m_Station,
            plots,
            tracks,
            wlr,
            origin,
            mode,
            eccm,
            detected,
            netOnline,
            m_bOpticsWanted);
    }

    //------------------------------------------------------------------------------------------------
    protected float ResolveRfRange()
    {
        if (!m_Station)
            return 12000.0;
        RDF_RadarComponent radar = m_Station.GetRadarComponent();
        if (!radar)
            return 12000.0;
        RDF_RadarSensor sensor = radar.GetSensor();
        if (!sensor)
            return 12000.0;
        RDF_RadarSettings settings = sensor.GetSettings();
        if (!settings)
            return 12000.0;
        float rangeM = settings.m_Range;
        if (rangeM <= 0.0)
            return 12000.0;
        return rangeM;
    }

    //------------------------------------------------------------------------------------------------
    protected float ResolveViewRange(float rfRange)
    {
        if (!m_bPpiZoomManual || m_PpiViewRangeM <= 0.0)
        {
            m_PpiViewRangeM = rfRange;
            return m_PpiViewRangeM;
        }
        if (m_PpiViewRangeM > rfRange)
            m_PpiViewRangeM = rfRange;
        float minView = PPI_RANGE_MIN_M;
        if (minView > rfRange)
            minView = rfRange;
        if (m_PpiViewRangeM < minView)
            m_PpiViewRangeM = minView;
        return m_PpiViewRangeM;
    }

    //------------------------------------------------------------------------------------------------
    protected void StoreReplicatedSnapshot(
        RplId stationId,
        vector origin,
        float scanAzDeg,
        float rangeM,
        string eccm,
        int snapshotSeq,
        array<int> packedInts,
        array<float> packedFloats)
    {
        if (!m_Station)
            return;
        if (m_Station.GetStationRplId() != stationId)
            return;
        if (snapshotSeq <= m_iLastSnapshotSeq)
            return;

        array<ref RDF_RadarTarget> plots;
        array<ref RDF_RadarTrack> tracks;
        array<ref RDF_RadarFusedTrack> fused;
        array<ref GBRS_WlrPersistDisplay> wlr;
        int detectedTotal;
        int netOnline;
        int lockedTrackId;
        if (!GBRS_PpiSnapshot.Unpack(
            packedInts, packedFloats, plots, tracks, fused, wlr, detectedTotal, netOnline, lockedTrackId))
            return;

        m_SnapOrigin = origin;
        m_SnapScanAzDeg = scanAzDeg;
        m_SnapRangeM = rangeM;
        m_SnapEccm = eccm;
        m_SnapDetectedTotal = detectedTotal;
        m_SnapNetOnline = netOnline;
        m_SnapLockedTrackId = lockedTrackId;
        m_SnapPlots = plots;
        m_SnapTracks = tracks;
        m_SnapFused = fused;
        m_SnapWlr = wlr;
        m_iLastSnapshotSeq = snapshotSeq;
        m_bHasSnapshot = true;
    }

    //------------------------------------------------------------------------------------------------
    protected void ClearSnapshot()
    {
        m_bHasSnapshot = false;
        m_iLastSnapshotSeq = 0;
        m_SnapPlots = null;
        m_SnapTracks = null;
        m_SnapFused = null;
        m_SnapWlr = null;
    }

    //------------------------------------------------------------------------------------------------
    protected void BindInputs()
    {
        if (m_bInputsBound)
            return;
        InputManager inputManager = GetGame().GetInputManager();
        if (!inputManager)
            return;

        inputManager.AddActionListener(ACTION_TAB_PREV, EActionTrigger.DOWN, OnTabPrev);
        inputManager.AddActionListener(ACTION_TAB_NEXT, EActionTrigger.DOWN, OnTabNext);
        inputManager.AddActionListener(ACTION_ZOOM_OUT, EActionTrigger.DOWN, OnZoomOut);
        inputManager.AddActionListener(ACTION_ZOOM_IN, EActionTrigger.DOWN, OnZoomIn);
        inputManager.AddActionListener(ACTION_SELECT, EActionTrigger.DOWN, OnSelect);
        inputManager.AddActionListener(ACTION_BACK, EActionTrigger.DOWN, OnBack);
        m_bInputsBound = true;
    }

    //------------------------------------------------------------------------------------------------
    protected void UnbindInputs()
    {
        if (!m_bInputsBound)
            return;
        InputManager inputManager = GetGame().GetInputManager();
        if (inputManager)
        {
            inputManager.RemoveActionListener(ACTION_TAB_PREV, EActionTrigger.DOWN, OnTabPrev);
            inputManager.RemoveActionListener(ACTION_TAB_NEXT, EActionTrigger.DOWN, OnTabNext);
            inputManager.RemoveActionListener(ACTION_ZOOM_OUT, EActionTrigger.DOWN, OnZoomOut);
            inputManager.RemoveActionListener(ACTION_ZOOM_IN, EActionTrigger.DOWN, OnZoomIn);
            inputManager.RemoveActionListener(ACTION_SELECT, EActionTrigger.DOWN, OnSelect);
            inputManager.RemoveActionListener(ACTION_BACK, EActionTrigger.DOWN, OnBack);
        }
        m_bInputsBound = false;
    }

    //------------------------------------------------------------------------------------------------
    protected void OnTabPrev()
    {
        CycleModeTab(-1);
    }

    //------------------------------------------------------------------------------------------------
    protected void OnTabNext()
    {
        CycleModeTab(1);
    }

    //------------------------------------------------------------------------------------------------
    protected void OnZoomOut()
    {
        AdjustPpiZoom(-1);
    }

    //------------------------------------------------------------------------------------------------
    protected void OnZoomIn()
    {
        AdjustPpiZoom(1);
    }

    //------------------------------------------------------------------------------------------------
    protected void OnSelect()
    {
        LockNearestConfirmedTrack();
    }

    //------------------------------------------------------------------------------------------------
    protected void OnBack()
    {
        Stop();
    }

    //------------------------------------------------------------------------------------------------
    protected bool CanAcceptModeNav()
    {
        float nowS = System.GetTickCount() * 0.001;
        if ((nowS - m_fLastModeNavS) < MODE_NAV_COOLDOWN_S)
            return false;
        m_fLastModeNavS = nowS;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected void CycleModeTab(int delta)
    {
        if (!CanAcceptModeNav())
            return;
        if (!m_Station)
            return;

        int tabCount = 4;
        m_iFocusedModeTab = m_iFocusedModeTab + delta;
        while (m_iFocusedModeTab < 0)
            m_iFocusedModeTab = m_iFocusedModeTab + tabCount;
        while (m_iFocusedModeTab >= tabCount)
            m_iFocusedModeTab = m_iFocusedModeTab - tabCount;

        string nextMode = TabToMode(m_iFocusedModeTab);
        m_Station.ApplyWorkstationMode(nextMode);
        FeedOnce();
    }

    //------------------------------------------------------------------------------------------------
    void CycleModeFromAction()
    {
        CycleModeTab(1);
    }

    //------------------------------------------------------------------------------------------------
    void ToggleOpticsFromAction()
    {
        if (!m_Console || !m_Station)
            return;

        if (m_bOpticsWanted)
        {
            m_bOpticsWanted = false;
            if (m_OpticsScreen)
            {
                m_OpticsScreen.DisableScreen();
                m_OpticsScreen = null;
            }
            FeedOnce();
            return;
        }

        IEntity opticsTagged = m_Console.FindScreenMesh(EGBRS_ScreenKind.OPTICS);
        if (!opticsTagged)
        {
            Print("[GBRS Console] OPTICS mesh missing", LogLevel.WARNING);
            return;
        }

        IEntity opticsMesh = GBRS_WorldScreen.PrepareScreenMesh(opticsTagged);
        m_OpticsScreen = new GBRS_OpticsScreen();
        if (!m_OpticsScreen.EnableScreen(opticsMesh, m_Station))
        {
            m_OpticsScreen = null;
            return;
        }
        m_bOpticsWanted = true;
        FeedOnce();
    }

    //------------------------------------------------------------------------------------------------
    protected void AdjustPpiZoom(int direction)
    {
        if (direction == 0)
            return;
        if (!CanAcceptModeNav())
            return;

        float rfMax = ResolveRfRange();
        if (m_SnapRangeM > 0.0)
            rfMax = m_SnapRangeM;
        if (rfMax <= 0.0)
            rfMax = 7000.0;

        float minView = PPI_RANGE_MIN_M;
        if (minView > rfMax)
            minView = rfMax;

        float current = m_PpiViewRangeM;
        if (current <= 0.0)
            current = rfMax;

        int idx = NearestPpiRangeIndex(current, rfMax);
        idx = idx + direction;
        if (idx < 0)
            idx = 0;
        if (idx >= PPI_RANGE_STEP_COUNT)
            idx = PPI_RANGE_STEP_COUNT - 1;

        float next = PpiRangeStepAt(idx, rfMax);
        if (next > rfMax)
            next = rfMax;
        if (next < minView)
            next = minView;
        if (Math.AbsFloat(next - current) < 1.0)
            return;

        m_PpiViewRangeM = next;
        m_bPpiZoomManual = true;
        if (m_PpiPanel)
            m_PpiPanel.SetDisplayRange(next);
        FeedOnce();
    }

    //------------------------------------------------------------------------------------------------
    protected int NearestPpiRangeIndex(float rangeM, float rfMax)
    {
        int best = 0;
        float bestDiff = Math.AbsFloat(PpiRangeStepAt(0, rfMax) - rangeM);
        int i = 1;
        while (i < PPI_RANGE_STEP_COUNT)
        {
            float diff = Math.AbsFloat(PpiRangeStepAt(i, rfMax) - rangeM);
            if (diff < bestDiff)
            {
                bestDiff = diff;
                best = i;
            }
            i = i + 1;
        }
        return best;
    }

    //------------------------------------------------------------------------------------------------
    protected float PpiRangeStepAt(int index, float rfMax)
    {
        if (index <= 0)
            return PPI_RANGE_MIN_M;
        if (index >= PPI_RANGE_STEP_COUNT - 1)
            return rfMax;
        float t = index / (PPI_RANGE_STEP_COUNT - 1.0);
        return PPI_RANGE_MIN_M + (rfMax - PPI_RANGE_MIN_M) * t;
    }

    //------------------------------------------------------------------------------------------------
    protected void LockNearestConfirmedTrack()
    {
        if (!m_Station)
            return;
        if (!m_SnapTracks)
            return;

        vector origin = m_SnapOrigin;
        if (!m_bHasSnapshot)
            origin = m_Station.GetScanOriginWorld();

        int bestId = -1;
        float bestDist = 1.0e12;
        foreach (RDF_RadarTrack tr : m_SnapTracks)
        {
            if (!tr)
                continue;
            if (!tr.m_Confirmed)
                continue;
            float dist = vector.Distance(tr.m_FilteredPosition, origin);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestId = tr.m_TrackId;
            }
        }

        if (bestId < 0)
            return;
        m_Station.RequestLockTrack(bestId);
    }

    //------------------------------------------------------------------------------------------------
    protected int ModeToTab(string mode)
    {
        if (mode == GBRS_RadarStationConstants.MODE_WLR)
            return 1;
        if (mode == GBRS_RadarStationConstants.MODE_LOCK)
            return 2;
        if (mode == GBRS_RadarStationConstants.MODE_MANUAL)
            return 3;
        return 0;
    }

    //------------------------------------------------------------------------------------------------
    protected string TabToMode(int tab)
    {
        if (tab == 1)
            return GBRS_RadarStationConstants.MODE_WLR;
        if (tab == 2)
            return GBRS_RadarStationConstants.MODE_LOCK;
        if (tab == 3)
            return GBRS_RadarStationConstants.MODE_MANUAL;
        return GBRS_RadarStationConstants.MODE_PD_SEARCH;
    }
}
