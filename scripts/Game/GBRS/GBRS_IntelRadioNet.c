//------------------------------------------------------------------------------------------------
//! Station transmitter + listen-only helpers for the locked GBRS intel net.
//! Players keep vanilla handsets; this only parks the radar radio on a
//! faction frequency and refuses CHANNEL PTT while tuned there.
//! Contact traffic is radio-only: handhelds must be on RADAR NET.
//! Local TX is INTEL_RADIO_RANGE_M (2 km). Farther hop uses either Conflict
//! HQ coverage or a BFS of powered world RelayTransceivers (GM towers,
//! antennas, command vehicles, Conflict relays). Handhelds do not hop.
//! RF OnDelivery is the real gate; NotifyListeners is the Game Master fallback.
class GBRS_IntelRadioNet
{
    protected static int s_iNotifyStamp;
    protected static ref map<int, int> s_mNotifyStamp;
    protected static int s_iRelayMeshStamp = -1;
    protected static RplId s_RelayMeshStationId;
    protected static ref array<BaseTransceiver> s_aRelayMesh;

    //------------------------------------------------------------------------------------------------
    static void BeginIntelTxBatch()
    {
        s_iNotifyStamp = s_iNotifyStamp + 1;
        if (s_iNotifyStamp > 1000000)
            s_iNotifyStamp = 1;

        if (!s_mNotifyStamp)
            s_mNotifyStamp = new map<int, int>();

        s_iRelayMeshStamp = -1;
    }

    //------------------------------------------------------------------------------------------------
    static void ConfigureStationRadio(GBRS_RadarStationComponent station, bool powered)
    {
        if (!station)
            return;

        IEntity owner = station.GetOwner();
        if (!owner)
            return;

        BaseRadioComponent radio = BaseRadioComponent.Cast(owner.FindComponent(BaseRadioComponent));
        if (!radio)
            return;

        if (!powered)
        {
            radio.SetPower(false);
            return;
        }

        Faction faction = SCR_Faction.GetEntityFaction(owner);
        int freqKhz = GBRS_RadarStationConstants.GetIntelFrequencyKhz(
            faction, station.GetFactionPreset());

        if (faction)
            radio.SetEncryptionKey(faction.GetFactionRadioEncryptionKey());

        radio.SetPower(true);

        if (radio.TransceiversCount() <= 0)
            return;

        BaseTransceiver transceiver = radio.GetTransceiver(0);
        if (!transceiver)
            return;

        int minKhz = transceiver.GetMinFrequency();
        int maxKhz = transceiver.GetMaxFrequency();
        if (freqKhz < minKhz)
            return;
        if (freqKhz > maxKhz)
            return;

        transceiver.SetFrequency(freqKhz);
        transceiver.SetRange(GBRS_RadarStationConstants.INTEL_RADIO_RANGE_M);
    }

