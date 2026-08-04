// GBRS Conflict build safety (minimal radar-only workarounds).
// Official Floodlight structure is in place, but SpawnPreview /
// SpawnAndApplyReference / unfinished Disassembly still native-AV on the
// radar EditorLink tree. Per plan contingency: keep only these scoped skips.
modded class SCR_CampaignBuildingLayoutComponent
{
    protected IEntity m_GBRS_LayoutPendingDelete;

    //------------------------------------------------------------------------------------------------
    protected IEntity GBRS_ResolveRadarRoot(IEntity fromEntity)
    {
        IEntity cursor = fromEntity;
        while (cursor)
        {
            if (cursor.FindComponent(GBRS_RadarStationComponent))
                return cursor;

            cursor = cursor.GetParent();
        }

        return null;
    }

    //------------------------------------------------------------------------------------------------
    protected bool GBRS_IsRadarLayout()
    {
        return GBRS_ResolveRadarRoot(GetOwner()) != null;
    }

    //------------------------------------------------------------------------------------------------
    // Shovel gadget repeatedly calls SpawnPreview when entering range. Loading
    // the full E_ radar prefab as SCR_PrefabPreviewEntity still crashes natively.
    override void SpawnPreview()
    {
        if (GBRS_IsRadarLayout())
            return;

        super.SpawnPreview();
    }

    //------------------------------------------------------------------------------------------------
    // CanBeShown for build action requires HasBuildingPreview(). Without a real
    // preview entity, still allow the shovel build prompt on GBRS pads.
    override bool HasBuildingPreview()
    {
        if (GBRS_IsRadarLayout())
        {
            if (m_iToBuildValue > 0 && m_fCurrentBuildValue >= m_iToBuildValue)
                return false;

            IEntity root = GBRS_ResolveRadarRoot(GetOwner());
            if (root)
            {
                GBRS_RadarStationComponent station =
                    GBRS_RadarStationComponent.Cast(root.FindComponent(GBRS_RadarStationComponent));
                if (station && station.IsCompositionReady())
                    return false;
            }

            return true;
        }

        return super.HasBuildingPreview();
    }

    //------------------------------------------------------------------------------------------------
    override void SpawnComposition()
    {
        if (!GBRS_IsRadarLayout())
        {
            super.SpawnComposition();
            return;
        }

        IEntity layoutOwner = GetOwner();
        IEntity root = GBRS_ResolveRadarRoot(layoutOwner);

        if (root)
        {
            SCR_EditorLinkComponent linkComponent =
                SCR_EditorLinkComponent.Cast(root.FindComponent(SCR_EditorLinkComponent));
            if (linkComponent)
                linkComponent.SpawnComposition();
        }

        LockCompositionInteraction();

        // Same-frame DeleteRplEntity right after EditorLink.SpawnComposition can
        // leave the FRB poles / build actions alive. Delete on the next frame.
        m_GBRS_LayoutPendingDelete = layoutOwner;
        GetGame().GetCallqueue().CallLater(GBRS_DeleteLayoutNow, 1, false);
    }

    //------------------------------------------------------------------------------------------------
    protected void GBRS_DeleteLayoutNow()
    {
        IEntity layoutOwner = m_GBRS_LayoutPendingDelete;
        m_GBRS_LayoutPendingDelete = null;

        if (!layoutOwner)
            layoutOwner = GetOwner();

        if (!layoutOwner)
            return;

        IEntity root = GBRS_ResolveRadarRoot(layoutOwner);

        // Belt-and-suspenders: strip interaction even if Rpl delete is delayed.
        ActionsManagerComponent actions =
            ActionsManagerComponent.Cast(layoutOwner.FindComponent(ActionsManagerComponent));
        if (actions)
            actions.Deactivate(layoutOwner);

        SCR_EntityHelper.DeleteEntityAndChildren(layoutOwner);

        // If a stray layout child remains under the radar root, clear it too.
        if (!root)
            return;

        IEntity child = root.GetChildren();
        while (child)
        {
            IEntity next = child.GetSibling();
            SCR_CampaignBuildingLayoutComponent leftover =
                SCR_CampaignBuildingLayoutComponent.Cast(child.FindComponent(SCR_CampaignBuildingLayoutComponent));
            if (leftover)
                SCR_EntityHelper.DeleteEntityAndChildren(child);

            child = next;
        }
    }
}

//------------------------------------------------------------------------------------------------
modded class SCR_CampaignBuildingBuildUserAction
{
    //------------------------------------------------------------------------------------------------
    protected bool GBRS_IsRadarBuildComplete()
    {
        IEntity owner = GetOwner();
        if (!owner)
            return false;

        IEntity root = owner.GetParent();
        while (root && !root.FindComponent(GBRS_RadarStationComponent))
            root = root.GetParent();

        if (!root)
            return false;

        GBRS_RadarStationComponent station =
            GBRS_RadarStationComponent.Cast(root.FindComponent(GBRS_RadarStationComponent));
        if (station && station.IsCompositionReady())
            return true;

        if (!m_LayoutComponent)
            return false;

        int requiredValue = m_LayoutComponent.GetToBuildValue();
        if (requiredValue > 0)
            return m_LayoutComponent.GetCurrentBuildValue() >= requiredValue;

        return false;
    }

    //------------------------------------------------------------------------------------------------
    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        if (GBRS_IsRadarBuildComplete())
            return;

        super.PerformAction(pOwnerEntity, pUserEntity);
    }

    //------------------------------------------------------------------------------------------------
    override bool CanBeShownScript(IEntity user)
    {
        if (GBRS_IsRadarBuildComplete())
            return false;

        return super.CanBeShownScript(user);
    }

    //------------------------------------------------------------------------------------------------
    // Vanilla CanBePerformedScript casts user then uses it without a null check.
    override bool CanBePerformedScript(IEntity user)
    {
        if (GBRS_IsRadarBuildComplete())
            return false;

        ChimeraCharacter character = ChimeraCharacter.Cast(user);
        if (!character)
            return false;

        return super.CanBePerformedScript(user);
    }
}

//------------------------------------------------------------------------------------------------
// Unfinished radar pads still trip native AV in IsHQService() -> GetInfo(owner).
modded class SCR_CampaignBuildingDisassemblyUserAction
{
    //------------------------------------------------------------------------------------------------
    override bool CanBeShownScript(IEntity user)
    {
        IEntity owner = GetOwner();
        if (owner)
        {
            IEntity root = owner.GetParent();
            while (root)
            {
                if (root.FindComponent(GBRS_RadarStationComponent))
                    return false;

                root = root.GetParent();
            }
        }

        return super.CanBeShownScript(user);
    }
}
