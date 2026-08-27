// WLR display adapter: RDF already runs RefreshWeaponLocates (budgeted per
// scan, DEM ground via m_EnableDemGroundForWlr). Do not re-solve launch/impact
// from the HUD feed — the old GBRS drag-grid solver duplicated DEM sampling
// thousands of times per second and could AV-crash the runtime DEM cache.
class GBRS_RadarWlrSolution
{
    ref RDF_RadarWlrFix m_Fix;
    float m_AirDrag;
    bool m_DragEstimated;
    float m_SpanS;
    int m_HitCount;
}

class GBRS_RadarWlrBallisticSolver
{
    // Same numeric prior as RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE (O832DU).
    static const float K_PRIOR = 0.000615;
    // Soft display gates only — hard enough to drop kilometre-wrong arcs, soft
    // enough that sector-sweep tracks still paint LCH/IMP.
    static const float MIN_ARC_HORIZ_M = 80.0;
    static const float MAX_ARC_HORIZ_M = 15000.0;
    static const float MIN_TOF_S = 0.35;
    static const float MAX_TOF_S = 90.0;

    static void Clear()
    {
    }

    static GBRS_RadarWlrSolution Resolve(RDF_RadarTrack track)
    {
        if (!track)
            return null;

        RDF_RadarWlrFix fix = SanitizeFixForDisplay(track.m_LastWlrFix, track);
        if (!fix)
            return null;

        GBRS_RadarWlrSolution sol = new GBRS_RadarWlrSolution();
        sol.m_Fix = fix;
        sol.m_AirDrag = K_PRIOR;
        if (track.m_AirDrag > 0.0)
            sol.m_AirDrag = track.m_AirDrag;
        sol.m_DragEstimated = false;
        sol.m_HitCount = track.m_HitCount;
        if (fix.m_FitSpanS > 0.0)
            sol.m_SpanS = fix.m_FitSpanS;
        return sol;
    }

    static RDF_RadarWlrFix ResolveFix(RDF_RadarTrack track)
    {
        if (!track)
            return null;
        return SanitizeFixForDisplay(track.m_LastWlrFix, track);
    }

    // Basic geometric sanity (TOF + horizontal span). No chord / u gates —
    // those rejected almost every sector-sweep fix and left the PPI with only
    // reversing Doppler headings.
    static bool IsDisplayableFix(RDF_RadarWlrFix fix, RDF_RadarTrack track)
    {
        return SanitizeFixForDisplay(fix, track) != null;
    }

    // Copy + optional launch↔impact swap when the measured position chord
    // clearly disagrees with the RDF arc (classic early-fit reversal).
    static RDF_RadarWlrFix SanitizeFixForDisplay(RDF_RadarWlrFix src, RDF_RadarTrack track)
    {
        if (!src)
            return null;
        if (track && track.m_GbrsDisplayFixCached)
        {
            if (track.m_GbrsDisplayFixSource == src)
                return track.m_GbrsDisplayFix;
        }

        RDF_RadarWlrFix result = SanitizeFixUncached(src, track);
        if (track)
        {
            track.m_GbrsDisplayFixSource = src;
            track.m_GbrsDisplayFix = result;
            track.m_GbrsDisplayFixCached = true;
        }
        return result;
    }

    static RDF_RadarWlrFix SanitizeFixUncached(RDF_RadarWlrFix src, RDF_RadarTrack track)
    {
        if (!src)
            return null;
        if (!src.m_LaunchValid || !src.m_ImpactValid)
            return null;

        float tof = src.m_ImpactTimeS - src.m_LaunchTimeS;
        if (tof != tof)
            return null;
        if (tof < MIN_TOF_S)
            return null;
        if (tof > MAX_TOF_S)
            return null;

        vector launch = src.m_LaunchPos;
        vector impact = src.m_ImpactPos;
        if (!IsFiniteVector(launch) || !IsFiniteVector(impact))
            return null;
        float tLaunch = src.m_LaunchTimeS;
        float tImpact = src.m_ImpactTimeS;
        bool swapped = false;

        vector arc = impact - launch;
        arc[1] = 0.0;
        float arcLen = arc.Length();
        if (arcLen < MIN_ARC_HORIZ_M)
            return null;
        if (arcLen > MAX_ARC_HORIZ_M)
            return null;

        vector motion = ChordVelocity(track);
        if (motion.LengthSq() >= 25.0)
        {
            float agree = vector.Dot(motion, arc);
            if (agree < 0.0)
            {
                // RDF early fit often swaps ends; flip for display / intel.
                vector tmpP = launch;
                launch = impact;
                impact = tmpP;
                swapped = true;
            }
        }

        // The normal path is already correctly oriented. Reuse RDF's fix
        // instead of allocating an identical copy on every HUD redraw,
        // snapshot bake, event update, and datalink update.
        if (!swapped)
            return src;

        RDF_RadarWlrFix outFix = new RDF_RadarWlrFix();
        outFix.m_LaunchValid = true;
        outFix.m_ImpactValid = true;
        outFix.m_LaunchPos = launch;
        outFix.m_ImpactPos = impact;
        outFix.m_LaunchTimeS = tLaunch;
        outFix.m_ImpactTimeS = tImpact;
        outFix.m_AnchorTimeS = src.m_AnchorTimeS;
        outFix.m_FitValid = src.m_FitValid;
        outFix.m_FitRmsM = src.m_FitRmsM;
        outFix.m_FitPointCount = src.m_FitPointCount;
        outFix.m_FitSpanS = src.m_FitSpanS;
        return outFix;
    }

    static bool IsFiniteVector(vector value)
    {
        int i = 0;
        while (i < 3)
        {
            float component = value[i];
            if (component != component)
                return false;
            if (component >= float.INFINITY || component <= -float.INFINITY)
                return false;
            i = i + 1;
        }
        return true;
    }

    // Horizontal motion from recent position history (m/s). Empty when sparse.
    static vector ChordVelocity(RDF_RadarTrack track)
    {
        vector zero = "0 0 0";
        if (!track || !track.m_Positions || !track.m_Times)
            return zero;

        int n = track.m_Positions.Count();
        if (n < 2)
            return zero;

        int last = n - 1;
        vector newest = track.m_Positions.Get(last);
        float tNew = track.m_Times.Get(last);
        int chosen = 0;
        int j = 0;
        while (j < last)
        {
            if (tNew - track.m_Times.Get(j) >= 0.5)
                chosen = j;
            j = j + 1;
        }

        vector older = track.m_Positions.Get(chosen);
        float dt = tNew - track.m_Times.Get(chosen);
        if (dt < 0.2)
            return zero;

        vector chord = newest - older;
        chord[1] = 0.0;
        return chord * (1.0 / dt);
    }

    static vector PredictLive(RDF_RadarTrack track, float worldTimeS)
    {
        if (!track)
            return "0 0 0";
        return track.m_FilteredPosition;
    }
}
