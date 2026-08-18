//------------------------------------------------------------------------------------------------
//! Station transmitter + listen-only helpers for the locked GBRS intel net.
//! Players keep vanilla handsets; this only parks the radar radio on a
//! faction frequency and refuses CHANNEL PTT while tuned there.
//! Contact traffic is radio-only: handhelds must be on RADAR NET.
//! RF OnDelivery is the real gate; NotifyListeners is the Game Master fallback.
class GBRS_IntelRadioNet
{
    protected static int s_iNotifyStamp;
    protected static ref map<int, int> s_mNotifyStamp;

    //------------------------------------------------------------------------------------------------
    static void BeginIntelTxBatch()
    {
        s_iNotifyStamp = s_iNotifyStamp + 1;
        if (s_iNotifyStamp > 1000000)
            s_iNotifyStamp = 1;

        if (!s_mNotifyStamp)
            s_mNotifyStamp = new map<int, int>();
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

        GBRS_IntelRadioMsg msg = new GBRS_IntelRadioMsg();
        FillIntelMsg(msg, title, subtitle, voiceKind, gridPacked, paramA, paramB, interrupt, factionKey);
        string encryptionKey = radio.GetEncryptionKey();
        msg.SetEncryptionKey(encryptionKey);
        transceiver.BeginTransmissionFreq(msg, freqKhz);

        // Game Master handsets never get Conflict's SetEncryptionKey. A second
        // unkeyed copy reaches those radios if the RadioManager is present.
        if (!encryptionKey.IsEmpty())
        {
            GBRS_IntelRadioMsg openMsg = new GBRS_IntelRadioMsg();
            FillIntelMsg(openMsg, title, subtitle, voiceKind, gridPacked, paramA, paramB, interrupt, factionKey);
            openMsg.SetEncryptionKey("");
            transceiver.BeginTransmissionFreq(openMsg, freqKhz);
        }

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

            if (!IsPlayerInIntelRange(playerId, stationPos))
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
            + " (" + mhz + "). Air: grid heading altitude. WLR: launch impact ETA. Console TX NET rebroadcasts.";

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
