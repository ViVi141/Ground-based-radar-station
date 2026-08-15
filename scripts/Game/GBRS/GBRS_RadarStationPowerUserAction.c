// Interactive power toggle for GBRS radar stations.
// Server-first: clients only submit a toggle ask; authority gates supplies and
// broadcasts the confirmed powered state.
class GBRS_RadarStationPowerUserAction : ScriptedUserAction
{
    [Attribute("#AR-UserAction_TurnOn", UIWidgets.EditBox, "Shown when radar is off", "")]
    protected LocalizedString m_sTurnOnName;

    [Attribute("#AR-UserAction_TurnOff", UIWidgets.EditBox, "Shown when radar is on", "")]
    protected LocalizedString m_sTurnOffName;

    [Attribute("#GBRS_STR_NotEnoughSupplies", UIWidgets.EditBox, "Shown when power-on is blocked by supplies", "")]
    protected LocalizedString m_sCannotAffordReason;

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

        if (m_RadarStation.IsPowered())
            return true;

        return m_RadarStation.CanAffordPowerOn();
    }

    override string GetCannotPerformReason()
    {
        if (m_RadarStation && m_RadarStation.IsDestroyed())
            return "#GBRS_STR_StationDestroyed";

        if (m_RadarStation && !m_RadarStation.IsCompositionReady())
            return "#GBRS_STR_FinishBuildingFirst";

        return m_sCannotAffordReason;
    }

    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        if (!m_RadarStation)
            m_RadarStation = FindRadarStation(pOwnerEntity);

        if (!m_RadarStation)
            return;

        m_RadarStation.RequestTogglePower();
    }

    override bool HasLocalEffectOnlyScript()
    {
        return true;
    }

    override bool CanBroadcastScript()
    {
        return false;
    }

    override bool GetActionNameScript(out string outName)
    {
        if (!m_RadarStation)
            return false;

        if (m_RadarStation.IsPowered())
            outName = m_sTurnOffName;
        else
            outName = m_sTurnOnName;

        return true;
    }
}
