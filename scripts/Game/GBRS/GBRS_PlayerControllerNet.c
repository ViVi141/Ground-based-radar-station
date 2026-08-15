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

        station.AuthoritySetManualParam(paramIndex, value);
    }
}
