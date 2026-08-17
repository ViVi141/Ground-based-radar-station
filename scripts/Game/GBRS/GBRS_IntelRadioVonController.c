//------------------------------------------------------------------------------------------------
//! Refuse CHANNEL / long-range PTT on the locked GBRS intel frequencies.
//! Direct speech is unchanged.
modded class SCR_VONController
{
    //------------------------------------------------------------------------------------------------
    override protected bool ActivateVON(notnull SCR_VONEntry entry, EVONTransmitType transmitType = EVONTransmitType.NONE)
    {
        if (transmitType != EVONTransmitType.DIRECT)
        {
            if (GBRS_IntelRadioNet.IsListenOnlyEntry(entry))
            {
                SCR_VonDisplay display = GetDisplay();
                if (display)
                    display.ShowSelectedVONDisabledHint();
                SCR_UISoundEntity.SoundEvent(SCR_SoundEvent.SOUND_RADIO_CHANGEFREQUENCY_ERROR);
                return false;
            }
        }

        return super.ActivateVON(entry, transmitType);
    }

    //------------------------------------------------------------------------------------------------
    override void Update(float timeSlice)
    {
        super.Update(timeSlice);

        if (!m_bIsActive)
            return;
        if (m_eVONType == EVONTransmitType.DIRECT)
            return;
        if (!GBRS_IntelRadioNet.IsListenOnlyEntry(GetActiveEntry()))
            return;

        DeactivateVON(m_eVONType);
    }
}
