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
}
