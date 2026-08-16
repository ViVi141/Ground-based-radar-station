//------------------------------------------------------------------------------------------------
//! Public event API for GBRS radar stations.
//!
//! Other mods / missions can listen to these events to build team awareness,
//! automatic air-defence reactions, artillery counter-battery loops, or
//! Conflict-base early-warning integration.
//!
//! Example:
//!   GBRS_RadarStationEvents.OnRadarContact.Insert(MyContactHandler);
class GBRS_RadarStationEvents
{
    // Fired when a radar plot transitions from unseen to detected.
    static ref ScriptInvoker<GBRS_RadarStationComponent, RDF_RadarTarget> OnRadarContact =
        new ScriptInvoker<GBRS_RadarStationComponent, RDF_RadarTarget>();

    // Fired when a previously detected contact is no longer present / timed out.
    static ref ScriptInvoker<GBRS_RadarStationComponent, RDF_RadarTarget> OnRadarContactLost =
        new ScriptInvoker<GBRS_RadarStationComponent, RDF_RadarTarget>();

    // Fired when WLR produces a valid launch/impact solution.
    static ref ScriptInvoker<GBRS_RadarStationComponent, RDF_RadarWlrFix> OnWlrSolution =
        new ScriptInvoker<GBRS_RadarStationComponent, RDF_RadarWlrFix>();

    // Fired when LOCK auto-acquire / fire-authorization state changes.
    static ref ScriptInvoker<GBRS_RadarStationComponent, bool> OnLockChanged =
        new ScriptInvoker<GBRS_RadarStationComponent, bool>();

    // Fired when the station is destroyed.
    static ref ScriptInvoker<GBRS_RadarStationComponent> OnRadarDestroyed =
        new ScriptInvoker<GBRS_RadarStationComponent>();
}
