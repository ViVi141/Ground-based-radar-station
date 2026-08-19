// GBRS station HUD — layout owns panel geometry; widgets registry binds every part.

// Persistent WLR display entry: keeps a solved launch -> impact chain on the
// PPI for a while after the raw RDF track has been dropped or has impacted,
// so the operator does not see the launch/impact markers blink out immediately.
class GBRS_WlrPersistDisplay
{
    int m_TrackId;
    string m_Id;
    vector m_LaunchPos;
    vector m_ImpactPos;
    vector m_LivePos;
    vector m_LiveVel;
    float m_LastSeenS;
    float m_ImpactTimeS;
    bool m_HasLaunch;
    bool m_HasImpact;
    bool m_HasLive;
}

class GBRS_RadarStationHud
{
    static const ResourceName LAYOUT =
        "{0DD3257B9B2D6BC9}UI/layouts/GBRS/RadarStationHUD.layout";
    // Drawn on the PPI Canvas (RHS Garmin-style ImageDrawCommand), not a sibling ImageWidget.
    static const ResourceName PPI_FACE_TEXTURE =
        "{F2196E35CB708A41}UI/Textures/GBRS/GBRS_PpiFace.edds";

    // Dedicated world camera slot for the workstation PIP (vanilla sights default to 1).
    // Index 0 is the live player camera — never bind the RT to GetCameraIndex() before
    // SetCameraIndex, and never put "camera N" in the layout file.
    static const int OPTICS_CAMERA_INDEX = 16;
    static const float OPTICS_FOV_DEG = 32.0;
    static const float OPTICS_NEAR_M = 0.25;
    static const float OPTICS_EYE_HEIGHT_M = 1.8;
    static const float OPTICS_FORWARD_CLEAR_M = 2.5;
    // Fixed upward look bias for PIP clearance — not antenna elevation.
    static const float OPTICS_LOOK_UP_Y = 0.08;
    // Match workstation feed (~30 Hz); optics RT can run faster.
    static const float UPDATE_INTERVAL = 0.05;
    static const int OPTICS_MAX_FPS = 45;
    // PIP cameras often start underexposed; sync main HDR then lift slightly.
    static const float OPTICS_HDR_BOOST = 1.35;

    static const int STATION_MARGIN = 0;
    static const int STATION_W = 1920;
    static const int STATION_H = 1080;

    static const int PPI_W = 640;
    static const int PPI_H = 640;
    static const float PPI_CX = 320.0;
    static const float PPI_CY = 320.0;
    static const float PPI_R = 300.0;

    // Must match AzElCanvasSize in RadarStationHUD.layout.
    static const int AZEL_W = 300;
    static const int AZEL_H = 400;
    static const float AZEL_EL_MIN = -5.0;
    static const float AZEL_EL_MAX = 55.0;

    static const float SWEEP_HALF_DEG = 8.0;
    static const int SWEEP_SEGMENTS = 16;
    static const int MAX_LIST_ROWS = 24;
    // Must match GBRS_RadarStationMenu.DISPLAY_MAX_BLIPS.
    static const int MAX_DRAW_BLIPS = 64;
    // Merge TWS symbols in polar (range/az), not 3D. Elevation noise and
    // coasting ghosts otherwise sit just outside a Cartesian 220 m ball.
    static const float TRACK_CLUSTER_RANGE_M = 700.0;
    static const float TRACK_CLUSTER_AZ_DEG = 8.0;
    static const float HEADING_SPAN_S = 1.0;

    // Static face is drawn on the same Canvas as sweep/blips (RHS Garmin pattern).
    static const int COL_PPI_SWEEP = ARGB(250, 90, 255, 170);
    static const int COL_PPI_WEDGE = ARGB(70, 40, 220, 130);
    static const int COL_VEHICLE = ARGB(255, 90, 255, 150);
    static const int COL_PROJ = ARGB(255, 255, 190, 55);
    static const int COL_EMITTER = ARGB(255, 255, 110, 240);
    static const int COL_ANON = ARGB(255, 255, 240, 160);
    static const int COL_FALSEPLOT = ARGB(255, 255, 95, 95);
    static const int COL_NLOS = ARGB(255, 110, 235, 255);
    static const int COL_VEL = ARGB(200, 110, 255, 200);
    static const int COL_WLR_LAUNCH = ARGB(220, 255, 160, 40);
    static const int COL_WLR_IMPACT = ARGB(220, 80, 180, 255);
    static const int COL_WLR_LINK = ARGB(140, 200, 200, 200);
    static const int COL_WLR_REMAIN = ARGB(220, 255, 210, 80);
    static const int COL_WLR_TEXT = ARGB(255, 255, 235, 180);
    static const int COL_WLR_SHELL = ARGB(255, 255, 220, 70);
    // Multi-radar network overlay: tracks fused across the GBRS net.
    static const int COL_NET = ARGB(230, 120, 180, 255);
    static const int COL_LOCK = ARGB(255, 255, 70, 70);
    static const int COL_PLOT_GLOW = ARGB(110, 90, 180, 130);
    static const int COL_TRACK = ARGB(255, 90, 255, 150);
    static const int COL_TRACK_TENT = ARGB(200, 210, 220, 100);
    static const int COL_TRACK_COAST = ARGB(230, 140, 210, 255);
    static const int COL_TRACK_LABEL = ARGB(220, 220, 255, 210);
    static const int COL_AZEL_GRID = ARGB(120, 80, 160, 200);
    static const float WLR_ALERT_RADIUS_M = 120.0;
    static const float WLR_PERSIST_S = 20.0;
    static const float WLR_IMPACT_PERSIST_S = 3.0;
    static const int MAX_WLR_PERSIST = 16;
    static const string MODE_WLR = GBRS_RadarStationConstants.MODE_WLR;
    static const string MODE_LOCK = GBRS_RadarStationConstants.MODE_LOCK;

    protected static ref GBRS_RadarStationHud s_Instance;

    // Multi-radar network overlay toggle + live network status.
    protected static bool s_NetworkOverlay = true;
    protected static int s_NetFusedCount;
    protected static int s_NetStationCount;

    static void SetNetworkOverlayEnabled(bool enabled)
    {
        s_NetworkOverlay = enabled;
    }

    static bool IsNetworkOverlayEnabled()
    {
        return s_NetworkOverlay;
    }

    protected Widget m_wRoot;
    protected ref GBRS_RadarStationHudWidgets m_Widgets;

    protected SCR_PIPCamera m_OpticsCamera;
    protected IEntity m_OpticsParent;
    protected float m_DisplayRange = 7000.0;
    protected float m_LastUpdateS;
    protected string m_Mode = GBRS_RadarStationConstants.MODE_PD_SEARCH;
    protected int m_DetectedTotal;
    protected RDF_RadarLockManager m_LockManager;
    // RDF 1.0.0 ECCM decision status ("eccm=0" | "eccm slb/prf/freq/burn").
    protected string m_EccmStatus = "eccm=0";
    // MANUAL mode parameter list rendering (set by the menu).
    protected string m_ManualParamText;
    protected int m_ManualFocusedParam = -1;

    protected float m_PpiW = PPI_W;
    protected float m_PpiH = PPI_H;
    protected float m_PpiCx = PPI_CX;
    protected float m_PpiCy = PPI_CY;
    protected float m_PpiR = PPI_R;
    protected float m_AzElW = AZEL_W;
    protected float m_AzElH = AZEL_H;

    protected ref array<ref CanvasWidgetCommand> m_PpiAll;
    protected ref array<ref CanvasWidgetCommand> m_AzElAll;
    protected ref SharedItemRef m_PpiFaceTex;
    protected ref array<ref GBRS_WlrPersistDisplay> m_WlrPersist;
    protected ref array<ref RDF_RadarTrack> m_CachedDisplayTracks;

    static GBRS_RadarStationHud GetInstance()
    {
        if (!s_Instance)
            s_Instance = new GBRS_RadarStationHud();
        return s_Instance;
    }

    // Bind drawing to a MenuManager-owned layout root (does not CreateWidgets).
    static void Attach(Widget root, IEntity opticsParent)
    {
        GetInstance().AttachInternal(root, opticsParent);
    }

    // Release cameras / draw state; leave menu root hierarchy alone.
    static void Detach()
    {
        GetInstance().DetachInternal();
    }

    static bool IsVisible()
    {
        return GetInstance().m_wRoot != null;
    }

    static GBRS_RadarStationHudWidgets GetWidgets()
    {
        return GetInstance().m_Widgets;
    }

    static void SetMode(string mode)
    {
        GBRS_RadarStationHud inst = GetInstance();
        inst.m_Mode = mode;
        if (inst.m_Widgets && inst.m_Widgets.m_wPpiMode)
            inst.m_Widgets.m_wPpiMode.SetText(mode);

        inst.ApplyModeChrome(mode);

        if (inst.m_Widgets && inst.m_Widgets.m_wListTitle)
        {
            if (mode == GBRS_RadarStationConstants.MODE_MANUAL)
                inst.m_Widgets.m_wListTitle.SetText("MANUAL TUNING");
            else if (mode == GBRS_RadarStationConstants.MODE_WLR)
                inst.m_Widgets.m_wListTitle.SetText("WLR FIRE SOLUTIONS");
            else
                inst.m_Widgets.m_wListTitle.SetText("TRACKED CONTACTS");
        }

        if (inst.m_Widgets && inst.m_Widgets.m_wListHType)
        {
            if (mode == GBRS_RadarStationConstants.MODE_WLR)
                inst.m_Widgets.m_wListHType.SetText("SHELL");
            else
                inst.m_Widgets.m_wListHType.SetText("TYPE");
        }

        if (inst.m_Widgets && inst.m_Widgets.m_wPpiHint)
        {
            if (mode == GBRS_RadarStationConstants.MODE_WLR)
                inst.m_Widgets.m_wPpiHint.SetText("LCH orange  IMP cyan   Up/Dn PPI range");
            else if (mode == GBRS_RadarStationConstants.MODE_LOCK)
                inst.m_Widgets.m_wPpiHint.SetText("auto-lock vehicles   Up/Dn PPI range");
            else if (mode == GBRS_RadarStationConstants.MODE_MANUAL)
                inst.m_Widgets.m_wPpiHint.SetText("Up/Dn parameter   Left/Right value   wheel PPI range");
            else
                inst.m_Widgets.m_wPpiHint.SetText("north-up AZ/EL   Up/Dn PPI range");
        }

        bool showTable = true;
        if (mode == GBRS_RadarStationConstants.MODE_MANUAL)
            showTable = false;
        else if (mode == GBRS_RadarStationConstants.MODE_WLR)
            showTable = false;
        inst.SetContactsTableVisible(showTable);

        // Keep the external optics PIP in every workstation mode (incl. WLR) so
        // the optical sight does not go dark when the radar station is open.
        if (inst.m_OpticsParent && !inst.m_OpticsCamera)
            inst.CreateOpticsCamera(inst.m_OpticsParent);
    }

    protected void SetContactsTableVisible(bool contacts)
    {
        if (!m_Widgets)
            return;

        if (m_Widgets.m_wListTable)
            m_Widgets.m_wListTable.SetVisible(contacts);
        if (m_Widgets.m_wListBody)
            m_Widgets.m_wListBody.SetVisible(!contacts);
    }

