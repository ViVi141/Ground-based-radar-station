//------------------------------------------------------------------------------------------------
//! Client -> server asks for workstation mode and power toggles that mutate shared state.
//! Scan frustum still follows placeable-light UserAction broadcast (local visual only).
class GBRS_PlayerControllerNet
{
    protected static const float MAX_REQUEST_DISTANCE_M = 25.0;

    //------------------------------------------------------------------------------------------------
    static bool RequestWorkstationMode(GBRS_RadarStationComponent station, string mode)
    {
        if (!station)
            return false;

        RplId stationId = station.GetStationRplId();
        if (!stationId.IsValid())
            return false;

        SCR_PlayerController playerController =
            SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!playerController)
            return false;

        playerController.GBRS_RpcAsk_WorkstationMode(stationId, mode);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    static bool RequestTogglePower(GBRS_RadarStationComponent station)
    {
        if (!station)
            return false;

        RplId stationId = station.GetStationRplId();
        if (!stationId.IsValid())
            return false;

        SCR_PlayerController playerController =
            SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!playerController)
            return false;

        playerController.GBRS_RpcAsk_TogglePower(stationId);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    static bool RequestManualParam(
        GBRS_RadarStationComponent station,
        int paramIndex,
        float value)
    {
        if (!station)
            return false;

        RplId stationId = station.GetStationRplId();
        if (!stationId.IsValid())
            return false;

        SCR_PlayerController playerController =
            SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!playerController)
            return false;

        playerController.GBRS_RpcAsk_ManualParam(stationId, paramIndex, value);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    static bool RequestAntennaStare(
        GBRS_RadarStationComponent station,
        bool enabled,
        float azDeg)
    {
        if (!station)
            return false;

        RplId stationId = station.GetStationRplId();
        if (!stationId.IsValid())
            return false;

        SCR_PlayerController playerController =
            SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!playerController)
            return false;

        playerController.GBRS_RpcAsk_AntennaStare(stationId, enabled, azDeg);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    static GBRS_RadarStationComponent ResolveStation(RplId stationId)
    {
        if (!stationId.IsValid())
            return null;

        return GBRS_RadarStationComponent.Cast(Replication.FindItem(stationId));
    }

    //------------------------------------------------------------------------------------------------
    static bool IsRequesterNearStation(notnull PlayerController controller, notnull IEntity stationOwner)
    {
        IEntity controlled = controller.GetControlledEntity();
        if (!controlled)
            return false;

        float distSq = vector.DistanceSq(controlled.GetOrigin(), stationOwner.GetOrigin());
        float maxDist = MAX_REQUEST_DISTANCE_M;
        return distSq <= (maxDist * maxDist);
    }

    //------------------------------------------------------------------------------------------------
    static bool IsRequesterFriendlyToStation(
        notnull PlayerController controller,
        notnull GBRS_RadarStationComponent station)
    {
        IEntity controlled = controller.GetControlledEntity();
        if (!controlled)
            return false;

        return station.IsFriendlyUser(controlled);
    }
}

//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController
{
    //------------------------------------------------------------------------------------------------
    void GBRS_RpcAsk_WorkstationMode(RplId stationId, string mode)
    {
        Rpc(RpcAsk_GBRS_WorkstationMode, stationId, mode);
    }

    //------------------------------------------------------------------------------------------------
    void GBRS_RpcAsk_TogglePower(RplId stationId)
    {
        Rpc(RpcAsk_GBRS_TogglePower, stationId);
    }

    //------------------------------------------------------------------------------------------------
    void GBRS_RpcAsk_ManualParam(RplId stationId, int paramIndex, float value)
    {
        Rpc(RpcAsk_GBRS_ManualParam, stationId, paramIndex, value);
    }

    //------------------------------------------------------------------------------------------------
    void GBRS_RpcAsk_AntennaStare(RplId stationId, bool enabled, float azDeg)
    {
        Rpc(RpcAsk_GBRS_AntennaStare, stationId, enabled, azDeg);
    }

    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_GBRS_WorkstationMode(RplId stationId, string mode)
    {
        GBRS_RadarStationComponent station = GBRS_PlayerControllerNet.ResolveStation(stationId);
        if (!station)
            return;

        IEntity owner = station.GetOwner();
        if (!owner)
            return;

        if (!GBRS_PlayerControllerNet.IsRequesterNearStation(this, owner))
            return;

        if (!GBRS_PlayerControllerNet.IsRequesterFriendlyToStation(this, station))
            return;

        station.AuthoritySetWorkstationMode(mode);
    }

    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_GBRS_TogglePower(RplId stationId)
    {
        GBRS_RadarStationComponent station = GBRS_PlayerControllerNet.ResolveStation(stationId);
        if (!station)
            return;

        IEntity owner = station.GetOwner();
        if (!owner)
            return;

        if (!GBRS_PlayerControllerNet.IsRequesterNearStation(this, owner))
            return;

        if (!GBRS_PlayerControllerNet.IsRequesterFriendlyToStation(this, station))
            return;

        if (!station.IsConfigured())
            return;

        if (station.IsDestroyed())
            return;

        if (!station.IsCompositionReady())
            return;

        bool turnOn = !station.IsPowered();
        if (turnOn && !station.CanAffordPowerOn())
            return;

        station.SetPowered(turnOn);
    }

    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_GBRS_ManualParam(RplId stationId, int paramIndex, float value)
    {
        GBRS_RadarStationComponent station = GBRS_PlayerControllerNet.ResolveStation(stationId);
        if (!station)
            return;

        IEntity owner = station.GetOwner();
        if (!owner)
            return;

        if (!GBRS_PlayerControllerNet.IsRequesterNearStation(this, owner))
            return;

        if (!GBRS_PlayerControllerNet.IsRequesterFriendlyToStation(this, station))
            return;

        station.AuthoritySetManualParam(paramIndex, value);
    }

    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_GBRS_AntennaStare(RplId stationId, bool enabled, float azDeg)
    {
        GBRS_RadarStationComponent station = GBRS_PlayerControllerNet.ResolveStation(stationId);
        if (!station)
            return;

        IEntity owner = station.GetOwner();
        if (!owner)
            return;

        if (!GBRS_PlayerControllerNet.IsRequesterNearStation(this, owner))
            return;

        if (!GBRS_PlayerControllerNet.IsRequesterFriendlyToStation(this, station))
            return;

        station.AuthoritySetAntennaStare(enabled, azDeg);
    }

    //------------------------------------------------------------------------------------------------
    void GBRS_NotifyRadarWarning(string title, string subtitle)
    {
        if (RplSession.Mode() == RplMode.None)
        {
            RpcDo_GBRS_RadarWarning(title, subtitle);
            return;
        }

        Rpc(RpcDo_GBRS_RadarWarning, title, subtitle);
    }

    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RpcDo_GBRS_RadarWarning(string title, string subtitle)
    {
        SCR_PopUpNotification popup = SCR_PopUpNotification.GetInstance();
        if (!popup)
            return;

        popup.PopupMsg(title, 6, subtitle);
    }
}
