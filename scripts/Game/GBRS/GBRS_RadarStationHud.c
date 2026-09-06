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
    float m_LaunchTimeS;
    float m_ImpactTimeS;
    bool m_HasLaunch;
    bool m_HasImpact;
    bool m_HasLive;
    float m_AirDrag;
    bool m_DragEstimated;
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
    static const int OPTICS_MAX_FPS = 60;
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
    static const float TRACK_CLUSTER_RANGE_M = 350.0;
    static const float TRACK_CLUSTER_AZ_DEG = 5.0;
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
    static const int COL_PLOT_GLOW = ARGB(255, 255, 235, 70);
    static const int COL_TRACK = ARGB(255, 255, 255, 255);
    static const int COL_TRACK_FILL = ARGB(255, 255, 40, 60);
    static const int COL_TRACK_OUTLINE = ARGB(255, 255, 255, 255);
    static const int COL_TRACK_TENT = ARGB(200, 210, 220, 100);
    static const int COL_TRACK_COAST = ARGB(230, 140, 210, 255);
    static const int COL_TRACK_LABEL = ARGB(220, 220, 255, 210);
    static const int COL_AZEL_GRID = ARGB(120, 80, 160, 200);
    static const float WLR_ALERT_RADIUS_M = 120.0;
    static const float WLR_PERSIST_S = 20.0;
    static const float WLR_IMPACT_PERSIST_S = 3.0;
    static const float WLR_LIST_UPDATE_INTERVAL_S = 0.1;
    static const int MAX_WLR_PERSIST = 16;
    static const string MODE_WLR = GBRS_RadarStationConstants.MODE_WLR;
    static const string MODE_LOCK = GBRS_RadarStationConstants.MODE_LOCK;

    protected static ref GBRS_RadarStationHud s_Instance;

    // Multi-radar network overlay toggle + live network status.
    protected static bool s_NetworkOverlay = true;
    protected static int s_NetFusedCount;
    protected static int s_NetStationCount;
    // Fused tracks that exist in the hub but lie outside the current PPI range.
    protected static int s_NetOutRangeCount;

    static void SetNetworkOverlayEnabled(bool enabled)
    {
        s_NetworkOverlay = enabled;
    }

    static bool IsNetworkOverlayEnabled()
    {
        return s_NetworkOverlay;
    }

    // Optical sight PIP is opt-in (costly RT). Default off until the operator
    // toggles OPTICS on the mode bar.
    static void SetOpticsEnabled(bool enabled)
    {
        GetInstance().SetOpticsEnabledInternal(enabled);
    }

    static bool IsOpticsEnabled()
    {
        return GetInstance().m_bOpticsEnabled;
    }

    static bool ToggleOpticsEnabled()
    {
        GBRS_RadarStationHud inst = GetInstance();
        bool next = true;
        if (inst.m_bOpticsEnabled)
            next = false;
        inst.SetOpticsEnabledInternal(next);
        return inst.m_bOpticsEnabled;
    }

    static void SetTrackCoastAnchor(float wallTimeS)
    {
        GetInstance().m_TrackCoastAnchorS = wallTimeS;
    }

    protected Widget m_wRoot;
    protected ref GBRS_RadarStationHudWidgets m_Widgets;

    protected SCR_PIPCamera m_OpticsCamera;
    protected IEntity m_OpticsParent;
    protected bool m_bOpticsEnabled;
    // Wall-clock when the last PPI track snap arrived; used to coast symbols
    // between ~5 Hz authority snapshots at the 60 Hz UI rate.
    protected float m_TrackCoastAnchorS;
    protected float m_DisplayRange = 12000.0;
    protected string m_Mode = GBRS_RadarStationConstants.MODE_PD_SEARCH;
    protected int m_DetectedTotal;
    protected float m_fNextWlrListUpdateS;
    protected RDF_RadarLockManager m_LockManager;
    protected int m_LockedTrackId;
    protected vector m_ScanOrigin;
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
    protected ref array<ref RDF_RadarTarget> m_CachedDisplayPlots;
    protected ref array<ref RDF_RadarFusedTrack> m_ReplicatedFused;
    protected int m_ReplicatedNetOnline = -1;

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

        inst.ApplyContactListHeaders(mode);

        if (inst.m_Widgets && inst.m_Widgets.m_wPpiHint)
        {
            if (mode == GBRS_RadarStationConstants.MODE_WLR)
                inst.m_Widgets.m_wPpiHint.SetText("LCH orange  IMP cyan   Up/Dn PPI range");
            else if (mode == GBRS_RadarStationConstants.MODE_LOCK)
                inst.m_Widgets.m_wPpiHint.SetText("click contact to lock   Up/Dn PPI range");
            else if (mode == GBRS_RadarStationConstants.MODE_MANUAL)
                inst.m_Widgets.m_wPpiHint.SetText("Up/Dn parameter   Left/Right value   wheel PPI range");
            else
                inst.m_Widgets.m_wPpiHint.SetText("click contact to lock   Up/Dn PPI range");
        }

        // Only MANUAL keeps free-text ListBody; WLR uses the same column table.
        bool showTable = true;
        if (mode == GBRS_RadarStationConstants.MODE_MANUAL)
            showTable = false;
        inst.SetContactsTableVisible(showTable);

        // Optics PIP is opt-in via OPTICS mode-bar button (see SetOpticsEnabled).
        if (inst.m_bOpticsEnabled && inst.m_OpticsParent && !inst.m_OpticsCamera)
            inst.CreateOpticsCamera(inst.m_OpticsParent);
    }

    // contacts=true: column table (NR/AZ/RNG/…). contacts=false: free-text ListBody
    // (MANUAL params only). Exactly one of the two is visible.
    protected void SetContactsTableVisible(bool contacts)
    {
        if (!m_Widgets)
            return;

        if (m_Widgets.m_wListTable)
            m_Widgets.m_wListTable.SetVisible(contacts);
        if (m_Widgets.m_wListBody)
            m_Widgets.m_wListBody.SetVisible(!contacts);
    }

    protected void ApplyContactListHeaders(string mode)
    {
        if (!m_Widgets)
            return;

        if (mode == GBRS_RadarStationConstants.MODE_WLR)
        {
            if (m_Widgets.m_wListHNr)
                m_Widgets.m_wListHNr.SetText("ID");
            if (m_Widgets.m_wListHAz)
                m_Widgets.m_wListHAz.SetText("AZ");
            if (m_Widgets.m_wListHRng)
                m_Widgets.m_wListHRng.SetText("RNG");
            if (m_Widgets.m_wListHAlt)
                m_Widgets.m_wListHAlt.SetText("ETA");
            if (m_Widgets.m_wListHSpd)
                m_Widgets.m_wListHSpd.SetText("TOF");
            if (m_Widgets.m_wListHType)
                m_Widgets.m_wListHType.SetText("TYPE");
            if (m_Widgets.m_wListHSnr)
                m_Widgets.m_wListHSnr.SetText("DRAG");
            return;
        }

        if (m_Widgets.m_wListHNr)
            m_Widgets.m_wListHNr.SetText("NR");
        if (m_Widgets.m_wListHAz)
            m_Widgets.m_wListHAz.SetText("AZ");
        if (m_Widgets.m_wListHRng)
            m_Widgets.m_wListHRng.SetText("RNG");
        if (m_Widgets.m_wListHAlt)
            m_Widgets.m_wListHAlt.SetText("ALT");
        if (m_Widgets.m_wListHSpd)
            m_Widgets.m_wListHSpd.SetText("SPD");
        if (m_Widgets.m_wListHType)
            m_Widgets.m_wListHType.SetText("TYPE");
        if (m_Widgets.m_wListHSnr)
            m_Widgets.m_wListHSnr.SetText("SNR");
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

        Color bodyCol = Color.FromRGBA(185, 230, 255, 235);
        if (mode == GBRS_RadarStationConstants.MODE_WLR)
            bodyCol = Color.FromRGBA(255, 214, 140, 245);
        else if (mode == GBRS_RadarStationConstants.MODE_LOCK)
            bodyCol = Color.FromRGBA(255, 200, 190, 240);
        if (m_Widgets.m_wListBNr)
            m_Widgets.m_wListBNr.SetColor(bodyCol);
        if (m_Widgets.m_wListBAz)
            m_Widgets.m_wListBAz.SetColor(bodyCol);
        if (m_Widgets.m_wListBRng)
            m_Widgets.m_wListBRng.SetColor(bodyCol);
        if (m_Widgets.m_wListBAlt)
            m_Widgets.m_wListBAlt.SetColor(bodyCol);
        if (m_Widgets.m_wListBSpd)
            m_Widgets.m_wListBSpd.SetColor(bodyCol);
        if (m_Widgets.m_wListBType)
            m_Widgets.m_wListBType.SetColor(bodyCol);
        if (m_Widgets.m_wListBSnr)
            m_Widgets.m_wListBSnr.SetColor(bodyCol);
    }

    static void SetDisplayRange(float rangeM)
    {
        if (rangeM <= 0.0)
            return;
        GetInstance().m_DisplayRange = rangeM;
    }

    static void SetLockedTrackId(int trackId)
    {
        GetInstance().m_LockedTrackId = trackId;
    }

    protected int PickTrackIdAtCanvasPixelsInternal(float pixelX, float pixelY)
    {
        if (!m_Widgets || !m_Widgets.m_wPpiCanvas)
            return 0;

        float canvasW;
        float canvasH;
        m_Widgets.m_wPpiCanvas.GetScreenSize(canvasW, canvasH);
        if (canvasW < 1.0)
            canvasW = 1.0;
        if (canvasH < 1.0)
            canvasH = 1.0;

        float clickX = (pixelX / canvasW) * m_PpiW;
        float clickY = (pixelY / canvasH) * m_PpiH;

        array<ref RDF_RadarTrack> tracks = m_CachedDisplayTracks;
        if (!tracks)
            return 0;

        int bestId = 0;
        float bestDist = 28.0;
        int i = 0;
        while (i < tracks.Count())
        {
            RDF_RadarTrack tr = tracks.Get(i);
            i = i + 1;
            if (!tr)
                continue;

            float bx;
            float by;
            vector drawPos = CoastTrackWorldPos(tr);
            if (!WorldToPpi(m_ScanOrigin, drawPos, bx, by))
                continue;

            float dx = bx - clickX;
            float dy = by - clickY;
            float dist = Math.Sqrt(dx * dx + dy * dy);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestId = tr.m_TrackId;
            }
        }

        return bestId;
    }

    static int PickTrackIdAtCanvasPixels(float pixelX, float pixelY)
    {
        return GetInstance().PickTrackIdAtCanvasPixelsInternal(pixelX, pixelY);
    }

    // Nearest painted plot (afterglow / raw blip) under the cursor.
    protected bool PickPlotAtCanvasPixelsInternal(
        float pixelX,
        float pixelY,
        out int scattererId,
        out vector worldPos)
    {
        scattererId = 0;
        worldPos = "0 0 0";
        if (!m_Widgets || !m_Widgets.m_wPpiCanvas)
            return false;

        array<ref RDF_RadarTarget> plots = m_CachedDisplayPlots;
        if (!plots)
            return false;

        float canvasW;
        float canvasH;
        m_Widgets.m_wPpiCanvas.GetScreenSize(canvasW, canvasH);
        if (canvasW < 1.0)
            canvasW = 1.0;
        if (canvasH < 1.0)
            canvasH = 1.0;

        float clickX = (pixelX / canvasW) * m_PpiW;
        float clickY = (pixelY / canvasH) * m_PpiH;

        float bestDist = 28.0;
        bool found = false;
        int i = 0;
        while (i < plots.Count())
        {
            RDF_RadarTarget t = plots.Get(i);
            i = i + 1;
            if (!t)
                continue;

            float bx;
            float by;
            if (!WorldToPpi(m_ScanOrigin, t.m_Position, bx, by))
                continue;

            float dx = bx - clickX;
            float dy = by - clickY;
            float dist = Math.Sqrt(dx * dx + dy * dy);
            if (dist < bestDist)
            {
                bestDist = dist;
                scattererId = t.m_ScattererId;
                worldPos = t.m_Position;
                found = true;
            }
        }

        return found;
    }

    static bool PickPlotAtCanvasPixels(
        float pixelX,
        float pixelY,
        out int scattererId,
        out vector worldPos)
    {
        return GetInstance().PickPlotAtCanvasPixelsInternal(
            pixelX, pixelY, scattererId, worldPos);
    }

    // Map a plot contact to a TWS track id (scatterer match, then spatial gate).
    protected int ResolveTrackIdNearContactInternal(int scattererId, vector worldPos)
    {
        array<ref RDF_RadarTrack> tracks = m_CachedDisplayTracks;
        if (!tracks)
            return 0;

        if (scattererId > 0)
        {
            int i = 0;
            while (i < tracks.Count())
            {
                RDF_RadarTrack tr = tracks.Get(i);
                i = i + 1;
                if (!tr)
                    continue;
                if (tr.m_ScattererId == scattererId)
                    return tr.m_TrackId;
            }
        }

        int bestId = 0;
        float bestDist = 400.0;
        int j = 0;
        while (j < tracks.Count())
        {
            RDF_RadarTrack tr = tracks.Get(j);
            j = j + 1;
            if (!tr)
                continue;

            vector drawPos = CoastTrackWorldPos(tr);
            float dist = vector.Distance(drawPos, worldPos);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestId = tr.m_TrackId;
            }
        }

        return bestId;
    }

    static int ResolveTrackIdNearContact(int scattererId, vector worldPos)
    {
        return GetInstance().ResolveTrackIdNearContactInternal(scattererId, worldPos);
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
        RDF_RadarLockManager lockMgr,
        array<ref RDF_RadarTrack> replicatedTracks,
        array<ref RDF_RadarFusedTrack> replicatedFused,
        int replicatedNetOnline,
        array<ref GBRS_WlrPersistDisplay> replicatedWlr)
    {
        GetInstance().Update(
            targets,
            origin,
            forward,
            range,
            tracker,
            detectedTotal,
            lockMgr,
            replicatedTracks,
            replicatedFused,
            replicatedNetOnline,
            replicatedWlr);
    }

    protected void AttachInternal(Widget root, IEntity opticsParent)
    {
        if (!root)
            return;

        if (m_wRoot == root)
        {
            m_OpticsParent = opticsParent;
            ApplyOpticsVisibility();
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
        }
        // Default to the authored column table; SetMode / UpdateList will switch
        // to ListBody only for MANUAL / WLR free-text panels.
        WriteContactColumns("--", "---", "-.-", "-.-", "---", "----", "--");
        SetContactsTableVisible(true);
        if (m_Widgets.m_wPpiMode)
            m_Widgets.m_wPpiMode.SetText(m_Mode);

        m_OpticsParent = opticsParent;
        // Default off: operator opts in with OPTICS. OpticsSlot stays sized so
        // Az/El keeps its authored FillWeight (placeholder shown until enabled).
        m_bOpticsEnabled = false;
        ApplyOpticsVisibility();
        m_DetectedTotal = 0;
        m_fNextWlrListUpdateS = 0.0;
        Print("[GBRS HUD] attached to menu root");
    }

    protected void DetachInternal()
    {
        DestroyOpticsCamera();
        m_bOpticsEnabled = false;
        m_wRoot = null;
        m_LockManager = null;
        m_LockedTrackId = 0;
        if (m_Widgets)
            m_Widgets.Clear();
        m_Widgets = null;
        m_PpiAll = null;
        m_AzElAll = null;
        m_PpiFaceTex = null;
        m_OpticsParent = null;
        if (m_WlrPersist)
            m_WlrPersist.Clear();
        m_CachedDisplayTracks = null;
        m_CachedDisplayPlots = null;
        m_ReplicatedFused = null;
        m_ReplicatedNetOnline = -1;
        m_DetectedTotal = 0;
        m_fNextWlrListUpdateS = 0.0;
        m_TrackCoastAnchorS = 0.0;
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

    protected void SetOpticsEnabledInternal(bool enabled)
    {
        if (m_bOpticsEnabled == enabled)
        {
            ApplyOpticsVisibility();
            return;
        }

        m_bOpticsEnabled = enabled;
        ApplyOpticsVisibility();
    }

    // Show/hide the optical PIP inside a fixed OpticsSlot. Never collapse the
    // slot — hiding it lets AzElSlot FillWeight swallow the left column and
    // stretch the elevation-azimuth plot.
    protected void ApplyOpticsVisibility()
    {
        if (m_Widgets)
        {
            if (m_Widgets.m_wOpticsSlot)
                m_Widgets.m_wOpticsSlot.SetVisible(true);

            if (m_Widgets.m_wOpticsRT)
                m_Widgets.m_wOpticsRT.SetVisible(m_bOpticsEnabled);

            if (m_Widgets.m_wOpticsPlaceholder)
                m_Widgets.m_wOpticsPlaceholder.SetVisible(!m_bOpticsEnabled);

            if (m_Widgets.m_wOpticsInfo)
            {
                if (m_bOpticsEnabled)
                    m_Widgets.m_wOpticsInfo.SetText("AZ --  EL FIXED");
                else
                    m_Widgets.m_wOpticsInfo.SetText("OFF");
            }
        }

        if (!m_bOpticsEnabled)
        {
            DestroyOpticsCamera();
            return;
        }

        if (m_OpticsParent && !m_OpticsCamera)
            CreateOpticsCamera(m_OpticsParent);
    }

    protected void CreateOpticsCamera(IEntity parent)
    {
        if (!m_bOpticsEnabled)
            return;

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
        RDF_RadarLockManager lockMgr,
        array<ref RDF_RadarTrack> replicatedTracks,
        array<ref RDF_RadarFusedTrack> replicatedFused,
        int replicatedNetOnline,
        array<ref GBRS_WlrPersistDisplay> replicatedWlr)
    {
        if (!m_wRoot)
            return;

        if (range > 0.0)
            m_DisplayRange = range;

        if (detectedTotal >= 0)
            m_DetectedTotal = detectedTotal;

        m_LockManager = lockMgr;
        m_ReplicatedFused = replicatedFused;
        m_ReplicatedNetOnline = replicatedNetOnline;
        m_ScanOrigin = origin;
        m_CachedDisplayPlots = targets;

        if (replicatedTracks)
            m_CachedDisplayTracks = replicatedTracks;
        else
            m_CachedDisplayTracks = CollectDisplayTracks(tracker, origin);

        CenterRoot();
        SyncPpiSquare();
        SyncAzElSize();

        if (m_bOpticsEnabled)
            UpdateOpticsCamera(origin, forward);
        if (m_Mode == MODE_WLR)
        {
            // The authority already bakes this at the snapshot cadence. Prefer
            // that result so the 60 Hz HUD does not duplicate persist work;
            // live tracker remains the Workbench/startup fallback.
            if (replicatedWlr && replicatedWlr.Count() > 0)
                m_WlrPersist = replicatedWlr;
            else if (tracker)
                UpdateWlrPersist(origin, tracker);
        }
        UpdatePpi(targets, origin, forward, tracker);
        if (m_Mode == MODE_WLR)
        {
            // PPI remains 60 Hz; text columns and layout only need 10 Hz.
            // This avoids seven SetText/layout passes on every render tick.
            float listNowS = GetWorldTimeS();
            if (listNowS >= m_fNextWlrListUpdateS)
            {
                m_fNextWlrListUpdateS =
                    listNowS + WLR_LIST_UPDATE_INTERVAL_S;
                UpdateList(targets, origin, tracker);
            }
        }
        else
        {
            m_fNextWlrListUpdateS = 0.0;
            UpdateList(targets, origin, tracker);
        }
        UpdateAzEl(targets, origin, tracker);

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

        if (rng > m_DisplayRange * 1.1)
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

        float s = delta[1] / dist;
        if (s > 1.0)
            s = 1.0;
        if (s < -1.0)
            s = -1.0;
        return Math.Asin(s) * Math.RAD2DEG;
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

            // Dotted scan line so it remains visible over the PPI face texture.
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
            DrawSearchPlots(targets, origin);
            DrawWlrShellTracks(origin, tracker);
            DrawWlrAlerts(origin);
        }
        else
        {
            DrawSearchPlots(targets, origin);
            DrawTwsTracks(origin, tracker);
        }

        // Multi-radar network overlay: fused tracks shared by the whole GBRS net.
        DrawNetworkFusedTracks(origin);

        m_Widgets.m_wPpiCanvas.SetDrawCommands(m_PpiAll);
    }

    // Frozen last-hit afterglow. Do not extrapolate these; TWS squares are
    // the predicted file. Sliding plots after the beam left looked like the
    // sweep was flinging contacts.
    protected void DrawSearchPlots(array<ref RDF_RadarTarget> targets, vector origin)
    {
        if (!targets)
            return;
        if (m_DisplayRange <= 0.0)
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
            // When a TWS track already owns this contact, skip the raw afterglow
            // so the PPI does not show two symbols for one aircraft (PD + WLR).
            if (PlotCoveredByCachedTrack(t, origin))
                continue;

            float bx;
            float by;
            if (!WorldToPpi(origin, t.m_Position, bx, by))
                continue;

            DrawPlotAfterglow(bx, by, t);
            index = index + 1;
        }
    }

    // WLR live shells: draw the track file itself. Launch/impact overlays
    // need a ballistic fit; without this the PPI only shows 0.45 s pin-pricks.
    protected void DrawWlrShellTracks(vector origin, RDF_RadarProjectileTracker tracker)
    {
        array<ref RDF_RadarTrack> tracks = GetCachedDisplayTracks(tracker, origin);
        map<int, bool> drawnIds = new map<int, bool>();

        int drawn = 0;
        if (tracks)
        {
            int i = 0;
            while (i < tracks.Count())
            {
                RDF_RadarTrack tr = tracks.Get(i);
                i = i + 1;
                if (!tr)
                    continue;
                if (drawn >= MAX_DRAW_BLIPS)
                    break;

                // Anchor the shell blip on the ballistic launch->impact arc when a
                // fix exists (stable), else fall back to the filtered position.
                vector shellPos = tr.m_FilteredPosition;
                GBRS_WlrPersistDisplay persist = FindWlrPersist(tr.m_TrackId);
                if (persist && persist.m_HasLive)
                    shellPos = persist.m_LivePos;

                float bx;
                float by;
                if (!WorldToPpi(origin, shellPos, bx, by))
                    continue;

                int color = COL_WLR_SHELL;
                float half = 7.0;

                DrawPpiSquare(bx, by, half, color);

                float dirX;
                float dirZ;
                float speed;
                // Prefer the accepted launch→impact arc direction. FitVacuum /
                // Doppler-radial headings reverse near CPA and with sparse hits.
                if (persist && persist.m_HasLive && persist.m_HasLaunch && persist.m_HasImpact)
                {
                    vector arcDir = persist.m_LiveVel;
                    dirX = arcDir[0];
                    dirZ = arcDir[2];
                    speed = Math.Sqrt(dirX * dirX + dirZ * dirZ);
                    if (speed < 0.001)
                        speed = 50.0;
                }
                else
                {
                    TrackDisplayMotion(tr, origin, dirX, dirZ, speed);
                }
                if (speed >= 3.0)
                {
                    DrawPpiChevron(bx, by, dirX, dirZ, color);
                    DrawPpiHeadingStick(bx, by, dirX, dirZ, color);
                }

                // Confirmed shell tracks get a map-grid coordinate label so the
                // operator can read launch-side positions directly from the PPI.
                string id = "W" + PadNum(tr.m_TrackId, 2);
                DrawPpiLabel(bx, by, id + " " + GetPpiMapLabel(shellPos), COL_WLR_TEXT);

                drawnIds.Set(tr.m_TrackId, true);
                drawn = drawn + 1;
            }
        }

        // Persist orphans: track file already coasted out, but keep the shell
        // on the PPI until WLR_PERSIST_S expires.
        if (!m_WlrPersist)
            return;

        float nowS = GetWorldTimeS();
        int p = 0;
        while (p < m_WlrPersist.Count())
        {
            GBRS_WlrPersistDisplay entry = m_WlrPersist.Get(p);
            p = p + 1;
            if (!entry || !entry.m_HasLive)
                continue;
            if (drawnIds.Contains(entry.m_TrackId))
                continue;
            if (entry.m_HasImpact && nowS >= entry.m_ImpactTimeS)
                continue;
            if (drawn >= MAX_DRAW_BLIPS)
                break;

            float persistX;
            float persistY;
            if (!WorldToPpi(origin, entry.m_LivePos, persistX, persistY))
                continue;

            DrawPpiSquare(persistX, persistY, 7.0, COL_WLR_SHELL);
            float persistDirX = entry.m_LiveVel[0];
            float persistDirZ = entry.m_LiveVel[2];
            float persistSpeed = Math.Sqrt(
                persistDirX * persistDirX + persistDirZ * persistDirZ);
            if (persistSpeed >= 3.0)
            {
                DrawPpiChevron(
                    persistX, persistY, persistDirX, persistDirZ, COL_WLR_SHELL);
                DrawPpiHeadingStick(
                    persistX, persistY, persistDirX, persistDirZ, COL_WLR_SHELL);
            }

            string persistId = entry.m_Id;
            if (persistId == "")
                persistId = "W" + PadNum(entry.m_TrackId, 2);
            DrawPpiLabel(
                persistX,
                persistY,
                persistId + " " + GetPpiMapLabel(entry.m_LivePos),
                COL_WLR_TEXT);
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
        s_NetOutRangeCount = 0;
        if (m_DisplayRange <= 0.0)
            return;

        RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
        array<ref RDF_RadarFusedTrack> fused = m_ReplicatedFused;
        if (!fused)
        {
            if (!hub || !hub.IsEnabled())
                return;
            fused = hub.GetFusedTracks();
        }
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
            {
                // Fused track exists but is outside the current display range.
                s_NetOutRangeCount = s_NetOutRangeCount + 1;
                continue;
            }

            // Count each contributing station only for tracks actually on the
            // PPI, so "stations" reflects the visible networked picture.
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

            vector v = FusedReliableVelocity(f);
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

    // Reliable motion direction for a fused/net track's chevron. A net track's
    // m_Velocity can still be a Doppler-radial reconstruction in early flight
    // (falls out of the publish fit prior to enough samples), which reverses
    // ~180 deg when a shell crosses closest approach or near apogee. For a
    // projectile with a WLR launch->impact fix, use the launch->impact vector
    // instead - it is the true motion heading and never reverses. Returns the
    // raw (unnormalized) delta so the caller's magnitude gate passes and the
    // chevron helper (which normalizes internally) still renders at fixed size.
    protected vector FusedReliableVelocity(RDF_RadarFusedTrack f)
    {
        if (f)
        {
            if (f.m_WlrImpactValid && f.m_WlrLaunchValid)
            {
                vector d = f.m_WlrImpactPos - f.m_WlrLaunchPos;
                d[1] = 0.0;
                if (d.Length() >= 3.0)
                    return d;
            }
            if (f.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE && f.m_WlrImpactValid)
            {
                vector d = f.m_WlrImpactPos - f.m_WorldPos;
                d[1] = 0.0;
                if (d.Length() >= 3.0)
                    return d;
            }
            return f.m_Velocity;
        }
        return "0 0 0";
    }

    // "NET" status line for the footer: powered stations on the datalink net,
    // visible fused tracks, and how many fused tracks lie beyond the current
    // PPI range. "Stations online" is tracked independent of tracks, so an
    // online net with no target does not read as off-line.
    protected string NetworkStatusString()
    {
        int online = m_ReplicatedNetOnline;
        if (online < 0)
            online = GBRS_RadarStationComponent.GetOnlineDatalinkStationCount();

        bool anyFused = (s_NetFusedCount > 0 || s_NetOutRangeCount > 0);
        if (online <= 0 && !anyFused)
            return "NET: off-line";

        string s = "NET: " + online.ToString() + " station";
        if (online != 1)
            s = s + "s";
        s = s + " online";

        if (anyFused)
        {
            s = s + "  " + s_NetFusedCount.ToString() + " in view / "
                + s_NetStationCount.ToString() + " src";
            if (s_NetOutRangeCount > 0)
                s = s + "  (+" + s_NetOutRangeCount.ToString() + " beyond range)";
        }
        return s;
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
            else if (t.m_LosBlocked)
                color = ARGB(90, 110, 235, 255);
        }

        vector centerPx = m_Widgets.m_wPpiCanvas.PosToPixels(Vector(bx, by, 0.0));
        float rPx = UnitSizeToPixels(2.4);
        if (m_Mode == MODE_WLR)
            rPx = UnitSizeToPixels(4.5);
        if (rPx < 1.0)
            rPx = 1.0;

        // Always draw a bright halo so the dot survives the dark PPI face texture.
        array<float> hv = new array<float>();
        float haloPx = rPx * 1.8;
        if (haloPx < 3.0)
            haloPx = 3.0;
        m_Widgets.m_wPpiCanvas.TessellateCircle(centerPx, haloPx, 10, hv);
        PolygonDrawCommand halo = new PolygonDrawCommand();
        halo.m_iColor = ARGB(255, 255, 255, 255);
        halo.m_Vertices = hv;
        m_PpiAll.Insert(halo);

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
            if (!GBRS_RadarStationConfig.ShouldDisplayAirSearchTrack(
                    tr,
                    m_Mode,
                    m_LockedTrackId,
                    false,
                    false))
                continue;

            float bx;
            float by;
            vector drawPos = TrackDrawWorldPos(tr, origin);
            if (!WorldToPpi(origin, drawPos, bx, by))
                continue;

            int color = TrackColor(tr);
            float half = 7.0;

            bool locked = false;
            if (hasLock)
                locked = IsLockMatchedTrack(tr, lockPos);
            if (!locked && m_LockedTrackId > 0 && tr.m_TrackId == m_LockedTrackId)
                locked = true;
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

            // Mark emitting contacts (other radars / jammers) plainly so the
            // operator can see where an interference source is.
            if (tr.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            {
                DrawPpiAlertRing(origin, drawPos, WLR_ALERT_RADIUS_M * 1.2, COL_EMITTER);
                DrawPpiLabel(bx, by, "JAM " + GetPpiMapLabel(drawPos), COL_EMITTER);
                drawn = drawn + 1;
                continue;
            }

            // TWS tracks show map-grid coordinates directly on the PPI.
            DrawPpiLabel(bx, by, GetPpiMapLabel(drawPos), COL_TRACK_LABEL);

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

    // Authority snapshot carries measured-anchor positions at ~5 Hz.
    protected vector CoastTrackWorldPos(RDF_RadarTrack tr)
    {
        return TrackDrawWorldPos(tr, m_ScanOrigin);
    }

    // Prefer the last measured hit for PPI symbols. FilteredPosition keeps
    // coasting between mechanical-scan updates and looked like contacts were
    // being flung across the scope. Heading/chevron still use TrackDisplayMotion.
    protected vector TrackDrawWorldPos(RDF_RadarTrack tr, vector origin)
    {
        if (!tr)
            return origin;

        if (tr.m_Positions && tr.m_Positions.Count() > 0)
        {
            int last = tr.m_Positions.Count() - 1;
            vector newest = tr.m_Positions.Get(last);
            if (VecFinite(newest))
            {
                vector deltaMeas = newest - origin;
                if (deltaMeas.LengthSq() > 1.0)
                    return newest;
            }
        }

        vector pos = tr.m_FilteredPosition;
        if (VecFinite(pos))
        {
            vector delta = pos - origin;
            if (delta.LengthSq() > 1.0)
                return pos;
        }

        float rng = tr.m_FilteredRangeM;
        if (rng > 1.0)
        {
            float azRad = tr.m_FilteredAzimuthDeg * 0.017453292519943295;
            vector rebuilt;
            rebuilt[0] = origin[0] + rng * Math.Cos(azRad);
            rebuilt[1] = origin[1];
            rebuilt[2] = origin[2] + rng * Math.Sin(azRad);
            return rebuilt;
        }

        return pos;
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
            if (!IsTrackInDisplayRange(tr, origin))
                continue;
            if (!GBRS_RadarStationConfig.ShouldDisplayAirSearchTrack(
                    tr,
                    m_Mode,
                    m_LockedTrackId,
                    false,
                    false))
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
                if (result.Count() < MAX_DRAW_BLIPS)
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

        if (a.m_Entity && b.m_Entity)
        {
            IEntity rootA = a.m_Entity.GetRootParent();
            if (!rootA)
                rootA = a.m_Entity;
            IEntity rootB = b.m_Entity.GetRootParent();
            if (!rootB)
                rootB = b.m_Entity;
            if (rootA == rootB)
                return true;
        }

        // Different known scatterers are different physical contacts. Do not
        // collapse close formations or projectile salvos by spatial gating.
        if (a.m_ScattererId > 0 && b.m_ScattererId > 0)
            return false;

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
        if (a.m_Confirmed != b.m_Confirmed)
        {
            if (a.m_Confirmed)
                return true;
            return false;
        }
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

        float vx = 0.0;
        float vz = 0.0;
        float vH = 0.0;
        vector filtVel = GBRS_RadarStationConfig.SanitizeTrackCoastVelocity(tr.m_FilteredVelocity);
        vx = filtVel[0];
        vz = filtVel[2];
        vH = Math.Sqrt(vx * vx + vz * vz);

        // 1) Accepted / sanitized WLR launch→impact (stable course).
        RDF_RadarWlrFix wlrFix = GBRS_RadarWlrBallisticSolver.SanitizeFixForDisplay(
            tr.m_LastWlrFix, tr);
        if (wlrFix)
        {
            vector arc = wlrFix.m_ImpactPos - wlrFix.m_LaunchPos;
            dirX = arc[0];
            dirZ = arc[2];
            float arcLen = Math.Sqrt(dirX * dirX + dirZ * dirZ);
            if (arcLen >= 3.0)
            {
                if (haveChord)
                    speed = GBRS_RadarStationConfig.SanitizeDisplaySpeedMs(chordSpeed, false);
                else
                    speed = GBRS_RadarStationConfig.SanitizeDisplaySpeedMs(vH, false);
                if (speed < 3.0)
                    speed = 3.0;
                return;
            }
        }

        // 2) Measured position chord — true ground track. Prefer this over a
        // sparse FitVacuum (3 pts / 0.25 s), which often reverses near CPA.
        if (haveChord)
        {
            dirX = chordX;
            dirZ = chordZ;
            speed = GBRS_RadarStationConfig.SanitizeDisplaySpeedMs(chordSpeed, false);
            return;
        }

        // 3) Filtered Cartesian velocity when it has a horizontal component.
        if (vH >= 3.0)
        {
            dirX = vx;
            dirZ = vz;
            speed = GBRS_RadarStationConfig.SanitizeDisplaySpeedMs(vH, false);
            return;
        }

        // 4) Projectiles: never fall back to LOS*(-radial). That path is what
        // painted "flying backwards" on crossing / inbound shells.
        if (tr.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return;

        // Radial Doppler can still carry rotor sideband energy on air tracks
        // (no sideband flag on RDF_RadarTrack). Reject absurd radials so
        // hovering helos / beam-entry jets do not paint at hundreds of m/s.
        float rr = tr.m_FilteredRangeRateMs;
        float radialAbs = rr;
        if (radialAbs < 0.0)
            radialAbs = -radialAbs;
        if (radialAbs < 3.0)
            return;
        if (radialAbs > 90.0)
            return;

        float azRad = tr.m_FilteredAzimuthDeg * 0.017453292519943295;
        dirX = Math.Cos(azRad) * (-rr);
        dirZ = Math.Sin(azRad) * (-rr);
        speed = GBRS_RadarStationConfig.SanitizeDisplaySpeedMs(radialAbs, false);
    }

    protected bool IsTrackInDisplayRange(RDF_RadarTrack tr, vector origin)
    {
        if (!tr)
            return false;

        float rng = tr.m_FilteredRangeM;
        if (rng <= 0.0)
        {
            vector d = TrackDrawWorldPos(tr, origin) - origin;
            rng = d.Length();
        }

        if (m_DisplayRange <= 0.0)
            return true;

        if (rng > m_DisplayRange * 1.1)
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
        if (a >= float.INFINITY || a <= -float.INFINITY)
            return false;
        if (b >= float.INFINITY || b <= -float.INFINITY)
            return false;
        if (c >= float.INFINITY || c <= -float.INFINITY)
            return false;
        return true;
    }

    // Returns false when the position is not finite. Clamps range to rim so
    // the blip is always visible when on the same radial, even beyond the
    // current display range.
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
        if (d2 <= 0.000001)
            return true;

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

                    GBRS_WlrPersistDisplay entry = FindWlrPersist(tr.m_TrackId);
                    RDF_RadarWlrFix fix =
                        GBRS_RadarWlrBallisticSolver.ResolveFix(tr);

                    // Quality-gated fix: create / refresh LCH→IMP. Ungated raw
                    // LastWlrFix is never painted (early reverse / wild arcs).
                    if (fix && (fix.m_LaunchValid || fix.m_ImpactValid))
                    {
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
                        {
                            entry.m_LaunchPos = fix.m_LaunchPos;
                            entry.m_LaunchTimeS = fix.m_LaunchTimeS;
                        }
                        entry.m_HasImpact = fix.m_ImpactValid;
                        if (fix.m_ImpactValid)
                        {
                            entry.m_ImpactPos = fix.m_ImpactPos;
                            entry.m_ImpactTimeS = fix.m_ImpactTimeS;
                        }
                        entry.m_AirDrag = tr.m_AirDrag;
                        entry.m_DragEstimated = false;

                        entry.m_LivePos = WlrPositionOnArc(entry, worldNowS);
                        entry.m_LiveVel = WlrArcDirection(entry, tr);
                        entry.m_HasLive = true;
                        continue;
                    }

                    // No accepted fix yet: still keep a live shell blip so the
                    // PPI is not empty between beam passes / before WLR solve.
                    if (!entry)
                    {
                        entry = new GBRS_WlrPersistDisplay();
                        entry.m_TrackId = tr.m_TrackId;
                        m_WlrPersist.Insert(entry);
                    }

                    entry.m_Id = "W" + PadNum(tr.m_TrackId, 2);
                    entry.m_LastSeenS = worldNowS;
                    entry.m_LivePos = tr.m_FilteredPosition;
                    entry.m_LiveVel = GBRS_RadarStationComponent.ReliableTrackVelocity(tr);
                    entry.m_HasLive = true;
                    entry.m_AirDrag = tr.m_AirDrag;
                    entry.m_DragEstimated = false;
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

    // Interpolate the shell's live position on the launch->impact arc by time.
    // Linear horizontal u=now/tRange between L and I (stable; quantified ~0-3 m
    // under WLR sensing vs 133-274 m for the alpha-beta filter). Falls back to
    // the raw track position before a fix exists.
    protected vector WlrPositionOnArc(GBRS_WlrPersistDisplay entry, float worldNowS)
    {
        if (!entry)
            return "0 0 0";
        if (!entry.m_HasLaunch && !entry.m_HasImpact)
            return entry.m_LivePos;

        if (!entry.m_HasLaunch || !entry.m_HasImpact)
            return entry.m_LivePos;

        float tLaunch = entry.m_LaunchTimeS;
        float tImpact = entry.m_ImpactTimeS;
        float tRange = tImpact - tLaunch;
        if (tRange <= 0.001)
            return entry.m_LivePos;

        float u = (worldNowS - tLaunch) / tRange;
        if (u < 0.0)
            u = 0.0;
        if (u > 1.0)
            u = 1.0;

        vector lp = entry.m_LaunchPos;
        vector ip = entry.m_ImpactPos;
        float b = 1.0 - u;
        // Horizontal linear; height follows the quadratic ballistic arc.
        vector pos;
        pos[0] = lp[0] * b + ip[0] * u;
        pos[2] = lp[2] * b + ip[2] * u;
        pos[1] = lp[1] * (1.0 - u * u) + ip[1] * (u * u);
        return pos;
    }

    // Ballistic direction launch->impact (horizontal, normalized). Falls back to
    // the fit track velocity if no arc is available yet.
    protected vector WlrArcDirection(GBRS_WlrPersistDisplay entry, RDF_RadarTrack tr)
    {
        if (entry && entry.m_HasLaunch && entry.m_HasImpact)
        {
            vector d = entry.m_ImpactPos - entry.m_LaunchPos;
            d[1] = 0.0;
            float len = d.Length();
            if (len >= 0.001)
                return d * (1.0 / len);
        }
        return GBRS_RadarStationComponent.ReliableTrackVelocity(tr);
    }

    // Shell position on the launch->impact arc by time, from a raw WLR fix.
    protected vector WlrFixPositionOnArc(RDF_RadarWlrFix fix, float worldNowS, vector fallback)
    {
        if (!fix)
            return fallback;
        if (!fix.m_LaunchValid || !fix.m_ImpactValid)
            return fallback;

        float tRange = fix.m_ImpactTimeS - fix.m_LaunchTimeS;
        if (tRange <= 0.001)
            return fallback;

        float u = (worldNowS - fix.m_LaunchTimeS) / tRange;
        if (u < 0.0)
            u = 0.0;
        if (u > 1.0)
            u = 1.0;

        float b = 1.0 - u;
        vector pos;
        pos[0] = fix.m_LaunchPos[0] * b + fix.m_ImpactPos[0] * u;
        pos[2] = fix.m_LaunchPos[2] * b + fix.m_ImpactPos[2] * u;
        pos[1] = fix.m_LaunchPos[1] * (1.0 - u * u) + fix.m_ImpactPos[1] * (u * u);
        return pos;
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
                if (WorldToPpi(origin, entry.m_LaunchPos, lx0, ly0))
                {
                    if (WorldToPpi(origin, entry.m_ImpactPos, lx1, ly1))
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
                if (WorldToPpi(origin, entry.m_LaunchPos, lx, ly))
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
                if (WorldToPpi(origin, entry.m_ImpactPos, ix, iy))
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
        array<float> fill = new array<float>();
        AppendUnitPoint(fill, cx - half, cy - half);
        AppendUnitPoint(fill, cx + half, cy - half);
        AppendUnitPoint(fill, cx + half, cy + half);
        AppendUnitPoint(fill, cx - half, cy + half);
        PolygonDrawCommand poly = new PolygonDrawCommand();
        poly.m_iColor = COL_TRACK_FILL;
        if (color == COL_LOCK)
            poly.m_iColor = ARGB(255, 255, 30, 30);
        else if (color == COL_TRACK_TENT)
            poly.m_iColor = ARGB(255, 255, 200, 60);
        else if (color == COL_PROJ)
            poly.m_iColor = ARGB(255, 255, 220, 70);
        poly.m_Vertices = fill;
        m_PpiAll.Insert(poly);

        array<float> verts = new array<float>();
        AppendUnitPoint(verts, cx - half, cy - half);
        AppendUnitPoint(verts, cx + half, cy - half);
        AppendUnitPoint(verts, cx + half, cy + half);
        AppendUnitPoint(verts, cx - half, cy + half);
        AppendUnitPoint(verts, cx - half, cy - half);
        LineDrawCommand sq = new LineDrawCommand();
        sq.m_iColor = COL_TRACK_OUTLINE;
        sq.m_fWidth = UnitSizeToPixels(2.2);
        if (sq.m_fWidth < 2.0)
            sq.m_fWidth = 2.0;
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
        if (!WorldToPpi(origin, worldPos, bx, by))
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

        // Label the interference fight on the PPI itself (the ring alone is
        // cryptic). Shown while ECCM is actively resisting a jammer.
        DrawPpiLabel(m_PpiCx, m_PpiCy - m_PpiR + 18.0, "JAMMING  " + EccmStatusString(), COL_LOCK);
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
        float speed = GBRS_RadarStationConfig.SanitizeDisplaySpeedMs(
            Math.Sqrt(vx * vx + vz * vz), t.m_RotorSidebandUsed);
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
        DrawAzElPlots(targets, origin);
        DrawAzElTracks(origin, tracker);

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
            if (index >= MAX_DRAW_BLIPS)
                break;
            // Track already owns this contact — avoid stacking a second blob.
            if (PlotCoveredByCachedTrack(t, origin))
                continue;

            float az = NorthUpAzimuthDeg(t, origin);
            float el = NorthUpElevationDeg(t, origin);
            DrawAzElMark(az, el, BlipColor(t), 3.5);
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

            vector drawPos = TrackDrawWorldPos(tr, origin);
            vector d = drawPos - origin;
            float az = Math.Atan2(d[0], d[2]) * Math.RAD2DEG;
            if (az < 0.0)
                az = az + 360.0;
            float horiz = Math.Sqrt(d[0] * d[0] + d[2] * d[2]);
            float el = Math.Atan2(d[1], Math.Max(0.001, horiz)) * Math.RAD2DEG;
            DrawAzElMark(az, el, TrackColor(tr), 4.5);
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

        float margin = 10.0;
        float innerW = m_AzElW - margin * 2.0;
        float innerH = m_AzElH - margin * 2.0;
        if (innerW < 1.0)
            innerW = m_AzElW;
        if (innerH < 1.0)
            innerH = m_AzElH;

        float az = azDeg;
        while (az < 0.0)
            az = az + 360.0;
        while (az >= 360.0)
            az = az - 360.0;

        float x = margin + (az / 360.0) * innerW;
        float y = margin + innerH - ((el - AZEL_EL_MIN) / (AZEL_EL_MAX - AZEL_EL_MIN)) * innerH;
        AddAzElBlip(x, y, color, sizeUnit);

        // 0 deg sits on the left edge; also paint the wrap so a due-north
        // contact is not clipped off the plot.
        if (az <= 12.0)
            AddAzElBlip(margin + innerW, y, color, sizeUnit);
        if (az >= 348.0)
            AddAzElBlip(margin, y, color, sizeUnit);
    }

    protected void AddAzElBlip(float x, float y, int color, float sizeUnit)
    {
        if (!m_Widgets || !m_Widgets.m_wAzElCanvas)
            return;

        if (x != x || y != y)
            return;
        if (x > float.INFINITY || x < -float.INFINITY)
            return;
        if (y > float.INFINITY || y < -float.INFINITY)
            return;

        vector centerPx = m_Widgets.m_wAzElCanvas.PosToPixels(Vector(x, y, 0.0));
        vector sizePx = m_Widgets.m_wAzElCanvas.SizeToPixels(Vector(sizeUnit, sizeUnit, 0.0));
        float rPx = sizePx[0] * 0.5;
        if (rPx < 2.0)
            rPx = 2.0;
        if (rPx > 6.0)
            rPx = 6.0;

        array<float> verts = new array<float>();
        m_Widgets.m_wAzElCanvas.TessellateCircle(centerPx, rPx, 10, verts);
        if (verts.Count() < 6)
            return;

        // Thin white halo — keep smaller than the old 16-unit filled ball.
        array<float> haloVerts = new array<float>();
        float haloPx = rPx + 1.5;
        m_Widgets.m_wAzElCanvas.TessellateCircle(centerPx, haloPx, 10, haloVerts);
        if (haloVerts.Count() >= 6)
        {
            PolygonDrawCommand halo = new PolygonDrawCommand();
            halo.m_iColor = ARGB(200, 255, 255, 255);
            halo.m_Vertices = haloVerts;
            m_AzElAll.Insert(halo);
        }

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

        SetContactsTableVisible(true);

        string colNr = "";
        string colAz = "";
        string colRng = "";
        string colAlt = "";
        string colSpd = "";
        string colType = "";
        string colSnr = "";
        int rows = 0;

        if (m_Mode == MODE_WLR)
        {
            rows = AppendWlrListRows(origin, tracker, colNr, colAz, colRng, colAlt, colSpd, colType, colSnr);
            if (colNr == "")
                WriteContactColumns("--", "---", "-.-", "---", "---", "----", "----");
            else
                WriteContactColumns(colNr, colAz, colRng, colAlt, colSpd, colType, colSnr);
            UpdateListFooter(rows, tracker);
            return;
        }

        rows = AppendTrackListRows(tracker, origin, colNr, colAz, colRng, colAlt, colSpd, colType, colSnr);
        // Plots only fill gaps — never duplicate a contact that already has a
        // TWS row (same helicopter was showing as both ANON and TRK).
        if (rows < MAX_LIST_ROWS && targets)
            rows = rows + AppendPlotListRows(targets, origin, colNr, colAz, colRng, colAlt, colSpd, colType, colSnr, rows);

        if (colNr == "")
            WriteContactColumns("--", "---", "-.-", "-.-", "---", "----", "--");
        else
            WriteContactColumns(colNr, colAz, colRng, colAlt, colSpd, colType, colSnr);

        UpdateListFooter(rows, tracker);
    }

    protected void WriteContactColumns(
        string colNr,
        string colAz,
        string colRng,
        string colAlt,
        string colSpd,
        string colType,
        string colSnr)
    {
        if (!m_Widgets)
            return;

        SetListCol(m_Widgets.m_wListBNr, colNr);
        SetListCol(m_Widgets.m_wListBAz, colAz);
        SetListCol(m_Widgets.m_wListBRng, colRng);
        SetListCol(m_Widgets.m_wListBAlt, colAlt);
        SetListCol(m_Widgets.m_wListBSpd, colSpd);
        SetListCol(m_Widgets.m_wListBType, colType);
        SetListCol(m_Widgets.m_wListBSnr, colSnr);
    }

    // WLR panel columns: ID / AZ / RNG / ETA / TOF / TYPE / DRAG
    protected int AppendWlrListRows(
        vector origin,
        RDF_RadarProjectileTracker tracker,
        inout string colNr,
        inout string colAz,
        inout string colRng,
        inout string colAlt,
        inout string colSpd,
        inout string colType,
        inout string colSnr)
    {
        int row = 0;
        float nowS = GetWorldTimeS();
        map<int, bool> listed = new map<int, bool>();

        if (m_WlrPersist)
        {
            int i = 0;
            while (i < m_WlrPersist.Count())
            {
                GBRS_WlrPersistDisplay entry = m_WlrPersist.Get(i);
                i = i + 1;
                if (!entry)
                    continue;
                if (!entry.m_HasLaunch && !entry.m_HasImpact && !entry.m_HasLive)
                    continue;
                if (row >= MAX_LIST_ROWS)
                    break;

                string id = entry.m_Id;
                if (id == "")
                    id = "W" + PadNum(entry.m_TrackId, 2);

                vector pos = origin;
                if (entry.m_HasLive)
                    pos = entry.m_LivePos;
                else if (entry.m_HasLaunch)
                    pos = entry.m_LaunchPos;
                else if (entry.m_HasImpact)
                    pos = entry.m_ImpactPos;

                vector d = pos - origin;
                float az = Math.Atan2(d[0], d[2]) * Math.RAD2DEG;
                if (az < 0.0)
                    az = az + 360.0;
                float rngKm = d.Length() / 1000.0;

                string eta = "--";
                string tof = "--";
                if (entry.m_HasImpact)
                {
                    eta = FormatEtaS(entry.m_ImpactTimeS - nowS);
                    if (entry.m_HasLaunch)
                    {
                        float tofS = entry.m_ImpactTimeS - entry.m_LaunchTimeS;
                        if (tofS < 0.0)
                            tofS = 0.0;
                        tof = Fmt1(tofS);
                    }
                }

                string typeTag = "SHELL";
                if (entry.m_HasLaunch && entry.m_HasImpact)
                    typeTag = "SOL ";
                else if (entry.m_HasLive)
                    typeTag = "LIVE";

                string dragTag = "----";
                if (entry.m_HasLaunch || entry.m_HasImpact)
                {
                    float dragK = entry.m_AirDrag;
                    if (dragK <= 0.0)
                        dragK = GBRS_RadarWlrBallisticSolver.K_PRIOR;
                    if (entry.m_DragEstimated)
                        dragTag = "E" + F0(dragK * 1000000.0);
                    else
                        dragTag = "P" + F0(dragK * 1000000.0);
                }

                colNr = AppendColLine(colNr, id);
                colAz = AppendColLine(colAz, PadNum(az, 3));
                colRng = AppendColLine(colRng, Fmt1(rngKm));
                colAlt = AppendColLine(colAlt, eta);
                colSpd = AppendColLine(colSpd, tof);
                colType = AppendColLine(colType, typeTag);
                colSnr = AppendColLine(colSnr, dragTag);
                listed.Set(entry.m_TrackId, true);
                row = row + 1;
            }
        }

        // Live track files not yet in persist (first hits before bake).
        array<ref RDF_RadarTrack> tracks = GetCachedDisplayTracks(tracker, origin);
        if (!tracks)
            return row;

        int t = 0;
        while (t < tracks.Count())
        {
            RDF_RadarTrack tr = tracks.Get(t);
            t = t + 1;
            if (!tr)
                continue;
            if (listed.Contains(tr.m_TrackId))
                continue;
            if (row >= MAX_LIST_ROWS)
                break;

            vector drawPos = TrackDrawWorldPos(tr, origin);
            vector trackDelta = drawPos - origin;
            float trackAz = Math.Atan2(trackDelta[0], trackDelta[2]) * Math.RAD2DEG;
            if (trackAz < 0.0)
                trackAz = trackAz + 360.0;
            float trackRangeKm = tr.m_FilteredRangeM / 1000.0;
            if (trackRangeKm <= 0.0)
                trackRangeKm = trackDelta.Length() / 1000.0;

            colNr = AppendColLine(colNr, "W" + PadNum(tr.m_TrackId, 2));
            colAz = AppendColLine(colAz, PadNum(trackAz, 3));
            colRng = AppendColLine(colRng, Fmt1(trackRangeKm));
            colAlt = AppendColLine(colAlt, "--");
            colSpd = AppendColLine(colSpd, "--");
            colType = AppendColLine(colType, "TRK ");
            colSnr = AppendColLine(colSnr, "----");
            row = row + 1;
        }

        return row;
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

            vector drawPos = TrackDrawWorldPos(tr, origin);
            vector d = drawPos - origin;
            float az = Math.Atan2(d[0], d[2]) * Math.RAD2DEG;
            if (az < 0.0)
                az = az + 360.0;
            float rngKm = tr.m_FilteredRangeM / 1000.0;
            if (rngKm <= 0.0)
                rngKm = d.Length() / 1000.0;
            float altKm = drawPos[1] / 1000.0;
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
        inout string colSnr,
        int startRow)
    {
        int row = startRow;
        foreach (RDF_RadarTarget t : targets)
        {
            if (!t)
                continue;
            if (!IsInDisplayRange(t, origin))
                continue;
            if (PlotCoveredByCachedTrack(t, origin))
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
            float spd = GBRS_RadarStationConfig.SanitizeDisplaySpeedMs(
                t.m_Velocity.Length(), t.m_RotorSidebandUsed);

            colNr = AppendColLine(colNr, PadNum(row + 1, 2));
            colAz = AppendColLine(colAz, PadNum(az, 3));
            colRng = AppendColLine(colRng, Fmt1(rngKm));
            colAlt = AppendColLine(colAlt, Fmt1(altKm));
            colSpd = AppendColLine(colSpd, PadNum(spd, 3));
            colType = AppendColLine(colType, TypeTag(t));
            colSnr = AppendColLine(colSnr, F0(t.m_SnrDb));
            row = row + 1;
        }

        return row - startRow;
    }

    // True when a painted plot is the same physical contact as a TWS track
    // already listed/drawn (scatterer, airframe root, or polar/spatial gate).
    protected bool PlotCoveredByCachedTrack(RDF_RadarTarget t, vector origin)
    {
        if (!t)
            return false;

        array<ref RDF_RadarTrack> tracks = m_CachedDisplayTracks;
        if (!tracks)
            return false;

        IEntity plotRoot = null;
        if (t.m_ScattererId > 0)
        {
            RDF_RadarScatterer entry = RDF_RadarScattererRegistry.FindById(t.m_ScattererId);
            if (entry && entry.m_Entity)
            {
                plotRoot = entry.m_Entity.GetRootParent();
                if (!plotRoot)
                    plotRoot = entry.m_Entity;
            }
        }

        float plotRng = t.m_Distance;
        if (plotRng <= 0.0)
            plotRng = vector.Distance(t.m_Position, origin);
        float plotAz = NorthUpAzimuthDeg(t, origin);

        int i = 0;
        while (i < tracks.Count())
        {
            RDF_RadarTrack tr = tracks.Get(i);
            i = i + 1;
            if (!tr)
                continue;

            if (t.m_ScattererId > 0 && t.m_ScattererId == tr.m_ScattererId)
                return true;

            if (plotRoot && tr.m_ScattererId > 0)
            {
                RDF_RadarScatterer trEntry = RDF_RadarScattererRegistry.FindById(tr.m_ScattererId);
                if (trEntry && trEntry.m_Entity)
                {
                    IEntity trackRoot = trEntry.m_Entity.GetRootParent();
                    if (!trackRoot)
                        trackRoot = trEntry.m_Entity;
                    if (trackRoot == plotRoot)
                        return true;
                }
            }

            if (tr.m_Entity)
            {
                IEntity entRoot = tr.m_Entity.GetRootParent();
                if (!entRoot)
                    entRoot = tr.m_Entity;
                if (plotRoot && entRoot == plotRoot)
                    return true;
            }

            if (t.m_ScattererId > 0 && tr.m_ScattererId > 0)
                continue;

            vector trackPos = TrackDrawWorldPos(tr, origin);
            float dist = vector.Distance(t.m_Position, trackPos);
            if (dist <= TRACK_CLUSTER_RANGE_M)
                return true;

            float dRange = plotRng - tr.m_FilteredRangeM;
            if (dRange < 0.0)
                dRange = -dRange;
            float dAz = plotAz - tr.m_FilteredAzimuthDeg;
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
        }

        return false;
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
        if (detected < 0)
            detected = 0;

        // Readable two-line footer: line 1 = scan summary; line 2 = mode /
        // ECCM / network status.
        string line1 = "Detected: " + detected.ToString()
            + "    Tracks: " + tracks.ToString()
            + "    Range: " + RangeLabel(m_DisplayRange);

        string line2 = "";
        if (m_Mode == MODE_WLR)
        {
            line2 = "WLR fire-solutions: " + wlrFixes.ToString();
        }
        else if (m_Mode == MODE_LOCK)
        {
            string lockStatus = "SEARCH";
            if (m_LockManager)
                lockStatus = m_LockManager.GetStatusShort();
            else if (m_LockedTrackId > 0)
                lockStatus = "TRACKING id=" + m_LockedTrackId.ToString();
            line2 = "Lock: " + lockStatus;
            if (m_LockManager && m_LockManager.IsLocked())
                line2 = line2 + "  FIRE AUTHORIZED";
            else if (m_LockedTrackId > 0)
                line2 = line2 + "  FIRE AUTHORIZED";
        }
        else
        {
            line2 = "";
        }

        string eccm = EccmStatusString();
        if (eccm != "")
        {
            if (line2 != "")
                line2 = line2 + "    ";
            line2 = line2 + eccm;
        }

        if (line2 != "")
            line2 = line2 + "    ";
        line2 = line2 + NetworkStatusString();

        m_Widgets.m_wListFooter.SetText(line1 + "\n" + line2);
    }

    // Translate the RDF ECCM status ("eccm=0" | "eccm slb/prf/freq/burn") into a
    // readable line. Empty when the decision layer is idle.
    protected string EccmStatusString()
    {
        if (m_EccmStatus == "eccm=0")
            return "";
        if (m_EccmStatus == "eccm")
            return "ECCM active";

        array<string> tokens = new array<string>();
        m_EccmStatus.Split(" ", tokens, true);

        array<string> outTok = new array<string>();
        int i = 0;
        while (i < tokens.Count())
        {
            string t = tokens.Get(i);
            i = i + 1;
            if (t == "slb")
                outTok.Insert("SLB");
            else if (t == "prf")
                outTok.Insert("PRF agility");
            else if (t == "freq")
                outTok.Insert("freq agility");
            else if (t == "burn")
                outTok.Insert("burn-through");
            else if (t == "jam")
                outTok.Insert("jamming");
        }

        string s = "ECCM: ";
        int n = 0;
        while (n < outTok.Count())
        {
            if (n > 0)
                s = s + " + ";
            s = s + outTok.Get(n);
            n = n + 1;
        }
        if (outTok.Count() <= 0)
            s = s + "active";
        return s;
    }

    protected string BuildWlrSolutionBody(vector origin, RDF_RadarProjectileTracker tracker)
    {
        if (!m_WlrPersist || m_WlrPersist.Count() < 1)
            return "(no fire solutions)\nwaiting for ballistic fit";

        float nowS = GetWorldTimeS();
        string body = "";
        int shown = 0;

        int i = 0;
        while (i < m_WlrPersist.Count())
        {
            GBRS_WlrPersistDisplay entry = m_WlrPersist.Get(i);
            i = i + 1;
            if (!entry)
                continue;
            if (!entry.m_HasLaunch && !entry.m_HasImpact)
                continue;
            if (shown >= 8)
                break;

            if (body != "")
                body = body + "\n";

            string id = entry.m_Id;
            if (id == "")
                id = "W" + PadNum(entry.m_TrackId, 2);

            string eta = "--";
            string tof = "--";
            if (entry.m_HasImpact)
            {
                eta = FormatEtaS(entry.m_ImpactTimeS - nowS);
                if (entry.m_HasLaunch)
                {
                    float tofS = entry.m_ImpactTimeS - entry.m_LaunchTimeS;
                    if (tofS < 0.0)
                        tofS = 0.0;
                    tof = Fmt1(tofS) + "s";
                }
            }

            string dragTag = "PRI";
            float dragK = entry.m_AirDrag;
            if (dragK <= 0.0)
                dragK = GBRS_RadarWlrBallisticSolver.K_PRIOR;
            if (entry.m_DragEstimated)
                dragTag = "EST";

            body = body + id + "  ETA " + eta + "  TOF " + tof
                + "  " + dragTag + " " + FormatDrag(dragK);

            if (entry.m_HasLaunch)
            {
                body = body + "\n LCH  " + FormatWorldXZ(entry.m_LaunchPos)
                    + "  " + FormatWorldGrid(entry.m_LaunchPos)
                    + "  " + FormatAzRng(origin, entry.m_LaunchPos);
            }
            else
            {
                body = body + "\n LCH  --";
            }

            if (entry.m_HasImpact)
            {
                body = body + "\n IMP  " + FormatWorldXZ(entry.m_ImpactPos)
                    + "  " + FormatWorldGrid(entry.m_ImpactPos)
                    + "  " + FormatAzRng(origin, entry.m_ImpactPos);
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
            return "SHEL";
        if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return "PROJ";
        if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return "EMIT";
        if (t.m_IsAnonymous)
            return "ANON";
        if (t.m_RotorSidebandUsed)
            return "ROTR";
        return "AIR ";
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

    protected string PadLeft(string text, int width)
    {
        while (text.Length() < width)
            text = " " + text;
        return text;
    }

    protected string FormatAngle(float v)
    {
        int t = (int)(v + 0.5);
        if (v < 0.0)
            t = (int)(v - 0.5);
        string s = t.ToString();
        while (s.Length() < 3)
            s = " " + s;
        return s;
    }

    protected string FormatDistanceM(float v)
    {
        int km = (int)(v * 0.001 + 0.5);
        if (v < 0.0)
            km = (int)(v * 0.001 - 0.5);
        string s = km.ToString();
        while (s.Length() < 3)
            s = " " + s;
        return s + "km";
    }

    protected string FormatSpeedMs(RDF_RadarTarget t)
    {
        float spd = GBRS_RadarStationConfig.SanitizeDisplaySpeedMs(
            t.m_Velocity.Length(), t.m_RotorSidebandUsed);
        int s = (int)(spd + 0.5);
        if (spd < 0.0)
            s = (int)(spd - 0.5);
        string txt = s.ToString();
        while (txt.Length() < 3)
            txt = " " + txt;
        return txt + "m";
    }

    protected string SnrLabel(float v)
    {
        int s = (int)(v + 0.5);
        if (v < 0.0)
            s = (int)(v - 0.5);
        string txt = s.ToString();
        while (txt.Length() < 3)
            txt = " " + txt;
        return txt + "d";
    }

    protected string TargetTypeLabel(RDF_RadarTarget t)
    {
        if (!t)
            return "----";
        if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return "SHEL";
        if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return "EMIT";
        if (t.m_IsAnonymous)
            return "ANON";
        if (t.m_RotorSidebandUsed)
            return "ROTR";
        return "AIR ";
    }
}
