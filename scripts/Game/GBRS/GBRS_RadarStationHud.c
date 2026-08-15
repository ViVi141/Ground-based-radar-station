// GBRS station HUD — layout owns panel geometry; widgets registry binds every part.
class GBRS_RadarStationHud
{
    static const ResourceName LAYOUT =
        "{0DD3257B9B2D6BC9}UI/layouts/GBRS/RadarStationHUD.layout";
    // Drawn on the PPI Canvas (RHS Garmin-style ImageDrawCommand), not a sibling ImageWidget.
    static const ResourceName PPI_FACE_TEXTURE =
        "{F2196E35CB708A41}UI/Textures/GBRS/GBRS_PpiFace.edds";

    static const int OPTICS_CAMERA_INDEX = 16;
    static const float OPTICS_FOV_DEG = 32.0;
    static const float OPTICS_NEAR_M = 0.25;
    static const float OPTICS_EYE_HEIGHT_M = 1.8;
    static const float OPTICS_FORWARD_CLEAR_M = 2.5;
    // Fixed upward look bias for PIP clearance — not antenna elevation.
    static const float OPTICS_LOOK_UP_Y = 0.08;
    // Match workstation feed (~30 Hz); optics RT can run faster.
    static const float UPDATE_INTERVAL = 0.033;
    static const int OPTICS_MAX_FPS = 45;
    // PIP cameras often start underexposed; sync main HDR then lift slightly.
    static const float OPTICS_HDR_BOOST = 1.35;

    static const int STATION_MARGIN = 20;
    static const int STATION_W = 1504;
    static const int STATION_H = 760;

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
    static const int MAX_LIST_ROWS = 18;
    // Must match GBRS_RadarStationMenu.DISPLAY_MAX_BLIPS.
    static const int MAX_DRAW_BLIPS = 64;

    // Static face is drawn on the same Canvas as sweep/blips (RHS Garmin pattern).
    static const int COL_PPI_SWEEP = ARGB(240, 130, 255, 180);
    static const int COL_PPI_WEDGE = ARGB(50, 50, 210, 120);
    static const int COL_VEHICLE = ARGB(255, 80, 255, 140);
    static const int COL_PROJ = ARGB(255, 255, 180, 50);
    static const int COL_EMITTER = ARGB(255, 255, 100, 235);
    static const int COL_ANON = ARGB(255, 255, 235, 150);
    static const int COL_FALSEPLOT = ARGB(255, 255, 90, 90);
    static const int COL_NLOS = ARGB(255, 100, 230, 255);
    static const int COL_VEL = ARGB(190, 100, 255, 190);
    static const int COL_WLR_LAUNCH = ARGB(220, 255, 160, 40);
    static const int COL_WLR_IMPACT = ARGB(220, 80, 180, 255);
    static const int COL_WLR_LINK = ARGB(140, 200, 200, 200);
    static const int COL_LOCK = ARGB(255, 255, 70, 70);
    static const float WLR_ALERT_RADIUS_M = 120.0;
    static const string MODE_WLR = GBRS_RadarStationConstants.MODE_WLR;
    static const string MODE_LOCK = GBRS_RadarStationConstants.MODE_LOCK;

    protected static ref GBRS_RadarStationHud s_Instance;

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

        // The contacts panel doubles as the manual-tuning panel in MANUAL mode;
        // retitle it so the operator knows which view they are looking at.
        if (inst.m_Widgets && inst.m_Widgets.m_wListTitle)
        {
            if (mode == GBRS_RadarStationConstants.MODE_MANUAL)
                inst.m_Widgets.m_wListTitle.SetText("MANUAL TUNING");
            else
                inst.m_Widgets.m_wListTitle.SetText("TRACKED CONTACTS");
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
        if (inst.m_Widgets && inst.m_Widgets.m_wListBody)
            inst.m_Widgets.m_wListBody.SetText(text);
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

        // The layout root uses 1920x1080-absolute coordinates (208,160). Re-center
        // it on the live screen so the station panel stays centered at any
        // resolution instead of being pinned to the top-left corner.
        CenterRoot();

        InitCanvases();

        if (m_Widgets.m_wListBody)
            m_Widgets.m_wListBody.SetText("(no contacts)");
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
        m_LastUpdateS = 0.0;
        m_DetectedTotal = 0;
    }

