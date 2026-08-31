//------------------------------------------------------------------------------------------------
//! Canvas PPI for world CRT — plots, sweep, TWS / WLR markers from snapshot data.
class GBRS_PpiPanel
{
    static const ResourceName PPI_FACE_TEXTURE =
        "{F2196E35CB708A41}UI/Textures/GBRS/GBRS_PpiFace.edds";

    // Match RDF_RadarClutterSurfacePanel unit space (full RT 288×256).
    static const int RT_W = 288;
    static const int RT_H = 256;
    static const int FACE_W = 288;
    static const int FACE_H = 226;
    static const float PPI_CX = 144.0;
    static const float PPI_CY = 118.0;
    static const float PPI_R = 108.0;
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

        // RDF_RadarClutterSurfacePanel.BindCanvas — unit space is full RT (not 226).
        if (m_wCanvas)
        {
            m_wCanvas.SetVisible(true);
            m_wCanvas.SetSizeInUnits(Vector(RT_W, RT_H, 0));
            FrameSlot.SetSizeX(m_wCanvas, RT_W);
            FrameSlot.SetSizeY(m_wCanvas, RT_H);
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
        DrawFace();
        DrawRangeRings();
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

        bool redrawContent = forceContent;
        if (contentRevision != m_ContentRevision)
            redrawContent = true;

        m_Cmds.Clear();
        DrawFace();
        DrawRangeRings();
        DrawSweep(forward);

        if (redrawContent)
            m_ContentRevision = contentRevision;

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
    protected void DrawFace()
    {
        if (!m_PpiFaceTex)
            return;

        vector topLeft = m_wCanvas.PosToPixels(Vector(0.0, 0.0, 0.0));
        vector size = m_wCanvas.SizeToPixels(Vector(FACE_W, FACE_H, 0.0));
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
    protected void DrawRangeRings()
    {
        int i = 1;
        while (i <= 3)
        {
            float r = PPI_R * (i / 3.0);
            DrawCircleOutline(PPI_CX, PPI_CY, r, ARGB(90, 40, 180, 120), 1.0);
            i = i + 1;
        }
        DrawCircleOutline(PPI_CX, PPI_CY, 2.0, ARGB(200, 90, 255, 170), 1.5);
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
        AppendUnitPoint(sweep, PPI_CX, PPI_CY);
        AppendUnitPoint(sweep, PPI_CX + nx * PPI_R, PPI_CY - nz * PPI_R);
        LineDrawCommand edge = new LineDrawCommand();
        edge.m_iColor = COL_PPI_SWEEP;
        edge.m_fWidth = UnitSizeToPixels(2.5);
        if (edge.m_fWidth < 1.0)
            edge.m_fWidth = 1.0;
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
                AppendUnitPoint(link, lx, ly);
                AppendUnitPoint(link, ix, iy);
                LineDrawCommand line = new LineDrawCommand();
                line.m_iColor = COL_WLR_LINK;
                line.m_fWidth = UnitSizeToPixels(1.5);
                if (line.m_fWidth < 1.0)
                    line.m_fWidth = 1.0;
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

        vector centerPx = m_wCanvas.PosToPixels(Vector(bx, by, 0.0));
        float rPx = UnitSizeToPixels(r);
        float haloPx = UnitSizeToPixels(r + 2.5);
        if (rPx < 1.0)
            rPx = 1.0;
        if (haloPx < rPx + 1.0)
            haloPx = rPx + 1.0;

        array<float> hv = new array<float>();
        m_wCanvas.TessellateCircle(centerPx, haloPx, 8, hv);
        PolygonDrawCommand haloPoly = new PolygonDrawCommand();
        haloPoly.m_iColor = halo;
        haloPoly.m_Vertices = hv;
        m_Cmds.Insert(haloPoly);

        array<float> bv = new array<float>();
        m_wCanvas.TessellateCircle(centerPx, rPx, 8, bv);
        PolygonDrawCommand blip = new PolygonDrawCommand();
        blip.m_iColor = color;
        blip.m_Vertices = bv;
        m_Cmds.Insert(blip);
    }

    //------------------------------------------------------------------------------------------------
    protected void DrawSquare(float bx, float by, float half, int color)
    {
        array<float> v = new array<float>();
        AppendUnitPoint(v, bx - half, by - half);
        AppendUnitPoint(v, bx + half, by - half);
        AppendUnitPoint(v, bx + half, by + half);
        AppendUnitPoint(v, bx - half, by + half);
        PolygonDrawCommand poly = new PolygonDrawCommand();
        poly.m_iColor = color;
        poly.m_Vertices = v;
        m_Cmds.Insert(poly);
    }

    //------------------------------------------------------------------------------------------------
    protected void DrawCircleOutline(float cx, float cy, float r, int color, float widthU)
    {
        vector centerPx = m_wCanvas.PosToPixels(Vector(cx, cy, 0.0));
        float rPx = UnitSizeToPixels(r);
        if (rPx < 1.0)
            rPx = 1.0;

        array<float> ring = new array<float>();
        m_wCanvas.TessellateCircle(centerPx, rPx, 32, ring);
        // Close the ring for LineDrawCommand.
        if (ring.Count() >= 2)
        {
            ring.Insert(ring.Get(0));
            ring.Insert(ring.Get(1));
        }
        LineDrawCommand line = new LineDrawCommand();
        line.m_iColor = color;
        line.m_fWidth = UnitSizeToPixels(widthU);
        if (line.m_fWidth < 1.0)
            line.m_fWidth = 1.0;
        line.m_Vertices = ring;
        m_Cmds.Insert(line);
    }

    //------------------------------------------------------------------------------------------------
    protected void AppendUnitPoint(array<float> pixels, float unitX, float unitY)
    {
        vector p = m_wCanvas.PosToPixels(Vector(unitX, unitY, 0.0));
        pixels.Insert(p[0]);
        pixels.Insert(p[1]);
    }

    //------------------------------------------------------------------------------------------------
    protected float UnitSizeToPixels(float unitSize)
    {
        vector px = m_wCanvas.SizeToPixels(Vector(unitSize, unitSize, 0.0));
        return px[0];
    }
}
