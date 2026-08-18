//------------------------------------------------------------------------------------------------
//! Station transmitter + listen-only helpers for the locked GBRS intel net.
//! Players keep vanilla handsets; this only parks the radar radio on a
//! faction frequency and refuses CHANNEL PTT while tuned there.
//!
//! Game Master editor radios sit on GM / platoon freqs, not RADAR NET, and
//! share one encryption key — ScriptedRadioMessage cannot reach them. Open
//! editors therefore get the same HQ voice + popup over PlayerController.
class GBRS_IntelRadioNet
{
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
        int paramB)
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

        GBRS_IntelRadioMsg msg = new GBRS_IntelRadioMsg();
        msg.SetIntelText(title, subtitle);
        msg.SetVoiceKind(voiceKind);
        msg.SetGridPacked(gridPacked);
        msg.SetVoiceParams(paramA, paramB);
        string encryptionKey = radio.GetEncryptionKey();
        msg.SetEncryptionKey(encryptionKey);
        transceiver.BeginTransmissionFreq(msg, freqKhz);

        // Game Master handsets never get Conflict's SetEncryptionKey. A second
        // unkeyed copy reaches those radios if the RadioManager is present.
        if (!encryptionKey.IsEmpty())
        {
            GBRS_IntelRadioMsg openMsg = new GBRS_IntelRadioMsg();
            openMsg.SetIntelText(title, subtitle);
            openMsg.SetVoiceKind(voiceKind);
            openMsg.SetGridPacked(gridPacked);
            openMsg.SetVoiceParams(paramA, paramB);
            openMsg.SetEncryptionKey("");
            transceiver.BeginTransmissionFreq(openMsg, freqKhz);
        }

        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Direct delivery. ScriptedRadioMessage is not enough in Game Master:
    //! editor VON is on GM channels, handsets have empty encryption, and
    //! listen-server Owner RPCs may never run locally.
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

        string factionKey = faction.GetFactionRadioEncryptionKey();

        array<int> playerIds = {};
        playerManager.GetPlayers(playerIds);

        foreach (int playerId : playerIds)
        {
            SCR_PlayerController playerController =
                SCR_PlayerController.Cast(playerManager.GetPlayerController(playerId));
            if (!playerController)
                continue;

            if (IsPlayerEditorOpened(playerId))
            {
                playerController.GBRS_NotifyIntelRadio(
                    title, subtitle, voiceKind, gridPacked, paramA, paramB, 1.0, faction.GetFactionKey(), interrupt);
                continue;
            }

            bool tuned = IsPlayerTunedToIntel(playerId, stationFreqKhz, factionKey);
            Faction playerFaction = GetPlayerFactionForIntel(playerId);
            bool sameFaction = false;
            if (playerFaction && playerFaction == faction)
                sameFaction = true;

            // GM-placed soldiers often have no slotted faction. If they
            // actually tuned this net, still deliver.
            if (!sameFaction)
            {
                if (!tuned)
                    continue;
                if (playerFaction)
                    continue;
            }

            if (!IsPlayerInIntelRange(playerId, stationPos))
                continue;

            if (tuned)
            {
                playerController.GBRS_NotifyIntelRadio(
                    title, subtitle, voiceKind, gridPacked, paramA, paramB, 1.0, faction.GetFactionKey(), interrupt);
                continue;
            }

            playerController.GBRS_NotifyRadarWarning(title, subtitle);
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
        if (IsEditorRadioTunedToIntel(playerId, freqKhz, encryptionKey))
            return true;

        IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
        if (!controlled)
            return false;

        SCR_GadgetManagerComponent gadgets =
            SCR_GadgetManagerComponent.Cast(controlled.FindComponent(SCR_GadgetManagerComponent));
        if (!gadgets)
            return false;

        if (IsRadioEntityTunedToIntel(gadgets.GetGadgetByType(EGadgetType.RADIO), freqKhz, encryptionKey))
            return true;
        if (IsRadioEntityTunedToIntel(gadgets.GetGadgetByType(EGadgetType.RADIO_BACKPACK), freqKhz, encryptionKey))
            return true;

        return false;
    }

    //------------------------------------------------------------------------------------------------
    static bool IsPlayerInIntelRange(int playerId, vector stationPos)
    {
        if (stationPos == vector.Zero)
            return true;

        IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
        if (!controlled)
            return true;

        float rangeM = GBRS_RadarStationConstants.INTEL_RADIO_RANGE_M;
        float distSq = vector.DistanceSq(controlled.GetOrigin(), stationPos);
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
    protected static bool IsEditorRadioTunedToIntel(int playerId, int freqKhz, string encryptionKey)
    {
        SCR_EditorManagerEntity editor = GetEditorManagerForPlayer(playerId);
        if (!editor)
            return false;
        if (!editor.IsOpened())
            return false;

        return IsRadioEntityTunedToIntel(editor, freqKhz, encryptionKey);
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
