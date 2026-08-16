// Ensure GBRS radar compositions stay in Conflict / FreeRoam placeable lists
// even when a mission still points BuildingManager at the vanilla
// Compositions_FreeRoamBuilding.conf (RDF can run on a world-placed station
// without ever touching that registry).
//
// Deployment policy (forward OK): keep THEME_MILITARY on E_ radar so
// construction trucks can place outside bases. Cost — not trait bans —
// constrains that: CAMPAIGN 450 (~truck cargo), build value 160, and
// powered drain from the station's local supply bunker (truck unloads
// into Generator_Store, then may leave).
modded class SCR_CampaignBuildingManagerComponent
{
    //------------------------------------------------------------------------------------------------
    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        GBRS_EnsureRadarPlaceablesRegistered();
    }

    //------------------------------------------------------------------------------------------------
    protected void GBRS_EnsureRadarPlaceablesRegistered()
    {
        if (!m_aPlaceablePrefabs)
            m_aPlaceablePrefabs = {};

        GBRS_InsertPlaceableIfMissing(GBRS_RadarStationConstants.PREFAB_E_US);
        GBRS_InsertPlaceableIfMissing(GBRS_RadarStationConstants.PREFAB_E_USSR);
    }

    //------------------------------------------------------------------------------------------------
    protected void GBRS_InsertPlaceableIfMissing(ResourceName prefab)
    {
        if (prefab.IsEmpty())
            return;

        if (m_aPlaceablePrefabs.Find(prefab) >= 0)
            return;

        m_aPlaceablePrefabs.Insert(prefab);
    }
}

// Outline table on GameMode may omit radars when PrefabsToBuild was patched
// without also extending m_aCompositionLayouts. Fall back to the GBRS FRB pad.
modded class SCR_CampaignBuildingCompositionOutlineManager
{
    //------------------------------------------------------------------------------------------------
    override ResourceName GetCompositionOutline(notnull SCR_EditableEntityComponent entity)
    {
        ResourceName radarLayout = GBRS_FindRadarLayout(entity);
        if (!radarLayout.IsEmpty())
            return radarLayout;

        return super.GetCompositionOutline(entity);
    }

    //------------------------------------------------------------------------------------------------
    override int GetCompositionBuildingValue(ResourceName originalComposition)
    {
        if (GBRS_RadarStationConstants.IsRadarPrefab(originalComposition))
            return GBRS_RadarStationConstants.BUILDING_VALUE;

        return super.GetCompositionBuildingValue(originalComposition);
    }

    //------------------------------------------------------------------------------------------------
    protected ResourceName GBRS_FindRadarLayout(notnull SCR_EditableEntityComponent entity)
    {
        ResourceName compositionResourceName = entity.GetPrefab();
        if (!GBRS_RadarStationConstants.IsRadarPrefab(compositionResourceName))
        {
            IEntity composition = entity.GetOwner();
            if (composition && composition.GetPrefabData())
                compositionResourceName = composition.GetPrefabData().GetPrefabName();
        }

        if (!GBRS_RadarStationConstants.IsRadarPrefab(compositionResourceName))
            return ResourceName.Empty;

        if (m_aCompositionLayouts)
        {
            foreach (SCR_CampaignBuildingCompositionOutline compositionLayout : m_aCompositionLayouts)
            {
                if (!compositionLayout)
                    continue;

                if (compositionLayout.GetEditableEntity() == compositionResourceName)
                    return compositionLayout.GetCompositionLayout();
            }
        }

        return GBRS_RadarStationConstants.PREFAB_FRB_LAYOUT;
    }
}


// Editor BUILDING / some GameMode configurations do not provide an outline
// manager. Ensure GBRS radar compositions still get their FreeRoamBuilding pad
// instead of the SlotFlatSmall failsafe (or an empty outline).
modded class SCR_CampaignBuildingCompositionComponent
{
    override ResourceName GetOutlineToSpawn(notnull SCR_EditableEntityComponent entity)
    {
        ResourceName prefab = entity.GetPrefab();
        if (GBRS_RadarStationConstants.IsRadarPrefab(prefab))
            return GBRS_RadarStationConstants.PREFAB_FRB_LAYOUT;

        IEntity owner = entity.GetOwner();
        if (owner && owner.GetPrefabData())
        {
            ResourceName spawnedPrefab = owner.GetPrefabData().GetPrefabName();
            if (GBRS_RadarStationConstants.IsRadarPrefab(spawnedPrefab))
                return GBRS_RadarStationConstants.PREFAB_FRB_LAYOUT;
        }

        return super.GetOutlineToSpawn(entity);
    }
}
