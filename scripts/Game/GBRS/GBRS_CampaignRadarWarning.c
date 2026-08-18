//------------------------------------------------------------------------------------------------
//! Conflict / Game Master consumer for GBRS_RadarStationEvents.
//!
//! Vanilla EvaluateEnemyPresence only looks at characters inside the HQ
//! compound. Distant air search must not set m_bEnemiesPresent (that flag
//! locks the build button). Powered stations transmit on the locked intel
//! net. Only players tuned to RADAR NET hear the voice traffic.
//! Same launch/impact sheaf counts as one intel-net fire mission.
class GBRS_WlrWarnMission
{
    string m_sFactionKey;
    vector m_LaunchPos;
    vector m_ImpactPos;
    bool m_HasLaunch;
    float m_LastWarnS;
}

class GBRS_CampaignRadarWarning
{
    protected static const float AIR_COOLDOWN_S = 20.0;
    protected static const float WLR_MISSION_MATCH_M = 350.0;
    protected static const float WLR_MISSION_HOLD_S = 40.0;
    protected static const int WLR_MISSION_MAX = 16;

    protected static bool s_Bound;
    protected static ref map<string, float> s_LastWarnS;
    protected static ref array<ref GBRS_WlrWarnMission> s_WlrMissions;

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
        int headingDeg = HeadingDegFromVelocity(target.m_Velocity);
        int altitudeM = AltitudeMFromTarget(target);

        string subtitle = grid;
        if (headingDeg >= 0)
            subtitle = subtitle + "  HDG " + headingDeg.ToString(3, 0);
        if (altitudeM > 0)
            subtitle = subtitle + "  " + altitudeM.ToString() + "M";

        SCR_CampaignMilitaryBaseComponent covering = station.GetCoveringCampaignBase(true);
        if (covering)
            subtitle = subtitle + "  " + covering.GetBaseNameUpperCase();

        BroadcastWarning(
            station,
            stationFaction,
            "RADAR CONTACT",
            subtitle,
            GBRS_RadarStationConstants.INTEL_VOICE_AIR,
            GBRS_MapGrid.Pack(target.m_Position),
            headingDeg,
            altitudeM,
            false);
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
        {
            if (SCR_GameModeCampaign.GetInstance())
                return;
        }

        if (!TryConsumeWlrMission(fix, stationFaction.GetFactionKey()))
            return;

        string impactGrid = GBRS_MapGrid.Format(fix.m_ImpactPos);
        int launchPacked = 0;
        string subtitle = "IMP " + impactGrid;
        if (fix.m_LaunchValid)
        {
            launchPacked = GBRS_MapGrid.Pack(fix.m_LaunchPos);
            subtitle = "LCH " + GBRS_MapGrid.Format(fix.m_LaunchPos) + "  " + subtitle;
        }

        int etaSec = EtaSecondsFromFix(fix);
        if (etaSec > 0)
            subtitle = subtitle + "  ETA " + etaSec.ToString() + "S";
        else
        {
            if (etaSec == 0)
                subtitle = subtitle + "  NOW";
        }

        if (threatened)
            subtitle = subtitle + "  " + threatened.GetBaseNameUpperCase();

