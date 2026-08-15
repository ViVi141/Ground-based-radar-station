// Workstation WLR fire solution: estimate AirDrag from track history, then
// integrate that k to launch / impact. Short or unstable arcs keep the
// 82 mm HE prior so a noisy k cannot make the fix worse.
class GBRS_RadarWlrSolution
{
    ref RDF_RadarWlrFix m_Fix;
    float m_AirDrag;
    bool m_DragEstimated;
    float m_SpanS;
    int m_HitCount;
}

class GBRS_RadarWlrSolveCache
{
    int m_TrackId;
    float m_LastSolveS;
    float m_AirDrag;
    int m_HistCount;
    float m_LastSampleS;
    ref GBRS_RadarWlrSolution m_Solution;
}

class GBRS_RadarWlrBallisticSolver
{
    // Same numeric prior as RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE (O832DU).
    static const float K_PRIOR = 0.000615;
    static const float K_MIN = 0.00035;
    static const float K_MAX = 0.00220;
    static const int K_GRID = 12;
    static const float ESTIMATE_MIN_SPAN_S = 1.5;
    static const int ESTIMATE_MIN_HITS = 6;
    static const float RMS_IMPROVE_M = 3.0;
    static const float RESOLVE_INTERVAL_S = 0.25;
    static const float SMOOTH_OLD = 0.6;
    static const float SMOOTH_NEW = 0.4;

    protected static ref GBRS_RadarWlrBallisticSolver s_Instance;
    protected ref array<ref GBRS_RadarWlrSolveCache> m_Cache;

    static GBRS_RadarWlrBallisticSolver GetInstance()
    {
        if (!s_Instance)
            s_Instance = new GBRS_RadarWlrBallisticSolver();
        return s_Instance;
    }

    static void Clear()
    {
        GBRS_RadarWlrBallisticSolver inst = GetInstance();
        if (inst.m_Cache)
            inst.m_Cache.Clear();
    }

    static GBRS_RadarWlrSolution Resolve(RDF_RadarTrack track)
    {
        return GetInstance().ResolveInternal(track);
    }

    static RDF_RadarWlrFix ResolveFix(RDF_RadarTrack track)
    {
        GBRS_RadarWlrSolution sol = Resolve(track);
        if (!sol)
            return null;
        return sol.m_Fix;
    }

    static vector PredictLive(RDF_RadarTrack track, float worldTimeS)
    {
        return GetInstance().PredictLiveInternal(track, worldTimeS);
    }

    void GBRS_RadarWlrBallisticSolver()
    {
        m_Cache = new array<ref GBRS_RadarWlrSolveCache>();
    }

    protected GBRS_RadarWlrSolution ResolveInternal(RDF_RadarTrack track)
    {
        if (!track)
            return null;

        float nowS = GetWorldTimeS();
        GBRS_RadarWlrSolveCache cache = FindCache(track.m_TrackId);
        int histCount = 0;
        float lastSampleS = -1.0;
        if (track.m_Positions)
            histCount = track.m_Positions.Count();
        if (track.m_Times && track.m_Times.Count() > 0)
            lastSampleS = track.m_Times.Get(track.m_Times.Count() - 1);

        if (cache && cache.m_Solution)
        {
            float age = nowS - cache.m_LastSolveS;
            bool histSame = false;
            if (cache.m_HistCount == histCount)
            {
                float dtSample = lastSampleS - cache.m_LastSampleS;
                if (dtSample < 0.0)
                    dtSample = -dtSample;
                if (dtSample < 0.05)
                    histSame = true;
            }

            if (age < RESOLVE_INTERVAL_S && histSame)
                return cache.m_Solution;
        }

        GBRS_RadarWlrSolution sol = SolveTrack(track, cache);
        if (!cache)
            cache = EnsureCache(track.m_TrackId);
        cache.m_LastSolveS = nowS;
        cache.m_HistCount = histCount;
        cache.m_LastSampleS = lastSampleS;
        cache.m_Solution = sol;
        if (sol)
            cache.m_AirDrag = sol.m_AirDrag;
        PruneStale(nowS);
        return sol;
    }

