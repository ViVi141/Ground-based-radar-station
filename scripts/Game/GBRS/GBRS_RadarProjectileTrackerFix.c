// GBRS mechanical-scan tracker fix.
//
// RDF_RadarSettings clamps m_TrackMaxMisses to 32. With the fast update
// intervals and rotating narrow beams used by GBRS (0.02–0.05 s per scan),
// 32 misses only cover about 0.6–1.6 s of wall time. A mechanically scanned
// radar can easily be away from a target for a full rotation (6–10 s), so
// tracks were pruned between beam passes and aircraft/shells appeared and
// vanished on the PPI.
//
// This modded tracker keeps a much larger miss allowance when mechanical scan
// is enabled. Stale contacts are still removed by the coast timeout
// (m_TrackCoastMaxSec), so this does not make dead tracks live forever.
modded class RDF_RadarTrack
{
    static const int GBRS_WLR_MIN_NEW_HITS_PER_RESOLVE = 4;
    static const float GBRS_WLR_MIN_RESOLVE_INTERVAL_S = 0.5;
    static const int GBRS_WLR_FINAL_MIN_HITS = 8;
    static const float GBRS_WLR_FINAL_MIN_SPAN_S = 1.0;

    int m_GbrsLastWlrSolveHitCount = -1;
    float m_GbrsLastWlrSolveSampleTimeS = -1.0;
    bool m_GbrsWlrSolutionFinal;
    int m_GbrsWlrAcceptedSolveCount;
    RDF_RadarWlrFix m_GbrsDisplayFixSource;
    ref RDF_RadarWlrFix m_GbrsDisplayFix;
    bool m_GbrsDisplayFixCached;

    override RDF_RadarWlrFix SolveWeaponLocate(float groundYM)
    {
        RDF_RadarWlrFix previousFix = m_LastWlrFix;
        RDF_RadarWlrFix fix = super.SolveWeaponLocate(groundYM);
        m_GbrsLastWlrSolveHitCount = m_HitCount;
        m_GbrsLastWlrSolveSampleTimeS = m_LastUpdateTime;

        // A failed refresh returns the previous fix. Only a newly accepted fit
        // may become final, otherwise one rejected sample window could freeze
        // an older low-quality result.
        if (fix && fix != previousFix && fix.m_FitValid)
        {
            m_GbrsWlrAcceptedSolveCount = m_GbrsWlrAcceptedSolveCount + 1;
            if (fix.m_FitSpanS >= GBRS_WLR_FINAL_MIN_SPAN_S)
            {
                if (m_HitCount >= GBRS_WLR_FINAL_MIN_HITS)
                    m_GbrsWlrSolutionFinal = true;
            }
            if (m_GbrsWlrAcceptedSolveCount >= 2)
                m_GbrsWlrSolutionFinal = true;
        }

        return fix;
    }

    bool GbrsNeedsWlrResolve()
    {
        if (m_GbrsWlrSolutionFinal)
            return false;
        if (m_GbrsLastWlrSolveHitCount < 0)
        {
            if (!m_Times || m_Times.Count() < m_WlrMinHits)
                return false;
            int last = m_Times.Count() - 1;
            float spanS = m_Times.Get(last) - m_Times.Get(0);
            if (spanS < m_WlrMinSpanS)
                return false;
            return true;
        }
        if (m_HitCount <= m_GbrsLastWlrSolveHitCount)
            return false;

        int newHits = m_HitCount - m_GbrsLastWlrSolveHitCount;
        if (newHits < GBRS_WLR_MIN_NEW_HITS_PER_RESOLVE)
            return false;

        float elapsedS = m_LastUpdateTime - m_GbrsLastWlrSolveSampleTimeS;
        if (elapsedS < GBRS_WLR_MIN_RESOLVE_INTERVAL_S)
            return false;

        return true;
    }
}

modded class RDF_RadarProjectileTracker
{
    override void ConfigureFromSettings(RDF_RadarSettings settings)
    {
        super.ConfigureFromSettings(settings);

        // A free-running mechanical scan needs a very large miss allowance to
        // coast between beam passes. A stare/locked scan still benefits from a
        // moderate allowance so brief detection gaps do not kill tracks, while
        // duplicates without fresh hits are pruned sooner.
        if (settings && settings.m_EnableMechanicalScan)
        {
            if (settings.m_bScanAngleLocked)
                m_MaxMisses = 96;
            else
                m_MaxMisses = 600;
        }
    }

    override protected bool WlrTrackEligible(RDF_RadarTrack track)
    {
        if (!super.WlrTrackEligible(track))
            return false;

        // A full drag/DEM solve runs a 90-iteration optimizer. Sector sweep can
        // add a hit every scan, so resolve only after a meaningful new sample
        // window and stop once the fit has enough hits and time span.
        if (!track.GbrsNeedsWlrResolve())
            return false;

        return true;
    }
}