        BroadcastWarning(
            station,
            stationFaction,
            "INCOMING FIRE",
            subtitle,
            GBRS_RadarStationConstants.INTEL_VOICE_WLR,
            GBRS_MapGrid.Pack(fix.m_ImpactPos),
            launchPacked,
            etaSec,
            false);
    }

    //------------------------------------------------------------------------------------------------
    //! Operator TX NET: ignore cooldown, interrupt VO, send the live picture.
    static bool ForceBroadcastFromStation(GBRS_RadarStationComponent station)
    {
        if (!station)
            return false;
        if (!station.IsStationAuthority())
            return false;
        if (!station.IsPowered())
            return false;
        if (station.IsDestroyed())
            return false;

        string mode = station.GetWorkstationMode();
        if (mode == GBRS_RadarStationConstants.MODE_WLR)
            return ForceBroadcastWlr(station);

        return ForceBroadcastAir(station);
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
    protected static void NoteAirCooldown(notnull GBRS_RadarStationComponent station)
    {
        if (!s_LastWarnS)
            s_LastWarnS = new map<string, float>();

        string key = string.Format("AIR:%1", station.GetStationRplId());
        s_LastWarnS.Set(key, System.GetTickCount() * 0.001);
    }

    //------------------------------------------------------------------------------------------------
    protected static void NoteWlrMission(notnull RDF_RadarWlrFix fix, string factionKey)
    {
        if (!s_WlrMissions)
            s_WlrMissions = new array<ref GBRS_WlrWarnMission>();

        float nowS = System.GetTickCount() * 0.001;
        GBRS_WlrWarnMission found = FindWlrMission(fix, factionKey);
        if (found)
        {
            found.m_LastWarnS = nowS;
            found.m_ImpactPos = fix.m_ImpactPos;
            if (fix.m_LaunchValid)
            {
                found.m_HasLaunch = true;
                found.m_LaunchPos = fix.m_LaunchPos;
            }
            return;
        }

        GBRS_WlrWarnMission created = new GBRS_WlrWarnMission();
        created.m_sFactionKey = factionKey;
        created.m_ImpactPos = fix.m_ImpactPos;
        created.m_HasLaunch = fix.m_LaunchValid;
        if (fix.m_LaunchValid)
            created.m_LaunchPos = fix.m_LaunchPos;
        created.m_LastWarnS = nowS;
        s_WlrMissions.Insert(created);

        while (s_WlrMissions.Count() > WLR_MISSION_MAX)
            s_WlrMissions.Remove(0);
    }

    //------------------------------------------------------------------------------------------------
    protected static bool ForceBroadcastAir(notnull GBRS_RadarStationComponent station)
    {
        IEntity owner = station.GetOwner();
        if (!owner)
            return false;

        Faction stationFaction = SCR_Faction.GetEntityFaction(owner);
        if (!stationFaction)
            return false;

        RDF_RadarTarget target = FindBestAirContact(station);
        if (!target)
            return false;

        NoteAirCooldown(station);

        string grid = GBRS_MapGrid.Format(target.m_Position);
        int headingDeg = HeadingDegFromVelocity(target.m_Velocity);
        int altitudeM = AltitudeMFromTarget(target);

        string subtitle = grid;
        if (headingDeg >= 0)
            subtitle = subtitle + "  HDG " + headingDeg.ToString(3, 0);
        if (altitudeM > 0)
            subtitle = subtitle + "  " + altitudeM.ToString() + "M";

        SCR_CampaignMilitaryBaseComponent covering = station.GetCoveringCampaignBase(true);
        if (covering)
            subtitle = subtitle + "  " + covering.GetBaseNameUpperCase();

        BroadcastWarning(
            station,
            stationFaction,
            "RADAR CONTACT",
            subtitle,
            GBRS_RadarStationConstants.INTEL_VOICE_AIR,
            GBRS_MapGrid.Pack(target.m_Position),
            headingDeg,
            altitudeM,
            true);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool ForceBroadcastWlr(notnull GBRS_RadarStationComponent station)
    {
        IEntity owner = station.GetOwner();
        if (!owner)
            return false;

        Faction stationFaction = SCR_Faction.GetEntityFaction(owner);
        if (!stationFaction)
            return false;

        RDF_RadarWlrFix fix = FindBestWlrFix(station);
        if (!fix)
            return false;
        if (!fix.m_ImpactValid)
            return false;

        NoteWlrMission(fix, stationFaction.GetFactionKey());

        string impactGrid = GBRS_MapGrid.Format(fix.m_ImpactPos);
        int launchPacked = 0;
        string subtitle = "IMP " + impactGrid;
        if (fix.m_LaunchValid)
        {
            launchPacked = GBRS_MapGrid.Pack(fix.m_LaunchPos);
            subtitle = "LCH " + GBRS_MapGrid.Format(fix.m_LaunchPos) + "  " + subtitle;
        }

        int etaSec = EtaSecondsFromFix(fix);
        if (etaSec > 0)
            subtitle = subtitle + "  ETA " + etaSec.ToString() + "S";
        else
        {
            if (etaSec == 0)
                subtitle = subtitle + "  NOW";
        }

        SCR_CampaignMilitaryBaseComponent threatened =
            FindFriendlyBaseAt(fix.m_ImpactPos, stationFaction);
        if (threatened)
            subtitle = subtitle + "  " + threatened.GetBaseNameUpperCase();

        BroadcastWarning(
            station,
            stationFaction,
            "INCOMING FIRE",
            subtitle,
            GBRS_RadarStationConstants.INTEL_VOICE_WLR,
            GBRS_MapGrid.Pack(fix.m_ImpactPos),
            launchPacked,
            etaSec,
            true);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected static RDF_RadarTarget FindBestAirContact(notnull GBRS_RadarStationComponent station)
    {
        RDF_RadarComponent radar = station.GetRadarComponent();
        if (!radar)
            return null;

        RDF_RadarSensor sensor = radar.GetSensor();
        if (!sensor)
            return null;

        IEntity owner = station.GetOwner();
        if (!owner)
            return null;

        vector origin = owner.GetOrigin();
        RDF_RadarSettings settings = sensor.GetSettings();
        array<ref RDF_RadarTarget> plots = sensor.GetPlots();
        if (!plots)
            return null;

        RDF_RadarTarget best = null;
        float bestDistSq = -1.0;
        int i = 0;
        while (i < plots.Count())
        {
            RDF_RadarTarget target = plots.Get(i);
            i = i + 1;
            if (!target)
                continue;
            if (target.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
                continue;
            if (!GBRS_RadarStationConfig.ShouldDisplayPlot(target, settings))
                continue;

            float distSq = vector.DistanceSqXZ(origin, target.m_Position);
            if (bestDistSq < 0.0 || distSq < bestDistSq)
            {
                bestDistSq = distSq;
                best = target;
            }
        }

        return best;
    }

    //------------------------------------------------------------------------------------------------
    protected static RDF_RadarWlrFix FindBestWlrFix(notnull GBRS_RadarStationComponent station)
    {
        RDF_RadarComponent radar = station.GetRadarComponent();
        if (!radar)
            return null;

        RDF_RadarSensor sensor = radar.GetSensor();
        if (!sensor)
            return null;

        RDF_RadarProjectileTracker tracker = sensor.GetTracker();
        if (!tracker)
            return null;

        array<ref RDF_RadarTrack> tracks = tracker.GetAllTracks();
        if (!tracks)
            return null;

        RDF_RadarWlrFix best = null;
        int bestEta = 0;
        bool haveBest = false;
        int i = 0;
        while (i < tracks.Count())
        {
            RDF_RadarTrack tr = tracks.Get(i);
            i = i + 1;
            if (!tr || !tr.m_Confirmed)
                continue;

            GBRS_RadarWlrSolution sol = GBRS_RadarWlrBallisticSolver.Resolve(tr);
            if (!sol || !sol.m_Fix)
                continue;

            RDF_RadarWlrFix fix = sol.m_Fix;
            if (!fix.m_ImpactValid)
                continue;

            int etaSec = EtaSecondsFromFix(fix);
            if (etaSec < 0)
                etaSec = 9999;

            if (!haveBest || etaSec < bestEta)
            {
                best = fix;
                bestEta = etaSec;
                haveBest = true;
            }
        }

        return best;
    }
    //! warning for WLR_MISSION_HOLD_S so a barrage cannot retrigger VO.
    protected static bool TryConsumeWlrMission(notnull RDF_RadarWlrFix fix, string factionKey)
    {
        if (!s_WlrMissions)
            s_WlrMissions = new array<ref GBRS_WlrWarnMission>();

        float nowS = System.GetTickCount() * 0.001;
        PruneWlrMissions(nowS);

        GBRS_WlrWarnMission found = FindWlrMission(fix, factionKey);
        if (found)
        {
            if ((nowS - found.m_LastWarnS) < WLR_MISSION_HOLD_S)
                return false;

            found.m_LastWarnS = nowS;
            found.m_ImpactPos = fix.m_ImpactPos;
            if (fix.m_LaunchValid)
            {
                found.m_HasLaunch = true;
                found.m_LaunchPos = fix.m_LaunchPos;
            }
            return true;
        }

        GBRS_WlrWarnMission created = new GBRS_WlrWarnMission();
        created.m_sFactionKey = factionKey;
        created.m_ImpactPos = fix.m_ImpactPos;
        created.m_HasLaunch = fix.m_LaunchValid;
        if (fix.m_LaunchValid)
            created.m_LaunchPos = fix.m_LaunchPos;
        created.m_LastWarnS = nowS;
        s_WlrMissions.Insert(created);

        while (s_WlrMissions.Count() > WLR_MISSION_MAX)
            s_WlrMissions.Remove(0);

        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected static GBRS_WlrWarnMission FindWlrMission(notnull RDF_RadarWlrFix fix, string factionKey)
    {
        if (!s_WlrMissions)
            return null;

        float matchSq = WLR_MISSION_MATCH_M * WLR_MISSION_MATCH_M;
        int i = 0;
        while (i < s_WlrMissions.Count())
        {
            GBRS_WlrWarnMission mission = s_WlrMissions.Get(i);
            i = i + 1;
            if (!mission)
                continue;
            if (mission.m_sFactionKey != factionKey)
                continue;
            if (vector.DistanceSqXZ(mission.m_ImpactPos, fix.m_ImpactPos) > matchSq)
                continue;

            if (mission.m_HasLaunch && fix.m_LaunchValid)
            {
                if (vector.DistanceSqXZ(mission.m_LaunchPos, fix.m_LaunchPos) > matchSq)
                    continue;
            }

            return mission;
        }

        return null;
    }

    //------------------------------------------------------------------------------------------------
    protected static void PruneWlrMissions(float nowS)
    {
        if (!s_WlrMissions)
            return;

        float keepS = WLR_MISSION_HOLD_S * 2.0;
        int i = s_WlrMissions.Count() - 1;
        while (i >= 0)
        {
            GBRS_WlrWarnMission mission = s_WlrMissions.Get(i);
            if (!mission)
                s_WlrMissions.Remove(i);
            else if ((nowS - mission.m_LastWarnS) > keepS)
                s_WlrMissions.Remove(i);

            i = i - 1;
        }
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
    protected static void BroadcastWarning(
        GBRS_RadarStationComponent station,
        notnull Faction faction,
        string title,
        string subtitle,
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        bool interrupt)
    {
        GBRS_IntelRadioNet.BeginIntelTxBatch();
        GBRS_IntelRadioNet.TransmitFromStation(
            station, title, subtitle, voiceKind, gridPacked, paramA, paramB, interrupt);
        GBRS_IntelRadioNet.NotifyListeners(
            station, faction, title, subtitle, voiceKind, gridPacked, paramA, paramB, interrupt);
    }

    //------------------------------------------------------------------------------------------------
    //! Compass heading from XZ velocity. 0 = north, 90 = east. -1 if too slow.
    protected static int HeadingDegFromVelocity(vector velocity)
    {
        float vx = velocity[0];
        float vz = velocity[2];
        float speed = Math.Sqrt(vx * vx + vz * vz);
        if (speed < 5.0)
            return -1;

        float heading = Math.Atan2(vx, vz) * Math.RAD2DEG;
        if (heading < 0.0)
            heading = heading + 360.0;

        int headingDeg = Math.Round(heading);
        if (headingDeg >= 360)
            headingDeg = 0;
        if (headingDeg < 0)
            headingDeg = 0;
        return headingDeg;
    }

    //------------------------------------------------------------------------------------------------
    //! AGL metres, rounded to 50. Falls back to world Y if AGL is unset.
    protected static int AltitudeMFromTarget(notnull RDF_RadarTarget target)
    {
        float agl = target.m_AglM;
        if (agl < 0.0)
            agl = target.m_Position[1];
        if (agl < 0.0)
            agl = 0.0;

        int altitudeM = Math.Round(agl / 50.0) * 50;
        if (altitudeM < 0)
            altitudeM = 0;
        return altitudeM;
    }

    //------------------------------------------------------------------------------------------------
    //! Seconds remaining to impact. 0 means now; -1 if the clock is missing.
    protected static int EtaSecondsFromFix(notnull RDF_RadarWlrFix fix)
    {
        if (!fix.m_ImpactValid)
            return -1;

        ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
        if (!world)
            return -1;

        float etaS = fix.m_ImpactTimeS - (world.GetWorldTime() * 0.001);
        int etaSec = Math.Round(etaS);
        if (etaSec < 0)
            etaSec = 0;
        return etaSec;
    }
}
