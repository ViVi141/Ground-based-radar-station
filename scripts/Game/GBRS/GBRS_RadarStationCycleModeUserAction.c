//------------------------------------------------------------------------------------------------
//! Cycle PD SEARCH → WLR → LOCK → MANUAL on the world console.
class GBRS_RadarStationCycleModeUserAction : ScriptedUserAction
{
    [Attribute("Cycle Mode", UIWidgets.EditBox, "Shown when the console can change mode", "")]
    protected LocalizedString m_sActionName;

    [Attribute("Radar must be powered on", UIWidgets.EditBox, "Shown when the radar is off", "")]
    protected LocalizedString m_sNotPoweredReason;

    protected GBRS_RadarStationComponent m_RadarStation;

    override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
    {
        GBRS_ConsoleComponent console =
            GBRS_ConsoleComponent.Cast(pOwnerEntity.FindComponent(GBRS_ConsoleComponent));
        if (console)
            m_RadarStation = console.FindRadarStation();
        else
            m_RadarStation = null;
    }

    override bool CanBeShownScript(IEntity user)
    {
        if (!m_RadarStation)
        {
            GBRS_ConsoleComponent console =
                GBRS_ConsoleComponent.Cast(GetOwner().FindComponent(GBRS_ConsoleComponent));
            if (console)
                m_RadarStation = console.FindRadarStation();
        }
        if (!m_RadarStation)
            return false;
        if (!m_RadarStation.IsConfigured())
            return false;
        if (m_RadarStation.IsDestroyed())
            return false;
        if (!m_RadarStation.IsCompositionReady())
            return false;
        if (!m_RadarStation.IsPowered())
            return false;
        if (!m_RadarStation.IsFriendlyUser(user))
            return false;
        if (!GBRS_ConsoleSession.IsActiveFor(m_RadarStation))
            return false;
        return true;
    }

    override bool CanBePerformedScript(IEntity user)
    {
        return CanBeShownScript(user);
    }

    override string GetCannotPerformReason()
    {
        return m_sNotPoweredReason;
    }

    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        GBRS_ConsoleSession session = GBRS_ConsoleSession.GetActive();
        if (!session)
            return;
        session.CycleModeFromAction();
    }

    override bool GetActionNameScript(out string outName)
    {
        outName = m_sActionName;
        return true;
    }

    override bool HasLocalEffectOnlyScript()
    {
        return true;
    }

    override bool CanBroadcastScript()
    {
        return false;
    }
}
