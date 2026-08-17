//------------------------------------------------------------------------------------------------
//! Show RADAR NET instead of a raw MHz label on the locked intel frequencies.
modded class SCR_VONMenu
{
    //------------------------------------------------------------------------------------------------
    override static string GetKnownChannel(int frequency)
    {
        string intelName = GBRS_RadarStationConstants.GetIntelChannelName(frequency);
        if (!intelName.IsEmpty())
            return intelName;

        return super.GetKnownChannel(frequency);
    }
}

//------------------------------------------------------------------------------------------------
modded class SCR_VONEntryRadio
{
    //------------------------------------------------------------------------------------------------
    override void InitEntry()
    {
        super.InitEntry();
        ApplyGbrsIntelChannelName();
    }

    //------------------------------------------------------------------------------------------------
    override void AdjustEntryModif(int modifier)
    {
        super.AdjustEntryModif(modifier);
        ApplyGbrsIntelChannelName();
    }

    //------------------------------------------------------------------------------------------------
    protected void ApplyGbrsIntelChannelName()
    {
        string intelName = GBRS_RadarStationConstants.GetIntelChannelName(m_iFrequency);
        if (intelName.IsEmpty())
            return;

        SetChannelText(intelName);
    }
}
