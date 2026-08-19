// GBRS IFF resolver for the multi-station datalink/fusion net.
//
// Three-state IFF:
//   - Own side is the *current camp/base-affiliated faction* of the station
//     owner (SCR_Faction.GetEntityFaction). Conflict keeps this current:
//     when a camp is seized, GBRS_RadarStationComponent.AdoptOccupyingFaction
//     rewrites the owner's FactionAffiliationComponent to the occupying faction,
//     so a USSR-seized US camp's radar treats US as FOE. In GM (no camp) the
//     owner's default affiliation drives it; the GBRS preset is a last-resort
//     fallback.
//   - FRIEND: target faction == own side.
//   - FOE: target has a different faction, or an inbound projectile.
//   - NEUTRAL: no identity can be determined (no entity / unaffiliated target) -
//     civilian/unaffiliated objects are neither friend nor foe.
class GBRS_RadarIffResolver : RDF_RadarIffResolver
{
    override ERDF_RadarIff Resolve(IEntity radarSubject, RDF_RadarTrack track)
    {
        string ownKey = ResolveOwnFactionKey(radarSubject);
        if (ownKey.IsEmpty())
            return ERDF_RadarIff.RDF_IFF_UNKNOWN;

        if (!track)
            return ERDF_RadarIff.RDF_IFF_NEUTRAL;
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
            return ERDF_RadarIff.RDF_IFF_NEUTRAL;

        Faction targetFaction = SCR_Faction.GetEntityFaction(target);
        if (!targetFaction)
            return ERDF_RadarIff.RDF_IFF_NEUTRAL;

        if (targetFaction.GetFactionKey() == ownKey)
            return ERDF_RadarIff.RDF_IFF_FRIEND;

        return ERDF_RadarIff.RDF_IFF_FOE;
    }

    // The station's own side: the owner's current FactionAffiliationComponent
    // faction (Conflict: follows camp occupation; GM: prefab default). If the
    // owner has none, fall back to the GBRS preset so IFF keeps an own-side.
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
