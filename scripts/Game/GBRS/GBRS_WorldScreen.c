//------------------------------------------------------------------------------------------------
//! Off-screen RTTextureWidget → CRT mesh $rendertarget.
//! Mirrors RDF_RadarClutterSurfaceScreen.EnableScreen (do not invent alternatives).
class GBRS_WorldScreen
{
    static const int MAX_FPS = 15;
    static const ResourceName SCREEN_RT_MATERIAL =
        "{C1A84F02B7D35E91}RadarData/ClutterSurface/Computer_Screen_RT.emat";

    protected ResourceName m_Layout;
    protected int m_RtW;
    protected int m_RtH;
    protected string m_CanvasWidgetName;

    protected Widget m_wRoot;
    protected RTTextureWidget m_wRTTexture;
    protected CanvasWidget m_wCanvas;
    protected TextWidget m_wStatus;
    protected IEntity m_ScreenMesh;

    //------------------------------------------------------------------------------------------------
    void GBRS_WorldScreen(ResourceName layout, int rtW, int rtH, string canvasName)
    {
        m_Layout = layout;
        m_RtW = rtW;
        m_RtH = rtH;
        m_CanvasWidgetName = canvasName;
    }

    //------------------------------------------------------------------------------------------------
    bool IsEnabled()
    {
        if (m_wRoot)
            return true;
        return false;
    }

    //------------------------------------------------------------------------------------------------
    CanvasWidget GetCanvas()
    {
        return m_wCanvas;
    }

    //------------------------------------------------------------------------------------------------
    TextWidget GetStatus()
    {
        return m_wStatus;
    }

    //------------------------------------------------------------------------------------------------
    Widget GetRoot()
    {
        return m_wRoot;
    }

    //------------------------------------------------------------------------------------------------
    IEntity GetScreenMesh()
    {
        return m_ScreenMesh;
    }

    //------------------------------------------------------------------------------------------------
    //! screenMesh must already be PrepareScreenMesh()'d (LOD0 + remap), same as
    //! RDF_RadarClutterSurfaceManualDemo.BindOfficialComputer → EnableScreen.
    bool EnableScreen(IEntity screenMesh)
    {
        DisableScreen();
        if (!screenMesh)
            return false;

        WorkspaceWidget ws = GetGame().GetWorkspace();
        if (!ws)
        {
            Print("[GBRS CRT] EnableScreen: workspace null", LogLevel.ERROR);
            return false;
        }

        // RDF: CreateWidgets(LAYOUT, null) — never parent to WorkspaceWidget.
        m_wRoot = ws.CreateWidgets(m_Layout, null);
        if (!m_wRoot)
        {
            Print("[GBRS CRT] EnableScreen: CreateWidgets failed — open layout in Workbench once", LogLevel.ERROR);
            return false;
        }

        m_wRTTexture = RTTextureWidget.Cast(m_wRoot.FindAnyWidget("RTTexture"));
        if (m_CanvasWidgetName != string.Empty)
        {
            Widget wCanvas = m_wRoot.FindAnyWidget(m_CanvasWidgetName);
            if (wCanvas)
                m_wCanvas = CanvasWidget.Cast(wCanvas);
        }
        m_wStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("StatusText"));

        if (!m_wRTTexture)
        {
            Print("[GBRS CRT] EnableScreen: RTTexture missing", LogLevel.ERROR);
            DisableScreen();
            return false;
        }
        if (m_CanvasWidgetName != string.Empty && !m_wCanvas)
        {
            Print(string.Format("[GBRS CRT] EnableScreen: canvas '%1' missing", m_CanvasWidgetName), LogLevel.ERROR);
            DisableScreen();
            return false;
        }

        m_ScreenMesh = screenMesh;

        // Keep in hierarchy so RT updates; park off-screen (BallisticTable / RDF CAS).
        // SetEnabled(false) matches SCR_DataDisplayGadget + RDF_RadarClutterSurfaceScreen.
        m_wRoot.SetVisible(true);
        FrameSlot.SetPos(m_wRoot, -4096, -4096);
        FrameSlot.SetSizeX(m_wRoot, m_RtW);
        FrameSlot.SetSizeY(m_wRoot, m_RtH);
        FrameSlot.SetPos(m_wRTTexture, 0, 0);
        FrameSlot.SetSizeX(m_wRTTexture, m_RtW);
        FrameSlot.SetSizeY(m_wRTTexture, m_RtH);
        if (m_wCanvas)
        {
            // RDF BindCanvas overwrites EnableScreen's 226 with full RT height.
            FrameSlot.SetPos(m_wCanvas, 0, 0);
            FrameSlot.SetSizeX(m_wCanvas, m_RtW);
            FrameSlot.SetSizeY(m_wCanvas, m_RtH);
        }

        m_wRTTexture.SetRenderTarget(screenMesh);
        m_wRTTexture.SetMaxFPS(MAX_FPS);
        m_wRTTexture.SetEnabled(false);

        Print(string.Format("[GBRS CRT] EnableScreen OK — RT %1x%2 (H×V) canvas=%3",
            m_RtW, m_RtH, m_wCanvas != null), LogLevel.NORMAL);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    void DisableScreen()
    {
        if (m_wRTTexture && m_ScreenMesh)
            m_wRTTexture.RemoveRenderTarget(m_ScreenMesh);

        if (m_wRoot)
        {
            m_wRoot.RemoveFromHierarchy();
            m_wRoot = null;
        }

        m_wRTTexture = null;
        m_wCanvas = null;
        m_wStatus = null;
        m_ScreenMesh = null;
    }

