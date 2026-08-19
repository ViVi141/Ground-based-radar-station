// GBRS IFF resolver for the multi-station datalink/fusion net.
//
// Basis (per user decision): compare the target entity's faction against the
// station's own faction. Same preset faction = FRIEND; a different faction =
// FOE; missing entity / unknown faction = UNKNOWN. "Own faction" is read from
// the radar subject entity (the station owner) so each station resolves IFF for
// itself; fused tracks then carry the merged IFF from RDF_RadarFusionService.
class GBRS_RadarIffResolver : RDF_RadarIffResolver
{
    override ERDF_RadarIff Resolve(IEntity radarSubject, RDF_RadarTrack track)
    {
        if (!track || !track.m_Entity)
            return ERDF_RadarIff.RDF_IFF_UNKNOWN;
        if (track.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return ERDF_RadarIff.RDF_IFF_FOE; // inbound ballistic = hostile

        Faction subjectFaction = SCR_Faction.GetEntityFaction(radarSubject);
        Faction targetFaction = SCR_Faction.GetEntityFaction(track.m_Entity);
        if (!subjectFaction || !targetFaction)
            return ERDF_RadarIff.RDF_IFF_UNKNOWN;

        if (subjectFaction.GetFactionKey() == targetFaction.GetFactionKey())
            return ERDF_RadarIff.RDF_IFF_FRIEND;
        return ERDF_RadarIff.RDF_IFF_FOE;
    }
}
