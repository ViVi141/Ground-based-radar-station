//------------------------------------------------------------------------------------------------
//! Station transmitter + listen-only helpers for the locked GBRS intel net.
//! Players keep vanilla handsets; this only parks the radar radio on a
//! faction frequency and refuses CHANNEL PTT while tuned there.
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
        int gridPacked)
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
        msg.SetEncryptionKey(radio.GetEncryptionKey());
        transceiver.BeginTransmissionFreq(msg, freqKhz);
        return true;
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
    static bool IsPlayerTunedToIntel(int playerId)
    {
        IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
        if (!controlled)
            return false;

        SCR_GadgetManagerComponent gadgets =
            SCR_GadgetManagerComponent.Cast(controlled.FindComponent(SCR_GadgetManagerComponent));
        if (!gadgets)
            return false;

        if (IsRadioEntityTunedToIntel(gadgets.GetGadgetByType(EGadgetType.RADIO)))
            return true;
        if (IsRadioEntityTunedToIntel(gadgets.GetGadgetByType(EGadgetType.RADIO_BACKPACK)))
            return true;

        return false;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool IsRadioEntityTunedToIntel(IEntity radioEnt)
    {
        if (!radioEnt)
            return false;

        BaseRadioComponent radio = BaseRadioComponent.Cast(radioEnt.FindComponent(BaseRadioComponent));
        if (!radio)
            return false;
        if (!radio.IsPowered())
            return false;

        int count = radio.TransceiversCount();
        int i;
        for (i = 0; i < count; i++)
        {
            BaseTransceiver transceiver = radio.GetTransceiver(i);
            if (!transceiver)
                continue;
            if (GBRS_RadarStationConstants.IsIntelFrequencyKhz(transceiver.GetFrequency()))
                return true;
        }

        return false;
    }
}
