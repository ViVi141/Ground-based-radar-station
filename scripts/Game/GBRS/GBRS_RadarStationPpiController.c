// Compatibility facade — PD SEARCH session now lives in GBRS_RadarStationMenu.
class GBRS_RadarStationPpiController
{
    static void OpenFor(GBRS_RadarStationComponent station)
    {
        GBRS_RadarStationMenu.OpenFor(station);
    }

    static void CloseIfBound(GBRS_RadarStationComponent station)
    {
        GBRS_RadarStationMenu.CloseIfBound(station);
    }

    static bool IsOpenFor(GBRS_RadarStationComponent station)
    {
        return GBRS_RadarStationMenu.IsOpenFor(station);
    }
}