    protected vector PredictLiveInternal(RDF_RadarTrack track, float worldTimeS)
    {
        if (!track)
            return "0 0 0";

        float k = K_PRIOR;
        GBRS_RadarWlrSolveCache cache = FindCache(track.m_TrackId);
        if (cache && cache.m_AirDrag > 0.0)
            k = cache.m_AirDrag;

        float lastTime = track.GetLastTime();
        float dt = 0.0;
        if (lastTime >= 0.0)
            dt = worldTimeS - lastTime;
        if (dt < 0.0)
            dt = 0.0;

        RDF_RadarGlobalWind wind = RDF_RadarBallistics.SampleGlobalWind();
        vector outP;
        vector outV;
        RDF_RadarBallistics.IntegrateForDurationEx(
            track.m_FilteredPosition,
            track.m_FilteredVelocity,
            dt,
            k,
            wind,
            outP,
            outV,
            RDF_RadarBallistics.GRAVITY_M_S2,
            RDF_RadarBallistics.DEFAULT_DT_S);
        return outP;
    }

    protected GBRS_RadarWlrSolution SolveTrack(
        RDF_RadarTrack track,
        GBRS_RadarWlrSolveCache cache)
    {
        GBRS_RadarWlrSolution sol = new GBRS_RadarWlrSolution();
        sol.m_AirDrag = K_PRIOR;
        sol.m_DragEstimated = false;
        sol.m_Fix = track.m_LastWlrFix;

        if (!track.m_Positions || !track.m_Times)
            return sol;

        int minHits = track.m_WlrMinHits;
        if (minHits < 3)
            minHits = 3;
        float minSpan = track.m_WlrMinSpanS;
        if (minSpan < 0.5)
            minSpan = 0.5;
        float maxRms = track.m_WlrMaxFitRmsM;
        if (maxRms < 20.0)
            maxRms = 80.0;
        int window = track.m_WlrFitWindow;
        if (window < minHits)
            window = minHits;

        RDF_RadarBallisticFitState fit = RDF_RadarBallistics.FitVacuumFromHistory(
            track.m_Positions,
            track.m_Times,
            RDF_RadarBallistics.GRAVITY_M_S2,
            minHits,
            minSpan,
            maxRms,
            window);
        if (!fit || !fit.m_Valid)
            return sol;

        sol.m_SpanS = fit.m_SpanS;
        sol.m_HitCount = fit.m_PointCount;

        int end = track.m_Positions.Count() - 1;
        if (track.m_Times.Count() - 1 < end)
            end = track.m_Times.Count() - 1;
        int start = end - window + 1;
        if (start < 0)
            start = 0;
        if (fit.m_PointCount > 0)
        {
            start = end - fit.m_PointCount + 1;
            if (start < 0)
                start = 0;
        }

        RDF_RadarGlobalWind wind = RDF_RadarBallistics.SampleGlobalWind();
        float k = K_PRIOR;
        bool estimated = false;

        bool enoughArc = false;
        if (fit.m_SpanS >= ESTIMATE_MIN_SPAN_S)
        {
            if (fit.m_PointCount >= ESTIMATE_MIN_HITS)
                enoughArc = true;
        }

        if (enoughArc)
        {
            float rmsPrior = ResidualRms(fit, track, start, end, K_PRIOR, wind);
            float bestK = K_PRIOR;
            float bestRms = rmsPrior;

            int i = 0;
            while (i < K_GRID)
            {
                float t = i;
                float denom = K_GRID - 1;
                if (denom < 1.0)
                    denom = 1.0;
                t = t / denom;
                float cand = K_MIN * Math.Pow(K_MAX / K_MIN, t);
                float rms = ResidualRms(fit, track, start, end, cand, wind);
                if (rms < bestRms)
                {
                    bestRms = rms;
                    bestK = cand;
                }
                i = i + 1;
            }

            if (bestRms < rmsPrior - RMS_IMPROVE_M)
            {
                k = bestK;
                estimated = true;
            }
        }

        if (cache && cache.m_AirDrag > 0.0)
            k = cache.m_AirDrag * SMOOTH_OLD + k * SMOOTH_NEW;

        float groundYM = track.m_GroundYM;
        if (!track.m_GroundYValid)
        {
            groundYM = RDF_RadarBallistics.SampleGroundYM(
                fit.m_Position[0], fit.m_Position[2], fit.m_Position[1]);
        }

        RDF_RadarWlrFix fresh = RDF_RadarBallistics.SolveLaunchAndImpact(
            fit.m_Position,
            fit.m_Velocity,
            groundYM,
            fit.m_AnchorTimeS,
            k,
            wind,
            RDF_RadarBallistics.GRAVITY_M_S2);
        if (fresh)
        {
            fresh.m_FitValid = true;
            fresh.m_FitRmsM = fit.m_RmsM;
            fresh.m_FitPointCount = fit.m_PointCount;
            fresh.m_FitSpanS = fit.m_SpanS;
        }

        bool usable = false;
        if (fresh)
        {
            if (fresh.m_LaunchValid || fresh.m_ImpactValid)
                usable = true;
        }

        if (usable)
            sol.m_Fix = fresh;
        else
            estimated = false;

        sol.m_AirDrag = k;
        sol.m_DragEstimated = estimated;
        return sol;
    }