    //------------------------------------------------------------------------------------------------
    static bool TransmitFromStation(
        GBRS_RadarStationComponent station,
        string title,
        string subtitle,
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        bool interrupt)
    {
        if (!station)
            return false;

        IEntity owner = station.GetOwner();
        if (!owner)
            return false;

        BaseRadioComponent radio = BaseRadioComponent.Cast(owner.FindComponent(BaseRadioComponent));
        if (!radio)
            return false;
        if (!radio.IsPowered())
            return false;
        if (radio.TransceiversCount() <= 0)
            return false;

        BaseTransceiver transceiver = radio.GetTransceiver(0);
        if (!transceiver)
            return false;

        Faction faction = SCR_Faction.GetEntityFaction(owner);
        int freqKhz = GBRS_RadarStationConstants.GetIntelFrequencyKhz(
            faction, station.GetFactionPreset());

        int minKhz = transceiver.GetMinFrequency();
        int maxKhz = transceiver.GetMaxFrequency();
        if (freqKhz < minKhz)
            return false;
        if (freqKhz > maxKhz)
            return false;

        string factionKey = "";
        if (faction)
            factionKey = faction.GetFactionKey();

        string encryptionKey = radio.GetEncryptionKey();
        TransmitOnIntelFreq(
            transceiver,
            freqKhz,
            encryptionKey,
            title,
            subtitle,
            voiceKind,
            gridPacked,
            paramA,
            paramB,
            interrupt,
            factionKey);

        if (IsStationOnFactionRadioNet(station, faction))
            RelayIntelFromFactionNet(
                station,
                faction,
                freqKhz,
                title,
                subtitle,
                voiceKind,
                gridPacked,
                paramA,
                paramB,
                interrupt,
                factionKey);

        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected static void FillIntelMsg(
        notnull GBRS_IntelRadioMsg msg,
        string title,
        string subtitle,
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        bool interrupt,
        string factionKey)
    {
        msg.SetIntelText(title, subtitle);
        msg.SetVoiceKind(voiceKind);
        msg.SetGridPacked(gridPacked);
        msg.SetVoiceParams(paramA, paramB);
        msg.SetInterrupt(interrupt);
        msg.SetFactionKey(factionKey);
    }

    //------------------------------------------------------------------------------------------------
    protected static void TransmitOnIntelFreq(
        notnull BaseTransceiver transceiver,
        int freqKhz,
        string encryptionKey,
        string title,
        string subtitle,
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        bool interrupt,
        string factionKey)
    {
        GBRS_IntelRadioMsg msg = new GBRS_IntelRadioMsg();
        FillIntelMsg(msg, title, subtitle, voiceKind, gridPacked, paramA, paramB, interrupt, factionKey);
        msg.SetEncryptionKey(encryptionKey);
        transceiver.BeginTransmissionFreq(msg, freqKhz);

        // Game Master handsets never get Conflict's SetEncryptionKey. A second
        // unkeyed copy reaches those radios if the RadioManager is present.
        if (encryptionKey.IsEmpty())
            return;

        GBRS_IntelRadioMsg openMsg = new GBRS_IntelRadioMsg();
        FillIntelMsg(openMsg, title, subtitle, voiceKind, gridPacked, paramA, paramB, interrupt, factionKey);
        openMsg.SetEncryptionKey("");
        transceiver.BeginTransmissionFreq(openMsg, freqKhz);
    }

    //------------------------------------------------------------------------------------------------
    //! HQ RelayTransceiver injects RADAR NET into the Conflict coverage mesh.
    //! Game Master / world RelayTransceivers hop from the collected mesh.
    //! Does not retune HQ; BeginTransmissionFreq keeps the platoon channel.
    protected static void RelayIntelFromFactionNet(
        GBRS_RadarStationComponent station,
        Faction faction,
        int freqKhz,
        string title,
        string subtitle,
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        bool interrupt,
        string factionKey)
    {
        string encryptionKey = "";
        if (faction)
            encryptionKey = faction.GetFactionRadioEncryptionKey();

        BaseTransceiver hqRelay = GetFactionIntelRelayTransceiver(station, faction);
        if (hqRelay)
        {
            TransmitFromRelayNode(
                hqRelay,
                freqKhz,
                encryptionKey,
                title,
                subtitle,
                voiceKind,
                gridPacked,
                paramA,
                paramB,
                interrupt,
                factionKey);
        }

        EnsureIntelRelayMesh(station, faction);
        if (!s_aRelayMesh)
            return;

        foreach (BaseTransceiver meshRelay : s_aRelayMesh)
        {
            if (!meshRelay)
                continue;
            if (meshRelay == hqRelay)
                continue;

            TransmitFromRelayNode(
                meshRelay,
                freqKhz,
                encryptionKey,
                title,
                subtitle,
                voiceKind,
                gridPacked,
                paramA,
                paramB,
                interrupt,
                factionKey);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected static void TransmitFromRelayNode(
        notnull BaseTransceiver relay,
        int freqKhz,
        string encryptionKey,
        string title,
        string subtitle,
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        bool interrupt,
        string factionKey)
    {
        BaseRadioComponent relayRadio = relay.GetRadio();
        if (!relayRadio)
            return;
        if (!relayRadio.IsPowered())
            return;

        if (!CanTransmitOnIntelFreq(relay, freqKhz))
            return;

        string txKey = encryptionKey;
        if (txKey.IsEmpty())
            txKey = relayRadio.GetEncryptionKey();

        TransmitOnIntelFreq(
            relay,
            freqKhz,
            txKey,
            title,
            subtitle,
            voiceKind,
            gridPacked,
            paramA,
            paramB,
            interrupt,
            factionKey);
    }

    //------------------------------------------------------------------------------------------------
    protected static bool CanTransmitOnIntelFreq(notnull BaseTransceiver relay, int freqKhz)
    {
        int minKhz = relay.GetMinFrequency();
        int maxKhz = relay.GetMaxFrequency();
        if (minKhz > 0)
        {
            if (freqKhz < minKhz)
                return false;
        }
        if (maxKhz > 0)
        {
            if (freqKhz > maxKhz)
                return false;
        }

        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Game Master / editor fallback when RF never reaches the handset.
    //! Players already claimed by OnDelivery are skipped.
    static void NotifyListeners(
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
        PlayerManager playerManager = GetGame().GetPlayerManager();
        if (!playerManager)
            return;

        vector stationPos = vector.Zero;
        IEntity stationOwner = null;
        if (station)
            stationOwner = station.GetOwner();
        if (stationOwner)
            stationPos = stationOwner.GetOrigin();

        int stationFreqKhz = GBRS_RadarStationConstants.GetIntelFrequencyKhz(
            faction, EGBRS_RadarFactionPreset.US);
        if (station)
        {
            stationFreqKhz = GBRS_RadarStationConstants.GetIntelFrequencyKhz(
                faction, station.GetFactionPreset());
        }

        string encryptionKey = faction.GetFactionRadioEncryptionKey();
        string factionKey = faction.GetFactionKey();
        bool relayed = IsStationOnFactionRadioNet(station, faction);

        array<int> playerIds = {};
        playerManager.GetPlayers(playerIds);

        foreach (int playerId : playerIds)
        {
            bool tuned = IsPlayerTunedToIntel(playerId, stationFreqKhz, encryptionKey);
            if (!tuned)
                continue;

            Faction playerFaction = GetPlayerFactionForIntel(playerId);
            if (playerFaction && playerFaction != faction)
                continue;

            if (!IsPlayerInIntelRange(playerId, stationPos, faction, relayed))
                continue;

            DeliverToPlayer(
                playerId, title, subtitle, voiceKind, gridPacked, paramA, paramB, 1.0, factionKey, interrupt);
        }
    }

    //------------------------------------------------------------------------------------------------
    static void DeliverToPlayer(
        int playerId,
        string title,
        string subtitle,
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        float quality,
        string factionKey,
        bool interrupt)
    {
        if (!TryClaimListener(playerId))
            return;

        PlayerManager playerManager = GetGame().GetPlayerManager();
        if (!playerManager)
            return;

        SCR_PlayerController playerController =
            SCR_PlayerController.Cast(playerManager.GetPlayerController(playerId));
        if (!playerController)
            return;

        playerController.GBRS_NotifyIntelRadio(
            title, subtitle, voiceKind, gridPacked, paramA, paramB, quality, factionKey, interrupt);
    }

    //------------------------------------------------------------------------------------------------
    //! One-shot how-to after a station finishes building. Not a contact alert.
    static void NotifyIntelBriefing(GBRS_RadarStationComponent station, notnull Faction faction)
    {
        PlayerManager playerManager = GetGame().GetPlayerManager();
        if (!playerManager)
            return;

        int freqKhz = GBRS_RadarStationConstants.GetIntelFrequencyKhz(
            faction, EGBRS_RadarFactionPreset.US);
        if (station)
        {
            freqKhz = GBRS_RadarStationConstants.GetIntelFrequencyKhz(
                faction, station.GetFactionPreset());
        }

        string mhz = GBRS_RadarStationConstants.FormatIntelFrequencyMhz(freqKhz);
        string title = GBRS_RadarStationConstants.INTEL_CHANNEL_NAME + " ONLINE";
        string subtitle =
            "Tune handheld to " + GBRS_RadarStationConstants.INTEL_CHANNEL_NAME
            + " (" + mhz + "). 2 km local; HQ or placed relay towers hop farther. Air: grid heading altitude. WLR: launch impact ETA.";

        array<int> playerIds = {};
        playerManager.GetPlayers(playerIds);

        foreach (int playerId : playerIds)
        {
            SCR_PlayerController playerController =
                SCR_PlayerController.Cast(playerManager.GetPlayerController(playerId));
            if (!playerController)
                continue;

            Faction playerFaction = GetPlayerFactionForIntel(playerId);
            if (playerFaction && playerFaction != faction)
                continue;

            playerController.GBRS_NotifyIntelBriefing(title, subtitle);
        }
    }

    //------------------------------------------------------------------------------------------------
    static bool IsListenOnlyEntry(SCR_VONEntry entry)
    {
        SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(entry);
        if (!radioEntry)
            return false;

        return GBRS_RadarStationConstants.IsIntelFrequencyKhz(radioEntry.GetEntryFrequency());
    }

    //------------------------------------------------------------------------------------------------
    static bool IsPlayerTunedToIntel(int playerId, int freqKhz = 0, string encryptionKey = "")
    {
        IEntity radio = GetPlayerIntelRadioEntity(playerId, freqKhz, encryptionKey);
        if (!radio)
            return false;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Handheld, backpack, or open editor radio currently on RADAR NET.
    static IEntity GetPlayerIntelRadioEntity(int playerId, int freqKhz = 0, string encryptionKey = "")
    {
        SCR_EditorManagerEntity editor = GetEditorManagerForPlayer(playerId);
        if (editor)
        {
            if (editor.IsOpened())
            {
                if (IsRadioEntityTunedToIntel(editor, freqKhz, encryptionKey))
                    return editor;
            }
        }

        IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
        if (!controlled)
            return null;

        SCR_GadgetManagerComponent gadgets =
            SCR_GadgetManagerComponent.Cast(controlled.FindComponent(SCR_GadgetManagerComponent));
        if (!gadgets)
            return null;

        IEntity held = gadgets.GetHeldGadget();
        if (IsRadioEntityTunedToIntel(held, freqKhz, encryptionKey))
            return held;

        IEntity handheld = gadgets.GetGadgetByType(EGadgetType.RADIO);
        if (IsRadioEntityTunedToIntel(handheld, freqKhz, encryptionKey))
            return handheld;

        IEntity backpack = gadgets.GetGadgetByType(EGadgetType.RADIO_BACKPACK);
        if (IsRadioEntityTunedToIntel(backpack, freqKhz, encryptionKey))
            return backpack;

        return null;
    }

    //------------------------------------------------------------------------------------------------
    static bool IsPlayerInIntelRange(
        int playerId,
        vector stationPos,
        Faction faction,
        bool relayed)
    {
        if (stationPos == vector.Zero)
            return true;

        IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
        if (!controlled)
            return true;

        vector playerPos = controlled.GetOrigin();
        if (IsWithinLocalIntelRange(playerPos, stationPos))
            return true;

        if (!relayed)
            return false;

        if (faction)
        {
            if (IsPositionLinkedToFactionRadio(
                playerPos, faction, SCR_ERadioCoverageStatus.RECEIVE, false))
                return true;
        }

        return IsPositionNearRelayMesh(playerPos);
    }

    //------------------------------------------------------------------------------------------------
    static bool IsStationOnFactionRadioNet(GBRS_RadarStationComponent station, Faction faction)
    {
        if (!station)
            return false;

        IEntity owner = station.GetOwner();
        if (!owner)
            return false;

        if (faction)
        {
            if (IsPositionLinkedToFactionRadio(
                owner.GetOrigin(), faction, SCR_ERadioCoverageStatus.SEND, true))
                return true;
        }

        EnsureIntelRelayMesh(station, faction);
        if (!s_aRelayMesh)
            return false;
        if (s_aRelayMesh.Count() <= 0)
            return false;

        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! True when pos is inside a Conflict coverage radio that can talk to HQ,
    //! or (allowLocalTxReach) a networked radio sits inside the station's 2 km TX.
    protected static bool IsPositionLinkedToFactionRadio(
        vector pos,
        notnull Faction faction,
        SCR_ERadioCoverageStatus direction,
        bool allowLocalTxReach)
    {
        SCR_CampaignFaction campaignFaction = SCR_CampaignFaction.Cast(faction);
        if (!campaignFaction)
            return false;

        SCR_MilitaryBaseSystem baseSystem = SCR_MilitaryBaseSystem.GetInstance();
        if (!baseSystem)
            return false;

        array<SCR_MilitaryBaseComponent> bases = {};
        baseSystem.GetBases(bases);
        if (bases.IsEmpty())
            return false;

        FactionKey factionKey = faction.GetFactionKey();
        float localRangeM = GBRS_RadarStationConstants.INTEL_RADIO_RANGE_M;
        float localRangeSq = localRangeM * localRangeM;

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
            if (baseFaction.GetFactionKey() != factionKey)
                continue;

            if (!campaignBase.IsHQRadioTrafficPossible(campaignFaction, direction))
                continue;

            IEntity baseOwner = campaignBase.GetOwner();
            if (!baseOwner)
                continue;

            float radioRangeM = GetCampaignBaseRadioRangeM(campaignBase);
            if (radioRangeM <= 0.0)
                continue;

            float distSq = vector.DistanceSqXZ(pos, baseOwner.GetOrigin());
            if (distSq <= (radioRangeM * radioRangeM))
                return true;

            if (allowLocalTxReach)
            {
                if (distSq <= localRangeSq)
                    return true;
            }
        }

        return false;
    }

    //------------------------------------------------------------------------------------------------
    protected static void EnsureIntelRelayMesh(GBRS_RadarStationComponent station, Faction faction)
    {
        RplId stationId;
        if (station)
            stationId = station.GetStationRplId();

        if (s_aRelayMesh)
        {
            if (s_iRelayMeshStamp == s_iNotifyStamp)
            {
                if (s_RelayMeshStationId == stationId)
                    return;
            }
        }

        s_iRelayMeshStamp = s_iNotifyStamp;
        s_RelayMeshStationId = stationId;
        if (!s_aRelayMesh)
            s_aRelayMesh = new array<BaseTransceiver>();
        s_aRelayMesh.Clear();

        CollectIntelRelayMesh(station, faction);
    }

    //------------------------------------------------------------------------------------------------
    protected static void CollectIntelRelayMesh(GBRS_RadarStationComponent station, Faction faction)
    {
        if (!station)
            return;

        IEntity owner = station.GetOwner();
        if (!owner)
            return;
        if (owner.IsDeleted())
            return;

        string factionEncryption = "";
        if (faction)
            factionEncryption = faction.GetFactionRadioEncryptionKey();
        if (factionEncryption.IsEmpty())
            return;

        array<BaseRadioComponent> visited = {};
        // Relay candidates are gathered from owned / campaign radios only, using
        // per-radio TransceiversCount/GetTransceiver iteration (safe native). The
        // former world-wide spatial scan used RadioManagerEntity.GetTransceiversInRange
        // (out array fill); on this engine build that native calls AV-crashes with an
        // illegal write regardless of input, so it must not be called here.
        array<BaseRadioComponent> radios = {};
        AddSourceRadios(station, faction, radios);

        int ri = 0;
        while (ri < radios.Count())
        {
            if (s_aRelayMesh.Count() >= GBRS_RadarStationConstants.INTEL_RELAY_MESH_MAX)
                break;

            BaseRadioComponent radio = radios.Get(ri);
            ri = ri + 1;
            if (!radio)
                continue;
            if (!radio.IsPowered())
                continue;
            if (visited.Contains(radio))
                continue;
            if (!RelayAcceptsFactionEncryption(radio, factionEncryption))
                continue;
            visited.Insert(radio);

            int tc = radio.TransceiversCount();
            int ti;
            for (ti = 0; ti < tc; ti++)
            {
                if (s_aRelayMesh.Count() >= GBRS_RadarStationConstants.INTEL_RELAY_MESH_MAX)
                    break;

                BaseTransceiver transceiver = radio.GetTransceiver(ti);
                if (!transceiver)
                    continue;

                RelayTransceiver relay = RelayTransceiver.Cast(transceiver);
                if (!relay)
                    continue;

                IEntity tsvOwner = radio.GetOwner();
                if (!tsvOwner || tsvOwner.IsDeleted())
                    continue;
                if (tsvOwner == owner)
                    continue;

                s_aRelayMesh.Insert(transceiver);
            }
        }
    }

    // Safe relay sources: the station's own covering campaign base (if any) plus
    // the station owner's override base relay. No world-wide spatial query.
    protected static void AddSourceRadios(
        GBRS_RadarStationComponent station,
        Faction faction,
        notnull array<BaseRadioComponent> radios)
    {
        BaseTransceiver hqRelay = GetFactionIntelRelayTransceiver(station, faction);
        if (hqRelay)
        {
            BaseRadioComponent r = hqRelay.GetRadio();
            if (r && !radios.Contains(r))
                radios.Insert(r);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected static bool RelayAcceptsFactionEncryption(
        notnull BaseRadioComponent radio,
        string factionEncryption)
    {
        if (factionEncryption.IsEmpty())
            return true;

        string radioKey = radio.GetEncryptionKey();
        if (radioKey.IsEmpty())
            return true;

        if (radioKey == factionEncryption)
            return true;

        return false;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool IsPositionNearRelayMesh(vector pos)
    {
        if (!s_aRelayMesh)
            return false;

        foreach (BaseTransceiver transceiver : s_aRelayMesh)
        {
            if (!transceiver)
                continue;

            BaseRadioComponent radio = transceiver.GetRadio();
            if (!radio)
                continue;

            IEntity owner = radio.GetOwner();
            if (!owner)
                continue;

            float rangeM = transceiver.GetRange();
            if (rangeM <= 0.0)
                continue;

            float distSq = vector.DistanceSqXZ(pos, owner.GetOrigin());
            if (distSq <= (rangeM * rangeM))
                return true;
        }

        return false;
    }

    //------------------------------------------------------------------------------------------------
    protected static float GetCampaignBaseRadioRangeM(notnull SCR_CampaignMilitaryBaseComponent campaignBase)
    {
        IEntity owner = campaignBase.GetOwner();
        if (!owner)
            return 0.0;

        BaseRadioComponent radio = BaseRadioComponent.Cast(owner.FindComponent(BaseRadioComponent));
        if (!radio)
            return 0.0;
        if (!radio.IsPowered())
            return 0.0;

        float rangeM = campaignBase.GetRadioRange();
        if (rangeM > 0.0)
            return rangeM;

        if (radio.TransceiversCount() <= 0)
            return 0.0;

        BaseTransceiver transceiver = radio.GetTransceiver(0);
        if (!transceiver)
            return 0.0;

        return transceiver.GetRange();
    }

    //------------------------------------------------------------------------------------------------
    protected static BaseTransceiver GetFactionIntelRelayTransceiver(
        GBRS_RadarStationComponent station,
        Faction faction)
    {
        SCR_CampaignFaction campaignFaction = SCR_CampaignFaction.Cast(faction);
        if (!campaignFaction)
            return null;

        BaseTransceiver hqRelay = GetBaseRelayTransceiver(campaignFaction.GetMainBase());
        if (hqRelay)
            return hqRelay;

        if (!station)
            return null;

        return GetBaseRelayTransceiver(station.GetCoveringCampaignBase(true));
    }

    //------------------------------------------------------------------------------------------------
    protected static BaseTransceiver GetBaseRelayTransceiver(SCR_CampaignMilitaryBaseComponent campaignBase)
    {
        if (!campaignBase)
            return null;

        IEntity owner = campaignBase.GetOwner();
        if (!owner)
            return null;

        BaseRadioComponent radio = BaseRadioComponent.Cast(owner.FindComponent(BaseRadioComponent));
        if (!radio)
            return null;
        if (!radio.IsPowered())
            return null;

        int count = radio.TransceiversCount();
        int i;
        for (i = 0; i < count; i++)
        {
            BaseTransceiver transceiver = radio.GetTransceiver(i);
            if (!transceiver)
                continue;

            RelayTransceiver relay = RelayTransceiver.Cast(transceiver);
            if (relay)
                return relay;
        }

        if (count <= 0)
            return null;

        return radio.GetTransceiver(0);
    }

    //------------------------------------------------------------------------------------------------
    protected static bool IsWithinLocalIntelRange(vector pos, vector stationPos)
    {
        float rangeM = GBRS_RadarStationConstants.INTEL_RADIO_RANGE_M;
        float distSq = vector.DistanceSq(pos, stationPos);
        return distSq <= (rangeM * rangeM);
    }

    //------------------------------------------------------------------------------------------------
    static bool IsPlayerEditorOpened(int playerId)
    {
        SCR_EditorManagerEntity editor = GetEditorManagerForPlayer(playerId);
        if (!editor)
            return false;

        return editor.IsOpened();
    }

    //------------------------------------------------------------------------------------------------
    //! Slotted Conflict faction, else the possessed / controlled entity.
    static Faction GetPlayerFactionForIntel(int playerId)
    {
        Faction slotted = SCR_FactionManager.SGetPlayerFaction(playerId);
        if (slotted)
            return slotted;

        IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
        if (!controlled)
            return null;

        return SCR_Faction.GetEntityFaction(controlled);
    }

    //------------------------------------------------------------------------------------------------
    static int ResolveListenerPlayerId(IEntity radioOwner)
    {
        IEntity owner = radioOwner;
        while (owner)
        {
            ChimeraCharacter player = ChimeraCharacter.Cast(owner);
            if (player)
            {
                int fromCharacter =
                    GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(player);
                if (fromCharacter > 0)
                    return fromCharacter;
            }

            SCR_EditorManagerEntity editor = SCR_EditorManagerEntity.Cast(owner);
            if (editor)
            {
                int fromEditor = editor.GetPlayerID();
                if (fromEditor > 0)
                    return fromEditor;
            }

            owner = owner.GetParent();
        }

        return 0;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool TryClaimListener(int playerId)
    {
        if (playerId <= 0)
            return false;

        if (!s_mNotifyStamp)
            s_mNotifyStamp = new map<int, int>();

        if (s_mNotifyStamp.Contains(playerId))
        {
            if (s_mNotifyStamp.Get(playerId) == s_iNotifyStamp)
                return false;
        }

        s_mNotifyStamp.Set(playerId, s_iNotifyStamp);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected static SCR_EditorManagerEntity GetEditorManagerForPlayer(int playerId)
    {
        SCR_EditorManagerCore core =
            SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
        if (core)
        {
            SCR_EditorManagerEntity fromMap = core.GetEditorManager(playerId);
            if (fromMap)
                return fromMap;
        }

        SCR_EditorManagerEntity localEditor = SCR_EditorManagerEntity.GetInstance();
        if (!localEditor)
            return null;
        if (localEditor.GetPlayerID() != playerId)
            return null;

        return localEditor;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool IsRadioEntityTunedToIntel(IEntity radioEnt, int freqKhz, string encryptionKey)
    {
        if (!radioEnt)
            return false;

        BaseRadioComponent radio = BaseRadioComponent.Cast(radioEnt.FindComponent(BaseRadioComponent));
        if (!radio)
            return false;

        int count = radio.TransceiversCount();
        int i;
        for (i = 0; i < count; i++)
        {
            BaseTransceiver transceiver = radio.GetTransceiver(i);
            if (!transceiver)
                continue;

            int tunedKhz = transceiver.GetFrequency();
            bool match = false;
            if (freqKhz > 0)
            {
                if (tunedKhz == freqKhz)
                    match = true;
            }
            else if (GBRS_RadarStationConstants.IsIntelFrequencyKhz(tunedKhz))
            {
                match = true;
            }

            if (!match)
                continue;

            if (!encryptionKey.IsEmpty())
            {
                if (radio.GetEncryptionKey() != encryptionKey)
                    radio.SetEncryptionKey(encryptionKey);
            }

            return true;
        }

        return false;
    }
}