    protected void ApplyModeChrome(string mode)
    {
        if (!m_Widgets)
            return;

        Color ppi = Color.FromRGBA(30, 140, 80, 242);
        Color list = Color.FromRGBA(45, 95, 140, 242);
        Color optics = Color.FromRGBA(55, 130, 190, 242);
        Color azel = Color.FromRGBA(40, 105, 155, 242);
        Color modeCol = Color.FromRGBA(90, 255, 160, 255);

        if (mode == GBRS_RadarStationConstants.MODE_WLR)
        {
            ppi = Color.FromRGBA(170, 110, 30, 242);
            list = Color.FromRGBA(160, 100, 35, 242);
            optics = Color.FromRGBA(150, 95, 30, 242);
            azel = Color.FromRGBA(145, 90, 28, 242);
            modeCol = Color.FromRGBA(255, 190, 70, 255);
        }
        else if (mode == GBRS_RadarStationConstants.MODE_LOCK)
        {
            ppi = Color.FromRGBA(160, 45, 40, 242);
            list = Color.FromRGBA(150, 50, 48, 242);
            optics = Color.FromRGBA(145, 48, 45, 242);
            azel = Color.FromRGBA(140, 42, 40, 242);
            modeCol = Color.FromRGBA(255, 95, 80, 255);
        }
        else if (mode == GBRS_RadarStationConstants.MODE_MANUAL)
        {
            ppi = Color.FromRGBA(30, 120, 150, 242);
            list = Color.FromRGBA(35, 130, 165, 242);
            optics = Color.FromRGBA(40, 125, 160, 242);
            azel = Color.FromRGBA(32, 115, 150, 242);
            modeCol = Color.FromRGBA(80, 210, 255, 255);
        }

        if (m_Widgets.m_wPpiBezel)
            m_Widgets.m_wPpiBezel.SetColor(ppi);
        if (m_Widgets.m_wListBezel)
            m_Widgets.m_wListBezel.SetColor(list);
        if (m_Widgets.m_wOpticsBezel)
            m_Widgets.m_wOpticsBezel.SetColor(optics);
        if (m_Widgets.m_wAzElBezel)
            m_Widgets.m_wAzElBezel.SetColor(azel);
        if (m_Widgets.m_wPpiMode)
            m_Widgets.m_wPpiMode.SetColor(modeCol);
        if (m_Widgets.m_wListBody)
        {
            if (mode == GBRS_RadarStationConstants.MODE_MANUAL)
                m_Widgets.m_wListBody.SetColor(Color.FromRGBA(160, 235, 255, 240));
            else if (mode == GBRS_RadarStationConstants.MODE_WLR)
                m_Widgets.m_wListBody.SetColor(Color.FromRGBA(255, 214, 140, 245));
            else
                m_Widgets.m_wListBody.SetColor(Color.FromRGBA(185, 230, 255, 235));
        }
    }

    static void SetDisplayRange(float rangeM)
    {
        if (rangeM <= 0.0)
            return;
        GetInstance().m_DisplayRange = rangeM;
    }

    // RDF 1.0.0 ECCM status from the Sensor (GetEccmStatusShort): drives the
    // PPI jam-warning ring and the list footer line.
    static void SetEccmStatus(string status)
    {
        if (status == "")
            status = "eccm=0";
        GetInstance().m_EccmStatus = status;
    }

    // MANUAL workstation mode: render the operator parameter list into the
    // contacts panel body (instead of radar contacts). text lines are
    // pre-formatted by the menu; focusedIndex highlights the active parameter.
    static void SetManualParamList(string text, int focusedIndex)
    {
        GBRS_RadarStationHud inst = GetInstance();
        inst.m_ManualParamText = text;
        inst.m_ManualFocusedParam = focusedIndex;
        inst.SetContactsTableVisible(false);
        if (inst.m_Widgets && inst.m_Widgets.m_wListBody)
        {
            inst.m_Widgets.m_wListBody.SetText(text);
            inst.m_Widgets.m_wListBody.SetColor(Color.FromRGBA(160, 235, 255, 240));
        }
    }

    // MANUAL workstation mode: footer line under the parameter list.
    static void SetManualParamFooter(string text)
    {
        GBRS_RadarStationHud inst = GetInstance();
        if (inst.m_Widgets && inst.m_Widgets.m_wListFooter)
            inst.m_Widgets.m_wListFooter.SetText(text);
    }

    static void FeedScan(
        array<ref RDF_RadarTarget> targets,
        vector origin,
        vector forward,
        float range,
        RDF_RadarProjectileTracker tracker,
        int detectedTotal,
        RDF_RadarLockManager lockMgr)
    {
        GetInstance().Update(targets, origin, forward, range, tracker, detectedTotal, lockMgr);
    }

    protected void AttachInternal(Widget root, IEntity opticsParent)
    {
        if (!root)
            return;

        if (m_wRoot == root)
        {
            m_OpticsParent = opticsParent;
            if (!m_OpticsCamera)
                CreateOpticsCamera(opticsParent);
            return;
        }

        if (m_wRoot)
            DetachInternal();

        m_wRoot = root;
        m_Widgets = new GBRS_RadarStationHudWidgets();
        if (!m_Widgets.Init(m_wRoot))
            Print("[GBRS HUD] widget registry incomplete", LogLevel.WARNING);

        if (!m_WlrPersist)
            m_WlrPersist = new array<ref GBRS_WlrPersistDisplay>();
        else
            m_WlrPersist.Clear();

        CenterRoot();
        InitCanvases();
        SyncPpiSquare();
        SyncAzElSize();

        if (m_Widgets.m_wListBody)
        {
            m_Widgets.m_wListBody.SetText("(no contacts)");
            m_Widgets.m_wListBody.SetVisible(false);
        }
        if (m_Widgets.m_wPpiMode)
            m_Widgets.m_wPpiMode.SetText(m_Mode);

        m_OpticsParent = opticsParent;
        CreateOpticsCamera(opticsParent);
        m_LastUpdateS = 0.0;
        m_DetectedTotal = 0;
        Print("[GBRS HUD] attached to menu root");
    }

    protected void DetachInternal()
    {
        DestroyOpticsCamera();
        m_wRoot = null;
        m_LockManager = null;
        if (m_Widgets)
            m_Widgets.Clear();
        m_Widgets = null;
        m_PpiAll = null;
        m_AzElAll = null;
        m_PpiFaceTex = null;
        m_OpticsParent = null;
        if (m_WlrPersist)
            m_WlrPersist.Clear();
        m_LastUpdateS = 0.0;
        m_DetectedTotal = 0;
        GBRS_RadarWlrBallisticSolver.Clear();
    }

    // Fill the live screen. MenuManager may re-place the authored root; re-assert
    // stretch every feed tick so the workstation stays full-screen.
    protected void CenterRoot()
    {
        if (!m_wRoot)
            return;

        FrameSlot.SetAnchorMin(m_wRoot, 0.0, 0.0);
        FrameSlot.SetAnchorMax(m_wRoot, 1.0, 1.0);
        FrameSlot.SetAlignment(m_wRoot, 0.0, 0.0);
        FrameSlot.SetOffsets(m_wRoot, 0.0, 0.0, 0.0, 0.0);
    }

    protected void SyncPpiSquare()
    {
        if (!m_wRoot || !m_Widgets || !m_Widgets.m_wPpiCanvas)
            return;

        Widget host = m_wRoot.FindAnyWidget("PpiCanvasHost");
        if (!host)
            host = m_Widgets.m_wPpiCanvas.GetParent();
        if (!host)
            return;

        float hw;
        float hh;
        host.GetScreenSize(hw, hh);
        if (hw < 64.0 || hh < 64.0)
            return;

        WorkspaceWidget ws = GetGame().GetWorkspace();
        if (ws)
        {
            float dpi = ws.DPIScale(1.0);
            if (dpi > 0.001)
            {
                hw = hw / dpi;
                hh = hh / dpi;
            }
        }

        float side = hw;
        if (hh < hw)
            side = hh;

        m_PpiW = PPI_W;
        m_PpiH = PPI_H;
        m_PpiCx = PPI_CX;
        m_PpiCy = PPI_CY;
        m_PpiR = PPI_R;

        FrameSlot.SetAnchorMin(m_Widgets.m_wPpiCanvas, 0.5, 0.5);
        FrameSlot.SetAnchorMax(m_Widgets.m_wPpiCanvas, 0.5, 0.5);
        FrameSlot.SetAlignment(m_Widgets.m_wPpiCanvas, 0.5, 0.5);
        FrameSlot.SetPos(m_Widgets.m_wPpiCanvas, 0.0, 0.0);
        FrameSlot.SetSize(m_Widgets.m_wPpiCanvas, side, side);
        m_Widgets.m_wPpiCanvas.SetSizeInUnits(Vector(PPI_W, PPI_H, 0));
    }

    protected void SyncAzElSize()
    {
        if (!m_Widgets || !m_Widgets.m_wAzElCanvas)
            return;

        m_AzElW = AZEL_W;
        m_AzElH = AZEL_H;
        m_Widgets.m_wAzElCanvas.SetSizeInUnits(Vector(AZEL_W, AZEL_H, 0));
    }

    // Force face + canvas onto the same absolute 640x640 rect so draw units map
    // 1:1 onto the static PPI texture (center 320, radius 300).
    protected void PinPpiSquare(Widget w)
    {
        if (!w)
            return;

        FrameSlot.SetAnchor(w, 0.0, 0.0);
        FrameSlot.SetAnchorMin(w, 0.0, 0.0);
        FrameSlot.SetAnchorMax(w, 0.0, 0.0);
        FrameSlot.SetAlignment(w, 0.0, 0.0);
        FrameSlot.SetPos(w, 0.0, 0.0);
        FrameSlot.SetSize(w, PPI_W, PPI_H);
    }

    // Hide layout Image face layers — the face texture is drawn on PpiCanvas.
    protected void HideDuplicatePpiFaceLayers()
    {
        if (!m_wRoot)
            return;

        array<string> names = new array<string>();
        names.Insert("PpiFaceHost");
        names.Insert("PpiFaceImage");
        names.Insert("PpiDiscBg");
        names.Insert("PpiMapSize");
        names.Insert("PpiRing25Size");
        names.Insert("PpiRing50Size");
        names.Insert("PpiRing75Size");
        names.Insert("PpiRing100");
        names.Insert("PpiCrossHSize");
        names.Insert("PpiCrossVSize");
        names.Insert("PpiCoreSize");
        names.Insert("AzElGridV0");
        names.Insert("AzElGridV1");
        names.Insert("AzElGridV2");
        names.Insert("AzElGridV3");
        names.Insert("AzElGridV4");
        names.Insert("AzElGridV5");
        names.Insert("AzElGridV6");
        names.Insert("AzElGridH0");
        names.Insert("AzElGridH1");
        names.Insert("AzElGridH2");
        names.Insert("AzElGridH3");
        names.Insert("AzElGridH4");

        int i = 0;
        while (i < names.Count())
        {
            Widget w = m_wRoot.FindAnyWidget(names.Get(i));
            if (w)
                w.SetVisible(false);
            i = i + 1;
        }
    }

    protected void InitCanvases()
    {
        m_PpiW = PPI_W;
        m_PpiH = PPI_H;
        m_PpiCx = PPI_CX;
        m_PpiCy = PPI_CY;
        m_PpiR = PPI_R;
        m_AzElW = AZEL_W;
        m_AzElH = AZEL_H;

        m_PpiAll = new array<ref CanvasWidgetCommand>();
        m_AzElAll = new array<ref CanvasWidgetCommand>();
        m_PpiFaceTex = CanvasWidget.LoadTexture(PPI_FACE_TEXTURE);
        if (!m_PpiFaceTex)
            Print("[GBRS HUD] failed to LoadTexture GBRS_PpiFace", LogLevel.WARNING);

        HideDuplicatePpiFaceLayers();
        SyncPpiSquare();
        SyncAzElSize();

        if (m_Widgets && m_Widgets.m_wPpiCanvas)
        {
            m_Widgets.m_wPpiCanvas.ClearFlags(WidgetFlags.CLIPCHILDREN);
            m_Widgets.m_wPpiCanvas.SetSizeInUnits(Vector(PPI_W, PPI_H, 0));
            m_Widgets.m_wPpiCanvas.SetDrawCommands(m_PpiAll);
        }

        if (m_Widgets && m_Widgets.m_wAzElCanvas)
        {
            m_Widgets.m_wAzElCanvas.ClearFlags(WidgetFlags.CLIPCHILDREN);
            m_Widgets.m_wAzElCanvas.SetSizeInUnits(Vector(AZEL_W, AZEL_H, 0));
            m_Widgets.m_wAzElCanvas.SetDrawCommands(m_AzElAll);
        }
    }