    protected float ResidualRms(
        RDF_RadarBallisticFitState fit,
        RDF_RadarTrack track,
        int start,
        int end,
        float airDrag,
        RDF_RadarGlobalWind wind)
    {
        if (!fit || !track)
            return 1.0e9;
        if (!track.m_Positions || !track.m_Times)
            return 1.0e9;
        if (end < start)
            return 1.0e9;

        vector p = fit.m_Position;
        vector v = fit.m_Velocity;
        float t = fit.m_AnchorTimeS;
        float sumSq = 0.0;
        int n = 0;

        int i = end;
        while (i >= start)
        {
            float ti = track.m_Times.Get(i);
            float dt = ti - t;
            float adt = dt;
            if (adt < 0.0)
                adt = -adt;
            if (adt > 0.0001)
            {
                vector np;
                vector nv;
                RDF_RadarBallistics.IntegrateForDurationEx(
                    p,
                    v,
                    dt,
                    airDrag,
                    wind,
                    np,
                    nv,
                    RDF_RadarBallistics.GRAVITY_M_S2,
                    RDF_RadarBallistics.DEFAULT_DT_S);
                p = np;
                v = nv;
                t = ti;
            }

            vector obs = track.m_Positions.Get(i);
            float dx = p[0] - obs[0];
            float dy = p[1] - obs[1];
            float dz = p[2] - obs[2];
            sumSq = sumSq + dx * dx + dy * dy + dz * dz;
            n = n + 1;
            i = i - 1;
        }

        if (n < 1)
            return 1.0e9;
        return Math.Sqrt(sumSq / n);
    }

    protected GBRS_RadarWlrSolveCache FindCache(int trackId)
    {
        if (!m_Cache)
            return null;
        int i = 0;
        while (i < m_Cache.Count())
        {
            GBRS_RadarWlrSolveCache c = m_Cache.Get(i);
            if (c && c.m_TrackId == trackId)
                return c;
            i = i + 1;
        }
        return null;
    }

    protected GBRS_RadarWlrSolveCache EnsureCache(int trackId)
    {
        GBRS_RadarWlrSolveCache existing = FindCache(trackId);
        if (existing)
            return existing;

        GBRS_RadarWlrSolveCache created = new GBRS_RadarWlrSolveCache();
        created.m_TrackId = trackId;
        created.m_AirDrag = 0.0;
        created.m_LastSolveS = -1000.0;
        m_Cache.Insert(created);
        return created;
    }

    protected void PruneStale(float nowS)
    {
        if (!m_Cache)
            return;
        int i = m_Cache.Count() - 1;
        while (i >= 0)
        {
            GBRS_RadarWlrSolveCache c = m_Cache.Get(i);
            bool drop = false;
            if (!c)
                drop = true;
            else if (nowS - c.m_LastSolveS > 30.0)
                drop = true;
            if (drop)
                m_Cache.Remove(i);
            i = i - 1;
        }
    }

    protected float GetWorldTimeS()
    {
        ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
        if (!world)
            return 0.0;
        return world.GetWorldTime() * 0.001;
    }
}