    // Keep design size (RDF-style). Shrinking the root breaks Canvas SizeInUnits
    // vs ImageWidget mapping and makes the sweep origin drift from the face center.
    // The layout root is authored in 1920x1080 absolute coordinates; re-center it
    // on the live screen so the whole station panel stays centered at any
    // resolution (Workbench preview is 1920x1080, gameplay may differ).
    protected void CenterRoot()
    {
        if (!m_wRoot)
            return;

        WorkspaceWidget ws = GetGame().GetWorkspace();
        if (!ws)
            return;

        int screenW = ws.GetWidth();
        int screenH = ws.GetHeight();
        if (screenW < 200)
            screenW = 1920;
        if (screenH < 200)
            screenH = 1080;

        int left = (screenW - STATION_W) / 2;
        int top = (screenH - STATION_H) / 2;
        if (left < STATION_MARGIN)
            left = STATION_MARGIN;
        if (top < STATION_MARGIN)
            top = STATION_MARGIN;

        FrameSlot.SetAnchor(m_wRoot, 0.0, 0.0);
        FrameSlot.SetAnchorMin(m_wRoot, 0.0, 0.0);
        FrameSlot.SetAnchorMax(m_wRoot, 0.0, 0.0);
        FrameSlot.SetAlignment(m_wRoot, 0.0, 0.0);
        FrameSlot.SetSize(m_wRoot, STATION_W, STATION_H);
        FrameSlot.SetPos(m_wRoot, left, top);
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

        if (m_Widgets)
            PinPpiSquare(m_Widgets.m_wPpiCanvas);

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

        // Keep the station panel centered: the layout root is authored in
        // 1920x1080 absolute coordinates and MenuManager may re-place it.
        // Re-assert centering every update tick (cheap FrameSlot writes).
        CenterRoot();

        UpdateOpticsCamera(origin, forward);
        UpdatePpi(targets, origin, forward, tracker);
        UpdateAzEl(targets, origin);
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
            DrawWlrAlerts(origin, tracker);

        vector lockPos;
        bool hasLock = false;
        if (m_Mode == MODE_LOCK && m_LockManager)
        {
            IEntity lockEnt;
            hasLock = m_LockManager.GetLockedTarget(lockEnt, lockPos);
        }

        if (targets && m_DisplayRange > 0.0)
        {
            int index = 0;
            foreach (RDF_RadarTarget t : targets)
            {
                if (!t || !t.m_Detected)
                    continue;
                if (!IsInDisplayRange(t, origin))
                    continue;
                if (index >= MAX_DRAW_BLIPS)
                    break;

                float bx;
                float by;
                if (!WorldToPpi(origin, t.m_Position, bx, by))
                    continue;

                int color = BlipColor(t);
                float radius = BlipRadius(t);
                if (hasLock)
                {
                    if (IsLockMatchedBlip(t, lockPos))
                    {
                        color = COL_LOCK;
                        radius = radius + 3.0;
                        DrawLockRing(bx, by, radius + 4.0);
                    }
                }

                DrawBlip(bx, by, radius, color);
                DrawVelocity(t, bx, by);
                index = index + 1;
            }
        }

        m_Widgets.m_wPpiCanvas.SetDrawCommands(m_PpiAll);
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

    // Returns false when the plot is outside the current display range.
    protected bool WorldToPpi(vector origin, vector worldPos, out float outX, out float outY)
    {
        outX = m_PpiCx;
        outY = m_PpiCy;

        if (m_DisplayRange <= 0.0)
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

    // Clamp to PPI rim so WLR launch/impact outside range still draw.
    protected bool WorldToPpiClamped(vector origin, vector worldPos, out float outX, out float outY)
    {
        outX = m_PpiCx;
        outY = m_PpiCy;

        if (m_DisplayRange <= 0.0)
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

    protected void DrawWlrAlerts(vector origin, RDF_RadarProjectileTracker tracker)
    {
        if (!tracker || !m_Widgets || !m_Widgets.m_wPpiCanvas)
            return;
        if (m_DisplayRange <= 0.0)
            return;

        array<ref RDF_RadarTrack> tracks = tracker.GetAllTracks();
        if (!tracks)
            return;

        float alertNorm = WLR_ALERT_RADIUS_M / m_DisplayRange;
        float alertUnit = alertNorm * m_PpiR;
        if (alertUnit < 6.0)
            alertUnit = 6.0;
        if (alertUnit > 22.0)
            alertUnit = 22.0;

        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (!tr || !tr.m_Confirmed)
                continue;

            RDF_RadarWlrFix fix = tr.m_LastWlrFix;
            if (!fix)
                continue;

            if (fix.m_LaunchValid && fix.m_ImpactValid)
            {
                float lx0;
                float ly0;
                float lx1;
                float ly1;
                if (WorldToPpiClamped(origin, fix.m_LaunchPos, lx0, ly0))
                {
                    if (WorldToPpiClamped(origin, fix.m_ImpactPos, lx1, ly1))
                    {
                        array<float> linkVerts = new array<float>();
                        AppendUnitPoint(linkVerts, lx0, ly0);
                        AppendUnitPoint(linkVerts, lx1, ly1);
                        LineDrawCommand link = new LineDrawCommand();
                        link.m_iColor = COL_WLR_LINK;
                        link.m_fWidth = UnitSizeToPixels(1.5);
                        if (link.m_fWidth < 1.0)
                            link.m_fWidth = 1.0;
                        link.m_Vertices = linkVerts;
                        m_PpiAll.Insert(link);
                    }
                }
            }

            if (fix.m_LaunchValid)
                DrawPpiAlertRing(origin, fix.m_LaunchPos, alertUnit, COL_WLR_LAUNCH);
            if (fix.m_ImpactValid)
                DrawPpiAlertRing(origin, fix.m_ImpactPos, alertUnit, COL_WLR_IMPACT);
        }
    }

    protected void DrawPpiAlertRing(vector origin, vector worldPos, float radiusUnit, int color)
    {
        float bx;
        float by;
        if (!WorldToPpiClamped(origin, worldPos, bx, by))
            return;

        vector centerPx = m_Widgets.m_wPpiCanvas.PosToPixels(Vector(bx, by, 0.0));
        float ringPx = UnitSizeToPixels(radiusUnit);
        if (ringPx < 3.0)
            ringPx = 3.0;

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
        if (corePx < 1.5)
            corePx = 1.5;
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

    protected void UpdateAzEl(array<ref RDF_RadarTarget> targets, vector origin)
    {
        if (!m_Widgets || !m_Widgets.m_wAzElCanvas)
            return;

        if (!m_AzElAll)
            m_AzElAll = new array<ref CanvasWidgetCommand>();
        m_AzElAll.Clear();

        if (targets)
        {
            int index = 0;
            foreach (RDF_RadarTarget t : targets)
            {
                if (!t || !t.m_Detected)
                    continue;
                if (!IsInDisplayRange(t, origin))
                    continue;
                if (index >= MAX_DRAW_BLIPS)
                    break;

                float az = NorthUpAzimuthDeg(t, origin);
                float el = NorthUpElevationDeg(t, origin);
                if (el < AZEL_EL_MIN)
                    el = AZEL_EL_MIN;
                if (el > AZEL_EL_MAX)
                    el = AZEL_EL_MAX;

                float x = (az / 360.0) * m_AzElW;
                float y = m_AzElH - ((el - AZEL_EL_MIN) / (AZEL_EL_MAX - AZEL_EL_MIN)) * m_AzElH;

                vector centerPx = m_Widgets.m_wAzElCanvas.PosToPixels(Vector(x, y, 0.0));
                vector sizePx = m_Widgets.m_wAzElCanvas.SizeToPixels(Vector(4.5, 4.5, 0.0));
                float rPx = sizePx[0];
                if (rPx < 1.0)
                    rPx = 1.0;

                array<float> verts = new array<float>();
                m_Widgets.m_wAzElCanvas.TessellateCircle(centerPx, rPx, 8, verts);
                PolygonDrawCommand blip = new PolygonDrawCommand();
                blip.m_iColor = BlipColor(t);
                blip.m_Vertices = verts;
                m_AzElAll.Insert(blip);
                index = index + 1;
            }
        }

        m_Widgets.m_wAzElCanvas.SetDrawCommands(m_AzElAll);
    }

    protected void UpdateList(
        array<ref RDF_RadarTarget> targets,
        vector origin,
        RDF_RadarProjectileTracker tracker)
    {
        if (!m_Widgets || !m_Widgets.m_wListBody)
            return;

        // MANUAL mode: the contacts panel shows the operator parameter list
        // instead of radar contacts; the menu owns its content.
        if (m_Mode == GBRS_RadarStationConstants.MODE_MANUAL)
        {
            if (m_ManualParamText != "")
                m_Widgets.m_wListBody.SetText(m_ManualParamText);
            return;
        }

        string body = "";
        int row = 0;
        if (targets)
        {
            foreach (RDF_RadarTarget t : targets)
            {
                if (!t || !t.m_Detected)
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

                string line = PadNum(row, 2) + "  "
                    + PadNum(az, 3) + "   "
                    + Fmt1(rngKm) + "   "
                    + Fmt1(altKm) + "   "
                    + PadNum(spd, 3) + "   "
                    + TypeTag(t) + "   "
                    + F0(t.m_SnrDb) + "dB";
                if (body != "")
                    body = body + "\n";
                body = body + line;
                row = row + 1;
            }
        }

        if (m_Mode == MODE_WLR && tracker)
            AppendWlrListRows(body, row, origin, tracker);

        if (body == "")
            body = "(no contacts)";
        m_Widgets.m_wListBody.SetText(body);

        int tracks = 0;
        int wlrFixes = 0;
        if (tracker)
        {
            array<ref RDF_RadarTrack> all = tracker.GetAllTracks();
            if (all)
            {
                foreach (RDF_RadarTrack tr : all)
                {
                    if (!tr || !tr.m_Confirmed)
                        continue;
                    tracks = tracks + 1;
                    if (tr.m_LastWlrFix && tr.m_LastWlrFix.m_LaunchValid)
                        wlrFixes = wlrFixes + 1;
                }
            }
        }

        int detected = m_DetectedTotal;
        if (detected < row)
            detected = row;

        if (m_Widgets && m_Widgets.m_wListFooter)
        {
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

                // RDF 1.0.0 fire-control bridge: TRACKING lock authorizes fire
                // for external weapons polling TryGetFireSolution.
                if (m_LockManager && m_LockManager.IsLocked())
                    footer = footer + "   FIRE";
            }

            // RDF 1.0.0 ECCM decision status: "eccm=0" hides; active jam shows
            // the countermeasure set (slb / prf / freq / burn).
            if (m_EccmStatus != "eccm=0")
                footer = footer + "   " + m_EccmStatus;

            m_Widgets.m_wListFooter.SetText(footer);
        }
    }

    protected void AppendWlrListRows(
        out string body,
        out int row,
        vector origin,
        RDF_RadarProjectileTracker tracker)
    {
        if (!tracker)
            return;

        array<ref RDF_RadarTrack> all = tracker.GetAllTracks();
        if (!all)
            return;

        foreach (RDF_RadarTrack tr : all)
        {
            if (!tr || !tr.m_Confirmed)
                continue;
            if (!tr.m_LastWlrFix)
                continue;
            if (!tr.m_LastWlrFix.m_LaunchValid && !tr.m_LastWlrFix.m_ImpactValid)
                continue;
            if (row >= MAX_LIST_ROWS)
                break;

            string line = "W" + PadNum(tr.m_TrackId, 2);
            if (tr.m_LastWlrFix.m_LaunchValid)
            {
                vector ld = tr.m_LastWlrFix.m_LaunchPos - origin;
                float laz = Math.Atan2(ld[0], ld[2]) * Math.RAD2DEG;
                if (laz < 0.0)
                    laz = laz + 360.0;
                float lkm = ld.Length() / 1000.0;
                line = line + "  LCH " + PadNum(laz, 3) + " " + Fmt1(lkm) + "km";
            }
            if (tr.m_LastWlrFix.m_ImpactValid)
            {
                vector id = tr.m_LastWlrFix.m_ImpactPos - origin;
                float iaz = Math.Atan2(id[0], id[2]) * Math.RAD2DEG;
                if (iaz < 0.0)
                    iaz = iaz + 360.0;
                float ikm = id.Length() / 1000.0;
                line = line + "  IMP " + PadNum(iaz, 3) + " " + Fmt1(ikm) + "km";
            }

            if (body != "")
                body = body + "\n";
            body = body + line;
            row = row + 1;
        }
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
