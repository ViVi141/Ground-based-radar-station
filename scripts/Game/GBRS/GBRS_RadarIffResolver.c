// GBRS IFF resolver for the multi-station datalink/fusion net.
//
// GM contract (per user): the station's own side is its preset faction — US
// station = friendly US; EVERYTHING else (other faction OR unaffiliated/unknown
// targets) is treated as FOE. There is no "neutral/unknown" gray for GM IFF.
//
// Own side: read from the station component's m_eFactionPreset (US/USSR) so the
// result does not depend on whether the owner entity's FactionAffiliationComponent
// was initialized in the GM scene.
// Target side: the detected vehicle/unit (track.m_Entity, or the scatterer-
// recovered entity when the workstation feed anonymized the plot).
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

    // The station's own side, from the GBRS preset (independent of the owner's
    // FactionAffiliationComponent, which a GM scene may not have initialized).
    protected string ResolveOwnFactionKey(IEntity radarSubject)
    {
        if (radarSubject)
        {
            GBRS_RadarStationComponent station =
                GBRS_RadarStationComponent.Cast(radarSubject.FindComponent(GBRS_RadarStationComponent));
            if (station)
            {
                if (station.GetFactionPreset() == EGBRS_RadarFactionPreset.USSR)
                    return "USSR";
                return "US";
            }
        }

        Faction entityFaction = SCR_Faction.GetEntityFaction(radarSubject);
        if (entityFaction)
            return entityFaction.GetFactionKey();

        return "";
    }
}
