// World-space markers for radar detections (current dwell + short afterglow).
class GBRS_RadarDetectBlip
{
    vector m_Pos;
    float m_BirthS;
    ERDF_RadarTargetType m_Type;
    bool m_DrawRay;
}

class GBRS_RadarDetectVisual
{
    protected static const int MAX_BLIPS = 256;
    protected static const float POINT_SIZE_M = 2.5;
    protected static const float LIFE_SEC_MIN = 2.5;
    protected static const float LIFE_SEC_MAX = 12.0;

    protected ref array<ref GBRS_RadarDetectBlip> m_Blips;
    protected int m_iWrite;

    void GBRS_RadarDetectVisual()
    {
        m_Blips = new array<ref GBRS_RadarDetectBlip>();
        m_iWrite = 0;
    }

    void Clear()
    {
        if (m_Blips)
            m_Blips.Clear();
        m_iWrite = 0;
    }

    void Ingest(array<ref RDF_RadarTarget> plots, vector origin, float nowS)
    {
        if (!plots)
            return;
        if (!m_Blips)
            m_Blips = new array<ref GBRS_RadarDetectBlip>();

        BaseWorld world = GetGame().GetWorld();

        int i = 0;
        while (i < plots.Count())
        {
            RDF_RadarTarget t = plots.Get(i);
            i = i + 1;
            if (!t || !t.m_Detected)
                continue;
            if (!t.m_IsFalsePlot)
            {
                if (!GBRS_RadarTerrainLos.IsClear(world, origin, t.m_Position))
                    continue;
            }

            GBRS_RadarDetectBlip blip = Alloc();
            if (!blip)
                continue;

            blip.m_Pos = t.m_Position;
            blip.m_BirthS = nowS;
            blip.m_Type = t.m_Type;
            blip.m_DrawRay = true;
        }
    }

    void Draw(vector origin, float scanRpm, float nowS)
    {
        if (!m_Blips)
            return;

        float life = 3.0;
        if (scanRpm > 0.0)
            life = (60.0 / scanRpm) * 1.15;
        if (life < LIFE_SEC_MIN)
            life = LIFE_SEC_MIN;
        if (life > LIFE_SEC_MAX)
            life = LIFE_SEC_MAX;

        BaseWorld world = GetGame().GetWorld();
        ShapeFlags flags = ShapeFlags.ONCE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP;

        int i = 0;
        while (i < m_Blips.Count())
        {
            GBRS_RadarDetectBlip blip = m_Blips.Get(i);
            i = i + 1;
            if (!blip)
                continue;

            float age = nowS - blip.m_BirthS;
            if (age < 0.0 || age > life)
                continue;

            if (!GBRS_RadarTerrainLos.IsClear(world, origin, blip.m_Pos))
            {
                blip.m_BirthS = nowS - life - 1.0;
                continue;
            }

            float fade = 1.0 - (age / life);
            if (fade < 0.15)
                fade = 0.15;

            int colour = ColourForType(blip.m_Type, fade);
            float size = POINT_SIZE_M * (0.55 + 0.45 * fade);
            Shape.CreateSphere(colour, flags, blip.m_Pos, size);

            if (blip.m_DrawRay && age < (life * 0.35))
            {
                vector ray[2];
                ray[0] = origin;
                ray[1] = blip.m_Pos;
                int rayCol = ColourForType(blip.m_Type, fade * 0.55);
                Shape.CreateLines(rayCol, flags, ray, 2);
            }
        }
    }

    protected GBRS_RadarDetectBlip Alloc()
    {
        if (!m_Blips)
            m_Blips = new array<ref GBRS_RadarDetectBlip>();

        if (m_Blips.Count() < MAX_BLIPS)
        {
            GBRS_RadarDetectBlip created = new GBRS_RadarDetectBlip();
            m_Blips.Insert(created);
            return created;
        }

        if (m_iWrite >= m_Blips.Count())
            m_iWrite = 0;

        GBRS_RadarDetectBlip slot = m_Blips.Get(m_iWrite);
        if (!slot)
        {
            slot = new GBRS_RadarDetectBlip();
            m_Blips.Set(m_iWrite, slot);
        }

        m_iWrite = m_iWrite + 1;
        return slot;
    }

    protected int ColourForType(ERDF_RadarTargetType type, float alpha)
    {
        int a = Math.Round(alpha * 220.0);
        if (a < 40)
            a = 40;
        if (a > 255)
            a = 255;

        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return ARGB(a, 255, 90, 40);
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return ARGB(a, 255, 80, 230);
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS)
            return ARGB(a, 255, 240, 140);

        return ARGB(a, 70, 255, 120);
    }
}
