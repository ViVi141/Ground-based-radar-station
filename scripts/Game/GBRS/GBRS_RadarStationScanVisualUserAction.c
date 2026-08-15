// Toggles RDF world-space scan visualization for a powered GBRS radar station.
// Same broadcast UserAction pattern as SCR_SwitchLightUserAction.
class GBRS_RadarStationScanVisualUserAction : ScriptedUserAction
{
    [Attribute("Enable Scan Frustum", UIWidgets.EditBox, "Shown when scan visuals are off", "")]
    protected LocalizedString m_sEnableName;

    [Attribute("Disable Scan Frustum", UIWidgets.EditBox, "Shown when scan visuals are on", "")]
    protected LocalizedString m_sDisableName;

    [Attribute("#GBRS_STR_NotPowered", UIWidgets.EditBox, "Shown when the radar is off", "")]
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

        return m_RadarStation.IsPowered();
    }

    override bool CanBePerformedScript(IEntity user)
    {
        if (!m_RadarStation)
            return false;

        if (m_RadarStation.IsDestroyed())
            return false;

        if (!m_RadarStation.IsCompositionReady())
            return false;

        return m_RadarStation.IsPowered();
    }

    override string GetCannotPerformReason()
    {
        if (m_RadarStation && !m_RadarStation.IsCompositionReady())
            return "#GBRS_STR_FinishBuildingFirst";

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

        m_RadarStation.ToggleScanVisual(!m_RadarStation.IsScanVisualEnabled());
    }

    override bool GetActionNameScript(out string outName)
    {
        if (!m_RadarStation)
            return false;

        if (m_RadarStation.IsScanVisualEnabled())
            outName = m_sDisableName;
        else
            outName = m_sEnableName;

        return true;
    }
}
