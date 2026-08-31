// Opens the GBRS world console operate session (PPI / CONTACT / OPTICS CRTs).
class GBRS_RadarStationOpenPpiUserAction : ScriptedUserAction
{
    [Attribute("Operate Console", UIWidgets.EditBox, "Shown when the radar is powered", "")]
    protected LocalizedString m_sOpenPpiName;

    [Attribute("Radar must be powered on", UIWidgets.EditBox, "Shown when the radar is off", "")]
    protected LocalizedString m_sNotPoweredReason;

    protected GBRS_RadarStationComponent m_RadarStation;

    override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
    {
        m_RadarStation = FindRadarStation(pOwnerEntity);
    }

    protected GBRS_RadarStationComponent FindRadarStation(IEntity fromEntity)
    {
        if (!fromEntity)
            return null;

        GBRS_RadarStationComponent onSelf =
            GBRS_RadarStationComponent.Cast(fromEntity.FindComponent(GBRS_RadarStationComponent));
        if (onSelf)
            return onSelf;

        GBRS_ConsoleComponent console =
            GBRS_ConsoleComponent.Cast(fromEntity.FindComponent(GBRS_ConsoleComponent));
        if (console)
        {
            GBRS_RadarStationComponent fromConsole = console.FindRadarStation();
            if (fromConsole)
                return fromConsole;
        }

        IEntity parent = fromEntity.GetParent();
        while (parent)
        {
            GBRS_RadarStationComponent onParent =
                GBRS_RadarStationComponent.Cast(parent.FindComponent(GBRS_RadarStationComponent));
            if (onParent)
                return onParent;

            parent = parent.GetParent();
        }

        return null;
    }

    override bool CanBeShownScript(IEntity user)
    {
        if (!m_RadarStation)
            m_RadarStation = FindRadarStation(GetOwner());

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

        if (GBRS_ConsoleSession.IsActiveFor(m_RadarStation))
            return false;

        return true;
    }

    override bool CanBePerformedScript(IEntity user)
    {
        if (!m_RadarStation)
            return false;

        if (m_RadarStation.IsDestroyed())
            return false;

        if (!m_RadarStation.IsCompositionReady())
            return false;

        if (!m_RadarStation.IsFriendlyUser(user))
            return false;

        return m_RadarStation.IsPowered();
    }

    override string GetCannotPerformReason()
    {
        if (m_RadarStation && !m_RadarStation.IsCompositionReady())
            return "Finish building first";

        return m_sNotPoweredReason;
    }

    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        if (!m_RadarStation)
            m_RadarStation = FindRadarStation(pOwnerEntity);

        if (!m_RadarStation)
            return;

        if (!m_RadarStation.IsPowered())
            return;

        if (!m_RadarStation.IsFriendlyUser(pUserEntity))
            return;

        GBRS_ConsoleSession.StartFor(m_RadarStation, pUserEntity);
    }

    override bool GetActionNameScript(out string outName)
    {
        outName = m_sOpenPpiName;
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
