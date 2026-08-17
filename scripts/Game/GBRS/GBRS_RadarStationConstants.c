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
    // Reserved: fire-control consumer (SAM / AAA) does not exist yet.
    static const string MODE_LOCK = "LOCK";
    // Reserved: operator-training / manual RF console, no matching mod yet.
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
    static const int BUILDING_VALUE = 250;

    // Parked beam does not need beam-overlap dwells. 8 Hz is enough for TWS
    // kinematics and keeps ScanOnce off the 60 fps cadence.
    static const float STARE_UPDATE_INTERVAL_S = 0.12;

    // RDF clamps classify to 256. Eden sphere discovery queues ~15k DYNAMIC
    // entities; infantry fail IsRadarCandidate only after that queue drains.
    static const int SCATTERER_CLASSIFY_PER_TICK = 256;
    static const int SCATTERER_REFRESH_PER_TICK = 128;
    static const int SCATTERER_MAX_ENTRIES = 512;
    // RDF Configure floors this at 0.25 s. 1 s is enough for new vehicles
    // and avoids a 7–10 km sphere query four times a second after the
    // first fill (s_Seen already skips re-queue).
    static const float SCATTERER_DISCOVERY_INTERVAL_S = 1.0;
    static const float SCATTERER_DISCOVERY_RANGE_SCALE = 1.25;

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
