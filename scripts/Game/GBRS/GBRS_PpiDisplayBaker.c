//------------------------------------------------------------------------------------------------
//! Authority-only PPI display bake: afterglow, clustering, WLR persist/arc.
//! Clients draw the packed result and do not re-run these steps.
class GBRS_PpiPersistRow
{
    ref RDF_RadarTarget m_Target;
    float m_LastFreshS;
}

class GBRS_PpiDisplayBaker
{
    protected static const int PERSIST_MAX_BLIPS = 512;
    protected static const int DISPLAY_MAX_BLIPS = 48;
    protected static const int MAX_TRACKS = 16;
    protected static const int MAX_WLR_PERSIST = 16;
    protected static const float PLOT_AFTERGLOW_S = 0.45;
    protected static const float WLR_PLOT_LIFE_S = 5.0;
    protected static const float DISPLAY_CLUSTER_M = 120.0;
    protected static const float DISPLAY_CLUSTER_AZ_DEG = 4.0;
    protected static const float DISPLAY_CLUSTER_RANGE_M = 400.0;
    protected static const float PERSIST_SPATIAL_M = 80.0;
    protected static const float TRACK_CLUSTER_RANGE_M = 700.0;
    protected static const float TRACK_CLUSTER_AZ_DEG = 8.0;
    protected static const float WLR_PERSIST_S = 20.0;
    protected static const float WLR_IMPACT_PERSIST_S = 3.0;

    protected ref array<ref GBRS_PpiPersistRow> m_PersistPlots;
    protected ref array<ref RDF_RadarTarget> m_DisplayPlots;
    protected ref array<ref RDF_RadarTrack> m_DisplayTracks;
    protected ref array<ref GBRS_WlrPersistDisplay> m_WlrPersist;
    protected int m_DetectedTotal;

    //------------------------------------------------------------------------------------------------
    void Clear()
    {
        if (m_PersistPlots)
            m_PersistPlots.Clear();
        if (m_DisplayPlots)
            m_DisplayPlots.Clear();
        if (m_DisplayTracks)
            m_DisplayTracks.Clear();
        if (m_WlrPersist)
            m_WlrPersist.Clear();
        m_DetectedTotal = 0;
    }

    //------------------------------------------------------------------------------------------------
    array<ref RDF_RadarTarget> GetDisplayPlots()
    {
        return m_DisplayPlots;
    }

    //------------------------------------------------------------------------------------------------
    array<ref RDF_RadarTrack> GetDisplayTracks()
    {
        return m_DisplayTracks;
    }

    //------------------------------------------------------------------------------------------------
    array<ref GBRS_WlrPersistDisplay> GetWlrPersist()
    {
        return m_WlrPersist;
    }

    //------------------------------------------------------------------------------------------------
    int GetDetectedTotal()
    {
        return m_DetectedTotal;
    }

    //------------------------------------------------------------------------------------------------
    void Tick(
        RDF_RadarSensor sensor,
        RDF_RadarSettings settings,
        vector origin,
        float rangeM,
        string workstationMode,
        float nowS,
        float worldNowS)
    {
        EnsureBuffers();

        array<ref RDF_RadarTarget> live = null;
        RDF_RadarProjectileTracker tracker = null;
        if (sensor)
        {
            live = sensor.GetPlots();
            tracker = sensor.GetTracker();
        }

        IngestLivePlots(live, settings, nowS);
        // Afterglow stays at last detection. Coasting persist plots made PPI
        // blips slide after the beam left, which looked like the sweep flung them.

        float lifeS = PLOT_AFTERGLOW_S;
        if (workstationMode == GBRS_RadarStationConstants.MODE_WLR)
            lifeS = WLR_PLOT_LIFE_S;
        PrunePersist(nowS, lifeS);

        BuildClusteredDisplayPlots(origin, rangeM);
        BuildClusteredDisplayTracks(tracker, origin, rangeM);

        if (workstationMode == GBRS_RadarStationConstants.MODE_WLR)
            UpdateWlrPersist(worldNowS);
    }

    //------------------------------------------------------------------------------------------------
    protected void EnsureBuffers()
    {
        if (!m_PersistPlots)
            m_PersistPlots = new array<ref GBRS_PpiPersistRow>();
        if (!m_DisplayPlots)
            m_DisplayPlots = new array<ref RDF_RadarTarget>();
        if (!m_DisplayTracks)
            m_DisplayTracks = new array<ref RDF_RadarTrack>();
        if (!m_WlrPersist)
            m_WlrPersist = new array<ref GBRS_WlrPersistDisplay>();
    }