    protected void CreateOpticsCamera(IEntity parent)
    {
        if (!parent)
            return;

        if (!m_Widgets || !m_Widgets.m_wOpticsRT)
        {
            Print("[GBRS HUD] optics RenderTarget widget missing", LogLevel.WARNING);
            return;
        }

        if (parent.IsDeleted())
            return;

        BaseWorld world = parent.GetWorld();
        if (!world)
            return;

        DestroyOpticsCamera();

        EntitySpawnParams params = new EntitySpawnParams();
        parent.GetWorldTransform(params.Transform);
        m_OpticsCamera = SCR_PIPCamera.Cast(GetGame().SpawnEntity(SCR_PIPCamera, world, params));
        if (!m_OpticsCamera)
        {
            Print("[GBRS HUD] optics camera spawn failed", LogLevel.WARNING);
            return;
        }

        float farPlane = GetGame().GetViewDistance();
        if (farPlane < 500.0)
            farPlane = 500.0;
        if (farPlane > 8000.0)
            farPlane = 8000.0;

        // HUD overlay path (MapWatch-style bare RenderTargetWidget):
        // SetWorld(camIndex) only — do NOT use RTTexture.SetRenderTarget.
        // SetRenderTarget requires object-selection / mesh $rendertarget and stays black on stations.
        m_OpticsCamera.SetCameraIndex(OPTICS_CAMERA_INDEX);
        m_OpticsCamera.SetVerticalFOV(OPTICS_FOV_DEG);
        m_OpticsCamera.SetNearPlane(OPTICS_NEAR_M);
        m_OpticsCamera.SetFarPlane(farPlane);
        m_OpticsCamera.ApplyProps(OPTICS_CAMERA_INDEX);
        world.SetCameraLensFlareSet(OPTICS_CAMERA_INDEX, CameraLensFlareSetType.FirstPerson, string.Empty);

        m_Widgets.m_wOpticsRT.SetVisible(true);
        m_Widgets.m_wOpticsRT.SetZOrder(5);
        m_Widgets.m_wOpticsRT.SetWorld(world, OPTICS_CAMERA_INDEX);
        m_Widgets.m_wOpticsRT.SetResolutionScale(1.0, 1.0);
        m_Widgets.m_wOpticsRT.SetMaxFPS(OPTICS_MAX_FPS);
        m_Widgets.m_wOpticsRT.SetFormat(RenderTargetWidgetFormat.HDR_HIGH);
        // Avoid dark green clear tint washing the PIP darker.
        m_Widgets.m_wOpticsRT.SetClearColor(true, ARGB(255, 0, 0, 0));

        SyncOpticsHdr(world);

        if (m_Widgets.m_wOpticsPlaceholder)
            m_Widgets.m_wOpticsPlaceholder.SetVisible(false);

        vector mat[4];
        parent.GetWorldTransform(mat);
        vector forward = mat[2];
        float flen = forward.Length();
        if (flen < 0.001)
            forward = Vector(0.0, 0.0, 1.0);
        else
            forward = forward * (1.0 / flen);

        vector look = BuildOpticsLook(forward);
        Math3D.DirectionAndUpMatrix(look, Vector(0.0, 1.0, 0.0), mat);
        mat[3] = params.Transform[3] + Vector(0.0, OPTICS_EYE_HEIGHT_M, 0.0) + (look * OPTICS_FORWARD_CLEAR_M);
        m_OpticsCamera.SetWorldTransform(mat);
        m_OpticsCamera.UpdatePIPCamera(1.0);

        Print("[GBRS HUD] optics PIP bound cam=" + OPTICS_CAMERA_INDEX.ToString()
            + " fov=" + OPTICS_FOV_DEG.ToString() + " far=" + farPlane.ToString()
            + " mode=RenderTargetWidget");
    }

    protected void DestroyOpticsCamera()
    {
        if (m_OpticsCamera)
        {
            BaseWorld world = m_OpticsCamera.GetWorld();
            if (world)
                world.SetCameraLensFlareSet(OPTICS_CAMERA_INDEX, CameraLensFlareSetType.None, string.Empty);

            SCR_EntityHelper.DeleteEntityAndChildren(m_OpticsCamera);
            m_OpticsCamera = null;
        }
    }

    protected vector BuildOpticsLook(vector forward)
    {
        // Slight upward look so antenna horizon is not buried in dirt.
        // This is a fixed PIP clearance bias, not antenna elevation.
        vector look = Vector(forward[0], OPTICS_LOOK_UP_Y, forward[2]);
        float llen = look.Length();
        if (llen > 0.001)
            look = look * (1.0 / llen);
        return look;
    }

    protected void UpdateOpticsCamera(vector origin, vector forward)
    {
        if (!m_OpticsCamera)
            return;

        float flen = forward.Length();
        if (flen < 0.001)
            forward = Vector(0.0, 0.0, 1.0);
        else
            forward = forward * (1.0 / flen);

        vector look = BuildOpticsLook(forward);

        // Push eye out of antenna lattice so the near plane is not buried in mesh.
        vector mat[4];
        Math3D.DirectionAndUpMatrix(look, Vector(0.0, 1.0, 0.0), mat);
        mat[3] = origin + Vector(0.0, OPTICS_EYE_HEIGHT_M, 0.0) + (look * OPTICS_FORWARD_CLEAR_M);
        m_OpticsCamera.SetWorldTransform(mat);
        m_OpticsCamera.UpdatePIPCamera(0.0);

        BaseWorld world = m_OpticsCamera.GetWorld();
        if (world)
            SyncOpticsHdr(world);

        if (m_Widgets && m_Widgets.m_wOpticsInfo)
        {
            float az = Math.Atan2(forward[0], forward[2]) * Math.RAD2DEG;
            if (az < 0.0)
                az = az + 360.0;
            // Do not report the artificial look-up as antenna elevation.
            m_Widgets.m_wOpticsInfo.SetText("AZ " + F0(az) + "  EL FIXED");
        }
    }

    // Match main-camera HDR (vanilla PIP sights), then apply a small workstation boost.
    protected void SyncOpticsHdr(BaseWorld world)
    {
        if (!world)
            return;

        int mainCameraIndex = 0;
        CameraManager manager = GetGame().GetCameraManager();
        if (manager)
        {
            CameraBase cam = manager.CurrentCamera();
            if (cam)
                mainCameraIndex = cam.GetCameraIndex();
        }

        float hdrBrightness = world.GetCameraHDRBrightness(mainCameraIndex);
        if (hdrBrightness <= 0.0)
            hdrBrightness = 1.0;

        float boosted = hdrBrightness * OPTICS_HDR_BOOST;
        if (boosted < 0.05)
            boosted = 0.05;
        if (boosted > 12.0)
            boosted = 12.0;

        world.SetCameraHDRBrightness(OPTICS_CAMERA_INDEX, boosted);
    }

    protected void Update(
        array<ref RDF_RadarTarget> targets,
        vector origin,
        vector forward,
        float range,
        RDF_RadarProjectileTracker tracker,
        int detectedTotal,
        RDF_RadarLockManager lockMgr)
    {
        if (!m_wRoot)
            return;

        if (range > 0.0)
            m_DisplayRange = range;

        if (detectedTotal >= 0)
            m_DetectedTotal = detectedTotal;

        m_LockManager = lockMgr;

        float now = System.GetTickCount() * 0.001;
        if (now - m_LastUpdateS < UPDATE_INTERVAL)
            return;
        m_LastUpdateS = now;

        // One clustered track pass per HUD tick. PPI, AZ/EL, list, and WLR
        // persist used to each walk GetAllTracks independently.
        m_CachedDisplayTracks = CollectDisplayTracks(tracker, origin);

        // Keep the station panel centered/covering the screen: the layout root is
        // authored at 1920x1080 absolute and MenuManager may re-place it, so
        // re-assert its stretch + canvas geometry every tick.
        CenterRoot();
        SyncPpiSquare();
        SyncAzElSize();

        UpdateOpticsCamera(origin, forward);
        if (m_Mode == MODE_WLR)
            UpdateWlrPersist(origin, tracker);
        UpdatePpi(targets, origin, forward, tracker);
        UpdateAzEl(targets, origin, tracker);
        UpdateList(targets, origin, tracker);

        if (m_Widgets && m_Widgets.m_wPpiRange)
            m_Widgets.m_wPpiRange.SetText(RangeLabel(m_DisplayRange));
    }

    protected bool IsInDisplayRange(RDF_RadarTarget t, vector origin)
    {
        if (!t)
            return false;

        float rng = t.m_Distance;
        if (rng <= 0.0)
        {
            vector d = t.m_Position - origin;
            rng = d.Length();
        }

        if (m_DisplayRange <= 0.0)
            return true;

        if (rng > m_DisplayRange)
            return false;

        return true;
    }

    // North-up azimuth from world delta — matches PPI north-up scan.
    protected float NorthUpAzimuthDeg(RDF_RadarTarget t, vector origin)
    {
        vector delta = t.m_Position - origin;
        float az = Math.Atan2(delta[0], delta[2]) * Math.RAD2DEG;
        if (az < 0.0)
            az = az + 360.0;
        while (az >= 360.0)
            az = az - 360.0;
        return az;
    }

    protected float NorthUpElevationDeg(RDF_RadarTarget t, vector origin)
    {
        vector delta = t.m_Position - origin;
        float dist = delta.Length();
        if (dist < 0.001)
            return 0.0;

        float el = Math.Asin(delta[1] / dist) * Math.RAD2DEG;
        return el;
    }

    // RHS Garmin pattern: face texture + sweep/blips share one Canvas command list.
    // All geometry is authored in SizeInUnits space, then converted with PosToPixels.
    protected void DrawPpiFace()
    {
        if (!m_Widgets || !m_Widgets.m_wPpiCanvas || !m_PpiFaceTex)
            return;

        CanvasWidget canvas = m_Widgets.m_wPpiCanvas;
        vector topLeft = canvas.PosToPixels(Vector(0.0, 0.0, 0.0));
        vector size = canvas.SizeToPixels(Vector(PPI_W, PPI_H, 0.0));

        ImageDrawCommand face = new ImageDrawCommand();
        face.m_pTexture = m_PpiFaceTex;
        face.m_Position = topLeft;
        face.m_Size = size;
        face.m_fRotation = 0.0;
        face.m_iColor = 0xffffffff;
        face.m_iFlags = WidgetFlags.STRETCH;
        m_PpiAll.Insert(face);
    }

    protected void AppendUnitPoint(array<float> pixels, float unitX, float unitY)
    {
        if (unitX != unitX || unitY != unitY)
            return;
        if (unitX > float.INFINITY || unitX < -float.INFINITY)
            return;
        if (unitY > float.INFINITY || unitY < -float.INFINITY)
            return;
        vector p = m_Widgets.m_wPpiCanvas.PosToPixels(Vector(unitX, unitY, 0.0));
        pixels.Insert(p[0]);
        pixels.Insert(p[1]);
    }

    protected float UnitSizeToPixels(float unitSize)
    {
        vector px = m_Widgets.m_wPpiCanvas.SizeToPixels(Vector(unitSize, unitSize, 0.0));
        return px[0];
    }

