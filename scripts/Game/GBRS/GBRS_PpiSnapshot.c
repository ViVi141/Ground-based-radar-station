//------------------------------------------------------------------------------------------------
//! PPI plot / track / fused-net wire format.
//! Authority packs live RDF output; clients reconstruct display DTOs.
//! RPC cannot carry RDF_RadarTarget / RDF_RadarTrack, so the stream is
//! parallel int + float arrays.
class GBRS_PpiSnapshot
{
    static const int MAX_PLOTS = 48;
    static const int MAX_TRACKS = 16;
    static const int MAX_FUSED = 16;
    static const int MAX_WLR = 16;

    protected static const int HEADER_INTS = 6;
    protected static const int PLOT_INTS = 2;
    protected static const int PLOT_FLOATS = 10;
    protected static const int TRACK_INTS = 3;
    protected static const int TRACK_FLOATS = 18;
    protected static const int FUSED_INTS = 3;
    protected static const int FUSED_FLOATS = 12;
    protected static const int WLR_INTS = 2;
    protected static const int WLR_FLOATS = 15;

    protected static const int TYPE_MASK = 255;
    protected static const int FLAG_DETECTED = 256;
    protected static const int FLAG_ANONYMOUS = 512;
    protected static const int FLAG_FALSE_PLOT = 1024;
    protected static const int FLAG_LOS_BLOCKED = 2048;
    protected static const int FLAG_ROTOR_SIDEBAND = 4096;
    protected static const int FLAG_CONFIRMED = 256;
    protected static const int FLAG_COASTING = 512;
    protected static const int FLAG_WLR_LAUNCH = 1024;
    protected static const int FLAG_WLR_IMPACT = 2048;
    protected static const int IFF_SHIFT = 8;
    protected static const int IFF_MASK = 255;
    protected static const int FUSED_CONTRIB_SHIFT = 16;
    protected static const int FUSED_FLAG_LAUNCH = 16777216;
    protected static const int FUSED_FLAG_IMPACT = 33554432;
    protected static const int WLR_FLAG_LAUNCH = 1;
    protected static const int WLR_FLAG_IMPACT = 2;
    protected static const int WLR_FLAG_LIVE = 4;
    protected static const int WLR_FLAG_DRAG_EST = 8;

    //------------------------------------------------------------------------------------------------
    static void Pack(
        array<ref RDF_RadarTarget> plotsSrc,
        array<ref RDF_RadarTrack> tracksSrc,
        array<ref RDF_RadarFusedTrack> fusedSrc,
        array<ref GBRS_WlrPersistDisplay> wlrSrc,
        int detectedTotal,
        int netOnline,
        notnull array<int> ints,
        notnull array<float> floats)
    {
        ints.Clear();
        floats.Clear();

        int plotCount = 0;
        if (plotsSrc)
            plotCount = plotsSrc.Count();
        if (plotCount > MAX_PLOTS)
            plotCount = MAX_PLOTS;

        int trackCount = 0;
        if (tracksSrc)
            trackCount = tracksSrc.Count();
        if (trackCount > MAX_TRACKS)
            trackCount = MAX_TRACKS;

        int fusedCount = 0;
        if (fusedSrc)
            fusedCount = fusedSrc.Count();
        if (fusedCount > MAX_FUSED)
            fusedCount = MAX_FUSED;

        int wlrCount = 0;
        if (wlrSrc)
            wlrCount = wlrSrc.Count();
        if (wlrCount > MAX_WLR)
            wlrCount = MAX_WLR;

        ints.Insert(plotCount);
        ints.Insert(trackCount);
        ints.Insert(fusedCount);
        ints.Insert(detectedTotal);
        ints.Insert(netOnline);
        ints.Insert(wlrCount);

        int i = 0;
        while (i < plotCount)
        {
            AppendPlot(plotsSrc.Get(i), ints, floats);
            i = i + 1;
        }

        i = 0;
        while (i < trackCount)
        {
            AppendTrack(tracksSrc.Get(i), ints, floats);
            i = i + 1;
        }

        i = 0;
        while (i < fusedCount)
        {
            AppendFused(fusedSrc.Get(i), ints, floats);
            i = i + 1;
        }

        i = 0;
        while (i < wlrCount)
        {
            AppendWlr(wlrSrc.Get(i), ints, floats);
            i = i + 1;
        }
    }

