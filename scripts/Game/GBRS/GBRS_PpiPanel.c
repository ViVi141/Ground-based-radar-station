//------------------------------------------------------------------------------------------------
//! Canvas PPI for world CRT — face texture + RDF unit-space sweep/blips.
class GBRS_PpiPanel
{
    static const ResourceName PPI_FACE_TEXTURE =
        "{F2196E35CB708A41}UI/Textures/GBRS/GBRS_PpiFace.edds";

    // RDF full RT unit space.
    static const int RT_W = 288;
    static const int RT_H = 256;
    static const int COL_BG = ARGB(255, 0, 0, 0);

    // Face uses bottom/side glass room; higher OY clears top UV clip.
    // Status strip starts ~230 — keep face bottom below that.
    static const int FACE_SIZE = 160;
    static const float FACE_OX = 64.0;  // (288 - 160) / 2
    static const float FACE_OY = 68.0;  // was 50; use empty bottom margin
    static const float PPI_CX = 144.0;
    static const float PPI_CY = 148.0;  // FACE_OY + FACE_SIZE/2
    static const float PPI_R = 75.0;    // FACE_SIZE * (300/640)
    static const int MAX_DRAW_BLIPS = 64;

    static const int COL_PPI_SWEEP = ARGB(250, 90, 255, 170);
    static const int COL_VEHICLE = ARGB(255, 90, 255, 150);
    static const int COL_PROJ = ARGB(255, 255, 190, 55);
    static const int COL_EMITTER = ARGB(255, 255, 110, 240);
    static const int COL_ANON = ARGB(255, 255, 240, 160);
    static const int COL_FALSEPLOT = ARGB(255, 255, 95, 95);
    static const int COL_TRACK = ARGB(255, 255, 255, 255);
    static const int COL_TRACK_COAST = ARGB(230, 140, 210, 255);
    static const int COL_WLR_SHELL = ARGB(255, 255, 220, 70);
    static const int COL_WLR_LAUNCH = ARGB(220, 255, 160, 40);
    static const int COL_WLR_IMPACT = ARGB(220, 80, 180, 255);
    static const int COL_WLR_LINK = ARGB(140, 200, 200, 200);
    static const int COL_LOCK = ARGB(255, 255, 70, 70);
    static const int COL_NET = ARGB(230, 120, 180, 255);

    protected CanvasWidget m_wCanvas;
    protected TextWidget m_wStatus;
    protected ref array<ref CanvasWidgetCommand> m_Cmds;
    protected ref SharedItemRef m_PpiFaceTex;
    protected ref PolygonDrawCommand m_BgCmd;
    protected ref array<float> m_BgVerts;
    protected float m_DisplayRangeM;
    protected string m_Mode;
    protected int m_LockedTrackId;
    protected int m_ContentRevision;

    //------------------------------------------------------------------------------------------------
    void Bind(CanvasWidget canvas, TextWidget status)
    {
        m_wCanvas = canvas;
        m_wStatus = status;
        m_Cmds = new array<ref CanvasWidgetCommand>();
        m_PpiFaceTex = CanvasWidget.LoadTexture(PPI_FACE_TEXTURE);
        m_DisplayRangeM = 12000.0;
        m_Mode = GBRS_RadarStationConstants.MODE_PD_SEARCH;
        m_LockedTrackId = 0;
        m_ContentRevision = -1;

        if (m_wCanvas)
        {
            m_wCanvas.SetVisible(true);
            m_wCanvas.SetSizeInUnits(Vector(RT_W, RT_H, 0));
            FrameSlot.SetPos(m_wCanvas, 0, 0);
            FrameSlot.SetSizeX(m_wCanvas, RT_W);
            FrameSlot.SetSizeY(m_wCanvas, RT_H);
            BuildBackground();
        }
        if (m_wStatus)
        {
            m_wStatus.SetVisible(true);
            m_wStatus.SetText("PPI");
        }
    }

    //------------------------------------------------------------------------------------------------
    void Destroy()
    {
        m_wCanvas = null;
        m_wStatus = null;
        m_PpiFaceTex = null;
        m_BgCmd = null;
        m_BgVerts = null;
        if (m_Cmds)
            m_Cmds.Clear();
        m_Cmds = null;
    }

    //------------------------------------------------------------------------------------------------
    void SetMode(string mode)
    {
        m_Mode = mode;
    }

    //------------------------------------------------------------------------------------------------
    void SetDisplayRange(float rangeM)
    {
        if (rangeM > 0.0)
            m_DisplayRangeM = rangeM;
    }

    //------------------------------------------------------------------------------------------------
    void SetLockedTrackId(int trackId)
    {
        m_LockedTrackId = trackId;
    }

