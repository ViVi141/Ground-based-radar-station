//------------------------------------------------------------------------------------------------
//! Last-line listen-only: if CHANNEL capture starts on the intel net, drop it.
modded class SCR_VoNComponent
{
    //------------------------------------------------------------------------------------------------
    override protected event void OnCapture(BaseTransceiver transmitter)
    {
        if (transmitter)
        {
            if (GBRS_RadarStationConstants.IsIntelFrequencyKhz(transmitter.GetFrequency()))
            {
                SetCapture(false);
                return;
            }
        }

        super.OnCapture(transmitter);
    }
}
