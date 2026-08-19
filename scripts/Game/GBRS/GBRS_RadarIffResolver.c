// GBRS IFF resolver for the multi-station datalink/fusion net.
//
// Basis: compare the *target entity's* faction against the *station's own* faction.
// Same preset faction = FRIEND; different faction = FOE; inbound projectile = FOE;
// missing entity / unknown faction = UNKNOWN. "Own faction" is read from the
// radar subject entity (the station owner).
//
// Identity source: the workstation feed strips m_Entity for realism (plots are
// anonymous). The detector still knows the entity through the scatterer
// registry, so when track.m_Entity is null we recover it by scatterer id and
// read the faction from that entity. IFF is resolved on the target *vehicle /
// unit* (what the radar sees), not the operator/driver inside it; in a GM scene
// the target must carry a FactionAffiliationComponent for a non-UNKNOWN result.
class GBRS_RadarIffResolver : RDF_RadarIffResolver
{
    override ERDF_RadarIff Resolve(IEntity radarSubject, RDF_RadarTrack track)
    {
        if (!track)
            return ERDF_RadarIff.RDF_IFF_UNKNOWN;
        if (track.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return ERDF_RadarIff.RDF_IFF_FOE; // inbound ballistic = hostile

        Faction subjectFaction = SCR_Faction.GetEntityFaction(radarSubject);
        if (!subjectFaction)
            return ERDF_RadarIff.RDF_IFF_UNKNOWN;

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
            return ERDF_RadarIff.RDF_IFF_UNKNOWN;

        Faction targetFaction = SCR_Faction.GetEntityFaction(target);
        if (!targetFaction)
            return ERDF_RadarIff.RDF_IFF_UNKNOWN;

        if (subjectFaction.GetFactionKey() == targetFaction.GetFactionKey())
            return ERDF_RadarIff.RDF_IFF_FRIEND;
        return ERDF_RadarIff.RDF_IFF_FOE;
    }
}
