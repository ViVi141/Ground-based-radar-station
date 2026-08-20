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
    static bool RequestForceIntelTx(GBRS_RadarStationComponent station)
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

        playerController.GBRS_RpcAsk_ForceIntelTx(stationId);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    static bool RequestSubscribePpi(GBRS_RadarStationComponent station)
    {
        return RequestPpiSubscription(station, true);
    }

    //------------------------------------------------------------------------------------------------
    static bool RequestUnsubscribePpi(GBRS_RadarStationComponent station)
    {
        return RequestPpiSubscription(station, false);
    }

    //------------------------------------------------------------------------------------------------
    protected static bool RequestPpiSubscription(GBRS_RadarStationComponent station, bool subscribe)
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

        playerController.GBRS_RpcAsk_PpiSubscribe(stationId, subscribe);
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
    void GBRS_RpcAsk_ForceIntelTx(RplId stationId)
    {
        Rpc(RpcAsk_GBRS_ForceIntelTx, stationId);
    }

    //------------------------------------------------------------------------------------------------
    void GBRS_RpcAsk_PpiSubscribe(RplId stationId, bool subscribe)
    {
        Rpc(RpcAsk_GBRS_PpiSubscribe, stationId, subscribe);
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
    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_GBRS_ForceIntelTx(RplId stationId)
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

        bool sent = station.AuthorityForceIntelTx();
        GBRS_NotifyIntelTxResult(sent);
    }

    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_GBRS_PpiSubscribe(RplId stationId, bool subscribe)
    {
        GBRS_RadarStationComponent station = GBRS_PlayerControllerNet.ResolveStation(stationId);
        if (!station)
            return;

        IEntity owner = station.GetOwner();
        if (!owner)
            return;

        int playerId = GetPlayerId();
        if (subscribe)
        {
            if (!GBRS_PlayerControllerNet.IsRequesterNearStation(this, owner))
                return;

            if (!GBRS_PlayerControllerNet.IsRequesterFriendlyToStation(this, station))
                return;

            station.AuthoritySubscribePpi(playerId);
            return;
        }

        station.AuthorityUnsubscribePpi(playerId);
    }

    //------------------------------------------------------------------------------------------------
    void GBRS_NotifyIntelTxResult(bool sent)
    {
        if (GBRS_IsLocalPlayerController())
        {
            RpcDo_GBRS_IntelTxResult(sent);
            return;
        }

        Rpc(RpcDo_GBRS_IntelTxResult, sent);
    }

    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RpcDo_GBRS_IntelTxResult(bool sent)
    {
        if (sent)
            return;

        SCR_HintManagerComponent.ShowCustomHint("NO CONTACT", "RADAR NET", 3, false);
    }

    //------------------------------------------------------------------------------------------------
    void GBRS_NotifyIntelBriefing(string title, string subtitle)
    {
        if (GBRS_IsLocalPlayerController())
        {
            RpcDo_GBRS_IntelBriefing(title, subtitle);
            return;
        }

        Rpc(RpcDo_GBRS_IntelBriefing, title, subtitle);
    }

    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RpcDo_GBRS_IntelBriefing(string title, string subtitle)
    {
        SCR_HintManagerComponent.ShowCustomHint(subtitle, title, 12, false);

        SCR_PopUpNotification popup = SCR_PopUpNotification.GetInstance();
        if (!popup)
            return;

        popup.PopupMsg(title, 12, subtitle);
    }

    //------------------------------------------------------------------------------------------------
    void GBRS_NotifyIntelRadio(
        string title,
        string subtitle,
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        float quality,
        string factionKey,
        bool interrupt)
    {
        int voicePacked = voiceKind;
        if (interrupt)
            voicePacked = voicePacked + 10;

        if (GBRS_IsLocalPlayerController())
        {
            RpcDo_GBRS_IntelRadio(
                title, subtitle, voicePacked, gridPacked, paramA, paramB, quality, factionKey);
            return;
        }

        Rpc(RpcDo_GBRS_IntelRadio, title, subtitle, voicePacked, gridPacked, paramA, paramB, quality, factionKey);
    }

    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RpcDo_GBRS_IntelRadio(
        string title,
        string subtitle,
        int voicePacked,
        int gridPacked,
        int paramA,
        int paramB,
        float quality,
        string factionKey)
    {
        bool interrupt = false;
        int voiceKind = voicePacked;
        if (voiceKind >= 10)
        {
            interrupt = true;
            voiceKind = voiceKind - 10;
        }

        GBRS_ShowIntelAlert(
            title, subtitle, true, voiceKind, gridPacked, paramA, paramB, quality, factionKey, interrupt);
    }

    //------------------------------------------------------------------------------------------------
    void GBRS_NotifyPpiSnapshot(
        RplId stationId,
        vector origin,
        float scanAzDeg,
        float rangeM,
        string eccm,
        array<int> packedInts,
        array<float> packedFloats)
    {
        if (GBRS_IsLocalPlayerController())
        {
            RpcDo_GBRS_PpiSnapshot(
                stationId, origin, scanAzDeg, rangeM, eccm, packedInts, packedFloats);
            return;
        }

        Rpc(RpcDo_GBRS_PpiSnapshot,
            stationId, origin, scanAzDeg, rangeM, eccm, packedInts, packedFloats);
    }

    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Unreliable, RplRcver.Owner)]
    protected void RpcDo_GBRS_PpiSnapshot(
        RplId stationId,
        vector origin,
        float scanAzDeg,
        float rangeM,
        string eccm,
        array<int> packedInts,
        array<float> packedFloats)
    {
        GBRS_RadarStationMenu.ApplyReplicatedSnapshot(
            stationId, origin, scanAzDeg, rangeM, eccm, packedInts, packedFloats);
    }

    //------------------------------------------------------------------------------------------------
    protected bool GBRS_IsLocalPlayerController()
    {
        PlayerController local = GetGame().GetPlayerController();
        if (!local)
            return false;
        if (local != this)
            return false;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected void GBRS_ShowIntelAlert(
        string title,
        string subtitle,
        bool playVoice,
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        float quality,
        string factionKey,
        bool interrupt)
    {
        if (playVoice)
        {
            GBRS_IntelRadioSoundEntity.PlayIntelVoice(
                voiceKind, gridPacked, paramA, paramB, quality, factionKey, interrupt);
        }
    }
}