    //------------------------------------------------------------------------------------------------
    float GetDisplayRange()
    {
        return m_DisplayRangeM;
    }

    //------------------------------------------------------------------------------------------------
    void DrawIdle(string status)
    {
        if (!m_wCanvas)
            return;

        m_Cmds.Clear();
        BeginFrame();
        DrawFace();
        m_wCanvas.SetDrawCommands(m_Cmds);
        if (m_wStatus)
            m_wStatus.SetText(status);
    }

    //------------------------------------------------------------------------------------------------
    void DrawFrame(
        array<ref RDF_RadarTarget> plots,
        array<ref RDF_RadarTrack> tracks,
        array<ref RDF_RadarFusedTrack> fused,
        array<ref GBRS_WlrPersistDisplay> wlr,
        vector origin,
        vector forward,
        float rangeM,
        string mode,
        string eccm,
        int lockedTrackId,
        int contentRevision,
        bool forceContent)
    {
        if (!m_wCanvas)
            return;

        if (rangeM > 0.0)
            m_DisplayRangeM = rangeM;
        m_Mode = mode;
        m_LockedTrackId = lockedTrackId;

        if (forceContent || contentRevision != m_ContentRevision)
            m_ContentRevision = contentRevision;

        m_Cmds.Clear();
        BeginFrame();
        DrawFace();
        DrawSweep(forward);
        DrawPlots(plots, origin);
        DrawTracks(tracks, origin);
        DrawFused(fused, origin);
        DrawWlr(wlr, origin);
        m_wCanvas.SetDrawCommands(m_Cmds);

        if (m_wStatus)
        {
            string line = mode;
            line = line + "  R";
            line = line + (m_DisplayRangeM * 0.001).ToString(1, 1);
            line = line + "km";
            if (eccm != string.Empty)
            {
                line = line + "  ";
                line = line + eccm;
            }
            m_wStatus.SetText(line);
        }
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
    protected void BeginFrame()
    {
        if (!m_BgCmd)
            BuildBackground();
        m_Cmds.Insert(m_BgCmd);
    }

    //------------------------------------------------------------------------------------------------
    protected void DrawFace()
    {
        if (!m_PpiFaceTex)
            return;

        // ImageDrawCommand is pixel space; geometry below stays in SizeInUnits.
        vector topLeft = m_wCanvas.PosToPixels(Vector(FACE_OX, FACE_OY, 0.0));
        vector size = m_wCanvas.SizeToPixels(Vector(FACE_SIZE, FACE_SIZE, 0.0));
        ImageDrawCommand face = new ImageDrawCommand();
        face.m_pTexture = m_PpiFaceTex;
        face.m_Position = topLeft;
        face.m_Size = size;
        face.m_fRotation = 0.0;
        face.m_iColor = 0xffffffff;
        face.m_iFlags = WidgetFlags.STRETCH;
        m_Cmds.Insert(face);
    }

    //------------------------------------------------------------------------------------------------
    protected void DrawSweep(vector forward)
    {
        float fx = forward[0];
        float fz = forward[2];
        float flen = Math.Sqrt(fx * fx + fz * fz);
        if (flen <= 0.0001)
            return;

        float nx = fx / flen;
        float nz = fz / flen;
        array<float> sweep = new array<float>();
        sweep.Insert(PPI_CX);
        sweep.Insert(PPI_CY);
        sweep.Insert(PPI_CX + nx * PPI_R);
        sweep.Insert(PPI_CY - nz * PPI_R);
        LineDrawCommand edge = new LineDrawCommand();
        edge.m_iColor = COL_PPI_SWEEP;
        edge.m_fWidth = 2.0;
        edge.m_Vertices = sweep;
        m_Cmds.Insert(edge);
    }

    //------------------------------------------------------------------------------------------------
    protected void DrawPlots(array<ref RDF_RadarTarget> plots, vector origin)
    {
        if (!plots)
            return;

        int index = 0;
        foreach (RDF_RadarTarget t : plots)
        {
            if (!t)
                continue;
            if (index >= MAX_DRAW_BLIPS)
                break;

            float bx;
            float by;
            if (!WorldToPpi(origin, t.m_Position, bx, by))
                continue;

            int color = COL_ANON;
            if (t.m_IsFalsePlot)
                color = COL_FALSEPLOT;
            else if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
                color = COL_PROJ;
            else if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
                color = COL_EMITTER;
            else if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE)
                color = COL_VEHICLE;

            DrawBlip(bx, by, 3.5, color);
            index = index + 1;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void DrawTracks(array<ref RDF_RadarTrack> tracks, vector origin)
    {
        if (!tracks)
            return;

        int index = 0;
        foreach (RDF_RadarTrack tr : tracks)
        {
            if (!tr)
                continue;
            if (index >= MAX_DRAW_BLIPS)
                break;

            float bx;
            float by;
            if (!WorldToPpi(origin, tr.m_FilteredPosition, bx, by))
                continue;

            int color = COL_TRACK;
            if (tr.m_Coasting)
                color = COL_TRACK_COAST;
            if (tr.m_TrackId == m_LockedTrackId && m_LockedTrackId > 0)
                color = COL_LOCK;

            if (m_Mode == GBRS_RadarStationConstants.MODE_WLR)
                DrawSquare(bx, by, 5.0, COL_WLR_SHELL);
            else
                DrawSquare(bx, by, 4.5, color);

            index = index + 1;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void DrawFused(array<ref RDF_RadarFusedTrack> fused, vector origin)
    {
        if (!fused)
            return;

        foreach (RDF_RadarFusedTrack f : fused)
        {
            if (!f)
                continue;

            float bx;
            float by;
            if (!WorldToPpi(origin, f.m_WorldPos, bx, by))
                continue;

            DrawBlip(bx, by, 4.0, COL_NET);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void DrawWlr(array<ref GBRS_WlrPersistDisplay> wlr, vector origin)
    {
        if (!wlr)
            return;

        foreach (GBRS_WlrPersistDisplay entry : wlr)
        {
            if (!entry)
                continue;

            float lx;
            float ly;
            float ix;
            float iy;
            bool hasL = false;
            bool hasI = false;
            if (entry.m_HasLaunch)
            {
                if (WorldToPpi(origin, entry.m_LaunchPos, lx, ly))
                {
                    DrawSquare(lx, ly, 5.0, COL_WLR_LAUNCH);
                    hasL = true;
                }
            }
            if (entry.m_HasImpact)
            {
                if (WorldToPpi(origin, entry.m_ImpactPos, ix, iy))
                {
                    DrawSquare(ix, iy, 5.0, COL_WLR_IMPACT);
                    hasI = true;
                }
            }
            if (hasL && hasI)
            {
                array<float> link = new array<float>();
                link.Insert(lx);
                link.Insert(ly);
                link.Insert(ix);
                link.Insert(iy);
                LineDrawCommand line = new LineDrawCommand();
                line.m_iColor = COL_WLR_LINK;
                line.m_fWidth = 1.5;
                line.m_Vertices = link;
                m_Cmds.Insert(line);
            }
            if (entry.m_HasLive)
            {
                float sx;
                float sy;
                if (WorldToPpi(origin, entry.m_LivePos, sx, sy))
                    DrawSquare(sx, sy, 4.0, COL_WLR_SHELL);
            }
        }
    }

    //------------------------------------------------------------------------------------------------
    protected bool WorldToPpi(vector origin, vector worldPos, out float bx, out float by)
    {
        bx = PPI_CX;
        by = PPI_CY;
        if (m_DisplayRangeM <= 0.0)
            return false;

        vector d = worldPos - origin;
        float dist = Math.Sqrt(d[0] * d[0] + d[2] * d[2]);
        if (dist > m_DisplayRangeM * 1.05)
            return false;

        float scale = PPI_R / m_DisplayRangeM;
        bx = PPI_CX + d[0] * scale;
        by = PPI_CY - d[2] * scale;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected void DrawBlip(float bx, float by, float r, int color)
    {
        int a;
        int rr;
        int gg;
        int bb;
        Color.UnpackInt(color, a, rr, gg, bb);
        int halo = ARGB(90, rr, gg, bb);

        vector center = Vector(bx, by, 0.0);
        array<float> hv = new array<float>();
        m_wCanvas.TessellateCircle(center, r + 2.5, 8, hv);
        PolygonDrawCommand haloPoly = new PolygonDrawCommand();
        haloPoly.m_iColor = halo;
        haloPoly.m_Vertices = hv;
        m_Cmds.Insert(haloPoly);

        array<float> bv = new array<float>();
        m_wCanvas.TessellateCircle(center, r, 8, bv);
        PolygonDrawCommand blip = new PolygonDrawCommand();
        blip.m_iColor = color;
        blip.m_Vertices = bv;
        m_Cmds.Insert(blip);
    }

    //------------------------------------------------------------------------------------------------
    protected void DrawSquare(float bx, float by, float half, int color)
    {
        array<float> v = new array<float>();
        v.Insert(bx - half);
        v.Insert(by - half);
        v.Insert(bx + half);
        v.Insert(by - half);
        v.Insert(bx + half);
        v.Insert(by + half);
        v.Insert(bx - half);
        v.Insert(by + half);
        PolygonDrawCommand poly = new PolygonDrawCommand();
        poly.m_iColor = color;
        poly.m_Vertices = v;
        m_Cmds.Insert(poly);
    }
}
