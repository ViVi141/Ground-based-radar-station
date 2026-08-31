//------------------------------------------------------------------------------------------------
//! OPTICS CRT — RDF RTTexture shell; idle Canvas keeps CRT dark until PIP is armed.
class GBRS_OpticsScreen
{
    static const ResourceName LAYOUT =
        "{69FCEDCEA0080003}UI/layouts/GBRS/OpticsScreen.layout";
    static const int RT_W = 288;
    static const int RT_H = 256;
    static const int OPTICS_CAMERA_INDEX = 17;
    static const float OPTICS_FOV_DEG = 32.0;
    static const float OPTICS_NEAR_M = 0.25;
    static const float OPTICS_EYE_HEIGHT_M = 1.8;
    static const float OPTICS_FORWARD_CLEAR_M = 2.5;
    static const float OPTICS_LOOK_UP_Y = 0.08;
    static const float OPTICS_HDR_BOOST = 1.35;
    static const int MAX_FPS = 12;
    static const int COL_BG = ARGB(255, 0, 0, 0);

    protected Widget m_wRoot;
    protected RTTextureWidget m_wRTTexture;
    protected CanvasWidget m_wCanvas;
    protected RenderTargetWidget m_wOpticsRT;
    protected TextWidget m_wStatus;
    protected IEntity m_ScreenMesh;
    protected GBRS_RadarStationComponent m_Station;
    protected SCR_PIPCamera m_OpticsCamera;
    protected bool m_bEnabled;
    protected bool m_bLive;
    protected ref array<ref CanvasWidgetCommand> m_Cmds;
    protected ref PolygonDrawCommand m_BgCmd;
    protected ref array<float> m_BgVerts;

    //------------------------------------------------------------------------------------------------
    bool IsEnabled()
    {
        return m_bEnabled;
    }

    //------------------------------------------------------------------------------------------------
    bool IsLive()
    {
        return m_bLive;
    }

    //------------------------------------------------------------------------------------------------
    //! Bind RT shell in idle (OPTICS OFF). Call SetLive(true) for PIP.
    //! screenMesh must already be PrepareScreenMesh()'d (LOD0 + remap).
    bool EnableScreen(IEntity screenMesh, GBRS_RadarStationComponent station)
    {
        DisableScreen();
        if (!screenMesh || !station)
            return false;

        WorkspaceWidget ws = GetGame().GetWorkspace();
        if (!ws)
            return false;

        m_wRoot = ws.CreateWidgets(LAYOUT, null);
        if (!m_wRoot)
        {
            Print("[GBRS OPTICS] CreateWidgets failed — open OpticsScreen.layout in Workbench once", LogLevel.ERROR);
            return false;
        }

        m_wRTTexture = RTTextureWidget.Cast(m_wRoot.FindAnyWidget("RTTexture"));
        m_wCanvas = CanvasWidget.Cast(m_wRoot.FindAnyWidget("OpticsCanvas"));
        m_wOpticsRT = RenderTargetWidget.Cast(m_wRoot.FindAnyWidget("OpticsRT"));
        m_wStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("StatusText"));
        if (!m_wRTTexture || !m_wCanvas || !m_wOpticsRT)
        {
            Print("[GBRS OPTICS] RTTexture/OpticsCanvas/OpticsRT missing", LogLevel.ERROR);
            DisableScreen();
            return false;
        }

        m_ScreenMesh = screenMesh;
        m_Station = station;
        m_Cmds = new array<ref CanvasWidgetCommand>();
        BuildBackground();

