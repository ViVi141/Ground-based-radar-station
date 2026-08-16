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
}
