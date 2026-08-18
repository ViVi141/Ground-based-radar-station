//------------------------------------------------------------------------------------------------
//! Scripted traffic on the GBRS locked intel net. Delivery only reaches
//! powered radios tuned to the same kHz with the matching faction key.
//! OnDelivery is the real RF gate. NotifyListeners covers Game Master
//! handsets that never receive a keyed ScriptedRadioMessage.
class GBRS_IntelRadioMsg : ScriptedRadioMessage
{
    protected string m_sTitle;
    protected string m_sSubtitle;
    protected string m_sFactionKey;
    protected int m_iVoiceKind;
    protected int m_iGridPacked;
    protected int m_iParamA;
    protected int m_iParamB;
    protected bool m_bInterrupt;

    //------------------------------------------------------------------------------------------------
    void SetIntelText(string title, string subtitle)
    {
        m_sTitle = title;
        m_sSubtitle = subtitle;
    }

    //------------------------------------------------------------------------------------------------
    void SetFactionKey(string factionKey)
    {
        m_sFactionKey = factionKey;
    }

    //------------------------------------------------------------------------------------------------
    void SetVoiceKind(int voiceKind)
    {
        m_iVoiceKind = voiceKind;
    }

    //------------------------------------------------------------------------------------------------
    void SetGridPacked(int gridPacked)
    {
        m_iGridPacked = gridPacked;
    }

    //------------------------------------------------------------------------------------------------
    void SetVoiceParams(int paramA, int paramB)
    {
        m_iParamA = paramA;
        m_iParamB = paramB;
    }

    //------------------------------------------------------------------------------------------------
    void SetInterrupt(bool interrupt)
    {
        m_bInterrupt = interrupt;
    }

    //------------------------------------------------------------------------------------------------
    override void OnDelivery(BaseTransceiver receiver, int freq, float quality)
    {
        if (!receiver)
            return;
        if (!GBRS_RadarStationConstants.IsIntelFrequencyKhz(freq))
            return;

        BaseRadioComponent radio = receiver.GetRadio();
        if (!radio)
            return;

        IEntity owner = radio.GetOwner();
        if (!owner)
            return;

        GBRS_RadarStationComponent station =
            GBRS_RadarStationComponent.Cast(owner.FindComponent(GBRS_RadarStationComponent));
        if (station)
            return;

        int playerId = GBRS_IntelRadioNet.ResolveListenerPlayerId(owner);
        if (playerId <= 0)
            return;

        if (!m_sFactionKey.IsEmpty())
        {
            Faction playerFaction = GBRS_IntelRadioNet.GetPlayerFactionForIntel(playerId);
            if (playerFaction)
            {
                if (playerFaction.GetFactionKey() != m_sFactionKey)
                    return;
            }
        }

        float clampedQuality = quality;
        if (clampedQuality < 0.05)
            clampedQuality = 0.05;
        if (clampedQuality > 1.0)
            clampedQuality = 1.0;

        GBRS_IntelRadioNet.DeliverToPlayer(
            playerId,
            m_sTitle,
            m_sSubtitle,
            m_iVoiceKind,
            m_iGridPacked,
            m_iParamA,
            m_iParamB,
            clampedQuality,
            m_sFactionKey,
            m_bInterrupt);
    }
}