    //------------------------------------------------------------------------------------------------
    //! Call once before EnableScreen — same order as RDF BindOfficialComputer.
    //! Do NOT call again every frame (re-SetObject breaks the RT bind → black CRT).
    static IEntity PrepareScreenMesh(IEntity taggedOrRoot)
    {
        if (!taggedOrRoot)
            return null;

        ForceHighDetailLod(taggedOrRoot);

        // Prefab MaterialAssign alone often leaves stock CRT art — always force remap.
        int remapped = RemapScreenMaterials(taggedOrRoot);
        Print(string.Format("[GBRS CRT] screen slot remaps=%1", remapped), LogLevel.NORMAL);

        IEntity monitor = FindMonitorEntity(taggedOrRoot);
        if (!monitor)
            monitor = taggedOrRoot;

        // Force remap again on the mesh entity that receives SetRenderTarget.
        remapped = remapped + ForceRemapComputerScreen(monitor);
        if (remapped < 1)
            DumpMaterials(taggedOrRoot);

        return monitor;
    }

    //------------------------------------------------------------------------------------------------
    // Stock Computer_E_01 collapses the CRT into a solid slab on far LODs.
    static void ForceHighDetailLod(IEntity ent)
    {
        if (!ent)
            return;
        ent.SetFixedLOD(0);
        IEntity child = ent.GetChildren();
        while (child)
        {
            ForceHighDetailLod(child);
            child = child.GetSibling();
        }
    }

    //------------------------------------------------------------------------------------------------
    static int ForceRemapComputerScreen(IEntity ent)
    {
        if (!ent)
            return 0;
        VObject mesh = ent.GetVObject();
        if (!mesh)
            return 0;
        string remap = string.Format("$remap 'Computer_Screen_On' '%1';", SCREEN_RT_MATERIAL);
        ent.SetObject(mesh, remap);
        return 1;
    }

    //------------------------------------------------------------------------------------------------
    static int RemapScreenMaterials(IEntity ent)
    {
        if (!ent)
            return 0;

        int count = 0;
        VObject mesh = ent.GetVObject();
        if (mesh)
        {
            string materials[256];
            int numMats = mesh.GetMaterials(materials);
            // Slot name used by Computer_E_01 Monitor.xob / MaterialAssign.
            string remap = string.Format("$remap 'Computer_Screen_On' '%1';", SCREEN_RT_MATERIAL);
            bool hit = false;
            int i = 0;
            while (i < numMats)
            {
                string matName = materials[i];
                i = i + 1;
                if (!IsComputerScreenMaterialName(matName))
                    continue;
                if (matName != "Computer_Screen_On")
                    remap = remap + string.Format("$remap '%1' '%2';", matName, SCREEN_RT_MATERIAL);
                hit = true;
                Print(string.Format("[GBRS CRT] mat slot '%1' → RT", matName), LogLevel.NORMAL);
            }
            if (hit)
            {
                ent.SetObject(mesh, remap);
                count = count + 1;
            }
        }

        IEntity child = ent.GetChildren();
        while (child)
        {
            count = count + RemapScreenMaterials(child);
            child = child.GetSibling();
        }
        return count;
    }

    //------------------------------------------------------------------------------------------------
    static void DumpMaterials(IEntity ent)
    {
        if (!ent)
            return;
        VObject mesh = ent.GetVObject();
        if (mesh)
        {
            string materials[256];
            int numMats = mesh.GetMaterials(materials);
            Print(string.Format("[GBRS CRT] entity mats=%1 (no Computer_Screen_* slot)", numMats), LogLevel.WARNING);
            int i = 0;
            while (i < numMats)
            {
                Print(string.Format("  [%1] %2", i, materials[i]), LogLevel.WARNING);
                i = i + 1;
            }
        }
        IEntity child = ent.GetChildren();
        while (child)
        {
            DumpMaterials(child);
            child = child.GetSibling();
        }
    }

    //------------------------------------------------------------------------------------------------
    static IEntity FindMonitorEntity(IEntity root)
    {
        if (!root)
            return null;
        if (EntityHasScreenMaterial(root))
            return root;

        IEntity child = root.GetChildren();
        while (child)
        {
            IEntity found = FindMonitorEntity(child);
            if (found)
                return found;
            child = child.GetSibling();
        }
        return null;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool EntityHasScreenMaterial(IEntity ent)
    {
        if (!ent)
            return false;
        VObject mesh = ent.GetVObject();
        if (!mesh)
            return false;
        string materials[256];
        int numMats = mesh.GetMaterials(materials);
        int i = 0;
        while (i < numMats)
        {
            if (IsComputerScreenMaterialName(materials[i]))
                return true;
            i = i + 1;
        }
        return false;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool IsComputerScreenMaterialName(string matName)
    {
        if (matName == string.Empty)
            return false;
        if (matName == "Computer_Screen_On")
            return true;
        string lower = matName;
        lower.ToLower();
        if (lower.Contains("computer_screen"))
            return true;
        if (lower.Contains("screen_on"))
            return true;
        if (lower.Contains("computer_e_01_screen"))
            return true;
        return false;
    }
}