    protected void UpdatePpi(
        array<ref RDF_RadarTarget> targets,
        vector origin,
        vector forward,
        RDF_RadarProjectileTracker tracker)
    {
        if (!m_Widgets || !m_Widgets.m_wPpiCanvas)
            return;

        if (!m_PpiAll)
            m_PpiAll = new array<ref CanvasWidgetCommand>();
        m_PpiAll.Clear();

        DrawPpiFace();
        DrawEccmJamRing();

        float fx = forward[0];
        float fz = forward[2];
        float flen = Math.Sqrt(fx * fx + fz * fz);
        if (flen > 0.0001)
        {
            float nx = fx / flen;
            float nz = fz / flen;
            float bearing = Math.Atan2(nx, nz);
            AddSweepWedge(bearing);

            array<float> sweep = new array<float>();
            AppendUnitPoint(sweep, m_PpiCx, m_PpiCy);
            AppendUnitPoint(sweep, m_PpiCx + nx * m_PpiR, m_PpiCy - nz * m_PpiR);
            LineDrawCommand edge = new LineDrawCommand();
            edge.m_iColor = COL_PPI_SWEEP;
            edge.m_fWidth = UnitSizeToPixels(3.0);
            if (edge.m_fWidth < 1.0)
                edge.m_fWidth = 1.0;
            edge.m_Vertices = sweep;
            m_PpiAll.Insert(edge);
        }

        if (m_Mode == MODE_WLR)
        {
            if (targets && m_DisplayRange > 0.0)
            {
                int index = 0;
                foreach (RDF_RadarTarget t : targets)
                {
                    if (!t)
                        continue;
                    if (!IsInDisplayRange(t, origin))
                        continue;
                    if (index >= MAX_DRAW_BLIPS)
                        break;

                    float bx;
                    float by;
                    if (!WorldToPpi(origin, t.m_Position, bx, by))
                        continue;

                    DrawPlotAfterglow(bx, by, t);
                    index = index + 1;
                }
            }

            DrawWlrShellTracks(origin, tracker);
            DrawWlrAlerts(origin);
        }
        else
        {
            DrawTwsTracks(origin, tracker);
        }

        // Multi-radar network overlay: fused tracks shared by the whole GBRS net.
        DrawNetworkFusedTracks(origin);

        m_Widgets.m_wPpiCanvas.SetDrawCommands(m_PpiAll);
    }

    // WLR live shells: draw the track file itself. Launch/impact overlays
    // need a ballistic fit; without this the PPI only shows 0.45 s pin-pricks.
    protected void DrawWlrShellTracks(vector origin, RDF_RadarProjectileTracker tracker)
    {
        if (!tracker)
            return;

        // Use the same confirmed-track clustering as PD/LOCK TWS so duplicate
        // tracker files for one physical shell do not flood the PPI.
        array<ref RDF_RadarTrack> tracks = GetCachedDisplayTracks(tracker, origin);
        if (!tracks)
            return;

        int drawn = 0;
        int i = 0;
        while (i < tracks.Count())
        {
            RDF_RadarTrack tr = tracks.Get(i);
            i = i + 1;
            if (!tr)
                continue;
            if (drawn >= MAX_DRAW_BLIPS)
                break;

            float bx;
            float by;
            if (!WorldToPpi(origin, tr.m_FilteredPosition, bx, by))
                continue;

            int color = COL_WLR_SHELL;
            float half = 7.0;

            DrawPpiSquare(bx, by, half, color);

            float dirX;
            float dirZ;
            float speed;
            TrackDisplayMotion(tr, origin, dirX, dirZ, speed);
            if (speed >= 3.0)
            {
                DrawPpiChevron(bx, by, dirX, dirZ, color);
                DrawPpiHeadingStick(bx, by, dirX, dirZ, color);
            }

            // Confirmed shell tracks get a map-grid coordinate label so the
            // operator can read launch-side positions directly from the PPI.
            string id = "W" + PadNum(tr.m_TrackId, 2);
            DrawPpiLabel(bx, by, id + " " + GetPpiMapLabel(tr.m_FilteredPosition), COL_WLR_TEXT);

            drawn = drawn + 1;
        }
    }

    // Network overlay: draw fused tracks from the shared datalink hub. These are
    // tracks confirmed by any powered GBRS station in the net, so a station can
    // see contacts detected by its peers (multi-radar network picture).
    protected void DrawNetworkFusedTracks(vector origin)
    {
        s_NetFusedCount = 0;
        s_NetStationCount = 0;
        if (m_DisplayRange <= 0.0)
            return;

        RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
        if (!hub || !hub.IsEnabled())
            return;

        array<ref RDF_RadarFusedTrack> fused = hub.GetFusedTracks();
        if (!fused || fused.Count() < 1)
            return;

        array<int> sourceIds = new array<int>();
        float rangeSq = m_DisplayRange * m_DisplayRange;
        int drawn = 0;
        foreach (RDF_RadarFusedTrack f : fused)
        {
            if (!f)
                continue;
            if (drawn >= MAX_DRAW_BLIPS)
                break;

            vector d = f.m_WorldPos - origin;
            float distSq = d[0] * d[0] + d[2] * d[2];
            if (distSq > rangeSq)
                continue;

            // Only count sources for fused tracks actually within the PPI range,
            // so 's' stays consistent with 'f' (a far/out-of-range fused track
            // does not make the readout claim a network station is present).
            int c0 = f.m_ContributorRadarId0;
            if (c0 > 0 && !sourceIds.Contains(c0))
                sourceIds.Insert(c0);

            s_NetFusedCount = s_NetFusedCount + 1;
            if (!s_NetworkOverlay)
                continue;

            float bx;
            float by;
            if (!WorldToPpi(origin, f.m_WorldPos, bx, by))
                continue;

            int color = NetworkColor(f.m_Iff);
            DrawPpiSquare(bx, by, 6.0, color);

            vector v = f.m_Velocity;
            float vLen = Math.Sqrt(v[0] * v[0] + v[2] * v[2]);
            if (vLen >= 3.0)
                DrawPpiChevron(bx, by, v[0], v[2], color);

            string tag = "NET";
            if (f.m_ContributorCount > 1)
                tag = "NET" + f.m_ContributorCount.ToString();
            DrawPpiLabel(bx, by, tag + " " + GetPpiMapLabel(f.m_WorldPos), color);

            drawn = drawn + 1;
        }

        s_NetStationCount = sourceIds.Count();
    }

    // IFF-based color for the network overlay: friend green, foe red, neutral
    // amber, unknown the default cyan-blue.
    protected int NetworkColor(ERDF_RadarIff iff)
    {
        if (iff == ERDF_RadarIff.RDF_IFF_FRIEND)
            return ARGB(230, 90, 255, 150);
        if (iff == ERDF_RadarIff.RDF_IFF_FOE)
            return ARGB(230, 255, 70, 70);
        if (iff == ERDF_RadarIff.RDF_IFF_NEUTRAL)
            return ARGB(230, 255, 200, 80);
        return COL_NET;
    }

    // "NET" status line for the footer: fused count + contributing stations.
    protected string NetworkStatusString()
    {
        if (s_NetFusedCount <= 0 && s_NetStationCount <= 0)
            return "NET --";
        return "NET f=" + s_NetFusedCount.ToString()
            + " s=" + s_NetStationCount.ToString();
    }

    protected void DrawPlotAfterglow(float bx, float by, RDF_RadarTarget t)
    {
        int color = COL_PLOT_GLOW;
        if (m_Mode == MODE_WLR)
            color = ARGB(150, 255, 220, 70);
        if (t)
        {
            if (t.m_IsFalsePlot)
                color = ARGB(90, 255, 95, 95);
            else
            {
                if (t.m_LosBlocked)
                    color = ARGB(90, 110, 235, 255);
            }
        }

        vector centerPx = m_Widgets.m_wPpiCanvas.PosToPixels(Vector(bx, by, 0.0));
        float rPx = UnitSizeToPixels(2.4);
        if (m_Mode == MODE_WLR)
            rPx = UnitSizeToPixels(4.5);
        if (rPx < 1.0)
            rPx = 1.0;

        array<float> bv = new array<float>();
        m_Widgets.m_wPpiCanvas.TessellateCircle(centerPx, rPx, 8, bv);
        PolygonDrawCommand blip = new PolygonDrawCommand();
        blip.m_iColor = color;
        blip.m_Vertices = bv;
        m_PpiAll.Insert(blip);
    }

    protected void DrawTwsTracks(vector origin, RDF_RadarProjectileTracker tracker)
    {
        array<ref RDF_RadarTrack> tracks = GetCachedDisplayTracks(tracker, origin);
        if (!tracks)
            return;

        vector lockPos;
        bool hasLock = false;
        if (m_Mode == MODE_LOCK && m_LockManager)
        {
            IEntity lockEnt;
            hasLock = m_LockManager.GetLockedTarget(lockEnt, lockPos);
        }

        int drawn = 0;
        int i = 0;
        while (i < tracks.Count())
        {
            RDF_RadarTrack tr = tracks.Get(i);
            i = i + 1;
            if (!tr)
                continue;
            if (drawn >= MAX_DRAW_BLIPS)
                break;

            float bx;
            float by;
            if (!WorldToPpi(origin, tr.m_FilteredPosition, bx, by))
                continue;

            int color = TrackColor(tr);
            float half = 6.0;

            bool locked = false;
            if (hasLock)
                locked = IsLockMatchedTrack(tr, lockPos);
            if (locked)
            {
                color = COL_LOCK;
                half = 7.5;
                DrawLockRing(bx, by, half + 5.0);
            }

            DrawPpiSquare(bx, by, half, color);

            float dirX;
            float dirZ;
            float speed;
            TrackDisplayMotion(tr, origin, dirX, dirZ, speed);
            if (speed >= 3.0)
            {
                DrawPpiChevron(bx, by, dirX, dirZ, color);
                DrawPpiHeadingStick(bx, by, dirX, dirZ, color);
            }

            // TWS tracks show map-grid coordinates directly on the PPI.
            DrawPpiLabel(bx, by, GetPpiMapLabel(tr.m_FilteredPosition), COL_TRACK_LABEL);

            drawn = drawn + 1;
        }
    }

    protected array<ref RDF_RadarTrack> GetCachedDisplayTracks(
        RDF_RadarProjectileTracker tracker,
        vector origin)
    {
        if (m_CachedDisplayTracks)
            return m_CachedDisplayTracks;

        m_CachedDisplayTracks = CollectDisplayTracks(tracker, origin);
        return m_CachedDisplayTracks;
    }

    protected array<ref RDF_RadarTrack> CollectDisplayTracks(
        RDF_RadarProjectileTracker tracker,
        vector origin)
    {
        array<ref RDF_RadarTrack> result = new array<ref RDF_RadarTrack>();
        if (!tracker)
            return result;

        array<ref RDF_RadarTrack> all = tracker.GetAllTracks();
        if (!all)
            return result;

        float gateSq = TRACK_CLUSTER_RANGE_M * TRACK_CLUSTER_RANGE_M;
        int i = 0;
        while (i < all.Count())
        {
            RDF_RadarTrack tr = all.Get(i);
            i = i + 1;
            if (!tr)
                continue;
            if (!tr.m_Confirmed)
                continue;
            if (!IsTrackInDisplayRange(tr, origin))
                continue;

            int match = -1;
            int j = 0;
            while (j < result.Count())
            {
                RDF_RadarTrack kept = result.Get(j);
                if (kept)
                {
                    if (TracksAreSameContact(tr, kept, gateSq))
                    {
                        match = j;
                        break;
                    }
                }
                j = j + 1;
            }

            if (match < 0)
            {
                result.Insert(tr);
                continue;
            }

            RDF_RadarTrack winner = result.Get(match);
            if (TrackIsBetter(tr, winner))
                result.Set(match, tr);
        }

        return result;
    }

    protected bool TracksAreSameContact(RDF_RadarTrack a, RDF_RadarTrack b, float horizGateSq)
    {
        if (!a || !b)
            return false;

        if (a.m_ScattererId > 0 && a.m_ScattererId == b.m_ScattererId)
            return true;

        float dRange = a.m_FilteredRangeM - b.m_FilteredRangeM;
        if (dRange < 0.0)
            dRange = -dRange;
        float dAz = a.m_FilteredAzimuthDeg - b.m_FilteredAzimuthDeg;
        while (dAz > 180.0)
            dAz = dAz - 360.0;
        while (dAz < -180.0)
            dAz = dAz + 360.0;
        if (dAz < 0.0)
            dAz = -dAz;
        if (dRange <= TRACK_CLUSTER_RANGE_M)
        {
            if (dAz <= TRACK_CLUSTER_AZ_DEG)
                return true;
        }

        float dx = a.m_FilteredPosition[0] - b.m_FilteredPosition[0];
        float dz = a.m_FilteredPosition[2] - b.m_FilteredPosition[2];
        if ((dx * dx + dz * dz) <= horizGateSq)
            return true;

        return false;
    }