    //------------------------------------------------------------------------------------------------
    protected void IngestLivePlots(
        array<ref RDF_RadarTarget> live,
        RDF_RadarSettings settings,
        float nowS)
    {
        if (!live)
            return;

        int i = 0;
        while (i < live.Count())
        {
            RDF_RadarTarget src = live.Get(i);
            i = i + 1;
            if (!GBRS_RadarStationConfig.ShouldDisplayPlot(src, settings))
                continue;

            GBRS_PpiPersistRow held = FindPersistMatch(src);
            if (held && held.m_Target)
            {
                CopyPlot(src, held.m_Target, nowS);
                held.m_LastFreshS = nowS;
                continue;
            }

            if (m_PersistPlots.Count() >= PERSIST_MAX_BLIPS)
                RemoveOldestPersist();

            RDF_RadarTarget created = new RDF_RadarTarget();
            CopyPlot(src, created, nowS);
            GBRS_PpiPersistRow row = new GBRS_PpiPersistRow();
            row.m_Target = created;
            row.m_LastFreshS = nowS;
            m_PersistPlots.Insert(row);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected GBRS_PpiPersistRow FindPersistMatch(RDF_RadarTarget src)
    {
        if (!src || !m_PersistPlots)
            return null;

        int i = 0;
        if (src.m_ScattererId > 0)
        {
            while (i < m_PersistPlots.Count())
            {
                GBRS_PpiPersistRow row = m_PersistPlots.Get(i);
                i = i + 1;
                if (!row || !row.m_Target)
                    continue;
                if (row.m_Target.m_ScattererId == src.m_ScattererId)
                    return row;
            }
            return null;
        }

        i = 0;
        while (i < m_PersistPlots.Count())
        {
            GBRS_PpiPersistRow row = m_PersistPlots.Get(i);
            i = i + 1;
            if (!row || !row.m_Target)
                continue;
            if (row.m_Target.m_ScattererId > 0)
                continue;

            vector d = row.m_Target.m_Position - src.m_Position;
            if (d.LengthSq() < (PERSIST_SPATIAL_M * PERSIST_SPATIAL_M))
                return row;
        }

        return null;
    }

    //------------------------------------------------------------------------------------------------
    protected void RemoveOldestPersist()
    {
        if (!m_PersistPlots || m_PersistPlots.Count() == 0)
            return;

        int oldest = 0;
        float oldestTime = 1.0e30;
        int i = 0;
        while (i < m_PersistPlots.Count())
        {
            GBRS_PpiPersistRow row = m_PersistPlots.Get(i);
            if (row && row.m_LastFreshS < oldestTime)
            {
                oldestTime = row.m_LastFreshS;
                oldest = i;
            }
            i = i + 1;
        }

        m_PersistPlots.Remove(oldest);
    }

    //------------------------------------------------------------------------------------------------
    protected void CopyPlot(RDF_RadarTarget src, RDF_RadarTarget dst, float nowS)
    {
        if (!src || !dst)
            return;

        dst.m_ScattererId = src.m_ScattererId;
        dst.m_Position = src.m_Position;
        dst.m_Distance = src.m_Distance;
        dst.m_Velocity = src.m_Velocity;
        dst.m_Type = src.m_Type;
        dst.m_Time = nowS;
        dst.m_AzimuthDeg = src.m_AzimuthDeg;
        dst.m_RadialSpeedMs = src.m_RadialSpeedMs;
        dst.m_SnrDb = src.m_SnrDb;
        dst.m_Detected = true;
        dst.m_IsAnonymous = src.m_IsAnonymous;
        dst.m_IsFalsePlot = src.m_IsFalsePlot;
        dst.m_LosBlocked = src.m_LosBlocked;
        dst.m_RotorSidebandUsed = src.m_RotorSidebandUsed;
    }

    //------------------------------------------------------------------------------------------------
    protected void PrunePersist(float nowS, float lifeS)
    {
        if (!m_PersistPlots)
            return;

        int i = m_PersistPlots.Count() - 1;
        while (i >= 0)
        {
            GBRS_PpiPersistRow row = m_PersistPlots.Get(i);
            if (!row || !row.m_Target || (nowS - row.m_LastFreshS) > lifeS)
                m_PersistPlots.Remove(i);
            i = i - 1;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected float PlotRangeM(RDF_RadarTarget t, vector origin)
    {
        if (!t)
            return 0.0;
        if (t.m_Distance > 0.0)
            return t.m_Distance;
        vector d = t.m_Position - origin;
        return d.Length();
    }

    protected bool PlotsSharePolarCell(
        RDF_RadarTarget a,
        RDF_RadarTarget b,
        vector origin,
        float rngB)
    {
        if (!a || !b)
            return false;

        vector da = a.m_Position - origin;
        vector db = b.m_Position - origin;
        float azA = Math.Atan2(da[0], da[2]) * Math.RAD2DEG;
        float azB = Math.Atan2(db[0], db[2]) * Math.RAD2DEG;
        float dAz = azA - azB;
        while (dAz > 180.0)
            dAz = dAz - 360.0;
        while (dAz < -180.0)
            dAz = dAz + 360.0;
        if (dAz < 0.0)
            dAz = -dAz;
        if (dAz > DISPLAY_CLUSTER_AZ_DEG)
            return false;

        float rngA = PlotRangeM(a, origin);
        float dRng = rngA - rngB;
        if (dRng < 0.0)
            dRng = -dRng;
        if (dRng > DISPLAY_CLUSTER_RANGE_M)
            return false;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected IEntity ResolveClusterRoot(RDF_RadarTarget src, map<int, IEntity> rootCache)
    {
        if (!src || src.m_ScattererId <= 0)
            return null;
        if (src.m_IsAnonymous)
            return null;

        if (rootCache && rootCache.Contains(src.m_ScattererId))
            return rootCache.Get(src.m_ScattererId);

        IEntity root = null;
        RDF_RadarScatterer entry = RDF_RadarScattererRegistry.FindById(src.m_ScattererId);
        if (entry && entry.m_Entity)
        {
            root = entry.m_Entity.GetRootParent();
            if (!root)
                root = entry.m_Entity;
        }

        if (rootCache)
            rootCache.Set(src.m_ScattererId, root);

        return root;
    }

    //------------------------------------------------------------------------------------------------
    protected void BuildClusteredDisplayPlots(vector origin, float rangeM)
    {
        m_DisplayPlots.Clear();
        m_DetectedTotal = 0;
        if (!m_PersistPlots || m_PersistPlots.Count() == 0)
            return;

        float rangeLimit = rangeM;
        if (rangeLimit <= 0.0)
            rangeLimit = 1.0e9;

        float gateSq = DISPLAY_CLUSTER_M * DISPLAY_CLUSTER_M;
        map<int, IEntity> rootCache = new map<int, IEntity>();
        array<IEntity> displayRoots = new array<IEntity>();

        int i = 0;
        while (i < m_PersistPlots.Count())
        {
            GBRS_PpiPersistRow row = m_PersistPlots.Get(i);
            i = i + 1;
            if (!row || !row.m_Target)
                continue;

            RDF_RadarTarget src = row.m_Target;
            float rng = PlotRangeM(src, origin);
            if (rng > rangeLimit)
                continue;

            IEntity srcRoot = ResolveClusterRoot(src, rootCache);
            int match = -1;
            int j = 0;
            while (j < m_DisplayPlots.Count())
            {
                RDF_RadarTarget kept = m_DisplayPlots.Get(j);
                if (kept)
                {
                    if (src.m_ScattererId > 0 && kept.m_ScattererId == src.m_ScattererId)
                    {
                        match = j;
                        break;
                    }

                    IEntity keptRoot = null;
                    if (j < displayRoots.Count())
                        keptRoot = displayRoots.Get(j);

                    if (srcRoot && keptRoot && srcRoot == keptRoot)
                    {
                        match = j;
                        break;
                    }

                    // One airframe yields several scatterers (fuselage + rotors).
                    // Merge by range even when entity-truth clustering is off.
                    vector d = kept.m_Position - src.m_Position;
                    if (d.LengthSq() <= gateSq)
                    {
                        match = j;
                        break;
                    }

                    if (PlotsSharePolarCell(kept, src, origin, rng))
                    {
                        match = j;
                        break;
                    }
                }
                j = j + 1;
            }

            if (match < 0)
            {
                RDF_RadarTarget created = new RDF_RadarTarget();
                CopyPlot(src, created, src.m_Time);
                if (rng > 0.0)
                    created.m_Distance = rng;
                m_DisplayPlots.Insert(created);
                displayRoots.Insert(srcRoot);
                continue;
            }

            RDF_RadarTarget winner = m_DisplayPlots.Get(match);
            if (!winner)
                continue;
            if (src.m_SnrDb > winner.m_SnrDb)
            {
                CopyPlot(src, winner, src.m_Time);
                if (rng > 0.0)
                    winner.m_Distance = rng;
            }
        }

        m_DetectedTotal = m_DisplayPlots.Count();
        while (m_DisplayPlots.Count() > DISPLAY_MAX_BLIPS)
        {
            int worst = 0;
            float worstSnr = 1.0e30;
            int k = 0;
            while (k < m_DisplayPlots.Count())
            {
                RDF_RadarTarget t = m_DisplayPlots.Get(k);
                if (t && t.m_SnrDb < worstSnr)
                {
                    worstSnr = t.m_SnrDb;
                    worst = k;
                }
                k = k + 1;
            }
            m_DisplayPlots.Remove(worst);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void BuildClusteredDisplayTracks(
        RDF_RadarProjectileTracker tracker,
        vector origin,
        float rangeM)
    {
        m_DisplayTracks.Clear();
        if (!tracker)
            return;

        array<ref RDF_RadarTrack> all = tracker.GetAllTracks();
        if (!all)
            return;

        float gateSq = TRACK_CLUSTER_RANGE_M * TRACK_CLUSTER_RANGE_M;
        int i = 0;
        while (i < all.Count())
        {
            RDF_RadarTrack tr = all.Get(i);
            i = i + 1;
            if (!tr)
                continue;
            if (!IsTrackInRange(tr, origin, rangeM))
                continue;

            int match = -1;
            int j = 0;
            while (j < m_DisplayTracks.Count())
            {
                RDF_RadarTrack kept = m_DisplayTracks.Get(j);
                if (kept && TracksAreSameContact(tr, kept, gateSq))
                {
                    match = j;
                    break;
                }
                j = j + 1;
            }

            if (match < 0)
            {
                if (m_DisplayTracks.Count() < MAX_TRACKS)
                    m_DisplayTracks.Insert(tr);
                continue;
            }

            RDF_RadarTrack winner = m_DisplayTracks.Get(match);
            if (TrackIsBetter(tr, winner))
                m_DisplayTracks.Set(match, tr);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected bool IsTrackInRange(RDF_RadarTrack tr, vector origin, float rangeM)
    {
        if (!tr)
            return false;

        float rng = tr.m_FilteredRangeM;
        if (rng <= 0.0)
        {
            vector d = tr.m_FilteredPosition - origin;
            rng = d.Length();
        }
        if (rangeM <= 0.0)
            return true;
        if (rng > rangeM)
            return false;
        return true;
    }

    //------------------------------------------------------------------------------------------------
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

    //------------------------------------------------------------------------------------------------
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

    //------------------------------------------------------------------------------------------------
    protected void UpdateWlrPersist(float worldNowS)
    {
        if (!m_WlrPersist)
            m_WlrPersist = new array<ref GBRS_WlrPersistDisplay>();

        int i = 0;
        while (i < m_DisplayTracks.Count())
        {
            RDF_RadarTrack tr = m_DisplayTracks.Get(i);
            i = i + 1;
            if (!tr)
                continue;

            GBRS_RadarWlrSolution sol = GBRS_RadarWlrBallisticSolver.Resolve(tr);
            RDF_RadarWlrFix fix = null;
            if (sol)
                fix = sol.m_Fix;
            if (!fix)
                fix = tr.m_LastWlrFix;
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

            entry.m_Id = "W" + tr.m_TrackId.ToString();
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
            if (sol)
            {
                entry.m_AirDrag = sol.m_AirDrag;
                entry.m_DragEstimated = sol.m_DragEstimated;
            }
            entry.m_LivePos = WlrPositionOnArc(entry, worldNowS);
            entry.m_LiveVel = WlrArcDirection(entry, tr);
            entry.m_HasLive = true;
        }

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

    //------------------------------------------------------------------------------------------------
    protected GBRS_WlrPersistDisplay FindWlrPersist(int trackId)
    {
        if (!m_WlrPersist)
            return null;

        int i = 0;
        while (i < m_WlrPersist.Count())
        {
            GBRS_WlrPersistDisplay entry = m_WlrPersist.Get(i);
            if (entry && entry.m_TrackId == trackId)
                return entry;
            i = i + 1;
        }
        return null;
    }

    //------------------------------------------------------------------------------------------------
    protected vector WlrPositionOnArc(GBRS_WlrPersistDisplay entry, float worldNowS)
    {
        if (!entry)
            return "0 0 0";
        if (!entry.m_HasLaunch || !entry.m_HasImpact)
            return entry.m_LivePos;

        float tRange = entry.m_ImpactTimeS - entry.m_LaunchTimeS;
        if (tRange <= 0.001)
            return entry.m_LivePos;

        float u = (worldNowS - entry.m_LaunchTimeS) / tRange;
        if (u < 0.0)
            u = 0.0;
        if (u > 1.0)
            u = 1.0;

        vector lp = entry.m_LaunchPos;
        vector ip = entry.m_ImpactPos;
        float b = 1.0 - u;
        vector pos;
        pos[0] = lp[0] * b + ip[0] * u;
        pos[2] = lp[2] * b + ip[2] * u;
        pos[1] = lp[1] * (1.0 - u * u) + ip[1] * (u * u);
        return pos;
    }

    //------------------------------------------------------------------------------------------------
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
}
