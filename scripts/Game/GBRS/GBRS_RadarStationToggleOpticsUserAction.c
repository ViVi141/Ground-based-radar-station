//------------------------------------------------------------------------------------------------
//! Toggle OPTICS CRT (default off) while operating the console.
class GBRS_RadarStationToggleOpticsUserAction : ScriptedUserAction
{
    [Attribute("Toggle Optics", UIWidgets.EditBox, "Shown when optics can be toggled", "")]
    protected LocalizedString m_sActionName;

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

    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        GBRS_ConsoleSession session = GBRS_ConsoleSession.GetActive();
        if (!session)
            return;
        session.ToggleOpticsFromAction();
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