    protected bool TrackIsBetter(RDF_RadarTrack a, RDF_RadarTrack b)
    {
        if (!a)
            return false;
        if (!b)
            return true;
        if (a.m_Coasting != b.m_Coasting)
        {
            if (!a.m_Coasting)
                return true;
            return false;
        }
        if (a.m_HitCount != b.m_HitCount)
        {
            if (a.m_HitCount > b.m_HitCount)
                return true;
            return false;
        }
        if (a.m_LastSnrDb > b.m_LastSnrDb)
            return true;
        return false;
    }

    // PPI heading is ground track (course), not LOS bearing.
    // Prefer a 1 s position chord so 25 Hz measurement noise does not yank
    // the chevron. Use FilteredVelocity only when it agrees with that chord.
    // Radial fallback is LOS * (-radial): RDF radial > 0 is approaching.
    protected void TrackDisplayMotion(
        RDF_RadarTrack tr,
        vector origin,
        out float dirX,
        out float dirZ,
        out float speed)
    {
        dirX = 0.0;
        dirZ = 1.0;
        speed = 0.0;
        if (!tr)
            return;

        bool haveChord = false;
        float chordX = 0.0;
        float chordZ = 1.0;
        float chordSpeed = 0.0;
        if (tr.m_Positions && tr.m_Times)
        {
            int n = tr.m_Positions.Count();
            if (n >= 2)
            {
                int last = n - 1;
                vector newest = tr.m_Positions.Get(last);
                float tNew = tr.m_Times.Get(last);
                int chosen = 0;
                int j = 0;
                while (j < last)
                {
                    float age = tNew - tr.m_Times.Get(j);
                    if (age >= HEADING_SPAN_S)
                        chosen = j;
                    j = j + 1;
                }

                vector older = tr.m_Positions.Get(chosen);
                float dt = tNew - tr.m_Times.Get(chosen);
                if (dt >= 0.25)
                {
                    chordX = newest[0] - older[0];
                    chordZ = newest[2] - older[2];
                    float dist = Math.Sqrt(chordX * chordX + chordZ * chordZ);
                    chordSpeed = dist / dt;
                    if (chordSpeed >= 3.0)
                        haveChord = true;
                }
            }
        }

        float vx = tr.m_FilteredVelocity[0];
        float vz = tr.m_FilteredVelocity[2];
        float vH = Math.Sqrt(vx * vx + vz * vz);
        if (vH >= 3.0)
        {
            bool agree = true;
            if (haveChord)
            {
                float dot = vx * chordX + vz * chordZ;
                if (dot <= 0.0)
                    agree = false;
            }

            if (agree)
            {
                dirX = vx;
                dirZ = vz;
                speed = vH;
                return;
            }
        }

        if (haveChord)
        {
            dirX = chordX;
            dirZ = chordZ;
            speed = chordSpeed;
            return;
        }

        float azRad = tr.m_FilteredAzimuthDeg * 0.017453292519943295;
        float rr = tr.m_FilteredRangeRateMs;
        dirX = Math.Cos(azRad) * (-rr);
        dirZ = Math.Sin(azRad) * (-rr);
        if (rr < 0.0)
            speed = -rr;
        else
            speed = rr;
    }

    protected bool IsTrackInDisplayRange(RDF_RadarTrack tr, vector origin)
    {
        if (!tr)
            return false;

        float rng = tr.m_FilteredRangeM;
        if (rng <= 0.0)
        {
            vector d = tr.m_FilteredPosition - origin;
            rng = d.Length();
        }

        if (m_DisplayRange <= 0.0)
            return true;

        if (rng > m_DisplayRange)
            return false;

        return true;
    }

