//------------------------------------------------------------------------------------------------
//! Conflict consumer for GBRS_RadarStationEvents.
//!
//! Vanilla EvaluateEnemyPresence only looks at characters inside the HQ
//! compound. Distant air search must not set m_bEnemiesPresent (that flag
//! locks the build button). This class instead radio-warns the station
//! faction: new air contacts, and WLR impacts that land in a friendly base.
class GBRS_CampaignRadarWarning
{
    protected static const float AIR_COOLDOWN_S = 20.0;
    protected static const float WLR_COOLDOWN_S = 12.0;

    protected static bool s_Bound;
    protected static ref map<string, float> s_LastWarnS;

    //------------------------------------------------------------------------------------------------
    static void EnsureBound()
    {
        if (!s_LastWarnS)
            s_LastWarnS = new map<string, float>();

        if (s_Bound)
            return;

        s_Bound = true;
        if (GBRS_RadarStationEvents.OnRadarContact)
            GBRS_RadarStationEvents.OnRadarContact.Insert(OnRadarContact);
        if (GBRS_RadarStationEvents.OnWlrSolution)
            GBRS_RadarStationEvents.OnWlrSolution.Insert(OnWlrSolution);
    }

    //------------------------------------------------------------------------------------------------
    protected static void OnRadarContact(GBRS_RadarStationComponent station, RDF_RadarTarget target)
    {
        if (!station || !target)
            return;
        if (!station.IsStationAuthority())
            return;
        if (!station.IsPowered())
            return;
        if (station.IsDestroyed())
            return;

        if (target.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return;

        IEntity owner = station.GetOwner();
        if (!owner)
            return;

        Faction stationFaction = SCR_Faction.GetEntityFaction(owner);
        if (!stationFaction)
            return;

        string key = string.Format("AIR:%1", station.GetStationRplId());
        if (!TryConsumeCooldown(key, AIR_COOLDOWN_S))
            return;

        string grid = GBRS_MapGrid.Format(target.m_Position);
        string subtitle = grid;
        SCR_CampaignMilitaryBaseComponent covering = station.GetCoveringCampaignBase(true);
        if (covering)
            subtitle = grid + "  " + covering.GetBaseNameUpperCase();

        NotifyFaction(stationFaction, "RADAR CONTACT", subtitle);
    }

    //------------------------------------------------------------------------------------------------
    protected static void OnWlrSolution(GBRS_RadarStationComponent station, RDF_RadarWlrFix fix)
    {
        if (!station || !fix)
            return;
        if (!station.IsStationAuthority())
            return;
        if (!fix.m_ImpactValid)
            return;

        IEntity owner = station.GetOwner();
        if (!owner)
            return;

        Faction stationFaction = SCR_Faction.GetEntityFaction(owner);
        if (!stationFaction)
            return;

        SCR_CampaignMilitaryBaseComponent threatened = FindFriendlyBaseAt(fix.m_ImpactPos, stationFaction);
        if (!threatened)
            return;

        string key = "WLR:none";
        IEntity baseOwner = threatened.GetOwner();
        if (baseOwner)
        {
            RplComponent baseRpl = RplComponent.Cast(baseOwner.FindComponent(RplComponent));
            if (baseRpl)
                key = string.Format("WLR:%1", baseRpl.Id());
        }
        if (!TryConsumeCooldown(key, WLR_COOLDOWN_S))
            return;

        string grid = GBRS_MapGrid.Format(fix.m_ImpactPos);
        string subtitle = grid + "  " + threatened.GetBaseNameUpperCase();
        NotifyFaction(stationFaction, "INCOMING FIRE", subtitle);
    }

    //------------------------------------------------------------------------------------------------
    protected static bool TryConsumeCooldown(string key, float cooldownS)
    {
        if (!s_LastWarnS)
            s_LastWarnS = new map<string, float>();

        float nowS = System.GetTickCount() * 0.001;
        float lastS = 0.0;
        if (s_LastWarnS.Contains(key))
            lastS = s_LastWarnS.Get(key);

        if (nowS - lastS < cooldownS)
            return false;

        s_LastWarnS.Set(key, nowS);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected static SCR_CampaignMilitaryBaseComponent FindFriendlyBaseAt(vector pos, notnull Faction faction)
    {
        SCR_MilitaryBaseSystem baseSystem = SCR_MilitaryBaseSystem.GetInstance();
        if (!baseSystem)
            return null;

        array<SCR_MilitaryBaseComponent> bases = {};
        baseSystem.GetBases(bases);

        SCR_CampaignMilitaryBaseComponent best = null;
        float bestDistSq = -1.0;

        foreach (SCR_MilitaryBaseComponent base : bases)
        {
            if (!base)
                continue;

            SCR_CampaignMilitaryBaseComponent campaignBase =
                SCR_CampaignMilitaryBaseComponent.Cast(base);
            if (!campaignBase)
                continue;

            Faction baseFaction = campaignBase.GetFaction();
            if (!baseFaction)
                continue;
            if (baseFaction != faction)
                continue;

            IEntity baseOwner = campaignBase.GetOwner();
            if (!baseOwner)
                continue;

            float radius = campaignBase.GetRadius();
            if (radius <= 0.0)
                continue;

            float distSq = vector.DistanceSqXZ(pos, baseOwner.GetOrigin());
            if (distSq > (radius * radius))
                continue;

            if (bestDistSq < 0.0 || distSq < bestDistSq)
            {
                bestDistSq = distSq;
                best = campaignBase;
            }
        }

        return best;
    }

    //------------------------------------------------------------------------------------------------
    protected static void NotifyFaction(notnull Faction faction, string title, string subtitle)
    {
        PlayerManager playerManager = GetGame().GetPlayerManager();
        if (!playerManager)
            return;

        array<int> playerIds = {};
        playerManager.GetPlayers(playerIds);

        foreach (int playerId : playerIds)
        {
            Faction playerFaction = SCR_FactionManager.SGetPlayerFaction(playerId);
            if (!playerFaction)
                continue;
            if (playerFaction != faction)
                continue;

            SCR_PlayerController playerController =
                SCR_PlayerController.Cast(playerManager.GetPlayerController(playerId));
            if (!playerController)
                continue;

            playerController.GBRS_NotifyRadarWarning(title, subtitle);
        }
    }
}
