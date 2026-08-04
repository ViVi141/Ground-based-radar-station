//------------------------------------------------------------------------------------------------
//! Client -> server ask for workstation mode only.
//! Power / scan frustum follow placeable-light UserAction broadcast (no PC RPC).
//! Mode comes from a local menu, so proxies ask via owned SCR_PlayerController.
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
}