    //------------------------------------------------------------------------------------------------
    static bool Unpack(
        array<int> ints,
        array<float> floats,
        out array<ref RDF_RadarTarget> plots,
        out array<ref RDF_RadarTrack> tracks,
        out array<ref RDF_RadarFusedTrack> fused,
        out array<ref GBRS_WlrPersistDisplay> wlr,
        out int detectedTotal,
        out int netOnline)
    {
        plots = new array<ref RDF_RadarTarget>();
        tracks = new array<ref RDF_RadarTrack>();
        fused = new array<ref RDF_RadarFusedTrack>();
        wlr = new array<ref GBRS_WlrPersistDisplay>();
        detectedTotal = 0;
        netOnline = 0;

        if (!ints || !floats)
            return false;
        if (ints.Count() < HEADER_INTS)
            return false;

        int plotCount = ints.Get(0);
        int trackCount = ints.Get(1);
        int fusedCount = ints.Get(2);
        detectedTotal = ints.Get(3);
        netOnline = ints.Get(4);
        int wlrCount = ints.Get(5);

        if (plotCount < 0)
            plotCount = 0;
        if (trackCount < 0)
            trackCount = 0;
        if (fusedCount < 0)
            fusedCount = 0;
        if (wlrCount < 0)
            wlrCount = 0;
        if (plotCount > MAX_PLOTS)
            plotCount = MAX_PLOTS;
        if (trackCount > MAX_TRACKS)
            trackCount = MAX_TRACKS;
        if (fusedCount > MAX_FUSED)
            fusedCount = MAX_FUSED;
        if (wlrCount > MAX_WLR)
            wlrCount = MAX_WLR;

        int needInts = HEADER_INTS
            + plotCount * PLOT_INTS
            + trackCount * TRACK_INTS
            + fusedCount * FUSED_INTS
            + wlrCount * WLR_INTS;
        int needFloats = plotCount * PLOT_FLOATS
            + trackCount * TRACK_FLOATS
            + fusedCount * FUSED_FLOATS
            + wlrCount * WLR_FLOATS;
        if (ints.Count() < needInts)
            return false;
        if (floats.Count() < needFloats)
            return false;

        int intCursor = HEADER_INTS;
        int floatCursor = 0;

        int i = 0;
        while (i < plotCount)
        {
            RDF_RadarTarget plot = ReadPlot(ints, floats, intCursor, floatCursor);
            intCursor = intCursor + PLOT_INTS;
            floatCursor = floatCursor + PLOT_FLOATS;
            if (plot)
                plots.Insert(plot);
            i = i + 1;
        }

        i = 0;
        while (i < trackCount)
        {
            RDF_RadarTrack track = ReadTrack(ints, floats, intCursor, floatCursor);
            intCursor = intCursor + TRACK_INTS;
            floatCursor = floatCursor + TRACK_FLOATS;
            if (track)
                tracks.Insert(track);
            i = i + 1;
        }

        i = 0;
        while (i < fusedCount)
        {
            RDF_RadarFusedTrack row = ReadFused(ints, floats, intCursor, floatCursor);
            intCursor = intCursor + FUSED_INTS;
            floatCursor = floatCursor + FUSED_FLOATS;
            if (row)
                fused.Insert(row);
            i = i + 1;
        }

        i = 0;
        while (i < wlrCount)
        {
            GBRS_WlrPersistDisplay row = ReadWlr(ints, floats, intCursor, floatCursor);
            intCursor = intCursor + WLR_INTS;
            floatCursor = floatCursor + WLR_FLOATS;
            if (row)
                wlr.Insert(row);
            i = i + 1;
        }

        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected static void AppendPlot(
        RDF_RadarTarget t,
        notnull array<int> ints,
        notnull array<float> floats)
    {
        if (!t)
        {
            ints.Insert(0);
            ints.Insert(0);
            int z = 0;
            while (z < PLOT_FLOATS)
            {
                floats.Insert(0.0);
                z = z + 1;
            }
            return;
        }

        int packed = t.m_Type;
        if (t.m_Detected)
            packed = packed | FLAG_DETECTED;
        if (t.m_IsAnonymous)
            packed = packed | FLAG_ANONYMOUS;
        if (t.m_IsFalsePlot)
            packed = packed | FLAG_FALSE_PLOT;
        if (t.m_LosBlocked)
            packed = packed | FLAG_LOS_BLOCKED;
        if (t.m_RotorSidebandUsed)
            packed = packed | FLAG_ROTOR_SIDEBAND;

        ints.Insert(t.m_ScattererId);
        ints.Insert(packed);
        floats.Insert(t.m_Position[0]);
        floats.Insert(t.m_Position[1]);
        floats.Insert(t.m_Position[2]);
        floats.Insert(t.m_Distance);
        floats.Insert(t.m_Velocity[0]);
        floats.Insert(t.m_Velocity[1]);
        floats.Insert(t.m_Velocity[2]);
        floats.Insert(t.m_AzimuthDeg);
        floats.Insert(t.m_SnrDb);
        floats.Insert(t.m_RadialSpeedMs);
    }

    //------------------------------------------------------------------------------------------------
    protected static void AppendTrack(
        RDF_RadarTrack tr,
        notnull array<int> ints,
        notnull array<float> floats)
    {
        if (!tr)
        {
            ints.Insert(0);
            ints.Insert(0);
            ints.Insert(0);
            int z = 0;
            while (z < TRACK_FLOATS)
            {
                floats.Insert(0.0);
                z = z + 1;
            }
            return;
        }

        int packed = tr.m_Type;
        if (tr.m_Confirmed)
            packed = packed | FLAG_CONFIRMED;
        if (tr.m_Coasting)
            packed = packed | FLAG_COASTING;

        vector launchPos = "0 0 0";
        float launchTime = 0.0;
        vector impactPos = "0 0 0";
        float impactTime = 0.0;
        RDF_RadarWlrFix fix = tr.m_LastWlrFix;
        if (fix)
        {
            if (fix.m_LaunchValid)
            {
                packed = packed | FLAG_WLR_LAUNCH;
                launchPos = fix.m_LaunchPos;
                launchTime = fix.m_LaunchTimeS;
            }
            if (fix.m_ImpactValid)
            {
                packed = packed | FLAG_WLR_IMPACT;
                impactPos = fix.m_ImpactPos;
                impactTime = fix.m_ImpactTimeS;
            }
        }

        vector vel = GBRS_RadarStationComponent.ReliableTrackVelocity(tr);
        ints.Insert(tr.m_TrackId);
        ints.Insert(tr.m_ScattererId);
        ints.Insert(packed);
        floats.Insert(tr.m_FilteredPosition[0]);
        floats.Insert(tr.m_FilteredPosition[1]);
        floats.Insert(tr.m_FilteredPosition[2]);
        floats.Insert(vel[0]);
        floats.Insert(vel[1]);
        floats.Insert(vel[2]);
        floats.Insert(tr.m_FilteredAzimuthDeg);
        floats.Insert(tr.m_FilteredRangeM);
        floats.Insert(tr.m_FilteredRangeRateMs);
        floats.Insert(tr.m_LastSnrDb);
        floats.Insert(launchPos[0]);
        floats.Insert(launchPos[1]);
        floats.Insert(launchPos[2]);
        floats.Insert(launchTime);
        floats.Insert(impactPos[0]);
        floats.Insert(impactPos[1]);
        floats.Insert(impactPos[2]);
        floats.Insert(impactTime);
    }

    //------------------------------------------------------------------------------------------------
    protected static void AppendFused(
        RDF_RadarFusedTrack f,
        notnull array<int> ints,
        notnull array<float> floats)
    {
        if (!f)
        {
            ints.Insert(0);
            ints.Insert(0);
            ints.Insert(0);
            int z = 0;
            while (z < FUSED_FLOATS)
            {
                floats.Insert(0.0);
                z = z + 1;
            }
            return;
        }

        int packed = f.m_Type;
        int iffBits = f.m_Iff;
        packed = packed | ((iffBits & IFF_MASK) << IFF_SHIFT);
        packed = packed | ((f.m_ContributorCount & TYPE_MASK) << FUSED_CONTRIB_SHIFT);
        if (f.m_WlrLaunchValid)
            packed = packed | FUSED_FLAG_LAUNCH;
        if (f.m_WlrImpactValid)
            packed = packed | FUSED_FLAG_IMPACT;

        ints.Insert(f.m_FusedId);
        ints.Insert(f.m_ContributorRadarId0);
        ints.Insert(packed);
        floats.Insert(f.m_WorldPos[0]);
        floats.Insert(f.m_WorldPos[1]);
        floats.Insert(f.m_WorldPos[2]);
        floats.Insert(f.m_Velocity[0]);
        floats.Insert(f.m_Velocity[1]);
        floats.Insert(f.m_Velocity[2]);
        floats.Insert(f.m_WlrLaunchPos[0]);
        floats.Insert(f.m_WlrLaunchPos[1]);
        floats.Insert(f.m_WlrLaunchPos[2]);
        floats.Insert(f.m_WlrImpactPos[0]);
        floats.Insert(f.m_WlrImpactPos[1]);
        floats.Insert(f.m_WlrImpactPos[2]);
    }

    //------------------------------------------------------------------------------------------------
    protected static RDF_RadarTarget ReadPlot(
        notnull array<int> ints,
        notnull array<float> floats,
        int intCursor,
        int floatCursor)
    {
        RDF_RadarTarget t = new RDF_RadarTarget();
        t.m_ScattererId = ints.Get(intCursor);
        int packed = ints.Get(intCursor + 1);
        t.m_Type = packed & TYPE_MASK;
        t.m_Detected = (packed & FLAG_DETECTED) != 0;
        t.m_IsAnonymous = (packed & FLAG_ANONYMOUS) != 0;
        t.m_IsFalsePlot = (packed & FLAG_FALSE_PLOT) != 0;
        t.m_LosBlocked = (packed & FLAG_LOS_BLOCKED) != 0;
        t.m_RotorSidebandUsed = (packed & FLAG_ROTOR_SIDEBAND) != 0;

        vector pos;
        pos[0] = floats.Get(floatCursor);
        pos[1] = floats.Get(floatCursor + 1);
        pos[2] = floats.Get(floatCursor + 2);
        t.m_Position = pos;
        t.m_Distance = floats.Get(floatCursor + 3);
        vector vel;
        vel[0] = floats.Get(floatCursor + 4);
        vel[1] = floats.Get(floatCursor + 5);
        vel[2] = floats.Get(floatCursor + 6);
        t.m_Velocity = vel;
        t.m_AzimuthDeg = floats.Get(floatCursor + 7);
        t.m_SnrDb = floats.Get(floatCursor + 8);
        t.m_RadialSpeedMs = floats.Get(floatCursor + 9);
        return t;
    }

    //------------------------------------------------------------------------------------------------
    protected static RDF_RadarTrack ReadTrack(
        notnull array<int> ints,
        notnull array<float> floats,
        int intCursor,
        int floatCursor)
    {
        RDF_RadarTrack tr = new RDF_RadarTrack();
        tr.m_TrackId = ints.Get(intCursor);
        tr.m_ScattererId = ints.Get(intCursor + 1);
        int packed = ints.Get(intCursor + 2);
        tr.m_Type = packed & TYPE_MASK;
        tr.m_Confirmed = (packed & FLAG_CONFIRMED) != 0;
        tr.m_Coasting = (packed & FLAG_COASTING) != 0;

        vector pos;
        pos[0] = floats.Get(floatCursor);
        pos[1] = floats.Get(floatCursor + 1);
        pos[2] = floats.Get(floatCursor + 2);
        tr.m_FilteredPosition = pos;
        vector vel;
        vel[0] = floats.Get(floatCursor + 3);
        vel[1] = floats.Get(floatCursor + 4);
        vel[2] = floats.Get(floatCursor + 5);
        tr.m_FilteredVelocity = vel;
        tr.m_FilteredAzimuthDeg = floats.Get(floatCursor + 6);
        tr.m_FilteredRangeM = floats.Get(floatCursor + 7);
        tr.m_FilteredRangeRateMs = floats.Get(floatCursor + 8);
        tr.m_LastSnrDb = floats.Get(floatCursor + 9);

        bool launchValid = (packed & FLAG_WLR_LAUNCH) != 0;
        bool impactValid = (packed & FLAG_WLR_IMPACT) != 0;
        if (launchValid || impactValid)
        {
            RDF_RadarWlrFix fix = new RDF_RadarWlrFix();
            vector launchPos;
            launchPos[0] = floats.Get(floatCursor + 10);
            launchPos[1] = floats.Get(floatCursor + 11);
            launchPos[2] = floats.Get(floatCursor + 12);
            fix.m_LaunchPos = launchPos;
            fix.m_LaunchTimeS = floats.Get(floatCursor + 13);
            fix.m_LaunchValid = launchValid;
            vector impactPos;
            impactPos[0] = floats.Get(floatCursor + 14);
            impactPos[1] = floats.Get(floatCursor + 15);
            impactPos[2] = floats.Get(floatCursor + 16);
            fix.m_ImpactPos = impactPos;
            fix.m_ImpactTimeS = floats.Get(floatCursor + 17);
            fix.m_ImpactValid = impactValid;
            tr.m_LastWlrFix = fix;
        }

        return tr;
    }

    //------------------------------------------------------------------------------------------------
    protected static RDF_RadarFusedTrack ReadFused(
        notnull array<int> ints,
        notnull array<float> floats,
        int intCursor,
        int floatCursor)
    {
        RDF_RadarFusedTrack f = new RDF_RadarFusedTrack();
        f.m_FusedId = ints.Get(intCursor);
        f.m_ContributorRadarId0 = ints.Get(intCursor + 1);
        int packed = ints.Get(intCursor + 2);
        f.m_Type = packed & TYPE_MASK;
        f.m_Iff = (packed >> IFF_SHIFT) & IFF_MASK;
        f.m_ContributorCount = (packed >> FUSED_CONTRIB_SHIFT) & TYPE_MASK;
        f.m_WlrLaunchValid = (packed & FUSED_FLAG_LAUNCH) != 0;
        f.m_WlrImpactValid = (packed & FUSED_FLAG_IMPACT) != 0;

        vector pos;
        pos[0] = floats.Get(floatCursor);
        pos[1] = floats.Get(floatCursor + 1);
        pos[2] = floats.Get(floatCursor + 2);
        f.m_WorldPos = pos;
        vector vel;
        vel[0] = floats.Get(floatCursor + 3);
        vel[1] = floats.Get(floatCursor + 4);
        vel[2] = floats.Get(floatCursor + 5);
        f.m_Velocity = vel;
        vector launchPos;
        launchPos[0] = floats.Get(floatCursor + 6);
        launchPos[1] = floats.Get(floatCursor + 7);
        launchPos[2] = floats.Get(floatCursor + 8);
        f.m_WlrLaunchPos = launchPos;
        vector impactPos;
        impactPos[0] = floats.Get(floatCursor + 9);
        impactPos[1] = floats.Get(floatCursor + 10);
        impactPos[2] = floats.Get(floatCursor + 11);
        f.m_WlrImpactPos = impactPos;
        return f;
    }

    //------------------------------------------------------------------------------------------------
    protected static void AppendWlr(
        GBRS_WlrPersistDisplay entry,
        notnull array<int> ints,
        notnull array<float> floats)
    {
        if (!entry)
        {
            ints.Insert(0);
            ints.Insert(0);
            int z = 0;
            while (z < WLR_FLOATS)
            {
                floats.Insert(0.0);
                z = z + 1;
            }
            return;
        }

        int packed = 0;
        if (entry.m_HasLaunch)
            packed = packed | WLR_FLAG_LAUNCH;
        if (entry.m_HasImpact)
            packed = packed | WLR_FLAG_IMPACT;
        if (entry.m_HasLive)
            packed = packed | WLR_FLAG_LIVE;
        if (entry.m_DragEstimated)
            packed = packed | WLR_FLAG_DRAG_EST;

        ints.Insert(entry.m_TrackId);
        ints.Insert(packed);
        floats.Insert(entry.m_LaunchPos[0]);
        floats.Insert(entry.m_LaunchPos[1]);
        floats.Insert(entry.m_LaunchPos[2]);
        floats.Insert(entry.m_LaunchTimeS);
        floats.Insert(entry.m_ImpactPos[0]);
        floats.Insert(entry.m_ImpactPos[1]);
        floats.Insert(entry.m_ImpactPos[2]);
        floats.Insert(entry.m_ImpactTimeS);
        floats.Insert(entry.m_LivePos[0]);
        floats.Insert(entry.m_LivePos[1]);
        floats.Insert(entry.m_LivePos[2]);
        floats.Insert(entry.m_LiveVel[0]);
        floats.Insert(entry.m_LiveVel[1]);
        floats.Insert(entry.m_LiveVel[2]);
        floats.Insert(entry.m_AirDrag);
    }

    //------------------------------------------------------------------------------------------------
    protected static GBRS_WlrPersistDisplay ReadWlr(
        notnull array<int> ints,
        notnull array<float> floats,
        int intCursor,
        int floatCursor)
    {
        GBRS_WlrPersistDisplay entry = new GBRS_WlrPersistDisplay();
        entry.m_TrackId = ints.Get(intCursor);
        int packed = ints.Get(intCursor + 1);
        entry.m_HasLaunch = (packed & WLR_FLAG_LAUNCH) != 0;
        entry.m_HasImpact = (packed & WLR_FLAG_IMPACT) != 0;
        entry.m_HasLive = (packed & WLR_FLAG_LIVE) != 0;
        entry.m_DragEstimated = (packed & WLR_FLAG_DRAG_EST) != 0;
        entry.m_Id = "W" + entry.m_TrackId.ToString();

        vector launchPos;
        launchPos[0] = floats.Get(floatCursor);
        launchPos[1] = floats.Get(floatCursor + 1);
        launchPos[2] = floats.Get(floatCursor + 2);
        entry.m_LaunchPos = launchPos;
        entry.m_LaunchTimeS = floats.Get(floatCursor + 3);
        vector impactPos;
        impactPos[0] = floats.Get(floatCursor + 4);
        impactPos[1] = floats.Get(floatCursor + 5);
        impactPos[2] = floats.Get(floatCursor + 6);
        entry.m_ImpactPos = impactPos;
        entry.m_ImpactTimeS = floats.Get(floatCursor + 7);
        vector livePos;
        livePos[0] = floats.Get(floatCursor + 8);
        livePos[1] = floats.Get(floatCursor + 9);
        livePos[2] = floats.Get(floatCursor + 10);
        entry.m_LivePos = livePos;
        vector liveVel;
        liveVel[0] = floats.Get(floatCursor + 11);
        liveVel[1] = floats.Get(floatCursor + 12);
        liveVel[2] = floats.Get(floatCursor + 13);
        entry.m_LiveVel = liveVel;
        entry.m_AirDrag = floats.Get(floatCursor + 14);
        return entry;
    }
}
