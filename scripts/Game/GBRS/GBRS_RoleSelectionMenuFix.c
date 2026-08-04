// Vanilla (newer builds) changed ShowFactionPlayerList to
// (Faction faction = null, bool show = true), but still Insert()s it on
// GetOnButtonFocused() which only Invoke()s a Faction. ScriptInvoker then
// errors: expected bool, got void. GBRS does not own deploy UI; this only
// rebinds a Faction-compatible wrapper so Conflict role select stays clean.
modded class SCR_RoleSelectionMenu
{
    //------------------------------------------------------------------------------------------------
    protected void GBRS_OnFactionButtonFocused(Faction faction)
    {
        ShowFactionPlayerList(faction);
    }

    //------------------------------------------------------------------------------------------------
    override protected void HookEvents()
    {
        super.HookEvents();

        if (!m_FactionRequestUIHandler)
            return;

        m_FactionRequestUIHandler.GetOnButtonFocused().Remove(ShowFactionPlayerList);
        m_FactionRequestUIHandler.GetOnButtonFocused().Insert(GBRS_OnFactionButtonFocused);
    }
}