        m_wRoot.SetVisible(true);
        FrameSlot.SetPos(m_wRoot, -4096, -4096);
        FrameSlot.SetSizeX(m_wRoot, RT_W);
        FrameSlot.SetSizeY(m_wRoot, RT_H);
        FrameSlot.SetPos(m_wRTTexture, 0, 0);
        FrameSlot.SetSizeX(m_wRTTexture, RT_W);
        FrameSlot.SetSizeY(m_wRTTexture, RT_H);
        FrameSlot.SetPos(m_wCanvas, 0, 0);
        FrameSlot.SetSizeX(m_wCanvas, RT_W);
        FrameSlot.SetSizeY(m_wCanvas, RT_H - 30);
        FrameSlot.SetPos(m_wOpticsRT, 0, 0);
        FrameSlot.SetSizeX(m_wOpticsRT, RT_W);
        FrameSlot.SetSizeY(m_wOpticsRT, RT_H - 30);

        m_wCanvas.SetVisible(true);
        m_wCanvas.SetSizeInUnits(Vector(RT_W, RT_H, 0));
        m_wOpticsRT.SetVisible(false);

        m_wRTTexture.SetRenderTarget(screenMesh);
        m_wRTTexture.SetMaxFPS(MAX_FPS);
        m_wRTTexture.SetEnabled(false);

