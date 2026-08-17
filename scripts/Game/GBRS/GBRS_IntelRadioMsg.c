//------------------------------------------------------------------------------------------------
//! Scripted traffic on the GBRS locked intel net. Delivery only reaches
//! powered radios tuned to the same kHz with the matching faction key.
class GBRS_IntelRadioMsg : ScriptedRadioMessage
{
    protected string m_sTitle;
    protected string m_sSubtitle;
    protected int m_iVoiceKind;
    protected int m_iGridPacked;

    //------------------------------------------------------------------------------------------------
    void SetIntelText(string title, string subtitle)
    {
        m_sTitle = title;
        m_sSubtitle = subtitle;
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
        ChimeraCharacter player;
        while (!player)
        {
            player = ChimeraCharacter.Cast(owner);
            if (player)
                break;

            if (!owner)
                return;

            owner = owner.GetParent();
            if (!owner)
                return;
        }

        PlayerManager playerManager = GetGame().GetPlayerManager();
        if (!playerManager)
            return;

        int playerId = playerManager.GetPlayerIdFromControlledEntity(player);
        if (playerId <= 0)
            return;

        PlayerController controller = playerManager.GetPlayerController(playerId);
        if (!controller)
            return;

        SCR_PlayerController gbrsController = SCR_PlayerController.Cast(controller);
        if (!gbrsController)
            return;

        gbrsController.GBRS_NotifyIntelRadio(m_sTitle, m_sSubtitle, m_iVoiceKind, m_iGridPacked, quality);
    }
}
