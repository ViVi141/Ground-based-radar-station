// Ensure GBRS radar compositions stay in Conflict / FreeRoam placeable lists
// even when a mission still points BuildingManager at the vanilla
// Compositions_FreeRoamBuilding.conf (RDF can run on a world-placed station
// without ever touching that registry).
modded class SCR_CampaignBuildingManagerComponent
{
    protected static const ResourceName GBRS_PLACEABLE_US =
        "{69FCEDCEA0010003}PrefabsEditable/Auto/Compositions/Misc/FreeRoamBuilding/E_RadarStation_S_US_01.et";
    protected static const ResourceName GBRS_PLACEABLE_USSR =
        "{69FCEDCEA0010004}PrefabsEditable/Auto/Compositions/Misc/FreeRoamBuilding/E_RadarStation_S_USSR_01.et";

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

        GBRS_InsertPlaceableIfMissing(GBRS_PLACEABLE_US);
        GBRS_InsertPlaceableIfMissing(GBRS_PLACEABLE_USSR);
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
    protected static const ResourceName GBRS_PLACEABLE_US =
        "{69FCEDCEA0010003}PrefabsEditable/Auto/Compositions/Misc/FreeRoamBuilding/E_RadarStation_S_US_01.et";
    protected static const ResourceName GBRS_PLACEABLE_USSR =
        "{69FCEDCEA0010004}PrefabsEditable/Auto/Compositions/Misc/FreeRoamBuilding/E_RadarStation_S_USSR_01.et";
    protected static const ResourceName GBRS_FRB_LAYOUT =
        "{69FCEDCEA0030002}Prefabs/Compositions/Misc/FreeRoamBuilding/Layouts/FRB_RadarStation_S_01.et";
    protected static const int GBRS_BUILDING_VALUE = 120;

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
        if (GBRS_IsRadarEditable(originalComposition))
            return GBRS_BUILDING_VALUE;

        return super.GetCompositionBuildingValue(originalComposition);
    }

    //------------------------------------------------------------------------------------------------
    protected ResourceName GBRS_FindRadarLayout(notnull SCR_EditableEntityComponent entity)
    {
        IEntity composition = entity.GetOwner();
        if (!composition)
            return ResourceName.Empty;

        ResourceName compositionResourceName = composition.GetPrefabData().GetPrefabName();
        if (!GBRS_IsRadarEditable(compositionResourceName))
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

        return GBRS_FRB_LAYOUT;
    }

    //------------------------------------------------------------------------------------------------
    protected bool GBRS_IsRadarEditable(ResourceName prefab)
    {
        if (prefab.IsEmpty())
            return false;

        if (prefab == GBRS_PLACEABLE_US)
            return true;

        if (prefab == GBRS_PLACEABLE_USSR)
            return true;

        return false;
    }
}