    protected int TrackColor(RDF_RadarTrack tr)
    {
        if (!tr)
            return COL_TRACK_TENT;
        if (tr.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return COL_EMITTER;
        if (tr.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return COL_PROJ;
        if (tr.m_Coasting)
            return COL_TRACK_COAST;
        if (!tr.m_Confirmed)
            return COL_TRACK_TENT;
        return COL_TRACK;
    }

    protected bool IsLockMatchedTrack(RDF_RadarTrack tr, vector lockPos)
    {
        if (!tr)
            return false;

        float dist = vector.Distance(tr.m_FilteredPosition, lockPos);
        if (dist <= 80.0)
            return true;

        return false;
    }

    protected void AddSweepWedge(float bearingRad)
    {
        float half = SWEEP_HALF_DEG * 0.017453292519943295;
        float a0 = bearingRad - half;
        float a1 = bearingRad + half;
        array<float> wedge = new array<float>();
        AppendUnitPoint(wedge, m_PpiCx, m_PpiCy);
        int i = 0;
        while (i <= SWEEP_SEGMENTS)
        {
            float t = i;
            t = t / SWEEP_SEGMENTS;
            float a = a0 + (a1 - a0) * t;
            AppendUnitPoint(wedge, m_PpiCx + Math.Sin(a) * m_PpiR, m_PpiCy - Math.Cos(a) * m_PpiR);
            i = i + 1;
        }
        PolygonDrawCommand poly = new PolygonDrawCommand();
        poly.m_iColor = COL_PPI_WEDGE;
        poly.m_Vertices = wedge;
        m_PpiAll.Insert(poly);
    }

    // True when every component is a real, finite number. Guards the PPI draw
    // path against NaN/Inf positions leaking in from an RDF ballistic fix or a
    // track extrapolation — a non-finite pixel coordinate reaching
    // TessellateCircle / LineDrawCommand vertices is what produced the
    // UpdateEntities access-violation during counter-battery WLR tests when the
    // PPI had drawn shell blips but before the launch/impact prediction markers
    // could render.
    protected bool VecFinite(vector v)
    {
        float a = v[0];
        float b = v[1];
        float c = v[2];
        if (a != a || b != b || c != c)
            return false;
        if (a > float.INFINITY || a < -float.INFINITY)
            return false;
        if (b > float.INFINITY || b < -float.INFINITY)
            return false;
        if (c > float.INFINITY || c < -float.INFINITY)
            return false;
        return true;
    }

    // Returns false when the plot is outside the current display range or the
    // position is not finite.
    protected bool WorldToPpi(vector origin, vector worldPos, out float outX, out float outY)
    {
        outX = m_PpiCx;
        outY = m_PpiCy;

        if (m_DisplayRange <= 0.0)
            return false;
        if (!VecFinite(worldPos) || !VecFinite(origin))
            return false;

        vector delta = worldPos - origin;
        float normX = delta[0] / m_DisplayRange;
        float normZ = delta[2] / m_DisplayRange;
        float d2 = normX * normX + normZ * normZ;
        if (d2 > 1.0)
            return false;

        outX = m_PpiCx + normX * m_PpiR;
        outY = m_PpiCy - normZ * m_PpiR;
        return true;
    }

    // Clamp to PPI rim so WLR launch/impact outside range still draw. Rejects
    // non-finite positions so a bad ballistic fix can never feed the draw path.
    protected bool WorldToPpiClamped(vector origin, vector worldPos, out float outX, out float outY)
    {
        outX = m_PpiCx;
        outY = m_PpiCy;

        if (m_DisplayRange <= 0.0)
            return false;
        if (!VecFinite(worldPos) || !VecFinite(origin))
            return false;

        vector delta = worldPos - origin;
        float normX = delta[0] / m_DisplayRange;
        float normZ = delta[2] / m_DisplayRange;
        float d2 = normX * normX + normZ * normZ;
        if (d2 > 1.0)
        {
            float d = Math.Sqrt(d2);
            normX = normX / d;
            normZ = normZ / d;
        }

        outX = m_PpiCx + normX * m_PpiR;
        outY = m_PpiCy - normZ * m_PpiR;
        return true;
    }

    // Merge live WLR tracks into a persistent display list so launch/impact
    // markers do not blink out the moment the raw track disappears.
    protected void UpdateWlrPersist(vector origin, RDF_RadarProjectileTracker tracker)
    {
        if (!m_WlrPersist)
            m_WlrPersist = new array<ref GBRS_WlrPersistDisplay>();

        float worldNowS = GetWorldTimeS();

        if (tracker)
        {
            // Use the clustered/confirmed display set so duplicate tracker files
            // for the same physical shell do not create duplicate LCH/IMP chains.
            array<ref RDF_RadarTrack> all = GetCachedDisplayTracks(tracker, origin);
            if (all)
            {
                int i = 0;
                while (i < all.Count())
                {
                    RDF_RadarTrack tr = all.Get(i);
                    i = i + 1;
                    if (!tr)
                        continue;

                    GBRS_RadarWlrSolution sol = GBRS_RadarWlrBallisticSolver.Resolve(tr);
                    RDF_RadarWlrFix fix = null;
                    if (sol)
                        fix = sol.m_Fix;
                    if (!fix)
                        continue;
                    if (!fix.m_LaunchValid && !fix.m_ImpactValid)
                        continue;

                    GBRS_WlrPersistDisplay entry = FindWlrPersist(tr.m_TrackId);
                    if (!entry)
                    {
                        entry = new GBRS_WlrPersistDisplay();
                        entry.m_TrackId = tr.m_TrackId;
                        m_WlrPersist.Insert(entry);
                    }

                    entry.m_Id = "W" + PadNum(tr.m_TrackId, 2);
                    entry.m_LastSeenS = worldNowS;
                    entry.m_HasLaunch = fix.m_LaunchValid;
                    if (fix.m_LaunchValid)
                        entry.m_LaunchPos = fix.m_LaunchPos;
                    entry.m_HasImpact = fix.m_ImpactValid;
                    if (fix.m_ImpactValid)
                    {
                        entry.m_ImpactPos = fix.m_ImpactPos;
                        entry.m_ImpactTimeS = fix.m_ImpactTimeS;
                    }

                    // Use the already-filtered position for the live marker,
                    // NOT track.PredictAt(). HUD reads track.m_LastWlrFix and the
                    // filtered state only — running a second ballistic/extrapolation
                    // solve here re-entered the RDF solver on the feed tick and
                    // could AV-crash the DEM/runtime solver right as a fresh fix
                    // became available (shells on the PPI, prediction not yet up).
                    entry.m_LivePos = tr.m_FilteredPosition;
                    entry.m_LiveVel = tr.m_FilteredVelocity;
                    entry.m_HasLive = true;
                }
            }
        }

        // Keep the last known live shell position for the persist window so
        // the chain does not break when a track briefly coasts or drops.

        int k = m_WlrPersist.Count() - 1;
        while (k >= 0)
        {
            GBRS_WlrPersistDisplay entry = m_WlrPersist.Get(k);
            k = k - 1;
            if (!entry)
                continue;
            if ((worldNowS - entry.m_LastSeenS) > WLR_PERSIST_S)
                m_WlrPersist.Remove(k + 1);
            else if (entry.m_HasImpact && (worldNowS - entry.m_ImpactTimeS) > WLR_IMPACT_PERSIST_S)
                m_WlrPersist.Remove(k + 1);
        }

        while (m_WlrPersist.Count() > MAX_WLR_PERSIST)
            m_WlrPersist.Remove(0);
    }

    protected GBRS_WlrPersistDisplay FindWlrPersist(int trackId)
    {
        if (!m_WlrPersist)
            return null;

        int i = 0;
        while (i < m_WlrPersist.Count())
        {
            GBRS_WlrPersistDisplay entry = m_WlrPersist.Get(i);
            i = i + 1;
            if (entry && entry.m_TrackId == trackId)
                return entry;
        }

        return null;
    }

    protected void DrawWlrAlerts(vector origin)
    {
        if (!m_Widgets || !m_Widgets.m_wPpiCanvas)
            return;
        if (m_DisplayRange <= 0.0)
            return;
        if (!m_WlrPersist)
            return;

        float nowS = GetWorldTimeS();
        float alertNorm = WLR_ALERT_RADIUS_M / m_DisplayRange;
        float alertUnit = alertNorm * m_PpiR;
        if (alertUnit < 6.0)
            alertUnit = 6.0;
        if (alertUnit > 22.0)
            alertUnit = 22.0;

        int i = 0;
        while (i < m_WlrPersist.Count())
        {
            GBRS_WlrPersistDisplay entry = m_WlrPersist.Get(i);
            i = i + 1;
            if (!entry)
                continue;
            if (!entry.m_HasLaunch && !entry.m_HasImpact && !entry.m_HasLive)
                continue;
            if (entry.m_HasImpact && (nowS - entry.m_ImpactTimeS) > WLR_IMPACT_PERSIST_S)
                continue;

            string id = entry.m_Id;
            float sx;
            float sy;
            bool liveOnPpi = false;
            if (entry.m_HasLive)
            {
                // After impact the live shell is gone; keep the chain as a
                // launch -> impact line instead of a frozen mid-air point.
                if (!entry.m_HasImpact || nowS < entry.m_ImpactTimeS)
                    liveOnPpi = WorldToPpi(origin, entry.m_LivePos, sx, sy);
            }

            if (entry.m_HasLaunch && entry.m_HasImpact)
            {
                float lx0;
                float ly0;
                float lx1;
                float ly1;
                if (WorldToPpiClamped(origin, entry.m_LaunchPos, lx0, ly0))
                {
                    if (WorldToPpiClamped(origin, entry.m_ImpactPos, lx1, ly1))
                    {
                        // Always connect launch and impact with a clear solid
                        // line; then overlay the live-remaining segment.
                        DrawSolidPpiLine(lx0, ly0, lx1, ly1, COL_WLR_LINK, 2.2);
                        if (liveOnPpi)
                            DrawDashedPpiLine(sx, sy, lx1, ly1, COL_WLR_REMAIN, 2.2, 12);
                    }
                }
            }

            if (entry.m_HasLaunch)
            {
                DrawPpiAlertRing(origin, entry.m_LaunchPos, alertUnit, COL_WLR_LAUNCH);
                float lx;
                float ly;
                if (WorldToPpiClamped(origin, entry.m_LaunchPos, lx, ly))
                {
                    DrawPpiSquare(lx, ly, 5.0, COL_WLR_LAUNCH);
                    DrawPpiLabel(lx, ly, "LCH " + id + " " + GetPpiMapLabel(entry.m_LaunchPos), COL_WLR_LAUNCH);
                }
            }

            if (entry.m_HasImpact)
            {
                DrawPpiAlertRing(origin, entry.m_ImpactPos, alertUnit, COL_WLR_IMPACT);
                float ix;
                float iy;
                if (WorldToPpiClamped(origin, entry.m_ImpactPos, ix, iy))
                {
                    DrawPpiCross(ix, iy, 7.0, COL_WLR_IMPACT);
                    string eta = FormatEtaS(entry.m_ImpactTimeS - nowS);
                    DrawPpiLabel(ix, iy, "IMP " + eta + " " + GetPpiMapLabel(entry.m_ImpactPos), COL_WLR_IMPACT);
                }
            }

            // If the raw shell track has dropped but the solution is still
            // within the persist window, keep a frozen last-known shell marker.
            if (liveOnPpi && (nowS - entry.m_LastSeenS) > 0.5)
                DrawPpiChevron(sx, sy, entry.m_LiveVel[0], entry.m_LiveVel[2], COL_WLR_SHELL);
        }
    }

    protected void DrawSolidPpiLine(float x0, float y0, float x1, float y1, int color, float widthUnit)
    {
        if (x0 != x0 || y0 != y0 || x1 != x1 || y1 != y1)
            return;
        if (x0 > float.INFINITY || x0 < -float.INFINITY)
            return;
        if (y0 > float.INFINITY || y0 < -float.INFINITY)
            return;
        if (x1 > float.INFINITY || x1 < -float.INFINITY)
            return;
        if (y1 > float.INFINITY || y1 < -float.INFINITY)
            return;

        array<float> verts = new array<float>();
        AppendUnitPoint(verts, x0, y0);
        AppendUnitPoint(verts, x1, y1);
        LineDrawCommand link = new LineDrawCommand();
        link.m_iColor = color;
        link.m_fWidth = UnitSizeToPixels(widthUnit);
        if (link.m_fWidth < 1.0)
            link.m_fWidth = 1.0;
        link.m_Vertices = verts;
        m_PpiAll.Insert(link);
    }

    protected void DrawDashedPpiLine(float x0, float y0, float x1, float y1, int color, float widthUnit, int dashes)
    {
        if (dashes < 2)
            dashes = 2;

        float dashesF = dashes;
        int i = 0;
        while (i < dashes)
        {
            int odd = i % 2;
            if (odd == 1)
            {
                i = i + 1;
                continue;
            }

            float t0 = i;
            t0 = t0 / dashesF;
            float t1 = i + 1;
            t1 = t1 / dashesF;
            float ax = x0 + (x1 - x0) * t0;
            float ay = y0 + (y1 - y0) * t0;
            float bx = x0 + (x1 - x0) * t1;
            float by = y0 + (y1 - y0) * t1;
            DrawSolidPpiLine(ax, ay, bx, by, color, widthUnit);
            i = i + 1;
        }
    }

    protected void DrawPpiSquare(float cx, float cy, float half, int color)
    {
        array<float> verts = new array<float>();
        AppendUnitPoint(verts, cx - half, cy - half);
        AppendUnitPoint(verts, cx + half, cy - half);
        AppendUnitPoint(verts, cx + half, cy + half);
        AppendUnitPoint(verts, cx - half, cy + half);
        AppendUnitPoint(verts, cx - half, cy - half);
        LineDrawCommand sq = new LineDrawCommand();
        sq.m_iColor = color;
        sq.m_fWidth = UnitSizeToPixels(1.6);
        if (sq.m_fWidth < 1.0)
            sq.m_fWidth = 1.0;
        sq.m_Vertices = verts;
        m_PpiAll.Insert(sq);
    }

    protected void DrawPpiCross(float cx, float cy, float half, int color)
    {
        DrawSolidPpiLine(cx - half, cy, cx + half, cy, color, 1.8);
        DrawSolidPpiLine(cx, cy - half, cx, cy + half, color, 1.8);
    }

    protected void DrawPpiChevron(float bx, float by, float dirX, float dirZ, int color)
    {
        float len = Math.Sqrt(dirX * dirX + dirZ * dirZ);
        float px = 0.0;
        float py = -1.0;
        if (len > 0.001)
        {
            px = dirX / len;
            py = -dirZ / len;
        }

        float size = 11.0;
        float tx = -py;
        float ty = px;
        float noseX = bx + px * size;
        float noseY = by + py * size;
        float leftX = bx - px * size * 0.55 + tx * size * 0.45;
        float leftY = by - py * size * 0.55 + ty * size * 0.45;
        float rightX = bx - px * size * 0.55 - tx * size * 0.45;
        float rightY = by - py * size * 0.55 - ty * size * 0.45;

        array<float> verts = new array<float>();
        AppendUnitPoint(verts, noseX, noseY);
        AppendUnitPoint(verts, leftX, leftY);
        AppendUnitPoint(verts, rightX, rightY);
        PolygonDrawCommand chev = new PolygonDrawCommand();
        chev.m_iColor = color;
        chev.m_Vertices = verts;
        m_PpiAll.Insert(chev);
    }

    protected void DrawPpiHeadingStick(float bx, float by, float dirX, float dirZ, int color)
    {
        float len = Math.Sqrt(dirX * dirX + dirZ * dirZ);
        if (len < 0.001)
            return;

        float px = dirX / len;
        float py = -dirZ / len;
        DrawSolidPpiLine(bx, by, bx + px * 18.0, by + py * 18.0, color, 2.0);
    }

    protected void DrawPpiLabel(float unitX, float unitY, string text, int color)
    {
        if (text == "")
            return;
        if (!m_Widgets || !m_Widgets.m_wPpiCanvas)
            return;

        float tx = unitX + 10.0;
        float ty = unitY - 8.0;
        if (tx > m_PpiCx + m_PpiR - 90.0)
            tx = unitX - 92.0;
        if (ty < m_PpiCy - m_PpiR + 6.0)
            ty = unitY + 10.0;

        vector posPx = m_Widgets.m_wPpiCanvas.PosToPixels(Vector(tx, ty, 0.0));
        vector sizePx = m_Widgets.m_wPpiCanvas.SizeToPixels(Vector(13.0, 13.0, 0.0));
        float fontSize = sizePx[0];
        if (fontSize < 11.0)
            fontSize = 11.0;

        TextDrawCommand cmd = new TextDrawCommand();
        cmd.m_sText = text;
        cmd.m_Position = posPx;
        cmd.m_iColor = color;
        cmd.m_fSize = fontSize;
        cmd.m_Pivot = Vector(0.0, 0.0, 0.0);
        cmd.m_fRotation = 0.0;
        cmd.m_iFontPropertiesId = 0;
        m_PpiAll.Insert(cmd);
    }

    protected void DrawPpiAlertRing(vector origin, vector worldPos, float radiusUnit, int color)
    {
        float bx;
        float by;
        if (!WorldToPpiClamped(origin, worldPos, bx, by))
            return;

        vector centerPx = m_Widgets.m_wPpiCanvas.PosToPixels(Vector(bx, by, 0.0));
        if (!VecFinite(centerPx))
            return;
        float ringPx = UnitSizeToPixels(radiusUnit);
        if (ringPx <= 0.0 || ringPx != ringPx || ringPx > float.INFINITY)
            return;
        if (centerPx[0] > 20000.0 || centerPx[1] > 20000.0)
            return;
        if (centerPx[0] < -20000.0 || centerPx[1] < -20000.0)
            return;
        ringPx = Math.Clamp(ringPx, 3.0, 200.0);

        array<float> ringVerts = new array<float>();
        m_Widgets.m_wPpiCanvas.TessellateCircle(centerPx, ringPx, 20, ringVerts);
        LineDrawCommand ring = new LineDrawCommand();
        ring.m_iColor = color;
        ring.m_fWidth = UnitSizeToPixels(1.5);
        if (ring.m_fWidth < 1.0)
            ring.m_fWidth = 1.0;
        ring.m_bShouldEnclose = true;
        ring.m_Vertices = ringVerts;
        m_PpiAll.Insert(ring);

        float corePx = UnitSizeToPixels(2.5);
        if (corePx <= 0.0 || corePx != corePx || corePx > float.INFINITY)
            corePx = 1.5;
        corePx = Math.Clamp(corePx, 1.5, 60.0);
        array<float> coreVerts = new array<float>();
        m_Widgets.m_wPpiCanvas.TessellateCircle(centerPx, corePx, 8, coreVerts);
        PolygonDrawCommand core = new PolygonDrawCommand();
        core.m_iColor = color;
        core.m_Vertices = coreVerts;
        m_PpiAll.Insert(core);
    }

    protected void DrawLockRing(float bx, float by, float radiusUnit)
    {
        vector centerPx = m_Widgets.m_wPpiCanvas.PosToPixels(Vector(bx, by, 0.0));
        float ringPx = UnitSizeToPixels(radiusUnit);
        if (ringPx < 4.0)
            ringPx = 4.0;

        array<float> ringVerts = new array<float>();
        m_Widgets.m_wPpiCanvas.TessellateCircle(centerPx, ringPx, 16, ringVerts);
        LineDrawCommand ring = new LineDrawCommand();
        ring.m_iColor = COL_LOCK;
        ring.m_fWidth = UnitSizeToPixels(2.0);
        if (ring.m_fWidth < 1.0)
            ring.m_fWidth = 1.0;
        ring.m_bShouldEnclose = true;
        ring.m_Vertices = ringVerts;
        m_PpiAll.Insert(ring);
    }

    // RDF 1.0.0 ECCM active → pulsing red warning ring just inside the PPI rim.
    // Uses the same sweep geometry as the scan wedge so jamming is visible in
    // all workstation modes while the decision layer fights it.
    protected void DrawEccmJamRing()
    {
        if (m_EccmStatus == "eccm=0")
            return;
        if (!m_Widgets || !m_Widgets.m_wPpiCanvas)
            return;

        float nowS = System.GetTickCount() * 0.001;
        float pulse = 0.5 + 0.5 * Math.Sin(nowS * 3.0);
        int alpha = Math.Round(90.0 + 90.0 * pulse);
        if (alpha < 40)
            alpha = 40;
        if (alpha > 255)
            alpha = 255;
        int ringColour = ARGB(alpha, 255, 70, 70);

        vector centerPx = m_Widgets.m_wPpiCanvas.PosToPixels(Vector(m_PpiCx, m_PpiCy, 0.0));
        float rimPx = UnitSizeToPixels(m_PpiR - 10.0);
        if (rimPx < 8.0)
            rimPx = 8.0;

        array<float> ringVerts = new array<float>();
        m_Widgets.m_wPpiCanvas.TessellateCircle(centerPx, rimPx, 28, ringVerts);
        LineDrawCommand ring = new LineDrawCommand();
        ring.m_iColor = ringColour;
        ring.m_fWidth = UnitSizeToPixels(3.0);
        if (ring.m_fWidth < 1.5)
            ring.m_fWidth = 1.5;
        ring.m_bShouldEnclose = true;
        ring.m_Vertices = ringVerts;
        m_PpiAll.Insert(ring);
    }

    protected bool IsLockMatchedBlip(RDF_RadarTarget t, vector lockPos)
    {
        if (!t)
            return false;

        // Distance gate fallback keeps the blip highlighted while kinematics
        // are still converging to the locked entity's true position.
        float dist = vector.Distance(t.m_Position, lockPos);
        if (dist <= 80.0)
            return true;

        // RDF 1.0.0 TruthSample split: plot.m_Entity stays null when
        // KeepEntityTruth is off. Resolve the scatterer handle instead and
        // compare roots against the lock manager's entity.
        if (m_LockManager)
        {
            IEntity lockedEntity = m_LockManager.GetLockedEntity();
            if (!lockedEntity)
                return false;

            IEntity lockedRoot = lockedEntity.GetRootParent();
            if (!lockedRoot)
                lockedRoot = lockedEntity;

            if (t.m_ScattererId > 0)
            {
                RDF_RadarScatterer entry = RDF_RadarScattererRegistry.FindById(t.m_ScattererId);
                if (entry && entry.m_Entity)
                {
                    IEntity plotRoot = entry.m_Entity.GetRootParent();
                    if (!plotRoot)
                        plotRoot = entry.m_Entity;
                    if (plotRoot == lockedRoot)
                        return true;
                }
            }
        }

        return false;
    }

    protected void DrawBlip(float bx, float by, float r, int color)
    {
        int a;
        int rr;
        int gg;
        int bb;
        Color.UnpackInt(color, a, rr, gg, bb);
        int halo = ARGB(90, rr, gg, bb);

        vector centerPx = m_Widgets.m_wPpiCanvas.PosToPixels(Vector(bx, by, 0.0));
        float rPx = UnitSizeToPixels(r);
        float haloPx = UnitSizeToPixels(r + 3.0);
        if (rPx < 1.0)
            rPx = 1.0;
        if (haloPx < rPx + 1.0)
            haloPx = rPx + 1.0;

        array<float> hv = new array<float>();
        m_Widgets.m_wPpiCanvas.TessellateCircle(centerPx, haloPx, 10, hv);
        PolygonDrawCommand haloPoly = new PolygonDrawCommand();
        haloPoly.m_iColor = halo;
        haloPoly.m_Vertices = hv;
        m_PpiAll.Insert(haloPoly);

        array<float> bv = new array<float>();
        m_Widgets.m_wPpiCanvas.TessellateCircle(centerPx, rPx, 10, bv);
        PolygonDrawCommand blip = new PolygonDrawCommand();
        blip.m_iColor = color;
        blip.m_Vertices = bv;
        m_PpiAll.Insert(blip);
    }

    protected void DrawVelocity(RDF_RadarTarget t, float bx, float by)
    {
        float vx = t.m_Velocity[0];
        float vz = t.m_Velocity[2];
        float speed = Math.Sqrt(vx * vx + vz * vz);
        if (speed < 2.0)
            return;
        float dx = vx * 0.22;
        float dy = -vz * 0.22;
        float len = Math.Sqrt(dx * dx + dy * dy);
        if (len < 4.0)
            return;
        if (len > 28.0)
        {
            dx = dx * (28.0 / len);
            dy = dy * (28.0 / len);
        }
        array<float> verts = new array<float>();
        AppendUnitPoint(verts, bx, by);
        AppendUnitPoint(verts, bx + dx, by + dy);
        LineDrawCommand tick = new LineDrawCommand();
        tick.m_iColor = COL_VEL;
        tick.m_fWidth = UnitSizeToPixels(2.0);
        if (tick.m_fWidth < 1.0)
            tick.m_fWidth = 1.0;
        tick.m_Vertices = verts;
        m_PpiAll.Insert(tick);
    }

    protected void UpdateAzEl(
        array<ref RDF_RadarTarget> targets,
        vector origin,
        RDF_RadarProjectileTracker tracker)
    {
        if (!m_Widgets || !m_Widgets.m_wAzElCanvas)
            return;

        if (!m_AzElAll)
            m_AzElAll = new array<ref CanvasWidgetCommand>();
        m_AzElAll.Clear();

        DrawAzElGrid();

        if (m_Mode == MODE_WLR)
        {
            DrawAzElPlots(targets, origin);
            DrawAzElTracks(origin, tracker);
        }
        else
        {
            DrawAzElTracks(origin, tracker);
        }

        m_Widgets.m_wAzElCanvas.SetDrawCommands(m_AzElAll);
    }

    protected void DrawAzElPlots(array<ref RDF_RadarTarget> targets, vector origin)
    {
        if (!targets)
            return;

        int index = 0;
        foreach (RDF_RadarTarget t : targets)
        {
            if (!t)
                continue;
            if (!IsInDisplayRange(t, origin))
                continue;
            if (index >= MAX_DRAW_BLIPS)
                break;

            float az = NorthUpAzimuthDeg(t, origin);
            float el = NorthUpElevationDeg(t, origin);
            DrawAzElMark(az, el, BlipColor(t), 4.5);
            index = index + 1;
        }
    }

    protected void DrawAzElTracks(vector origin, RDF_RadarProjectileTracker tracker)
    {
        array<ref RDF_RadarTrack> tracks = GetCachedDisplayTracks(tracker, origin);
        if (!tracks)
            return;

        int index = 0;
        int i = 0;
        while (i < tracks.Count())
        {
            RDF_RadarTrack tr = tracks.Get(i);
            i = i + 1;
            if (!tr)
                continue;
            if (index >= MAX_DRAW_BLIPS)
                break;

            vector d = tr.m_FilteredPosition - origin;
            float az = Math.Atan2(d[0], d[2]) * Math.RAD2DEG;
            if (az < 0.0)
                az = az + 360.0;
            float horiz = Math.Sqrt(d[0] * d[0] + d[2] * d[2]);
            float el = Math.Atan2(d[1], Math.Max(0.001, horiz)) * Math.RAD2DEG;
            DrawAzElMark(az, el, TrackColor(tr), 5.0);
            index = index + 1;
        }
    }

    protected void DrawAzElMark(float azDeg, float elDeg, int color, float sizeUnit)
    {
        float el = elDeg;
        if (el < AZEL_EL_MIN)
            el = AZEL_EL_MIN;
        if (el > AZEL_EL_MAX)
            el = AZEL_EL_MAX;

        float x = (azDeg / 360.0) * m_AzElW;
        float y = m_AzElH - ((el - AZEL_EL_MIN) / (AZEL_EL_MAX - AZEL_EL_MIN)) * m_AzElH;

        vector centerPx = m_Widgets.m_wAzElCanvas.PosToPixels(Vector(x, y, 0.0));
        vector sizePx = m_Widgets.m_wAzElCanvas.SizeToPixels(Vector(sizeUnit, sizeUnit, 0.0));
        float rPx = sizePx[0];
        if (rPx < 1.0)
            rPx = 1.0;

        array<float> verts = new array<float>();
        m_Widgets.m_wAzElCanvas.TessellateCircle(centerPx, rPx, 8, verts);
        PolygonDrawCommand blip = new PolygonDrawCommand();
        blip.m_iColor = color;
        blip.m_Vertices = verts;
        m_AzElAll.Insert(blip);
    }

    // Draw AZ/EL grid in canvas units so every line survives UI scale.
    // Layout ImageWidget 1px lines vanish at the right edge in-game.
    protected void DrawAzElGrid()
    {
        if (!m_Widgets || !m_Widgets.m_wAzElCanvas)
            return;

        float inset = 1.0;
        float x0 = inset;
        float y0 = inset;
        float x1 = m_AzElW - inset;
        float y1 = m_AzElH - inset;

        int i = 0;
        while (i <= 6)
        {
            float x = x0 + (x1 - x0) * i / 6.0;
            DrawAzElGridLine(x, y0, x, y1);
            i = i + 1;
        }

        i = 0;
        while (i <= 4)
        {
            float y = y0 + (y1 - y0) * i / 4.0;
            DrawAzElGridLine(x0, y, x1, y);
            i = i + 1;
        }
    }

    protected void DrawAzElGridLine(float x0, float y0, float x1, float y1)
    {
        array<float> verts = new array<float>();
        vector p0 = m_Widgets.m_wAzElCanvas.PosToPixels(Vector(x0, y0, 0.0));
        vector p1 = m_Widgets.m_wAzElCanvas.PosToPixels(Vector(x1, y1, 0.0));
        verts.Insert(p0[0]);
        verts.Insert(p0[1]);
        verts.Insert(p1[0]);
        verts.Insert(p1[1]);

        LineDrawCommand line = new LineDrawCommand();
        line.m_iColor = COL_AZEL_GRID;
        vector widthPx = m_Widgets.m_wAzElCanvas.SizeToPixels(Vector(1.5, 1.5, 0.0));
        line.m_fWidth = widthPx[0];
        if (line.m_fWidth < 1.0)
            line.m_fWidth = 1.0;
        line.m_Vertices = verts;
        m_AzElAll.Insert(line);
    }

    protected void UpdateList(
        array<ref RDF_RadarTarget> targets,
        vector origin,
        RDF_RadarProjectileTracker tracker)
    {
        if (!m_Widgets)
            return;

        if (m_Mode == GBRS_RadarStationConstants.MODE_MANUAL)
        {
            SetContactsTableVisible(false);
            if (m_Widgets.m_wListBody && m_ManualParamText != "")
                m_Widgets.m_wListBody.SetText(m_ManualParamText);
            return;
        }

        if (m_Mode == MODE_WLR)
        {
            SetContactsTableVisible(false);
            if (m_Widgets.m_wListBody)
                m_Widgets.m_wListBody.SetText(BuildWlrSolutionBody(origin, tracker));
            UpdateListFooter(0, tracker);
            return;
        }

        SetContactsTableVisible(true);

        string colNr = "";
        string colAz = "";
        string colRng = "";
        string colAlt = "";
        string colSpd = "";
        string colType = "";
        string colSnr = "";
        int row = 0;
        if (tracker)
        {
            row = AppendTrackListRows(
                tracker,
                origin,
                colNr,
                colAz,
                colRng,
                colAlt,
                colSpd,
                colType,
                colSnr);
        }
        else
        {
            if (targets)
            {
                row = AppendPlotListRows(
                    targets,
                    origin,
                    colNr,
                    colAz,
                    colRng,
                    colAlt,
                    colSpd,
                    colType,
                    colSnr);
            }
        }

        if (row == 0)
        {
            colNr = "--";
            colAz = "---";
            colRng = "-.-";
            colAlt = "-.-";
            colSpd = "---";
            colType = "----";
            colSnr = "--";
        }

        SetListCol(m_Widgets.m_wListBNr, colNr);
        SetListCol(m_Widgets.m_wListBAz, colAz);
        SetListCol(m_Widgets.m_wListBRng, colRng);
        SetListCol(m_Widgets.m_wListBAlt, colAlt);
        SetListCol(m_Widgets.m_wListBSpd, colSpd);
        SetListCol(m_Widgets.m_wListBType, colType);
        SetListCol(m_Widgets.m_wListBSnr, colSnr);

        UpdateListFooter(row, tracker);
    }

    protected int AppendTrackListRows(
        RDF_RadarProjectileTracker tracker,
        vector origin,
        inout string colNr,
        inout string colAz,
        inout string colRng,
        inout string colAlt,
        inout string colSpd,
        inout string colType,
        inout string colSnr)
    {
        array<ref RDF_RadarTrack> tracks = GetCachedDisplayTracks(tracker, origin);
        int row = 0;
        if (!tracks)
            return row;

        int i = 0;
        while (i < tracks.Count())
        {
            RDF_RadarTrack tr = tracks.Get(i);
            i = i + 1;
            if (!tr)
                continue;
            if (row >= MAX_LIST_ROWS)
                break;

            vector d = tr.m_FilteredPosition - origin;
            float az = Math.Atan2(d[0], d[2]) * Math.RAD2DEG;
            if (az < 0.0)
                az = az + 360.0;
            float rngKm = tr.m_FilteredRangeM / 1000.0;
            if (rngKm <= 0.0)
                rngKm = d.Length() / 1000.0;
            float altKm = tr.m_FilteredPosition[1] / 1000.0;
            float dirX;
            float dirZ;
            float spd;
            TrackDisplayMotion(tr, origin, dirX, dirZ, spd);

            colNr = AppendColLine(colNr, PadNum(tr.m_TrackId, 2));
            colAz = AppendColLine(colAz, PadNum(az, 3));
            colRng = AppendColLine(colRng, Fmt1(rngKm));
            colAlt = AppendColLine(colAlt, Fmt1(altKm));
            colSpd = AppendColLine(colSpd, PadNum(spd, 3));
            colType = AppendColLine(colType, TrackTypeTag(tr));
            colSnr = AppendColLine(colSnr, F0(tr.m_LastSnrDb));
            row = row + 1;
        }

        return row;
    }

    protected int AppendPlotListRows(
        array<ref RDF_RadarTarget> targets,
        vector origin,
        inout string colNr,
        inout string colAz,
        inout string colRng,
        inout string colAlt,
        inout string colSpd,
        inout string colType,
        inout string colSnr)
    {
        int row = 0;
        foreach (RDF_RadarTarget t : targets)
        {
            if (!t)
                continue;
            if (!IsInDisplayRange(t, origin))
                continue;
            if (row >= MAX_LIST_ROWS)
                break;

            float az = NorthUpAzimuthDeg(t, origin);
            float rngKm = t.m_Distance / 1000.0;
            if (rngKm <= 0.0)
            {
                vector d = t.m_Position - origin;
                rngKm = d.Length() / 1000.0;
            }
            float altKm = t.m_Position[1] / 1000.0;
            float spd = t.m_Velocity.Length();

            colNr = AppendColLine(colNr, PadNum(row, 2));
            colAz = AppendColLine(colAz, PadNum(az, 3));
            colRng = AppendColLine(colRng, Fmt1(rngKm));
            colAlt = AppendColLine(colAlt, Fmt1(altKm));
            colSpd = AppendColLine(colSpd, PadNum(spd, 3));
            colType = AppendColLine(colType, TypeTag(t));
            colSnr = AppendColLine(colSnr, F0(t.m_SnrDb));
            row = row + 1;
        }

        return row;
    }

    protected string TrackTypeTag(RDF_RadarTrack tr)
    {
        if (!tr)
            return "----";
        if (tr.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return "SHELL";
        if (tr.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return "EMIT";
        if (!tr.m_Confirmed)
            return "TENT";
        if (tr.m_Coasting)
            return "COAST";
        return "TRK ";
    }

    protected void SetListCol(TextWidget w, string text)
    {
        if (w)
            w.SetText(text);
    }

    protected string AppendColLine(string col, string cell)
    {
        if (col == "")
            return cell;
        return col + "\n" + cell;
    }

    protected void UpdateListFooter(int row, RDF_RadarProjectileTracker tracker)
    {
        if (!m_Widgets || !m_Widgets.m_wListFooter)
            return;

        int tracks = row;
        int wlrFixes = 0;
        if (m_Mode == MODE_WLR)
        {
            tracks = 0;
            if (m_WlrPersist)
            {
                int i = 0;
                while (i < m_WlrPersist.Count())
                {
                    GBRS_WlrPersistDisplay entry = m_WlrPersist.Get(i);
                    i = i + 1;
                    if (!entry)
                        continue;

                    tracks = tracks + 1;
                    if (entry.m_HasLaunch)
                        wlrFixes = wlrFixes + 1;
                }
            }
        }

        int detected = m_DetectedTotal;
        if (detected < row)
            detected = row;

        string footer = "DET " + detected.ToString()
            + "   TRK " + tracks.ToString()
            + "   RNG " + RangeLabel(m_DisplayRange);

        if (m_Mode == MODE_WLR)
        {
            footer = footer + "   WLR " + wlrFixes.ToString();
        }
        else if (m_Mode == MODE_LOCK)
        {
            string lockStatus = "SEARCH";
            if (m_LockManager)
                lockStatus = m_LockManager.GetStatusShort();
            footer = footer + "   " + lockStatus;

            if (m_LockManager && m_LockManager.IsLocked())
                footer = footer + "   FIRE";
        }

        if (m_EccmStatus != "eccm=0")
            footer = footer + "   " + m_EccmStatus;

        footer = footer + "   " + NetworkStatusString();

        m_Widgets.m_wListFooter.SetText(footer);
    }

    protected string BuildWlrSolutionBody(vector origin, RDF_RadarProjectileTracker tracker)
    {
        if (!tracker)
            return "(no fire solutions)\nwaiting for ballistic fit";

        array<ref RDF_RadarTrack> all = GetCachedDisplayTracks(tracker, origin);
        if (!all)
            return "(no fire solutions)\nwaiting for ballistic fit";

        float nowS = GetWorldTimeS();
        string body = "";
        int shown = 0;

        foreach (RDF_RadarTrack tr : all)
        {
            if (!tr)
                continue;

            GBRS_RadarWlrSolution sol = GBRS_RadarWlrBallisticSolver.Resolve(tr);
            RDF_RadarWlrFix fix = null;
            if (sol)
                fix = sol.m_Fix;
            if (!fix)
                continue;
            if (!fix.m_LaunchValid && !fix.m_ImpactValid)
                continue;
            if (shown >= 8)
                break;

            if (body != "")
                body = body + "\n";

            string id = "W" + PadNum(tr.m_TrackId, 2);
            string eta = "--";
            string tof = "--";
            if (fix.m_ImpactValid)
            {
                eta = FormatEtaS(fix.m_ImpactTimeS - nowS);
                if (fix.m_LaunchValid)
                {
                    float tofS = fix.m_ImpactTimeS - fix.m_LaunchTimeS;
                    if (tofS < 0.0)
                        tofS = 0.0;
                    tof = Fmt1(tofS) + "s";
                }
            }

            string dragTag = "PRI";
            float dragK = GBRS_RadarWlrBallisticSolver.K_PRIOR;
            if (sol)
            {
                dragK = sol.m_AirDrag;
                if (sol.m_DragEstimated)
                    dragTag = "EST";
            }

            body = body + id + "  ETA " + eta + "  TOF " + tof
                + "  " + dragTag + " " + FormatDrag(dragK);

            if (fix.m_LaunchValid)
            {
                body = body + "\n LCH  " + FormatWorldXZ(fix.m_LaunchPos)
                    + "  " + FormatWorldGrid(fix.m_LaunchPos)
                    + "  " + FormatAzRng(origin, fix.m_LaunchPos);
            }
            else
            {
                body = body + "\n LCH  --";
            }

            if (fix.m_ImpactValid)
            {
                body = body + "\n IMP  " + FormatWorldXZ(fix.m_ImpactPos)
                    + "  " + FormatWorldGrid(fix.m_ImpactPos)
                    + "  " + FormatAzRng(origin, fix.m_ImpactPos);
            }
            else
            {
                body = body + "\n IMP  --";
            }

            shown = shown + 1;
        }

        if (body == "")
            return "(no fire solutions)\nwaiting for ballistic fit";
        return body;
    }

    protected float GetWorldTimeS()
    {
        ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
        if (!world)
            return 0.0;
        return world.GetWorldTime() * 0.001;
    }

    protected string FormatEtaS(float etaS)
    {
        if (etaS <= 0.05)
            return "NOW";
        if (etaS >= 100.0)
            return F0(etaS) + "s";
        return Fmt1(etaS) + "s";
    }

    protected string FormatDrag(float k)
    {
        int scaled = (int)(k * 1000000.0 + 0.5);
        if (k < 0.0)
            scaled = (int)(k * 1000000.0 - 0.5);
        if (scaled < 0)
            scaled = 0;
        string digits = scaled.ToString();
        while (digits.Length() < 6)
            digits = "0" + digits;
        return "0." + digits;
    }

    protected string FormatWorldXZ(vector pos)
    {
        return "E" + PadCoord(pos[0]) + " N" + PadCoord(pos[2]);
    }

    protected string FormatWorldGrid(vector pos)
    {
        return GBRS_MapGrid.Format(pos);
    }

    // Short map label for PPI callouts: prefer the map grid, fall back to
    // raw E/N metres if the map helper has no grid data.
    protected string GetPpiMapLabel(vector pos)
    {
        string grid = FormatWorldGrid(pos);
        if (grid == "")
            return FormatWorldXZ(pos);
        return grid;
    }

    protected string FormatAzRng(vector origin, vector worldPos)
    {
        vector d = worldPos - origin;
        float az = Math.Atan2(d[0], d[2]) * Math.RAD2DEG;
        if (az < 0.0)
            az = az + 360.0;
        float km = d.Length() / 1000.0;
        return "AZ" + PadNum(az, 3) + " " + Fmt1(km) + "km";
    }

    protected string PadCoord(float metres)
    {
        int v = (int)(metres + 0.5);
        if (metres < 0.0)
            v = (int)(metres - 0.5);
        if (v < 0)
            return v.ToString();

        string s = v.ToString();
        while (s.Length() < 6)
            s = "0" + s;
        return s;
    }

    protected int BlipColor(RDF_RadarTarget t)
    {
        if (t.m_LosBlocked)
            return COL_NLOS;
        if (t.m_IsFalsePlot)
            return COL_FALSEPLOT;
        if (t.m_IsAnonymous)
            return COL_ANON;
        if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return COL_PROJ;
        if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return COL_EMITTER;
        return COL_VEHICLE;
    }

    protected float BlipRadius(RDF_RadarTarget t)
    {
        float r = 5.0 + t.m_SnrDb * 0.12;
        if (r < 5.0)
            r = 5.0;
        if (r > 12.0)
            r = 12.0;
        return r;
    }

    protected string TypeTag(RDF_RadarTarget t)
    {
        if (!t)
            return "----";
        if (t.m_IsFalsePlot)
            return "FAKE";
        if (m_Mode == MODE_WLR)
            return "SHELL";
        if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return "PROJ";
        if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return "EMIT";
        if (t.m_IsAnonymous)
            return "ANON";
        return "VEH ";
    }

    protected string RangeLabel(float rangeM)
    {
        if (rangeM < 1000.0)
            return F0(rangeM) + "m";
        int km10 = (int)(rangeM / 100.0 + 0.5);
        return (km10 / 10).ToString() + "." + (km10 % 10).ToString() + "km";
    }

    protected string F0(float v)
    {
        if (v < 0.0)
            return "-" + ((int)(-v + 0.5)).ToString();
        return ((int)(v + 0.5)).ToString();
    }

    protected string Fmt1(float v)
    {
        int t = (int)(v * 10.0 + 0.5);
        int whole = t / 10;
        int frac = t % 10;
        if (frac < 0)
            frac = -frac;
        return whole.ToString() + "." + frac.ToString();
    }

    protected string PadNum(float v, int width)
    {
        string s = F0(v);
        while (s.Length() < width)
            s = " " + s;
        return s;
    }
}
