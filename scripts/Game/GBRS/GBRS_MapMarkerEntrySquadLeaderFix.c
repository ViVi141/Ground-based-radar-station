// Vanilla SCR_MapMarkerEntrySquadLeader can NPE when a leader changes before
// GroupsManager is ready, or after it was torn down (common in Workbench /
// Conflict listen-server). GBRS does not own squad markers; this only guards
// the crash so playtests are not interrupted.
modded class SCR_MapMarkerEntrySquadLeader
{
    //------------------------------------------------------------------------------------------------
    override protected void OnPlayerLeaderChanged(int groupID, int playerId)
    {
        if (!m_GroupsManager)
            m_GroupsManager = SCR_GroupsManagerComponent.GetInstance();

        if (!m_GroupsManager)
            return;

        SCR_AIGroup group = m_GroupsManager.FindGroup(groupID);
        if (!group)
            return;

        SCR_MapMarkerSquadLeader marker = m_mGroupMarkers.Get(group);
        if (marker)
            marker.SetPlayerID(playerId);
    }

    //------------------------------------------------------------------------------------------------
    override protected void UpdateMarkerTarget(int playerID)
    {
        if (!m_GroupsManager)
            m_GroupsManager = SCR_GroupsManagerComponent.GetInstance();

        if (!m_GroupsManager)
            return;

        SCR_AIGroup group = m_GroupsManager.GetPlayerGroup(playerID);
        if (!group)
            return;

        if (!group.IsPlayerLeader(playerID))
            return;

        SCR_MapMarkerSquadLeader marker = m_mGroupMarkers.Get(group);
        if (marker)
            marker.SetPlayerID(playerID);
    }
}
