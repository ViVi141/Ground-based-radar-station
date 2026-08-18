//------------------------------------------------------------------------------------------------
//! Scripted traffic on the GBRS locked intel net. Delivery only reaches
//! powered radios tuned to the same kHz with the matching faction key.
//! Game Master editor radios are also accepted so a later freq match still
//! plays; open editors normally arrive via NotifyOpenEditors instead.
class GBRS_IntelRadioMsg : ScriptedRadioMessage
{
    protected string m_sTitle;
    protected string m_sSubtitle;
    protected int m_iVoiceKind;
    protected int m_iGridPacked;

    protected int m_iParamA;
    protected int m_iParamB;

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
    void SetVoiceParams(int paramA, int paramB)
    {
        m_iParamA = paramA;
        m_iParamB = paramB;
    }

    //------------------------------------------------------------------------------------------------
    override void OnDelivery(BaseTransceiver receiver, int freq, float quality)
    {
        // Direct NotifyListeners is the Game Master / listen-server path.
        // RF OnDelivery used to be the only client cue, but GM handsets do
        // not carry Conflict encryption and never received the message.
        return;
    }
}