        m_bEnabled = true;
        m_bLive = false;
        PushIdleFrame();
        if (m_wStatus)
        {
            m_wStatus.SetVisible(true);
            m_wStatus.SetText("OPTICS OFF");
        }
        return true;
    }

    //------------------------------------------------------------------------------------------------
    bool SetLive(bool live)
    {
        if (!m_bEnabled || !m_Station)
            return false;

        if (!live)
        {
            DestroyOpticsCamera();
            if (m_wOpticsRT)
                m_wOpticsRT.SetVisible(false);
            m_bLive = false;
            PushIdleFrame();
            if (m_wStatus)
                m_wStatus.SetText("OPTICS OFF");
            return true;
        }

        IEntity owner = m_Station.GetOwner();
        if (!owner)
            return false;

        if (!CreateOpticsCamera(owner))
            return false;

        m_bLive = true;
        UpdatePose();
        if (m_wStatus)
            m_wStatus.SetText("OPTICS LIVE");
        return true;
    }

    //------------------------------------------------------------------------------------------------
    void DisableScreen()
    {
        DestroyOpticsCamera();
        if (m_wRTTexture && m_ScreenMesh)
            m_wRTTexture.RemoveRenderTarget(m_ScreenMesh);
        if (m_wRoot)
        {
            m_wRoot.RemoveFromHierarchy();
            m_wRoot = null;
        }
        m_wRTTexture = null;
        m_wCanvas = null;
        m_wOpticsRT = null;
        m_wStatus = null;
        m_ScreenMesh = null;
        m_Station = null;
        m_BgCmd = null;
        m_BgVerts = null;
        if (m_Cmds)
            m_Cmds.Clear();
        m_Cmds = null;
        m_bEnabled = false;
        m_bLive = false;
    }

    //------------------------------------------------------------------------------------------------
    void Tick()
    {
        if (!m_bEnabled)
            return;
        if (m_bLive)
            UpdatePose();
        else
            PushIdleFrame();
    }

    //------------------------------------------------------------------------------------------------
    void UpdatePose()
    {
        if (!m_bEnabled || !m_bLive || !m_OpticsCamera || !m_Station)
            return;

        vector origin = m_Station.GetScanOriginWorld();
        vector forward = m_Station.GetScanForwardWorld();
        ApplyPose(origin, forward);
    }

    //------------------------------------------------------------------------------------------------
    protected void PushIdleFrame()
    {
        if (!m_wCanvas || !m_Cmds)
            return;
        if (!m_BgCmd)
            BuildBackground();
        m_Cmds.Clear();
        m_Cmds.Insert(m_BgCmd);
        m_wCanvas.SetDrawCommands(m_Cmds);
    }

    //------------------------------------------------------------------------------------------------
    protected void BuildBackground()
    {
        m_BgVerts = new array<float>();
        m_BgVerts.Insert(0.0);
        m_BgVerts.Insert(0.0);
        m_BgVerts.Insert(RT_W);
        m_BgVerts.Insert(0.0);
        m_BgVerts.Insert(RT_W);
        m_BgVerts.Insert(RT_H);
        m_BgVerts.Insert(0.0);
        m_BgVerts.Insert(RT_H);
        m_BgCmd = new PolygonDrawCommand();
        m_BgCmd.m_iColor = COL_BG;
        m_BgCmd.m_Vertices = m_BgVerts;
    }

    //------------------------------------------------------------------------------------------------
    protected void ApplyPose(vector origin, vector forward)
    {
        float flen = forward.Length();
        if (flen < 0.001)
            forward = Vector(0.0, 0.0, 1.0);
        else
            forward = forward * (1.0 / flen);

        vector look = Vector(forward[0], OPTICS_LOOK_UP_Y, forward[2]);
        float llen = look.Length();
        if (llen > 0.001)
            look = look * (1.0 / llen);

        vector mat[4];
        Math3D.DirectionAndUpMatrix(look, Vector(0.0, 1.0, 0.0), mat);
        mat[3] = origin + Vector(0.0, OPTICS_EYE_HEIGHT_M, 0.0) + (look * OPTICS_FORWARD_CLEAR_M);
        m_OpticsCamera.SetWorldTransform(mat);
        m_OpticsCamera.UpdatePIPCamera(0.0);

        BaseWorld world = m_OpticsCamera.GetWorld();
        if (world)
        {
            float mainHdr = world.GetCameraHDRBrightness(0);
            world.SetCameraHDRBrightness(OPTICS_CAMERA_INDEX, mainHdr * OPTICS_HDR_BOOST);
        }

        if (m_wStatus)
        {
            float az = Math.Atan2(forward[0], forward[2]) * Math.RAD2DEG;
            if (az < 0.0)
                az = az + 360.0;
            string line = "OPT AZ ";
            line = line + az.ToString(1, 0);
            m_wStatus.SetText(line);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected bool CreateOpticsCamera(IEntity parent)
    {
        if (!parent || parent.IsDeleted())
            return false;

        BaseWorld world = parent.GetWorld();
        if (!world)
            return false;

        DestroyOpticsCamera();

        EntitySpawnParams params = new EntitySpawnParams();
        parent.GetWorldTransform(params.Transform);
        m_OpticsCamera = SCR_PIPCamera.Cast(GetGame().SpawnEntity(SCR_PIPCamera, world, params));
        if (!m_OpticsCamera)
            return false;

        float farPlane = GetGame().GetViewDistance();
        if (farPlane < 500.0)
            farPlane = 500.0;
        if (farPlane > 8000.0)
            farPlane = 8000.0;

        m_OpticsCamera.SetCameraIndex(OPTICS_CAMERA_INDEX);
        m_OpticsCamera.SetVerticalFOV(OPTICS_FOV_DEG);
        m_OpticsCamera.SetNearPlane(OPTICS_NEAR_M);
        m_OpticsCamera.SetFarPlane(farPlane);
        m_OpticsCamera.ApplyProps(OPTICS_CAMERA_INDEX);
        world.SetCameraLensFlareSet(OPTICS_CAMERA_INDEX, CameraLensFlareSetType.FirstPerson, string.Empty);

        m_wOpticsRT.SetVisible(true);
        m_wOpticsRT.SetZOrder(5);
        m_wOpticsRT.SetWorld(world, OPTICS_CAMERA_INDEX);
        m_wOpticsRT.SetResolutionScale(1.0, 1.0);
        m_wOpticsRT.SetMaxFPS(MAX_FPS);
        m_wOpticsRT.SetFormat(RenderTargetWidgetFormat.HDR_HIGH);
        m_wOpticsRT.SetClearColor(true, ARGB(255, 0, 0, 0));

        float mainHdr = world.GetCameraHDRBrightness(0);
        world.SetCameraHDRBrightness(OPTICS_CAMERA_INDEX, mainHdr * OPTICS_HDR_BOOST);
        return true;
    }

    //------------------------------------------------------------------------------------------------
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
}
