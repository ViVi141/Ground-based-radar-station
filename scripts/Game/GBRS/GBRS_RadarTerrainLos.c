// Hard terrain LOS for GBRS display / post-filter.
// RDF TraceMove (Projectile + WORLD) can miss long-range hills; live GetSurfaceY
// sampling matches RDF knife-edge DEM geometry and blocks PPI/visual blips.
class GBRS_RadarTerrainLos
{
    protected static const float DEFAULT_SLACK_M = 2.0;
    protected static const float SAMPLE_STEP_M = 75.0;
    protected static const int SAMPLE_MIN = 4;
    protected static const int SAMPLE_MAX = 32;
    // Official SCR_PhysicsHelper uses 100 m segments for long traces.
    protected static const float TRACE_SEGMENT_M = 100.0;

    // True when no terrain sample rises above the geometric LOS segment.
    static bool IsClear(BaseWorld world, vector origin, vector targetPos)
    {
        return IsClearWithSlack(world, origin, targetPos, DEFAULT_SLACK_M);
    }

    static bool IsClearWithSlack(
        BaseWorld world,
        vector origin,
        vector targetPos,
        float slackM)
    {
        if (!world)
            return true;

        vector delta = targetPos - origin;
        float dist = delta.Length();
        if (dist < 1.0)
            return true;

        float slack = slackM;
        if (slack < 0.0)
            slack = 0.0;

        int samples = Math.Ceil(dist / SAMPLE_STEP_M);
        if (samples < SAMPLE_MIN)
            samples = SAMPLE_MIN;
        if (samples > SAMPLE_MAX)
            samples = SAMPLE_MAX;

        int i = 1;
        while (i <= samples)
        {
            float u = i / (samples + 1.0);
            float x = origin[0] + delta[0] * u;
            float yLos = origin[1] + delta[1] * u;
            float z = origin[2] + delta[2] * u;
            float terrainY = world.GetSurfaceY(x, z);
            float obstacleH = terrainY - slack - yLos;
            if (obstacleH > 0.0)
                return false;
            i = i + 1;
        }

        return true;
    }

    // Official-style visibility Trace (SCR_NameTagRulesetBase / TraceFlags docs):
    // ANY_CONTACT | WORLD | ENTS, LayerMask = EPhysicsLayerDefs.Projectile,
    // ExcludeArray = {radar, target}. Clear iff fraction reaches 1.
    // Long rays are segmented like SCR_PhysicsHelper (100 m).
    static bool TraceOfficialClear(
        BaseWorld world,
        IEntity radarSubject,
        IEntity target,
        vector origin,
        vector targetPos,
        out float outHitFraction,
        out string outHitName)
    {
        outHitFraction = 1.0;
        outHitName = "-";
        if (!world)
            return true;

        vector delta = targetPos - origin;
        float dist = delta.Length();
        if (dist < 1.0)
            return true;

        vector dir = delta * (1.0 / dist);
        array<IEntity> exclude = new array<IEntity>();
        if (radarSubject)
            exclude.Insert(radarSubject);
        if (target)
            exclude.Insert(target);

        TraceParam param = new TraceParam();
        param.Flags = TraceFlags.ANY_CONTACT | TraceFlags.WORLD | TraceFlags.ENTS;
        param.LayerMask = EPhysicsLayerDefs.Projectile;
        param.ExcludeArray = exclude;

        float travelled = 0.0;
        while (travelled < dist - 0.05)
        {
            float seg = TRACE_SEGMENT_M;
            if (travelled + seg > dist)
                seg = dist - travelled;

            param.Start = origin + (dir * travelled);
            param.End = origin + (dir * (travelled + seg));
            param.TraceEnt = null;

            float frac = world.TraceMove(param, null);
            if (frac < 0.999)
            {
                outHitFraction = (travelled + seg * frac) / dist;
                outHitName = DescribeHit(param.TraceEnt);
                return false;
            }

            travelled = travelled + seg;
        }

        outHitFraction = 1.0;
        return true;
    }

    // RDF_RadarScanner style (for A/B debug): WORLD|ENTS, Presets.Projectile,
    // Exclude = subject only, End = target center, no ANY_CONTACT, one long ray.
    static bool TraceRdfStyleClear(
        BaseWorld world,
        IEntity radarSubject,
        IEntity target,
        vector origin,
        vector targetPos,
        out float outHitFraction,
        out string outHitName)
    {
        outHitFraction = 1.0;
        outHitName = "-";
        if (!world)
            return true;

        TraceParam param = new TraceParam();
        param.Start = origin;
        param.End = targetPos;
        param.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
        param.LayerMask = EPhysicsLayerPresets.Projectile;
        param.Exclude = radarSubject;
        param.TraceEnt = null;

        float frac = world.TraceMove(param, null);
        outHitFraction = frac;

        if (frac >= 0.999)
            return true;

        if (target && param.TraceEnt)
        {
            if (IsEntityOrChildOf(param.TraceEnt, target))
            {
                outHitName = DescribeHit(param.TraceEnt);
                return true;
            }
        }

        outHitName = DescribeHit(param.TraceEnt);
        return false;
    }

    protected static bool IsEntityOrChildOf(IEntity hitEntity, IEntity target)
    {
        if (!hitEntity || !target)
            return false;
        IEntity cur = hitEntity;
        int guard = 0;
        while (cur && guard < 16)
        {
            if (cur == target)
                return true;
            cur = cur.GetParent();
            guard = guard + 1;
        }
        return false;
    }

    protected static string DescribeHit(IEntity ent)
    {
        if (!ent)
            return "terrain/null";

        EntityPrefabData prefabData = ent.GetPrefabData();
        if (!prefabData)
            return ent.ToString();

        ResourceName prefabName = prefabData.GetPrefabName();
        string path = prefabName;
        int slash = path.LastIndexOf("/");
        if (slash >= 0 && slash + 1 < path.Length())
            path = path.Substring(slash + 1, path.Length() - slash - 1);
        return path;
    }

    // Drop plots whose ray is terrain-blocked. Leaves false/EW plots alone.
    static void FilterPlots(
        BaseWorld world,
        vector origin,
        array<ref RDF_RadarTarget> plots,
        out int kept,
        out int blocked)
    {
        kept = 0;
        blocked = 0;
        if (!plots)
            return;

        int i = plots.Count() - 1;
        while (i >= 0)
        {
            RDF_RadarTarget t = plots.Get(i);
            if (!t)
            {
                plots.Remove(i);
                i = i - 1;
                continue;
            }

            if (t.m_IsFalsePlot)
            {
                kept = kept + 1;
                i = i - 1;
                continue;
            }

            if (IsClear(world, origin, t.m_Position))
            {
                kept = kept + 1;
                i = i - 1;
                continue;
            }

            blocked = blocked + 1;
            plots.Remove(i);
            i = i - 1;
        }
    }
}
