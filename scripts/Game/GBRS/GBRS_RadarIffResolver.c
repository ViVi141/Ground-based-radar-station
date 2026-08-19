// GBRS IFF resolver for the multi-station datalink/fusion net.
//
// The station's own side is the *current camp/base-affiliated faction* of the
// station owner (SCR_Faction.GetEntityFaction). Conflict keeps this up to date:
// when a camp is seized, GBRS_RadarStationComponent.AdoptOccupyingFaction
// rewrites the owner's FactionAffiliationComponent to the occupying faction, so
// a USSR-seized US camp's radar treats US as FOE. In GM (no camp) the owner's
// default affiliation ("US"/"USSR" per prefab) drives it.
//
// Target side: the detected vehicle/unit's faction (track.m_Entity, or the
// scatterer-recovered entity when the feed anonymously strips identity). If a
// target's faction equals the station's own side it is FRIEND; EVERYTHING else
// (other faction, unaffiliated, unrecoverable, projectile) is FOE. There is no
// neutral/unknown gray.
class GBRS_RadarIffResolver : RDF_RadarIffResolver
{
    override ERDF_RadarIff Resolve(IEntity radarSubject, RDF_RadarTrack track)
    {
        string ownKey = ResolveOwnFactionKey(radarSubject);
        if (ownKey.IsEmpty())
            return ERDF_RadarIff.RDF_IFF_UNKNOWN;

        if (!track)
            return ERDF_RadarIff.RDF_IFF_FOE;
        if (track.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return ERDF_RadarIff.RDF_IFF_FOE; // inbound ballistic = hostile

        IEntity target = track.m_Entity;
        if (!target && track.m_ScattererId > 0)
        {
            // Recover the detected entity the scatterer retains after the
            // workstation feed anonymizes plots.
            RDF_RadarScatterer scat = RDF_RadarScattererRegistry.FindById(track.m_ScattererId);
            if (scat)
                target = scat.m_Entity;
        }
        if (!target)
            return ERDF_RadarIff.RDF_IFF_FOE;

        Faction targetFaction = SCR_Faction.GetEntityFaction(target);
        if (!targetFaction)
            return ERDF_RadarIff.RDF_IFF_FOE;

        if (targetFaction.GetFactionKey() == ownKey)
            return ERDF_RadarIff.RDF_IFF_FRIEND;

        return ERDF_RadarIff.RDF_IFF_FOE;
    }

    // The station's own side: the owner's current FactionAffiliationComponent
    // faction (Conflict: follows camp occupation via AdoptOccupyingFaction;
    // GM: falls back to the prefab default "US"/"USSR"). If the owner has no
    // affiliation component at all, fall back to the GBRS preset so IFF still
    // has an own-side to compare against.
    protected string ResolveOwnFactionKey(IEntity radarSubject)
    {
        if (radarSubject)
        {
            Faction entityFaction = SCR_Faction.GetEntityFaction(radarSubject);
            if (entityFaction)
                return entityFaction.GetFactionKey();

            GBRS_RadarStationComponent station =
                GBRS_RadarStationComponent.Cast(radarSubject.FindComponent(GBRS_RadarStationComponent));
            if (station)
            {
                if (station.GetFactionPreset() == EGBRS_RadarFactionPreset.USSR)
                    return "USSR";
                return "US";
            }
        }

        return "";
    }
}
