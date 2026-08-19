// WLR display adapter: RDF already runs RefreshWeaponLocates (budgeted per
// scan, DEM ground via m_EnableDemGroundForWlr). Do not re-solve launch/impact
// from the HUD feed — the old GBRS drag-grid solver duplicated DEM sampling
// thousands of times per second and could AV-crash the runtime DEM cache. The
// RDF DEM-ground WLR fit is kept on; a non-finite-coordinate terrain crash is
// fixed at the RDF sampling entry points.
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

    static void Clear()
    {
    }

    static GBRS_RadarWlrSolution Resolve(RDF_RadarTrack track)
    {
        if (!track)
            return null;

        GBRS_RadarWlrSolution sol = new GBRS_RadarWlrSolution();
        sol.m_Fix = track.m_LastWlrFix;
        sol.m_AirDrag = K_PRIOR;
        if (track.m_AirDrag > 0.0)
            sol.m_AirDrag = track.m_AirDrag;
        sol.m_DragEstimated = false;
        sol.m_HitCount = track.m_HitCount;
        if (track.m_LastWlrFix && track.m_LastWlrFix.m_FitSpanS > 0.0)
            sol.m_SpanS = track.m_LastWlrFix.m_FitSpanS;
        return sol;
    }

    static RDF_RadarWlrFix ResolveFix(RDF_RadarTrack track)
    {
        if (!track)
            return null;
        return track.m_LastWlrFix;
    }

    // Live-position adapter. RDF's own ballistic solver already writes
    // m_LastWlrFix; do not re-run track.PredictAt()/a second extrapolation on a
    // HUD feed tick — that re-entered the solver at the wrong time and crashed
    // native code. Prefer the already-filtered track position.
    static vector PredictLive(RDF_RadarTrack track, float worldTimeS)
    {
        if (!track)
            return "0 0 0";
        return track.m_FilteredPosition;
    }
}
