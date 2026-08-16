//------------------------------------------------------------------------------------------------
//! Shared workstation-mode string constants for the GBRS radar station.
//!
//! PD SEARCH / WLR / LOCK are referenced from GBRS_RadarStationComponent,
//! GBRS_RadarStationMenu and GBRS_RadarStationHud. Keeping them in one place
//! prevents silent string drift when a mode name changes.
class GBRS_RadarStationConstants
{
    static const string MODE_PD_SEARCH = "PD SEARCH";
    static const string MODE_WLR = "WLR";
    static const string MODE_LOCK = "LOCK";
    static const string MODE_MANUAL = "MANUAL";

    static const ResourceName PREFAB_E_US =
        "{69FCEDCEA0010003}PrefabsEditable/Auto/Compositions/Misc/FreeRoamBuilding/E_RadarStation_S_US_01.et";
    static const ResourceName PREFAB_E_USSR =
        "{69FCEDCEA0010004}PrefabsEditable/Auto/Compositions/Misc/FreeRoamBuilding/E_RadarStation_S_USSR_01.et";
    static const ResourceName PREFAB_ROOT_US =
        "{69FCEDCEA0010001}Prefabs/Compositions/Misc/FreeRoamBuilding/RadarStation_S_US_01.et";
    static const ResourceName PREFAB_ROOT_USSR =
        "{69FCEDCEA0010002}Prefabs/Compositions/Misc/FreeRoamBuilding/RadarStation_S_USSR_01.et";
    static const ResourceName PREFAB_FRB_LAYOUT =
        "{69FCEDCEA0030002}Prefabs/Compositions/Misc/FreeRoamBuilding/Layouts/FRB_RadarStation_S_01.et";

    // Must stay > 0. Layout SetPrefabId calls EvaluateBuildingStatus with
    // current=0; a zero ToBuildValue instantly SpawnComposition() and deletes
    // the FRB pad on the same placement.
    static const int BUILDING_VALUE = 160;

    //------------------------------------------------------------------------------------------------
    static bool IsRadarPrefab(ResourceName prefab)
    {
        if (prefab.IsEmpty())
            return false;

        if (prefab == PREFAB_E_US)
            return true;

        if (prefab == PREFAB_E_USSR)
            return true;

        if (prefab == PREFAB_ROOT_US)
            return true;

        if (prefab == PREFAB_ROOT_USSR)
            return true;

        return false;
    }
}
