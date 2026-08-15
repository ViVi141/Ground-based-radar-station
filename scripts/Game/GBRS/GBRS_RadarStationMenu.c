//------------------------------------------------------------------------------------------------
//! Formal GBRS radar workstation menu (PD SEARCH / WLR / LOCK).
class GBRS_RadarStationMenu : ChimeraMenuBase
{
	protected static const int FEED_INTERVAL_MS = 33;
	protected static const int CLUSTER_INTERVAL_MS = 100;
	protected static const int PERSIST_MAX_BLIPS = 512;
	protected static const int DISPLAY_MAX_BLIPS = 64;
	// ~1 scan period fade: long afterglow was painting over intermittent MTI misses.
	protected static const float PERSIST_SEC_MIN = 1.2;
	protected static const float PERSIST_SEC_MAX = 8.0;
	protected static const float DISPLAY_CLUSTER_M = 90.0;
	protected static const string MODE_PD_SEARCH = GBRS_RadarStationConstants.MODE_PD_SEARCH;
	protected static const string MODE_WLR = GBRS_RadarStationConstants.MODE_WLR;
	protected static const string MODE_LOCK = GBRS_RadarStationConstants.MODE_LOCK;
	protected static const string MODE_MANUAL = GBRS_RadarStationConstants.MODE_MANUAL;
	protected static const string HINT_NOT_AVAILABLE = "Not available";
	protected static const string HINT_CONTEXT = "north-up AZ/EL";
	protected static const string HINT_WLR = "WLR launch/impact";
	protected static const string HINT_LOCK = "auto-lock vehicles";
	protected static const string HINT_MANUAL = "tune radar params";

	protected GBRS_RadarStationComponent m_Station;
	protected bool m_bBound;
	protected bool m_bFeedScheduled;
	protected float m_LastClusterS;
	protected int m_DetectedInRange;
	protected ref array<ref RDF_RadarTarget> m_PersistPlots;
	protected ref array<ref RDF_RadarTarget> m_DisplayPlots;
	protected string m_ActiveMode;
	protected bool m_bDeviceListenerBound;
	protected bool m_bNavBound;
	protected int m_iFocusedModeTab;
	protected static const int MODE_TAB_COUNT = 4;
	protected static const string ACTION_TAB_PREV = "MenuTabLeft";
	protected static const string ACTION_TAB_NEXT = "MenuTabRight";
	protected static const string ACTION_SELECT = "MenuSelect";
	protected static const string ACTION_CLOSE = "MenuBack";
	protected static const int MODE_NAV_COOLDOWN_MS = 180;

	// MANUAL mode: focused parameter index (0..PARAM_COUNT-1).
	protected int m_iFocusedManualParam;

	protected float m_fLastModeNavS;

	//------------------------------------------------------------------------------------------------
	static void OpenFor(GBRS_RadarStationComponent station)
	{
		if (!station)
			return;

		if (!station.IsPowered())
			return;

		MenuManager menuManager = GetGame().GetMenuManager();
		if (!menuManager)
			return;

		MenuBase existing = menuManager.FindMenuByPreset(ChimeraMenuPreset.GBRS_RadarStationMenu);
		GBRS_RadarStationMenu openMenu = GBRS_RadarStationMenu.Cast(existing);
		if (openMenu)
		{
			if (openMenu.IsBoundTo(station))
			{
				openMenu.FeedOnce();
				return;
			}

			menuManager.CloseMenu(openMenu);
		}

		MenuBase opened = menuManager.OpenMenu(ChimeraMenuPreset.GBRS_RadarStationMenu, 0, true);
		GBRS_RadarStationMenu stationMenu = GBRS_RadarStationMenu.Cast(opened);
		if (!stationMenu)
			return;

		stationMenu.BindStation(station);
	}

	//------------------------------------------------------------------------------------------------
	static void CloseIfBound(GBRS_RadarStationComponent station)
	{
		if (!station)
			return;

		MenuManager menuManager = GetGame().GetMenuManager();
		if (!menuManager)
			return;

		MenuBase existing = menuManager.FindMenuByPreset(ChimeraMenuPreset.GBRS_RadarStationMenu);
		GBRS_RadarStationMenu openMenu = GBRS_RadarStationMenu.Cast(existing);
		if (!openMenu)
			return;

		if (!openMenu.IsBoundTo(station))
			return;

		menuManager.CloseMenu(openMenu);
	}

	//------------------------------------------------------------------------------------------------
	static bool IsOpenFor(GBRS_RadarStationComponent station)
	{
		if (!station)
			return false;

		MenuManager menuManager = GetGame().GetMenuManager();
		if (!menuManager)
			return false;

		MenuBase existing = menuManager.FindMenuByPreset(ChimeraMenuPreset.GBRS_RadarStationMenu);
		GBRS_RadarStationMenu openMenu = GBRS_RadarStationMenu.Cast(existing);
		if (!openMenu)
			return false;

		return openMenu.IsBoundTo(station);
	}

	//------------------------------------------------------------------------------------------------
	bool IsBoundTo(GBRS_RadarStationComponent station)
	{
		if (!m_bBound)
			return false;

		return m_Station == station;
	}

	//------------------------------------------------------------------------------------------------
	void BindStation(GBRS_RadarStationComponent station)
	{
		if (!station)
			return;

		if (!station.IsPowered())
		{
			Close();
			return;
		}

		m_Station = station;
		m_bBound = true;
		m_LastClusterS = 0.0;
		m_DetectedInRange = 0;
		m_iFocusedManualParam = 0;
		m_ActiveMode = station.GetWorkstationMode();
		if (m_ActiveMode != MODE_WLR && m_ActiveMode != MODE_LOCK
			&& m_ActiveMode != MODE_PD_SEARCH && m_ActiveMode != MODE_MANUAL)
			m_ActiveMode = MODE_PD_SEARCH;
		EnsurePersistBuffer();
		ClearPersist();

		Widget root = GetRootWidget();
		IEntity opticsParent = station.GetOwner();
		GBRS_RadarStationHud.Attach(root, opticsParent);
		GBRS_RadarStationHud.SetMode(m_ActiveMode);
		m_iFocusedModeTab = 0;
		if (m_ActiveMode == MODE_WLR)
			m_iFocusedModeTab = 1;
		else if (m_ActiveMode == MODE_LOCK)
			m_iFocusedModeTab = 2;
		else if (m_ActiveMode == MODE_MANUAL)
			m_iFocusedModeTab = 3;
		UpdateModeTabVisuals();
		BindNavigation();
		UpdateContextHint();
		FocusForCurrentDevice();

		if (m_ActiveMode == MODE_MANUAL)
			RefreshManualParamList();

		StartFeed();
		FeedOnce();
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		m_ActiveMode = MODE_PD_SEARCH;
		m_bBound = false;
		m_Station = null;
		m_LastClusterS = 0.0;
		m_DetectedInRange = 0;
		m_iFocusedModeTab = 0;
		EnsurePersistBuffer();
		ClearPersist();

		Widget root = GetRootWidget();
		if (root)
			GBRS_RadarStationHud.Attach(root, null);

		BindModeTabs();
		BindNavigation();
		UpdateModeTabVisuals();
		StartDeviceListener();
		UpdateContextHint();
		FocusForCurrentDevice();
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{
		UnbindNavigation();
		StopDeviceListener();
		StopFeed();
		ClearPersist();
		m_bBound = false;
		m_Station = null;
		m_LastClusterS = 0.0;
		m_DetectedInRange = 0;
		GBRS_RadarStationHud.Detach();
		super.OnMenuClose();
	}

	//------------------------------------------------------------------------------------------------
	protected void BindModeTabs()
	{
		GBRS_RadarStationHudWidgets widgets = GBRS_RadarStationHud.GetWidgets();
		if (!widgets)
			return;

		if (widgets.m_wModeTabPd)
		{
			MuteWLibSounds(widgets.m_wModeTabPd);
			ScriptInvoker invPd = ButtonActionComponent.GetOnAction(widgets.m_wModeTabPd, true);
			if (invPd)
				invPd.Insert(OnModeTabPd);
		}

		if (widgets.m_wModeTabWlr)
		{
			MuteWLibSounds(widgets.m_wModeTabWlr);
			ScriptInvoker invWlr = ButtonActionComponent.GetOnAction(widgets.m_wModeTabWlr, true);
			if (invWlr)
				invWlr.Insert(OnModeTabWlr);
		}

		if (widgets.m_wModeTabLock)
		{
			MuteWLibSounds(widgets.m_wModeTabLock);
			ScriptInvoker invLock = ButtonActionComponent.GetOnAction(widgets.m_wModeTabLock, true);
			if (invLock)
				invLock.Insert(OnModeTabLock);
		}

		if (widgets.m_wModeTabManual)
		{
			MuteWLibSounds(widgets.m_wModeTabManual);
			ScriptInvoker invManual = ButtonActionComponent.GetOnAction(widgets.m_wModeTabManual, true);
			if (invManual)
				invManual.Insert(OnModeTabManual);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void BindNavigation()
	{
		if (m_bNavBound)
			return;

		GBRS_RadarStationHudWidgets widgets = GBRS_RadarStationHud.GetWidgets();
		if (!widgets)
			return;

		if (widgets.m_wHintTabPrev)
		{
			SCR_InputButtonComponent prev =
				SCR_InputButtonComponent.Cast(widgets.m_wHintTabPrev.FindHandler(SCR_InputButtonComponent));
			if (prev)
			{
				MuteInputButtonSounds(prev);
				prev.m_OnActivated.Insert(OnNavTabPrev);
			}
		}

		if (widgets.m_wHintTabNext)
		{
			SCR_InputButtonComponent next =
				SCR_InputButtonComponent.Cast(widgets.m_wHintTabNext.FindHandler(SCR_InputButtonComponent));
			if (next)
			{
				MuteInputButtonSounds(next);
				next.m_OnActivated.Insert(OnNavTabNext);
			}
		}

		if (widgets.m_wHintSelect)
		{
			SCR_InputButtonComponent select =
				SCR_InputButtonComponent.Cast(widgets.m_wHintSelect.FindHandler(SCR_InputButtonComponent));
			if (select)
			{
				MuteInputButtonSounds(select);
				select.m_OnActivated.Insert(OnNavSelect);
			}
		}

		if (widgets.m_wHintClose)
		{
			SCR_InputButtonComponent closeBtn =
				SCR_InputButtonComponent.Cast(widgets.m_wHintClose.FindHandler(SCR_InputButtonComponent));
			if (closeBtn)
			{
				MuteInputButtonSounds(closeBtn);
				closeBtn.m_OnActivated.Insert(OnNavClose);
			}
		}

		m_bNavBound = true;
		RefreshNavHintGlyphs();
	}

	// Mode bar / nav hints inherit WLib hover+click sounds; mute to avoid
	// AudioCategory queue overflow when focus cycles during mode changes.
	protected void MuteWLibSounds(Widget w)
	{
		if (!w)
			return;

		SCR_ButtonTextComponent textBtn =
			SCR_ButtonTextComponent.Cast(w.FindHandler(SCR_ButtonTextComponent));
		if (textBtn)
		{
			textBtn.SetHoverSound(string.Empty);
			textBtn.SetClickedSound(string.Empty);
			return;
		}

		SCR_WLibComponentBase wlib =
			SCR_WLibComponentBase.Cast(w.FindHandler(SCR_WLibComponentBase));
		if (!wlib)
			return;

		wlib.SetHoverSound(string.Empty);
		wlib.SetClickedSound(string.Empty);
	}

	protected void MuteInputButtonSounds(SCR_InputButtonComponent button)
	{
		if (!button)
			return;

		button.SetClickSoundDisabled(true);
		button.SetHoverSound(string.Empty);
		button.SetClickedSound(string.Empty);
	}

	//------------------------------------------------------------------------------------------------
	protected void UnbindNavigation()
	{
		m_bNavBound = false;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnModeTabPd(Widget w, float value, EActionTrigger reason)
	{
		m_iFocusedModeTab = 0;
		ActivateFocusedModeTab();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnModeTabWlr(Widget w, float value, EActionTrigger reason)
	{
		m_iFocusedModeTab = 1;
		ActivateFocusedModeTab();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnModeTabLock(Widget w, float value, EActionTrigger reason)
	{
		m_iFocusedModeTab = 2;
		ActivateFocusedModeTab();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnModeTabManual(Widget w, float value, EActionTrigger reason)
	{
		m_iFocusedModeTab = 3;
		ActivateFocusedModeTab();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnModeTabUnavailable(Widget w, float value, EActionTrigger reason)
	{
		if (w)
		{
			GBRS_RadarStationHudWidgets widgets = GBRS_RadarStationHud.GetWidgets();
			if (widgets)
			{
				if (w == widgets.m_wModeTabWlr)
					m_iFocusedModeTab = 1;
				else if (w == widgets.m_wModeTabLock)
					m_iFocusedModeTab = 2;
				else if (w == widgets.m_wModeTabManual)
					m_iFocusedModeTab = 3;
			}
		}

		ShowModeHint(HINT_NOT_AVAILABLE);
		UpdateModeTabVisuals();
		FocusModeTabIndex(m_iFocusedModeTab);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnNavTabPrev(SCR_InputButtonComponent button, string actionName)
	{
		if (m_ActiveMode == MODE_MANUAL)
		{
			// TabLeft in manual mode decreases the focused parameter.
			DecreaseManualParam();
			return;
		}
		CycleModeTab(-1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnNavTabNext(SCR_InputButtonComponent button, string actionName)
	{
		if (m_ActiveMode == MODE_MANUAL)
		{
			// TabRight in manual mode cycles to the next parameter.
			CycleManualParam(1);
			return;
		}
		CycleModeTab(1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnNavSelect(SCR_InputButtonComponent button, string actionName)
	{
		if (m_ActiveMode == MODE_MANUAL)
		{
			// MenuSelect in manual mode increases the focused parameter.
			AdjustManualParam(1);
			return;
		}
		ActivateFocusedModeTab();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnNavClose(SCR_InputButtonComponent button, string actionName)
	{
		Close();
	}

	//------------------------------------------------------------------------------------------------
	protected void CycleModeTab(int delta)
	{
		if (!CanAcceptModeNav())
			return;

		m_iFocusedModeTab = m_iFocusedModeTab + delta;
		while (m_iFocusedModeTab < 0)
			m_iFocusedModeTab = m_iFocusedModeTab + MODE_TAB_COUNT;
		while (m_iFocusedModeTab >= MODE_TAB_COUNT)
			m_iFocusedModeTab = m_iFocusedModeTab - MODE_TAB_COUNT;

		UpdateModeTabVisuals();
		FocusModeTabIndex(m_iFocusedModeTab);
		UpdateContextHint();
	}

	//------------------------------------------------------------------------------------------------
	// MANUAL mode: move the focused parameter up/down (TabLeft/Right).
	protected void CycleManualParam(int delta)
	{
		if (!CanAcceptModeNav())
			return;

		int count = GBRS_RadarManualConfig.PARAM_COUNT;
		m_iFocusedManualParam = m_iFocusedManualParam + delta;
		if (m_iFocusedManualParam < 0)
			m_iFocusedManualParam = count - 1;
		if (m_iFocusedManualParam >= count)
			m_iFocusedManualParam = 0;

		RefreshManualParamList();
	}

	//------------------------------------------------------------------------------------------------
	// MANUAL mode: increase the focused parameter by one step (MenuSelect).
	protected void AdjustManualParam(int direction)
	{
		if (!m_Station)
			return;

		if (!m_Station.IsManualMode())
			return;

		if (!m_Station.IsPowered())
			return;

		GBRS_RadarManualConfig cfg = m_Station.GetManualConfig();
		if (!cfg)
			return;

		int index = m_iFocusedManualParam;
		float current = cfg.GetParam(index);
		float step = GBRS_RadarManualConfig.StepParam(index);
		float next = current + step * direction;

		if (!m_Station.ApplyManualParam(index, next))
			return;

		RefreshManualParamList();
	}

	//------------------------------------------------------------------------------------------------
	// Manual decrease (MenuTabPrev in manual mode = -step on the focused param).
	protected void DecreaseManualParam()
	{
		if (!m_Station)
			return;

		if (!m_Station.IsManualMode())
			return;

		if (!m_Station.IsPowered())
			return;

		GBRS_RadarManualConfig cfg = m_Station.GetManualConfig();
		if (!cfg)
			return;

		int index = m_iFocusedManualParam;
		float current = cfg.GetParam(index);
		float step = GBRS_RadarManualConfig.StepParam(index);
		if (!m_Station.ApplyManualParam(index, current - step))
			return;

		RefreshManualParamList();
	}

	//------------------------------------------------------------------------------------------------
	protected void ActivateFocusedModeTab()
	{
		string nextMode = MODE_PD_SEARCH;
		if (m_iFocusedModeTab == 1)
			nextMode = MODE_WLR;
		else if (m_iFocusedModeTab == 2)
			nextMode = MODE_LOCK;
		else if (m_iFocusedModeTab == 3)
			nextMode = MODE_MANUAL;

		if (nextMode == m_ActiveMode)
		{
			UpdateModeTabVisuals();
			UpdateContextHint();
			return;
		}

		if (!m_Station)
		{
			ShowModeHint(HINT_NOT_AVAILABLE);
			UpdateModeTabVisuals();
			return;
		}

		// Client only submits the ask. Labels / PPI refresh after authority
		// confirmation lands on GetWorkstationMode().
		if (!m_Station.ApplyWorkstationMode(nextMode))
		{
			ShowModeHint(HINT_NOT_AVAILABLE);
			UpdateModeTabVisuals();
			return;
		}

		if (nextMode == MODE_MANUAL)
			m_iFocusedManualParam = 0;

		UpdateContextHint();
	}

	//------------------------------------------------------------------------------------------------
	protected bool CanAcceptModeNav()
	{
		float nowS = System.GetTickCount() * 0.001;
		float cooldownS = MODE_NAV_COOLDOWN_MS * 0.001;
		if ((nowS - m_fLastModeNavS) < cooldownS)
			return false;

		m_fLastModeNavS = nowS;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateModeTabVisuals()
	{
		GBRS_RadarStationHudWidgets widgets = GBRS_RadarStationHud.GetWidgets();
		if (!widgets)
			return;

		bool pdActive = false;
		bool wlrActive = false;
		bool lockActive = false;
		bool manualActive = false;
		if (m_iFocusedModeTab == 0)
			pdActive = true;
		else if (m_iFocusedModeTab == 1)
			wlrActive = true;
		else if (m_iFocusedModeTab == 2)
			lockActive = true;
		else if (m_iFocusedModeTab == 3)
			manualActive = true;

		SetTabEmphasis(widgets.m_wModeTabPd, pdActive);
		SetTabEmphasis(widgets.m_wModeTabWlr, wlrActive);
		SetTabEmphasis(widgets.m_wModeTabLock, lockActive);
		SetTabEmphasis(widgets.m_wModeTabManual, manualActive);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetTabEmphasis(Widget tab, bool active)
	{
		if (!tab)
			return;

		if (active)
		{
			tab.SetColor(Color.FromRGBA(80, 220, 140, 255));
			return;
		}

		tab.SetColor(Color.FromRGBA(120, 140, 130, 180));
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowModeHint(string text)
	{
		GBRS_RadarStationHudWidgets widgets = GBRS_RadarStationHud.GetWidgets();
		if (!widgets)
			return;

		if (!widgets.m_wPpiHint)
			return;

		widgets.m_wPpiHint.SetText(text);
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearModeHint()
	{
		UpdateContextHint();
	}

	//------------------------------------------------------------------------------------------------
	// MANUAL mode: build the parameter list text and push it to the HUD.
	// Each line: "NN NAME  value", focused line prefixed with ">".
	protected void RefreshManualParamList()
	{
		if (!m_Station)
			return;

		GBRS_RadarManualConfig cfg = m_Station.GetManualConfig();
		if (!cfg)
			return;

		if (m_iFocusedManualParam < 0)
			m_iFocusedManualParam = 0;
		if (m_iFocusedManualParam >= GBRS_RadarManualConfig.PARAM_COUNT)
			m_iFocusedManualParam = GBRS_RadarManualConfig.PARAM_COUNT - 1;

		string body = "";
		int i = 0;
		while (i < GBRS_RadarManualConfig.PARAM_COUNT)
		{
			string marker = "  ";
			if (i == m_iFocusedManualParam)
				marker = "> ";

			string name = ManualParamName(i);
			float value = cfg.GetParam(i);
			string valueStr = ManualParamValue(i, value);

			string line = marker + PadParamName(name) + " " + valueStr;
			if (body != "")
				body = body + "\n";
			body = body + line;
			i = i + 1;
		}

		GBRS_RadarStationHud.SetManualParamList(body, m_iFocusedManualParam);
	}

	//------------------------------------------------------------------------------------------------
	protected string ManualParamName(int index)
	{
		switch (index)
		{
			case 0: return "SNR dB";
			case 1: return "CLUTTER";
			case 2: return "RPM";
			case 3: return "RANGE";
			case 4: return "EL BORE";
			case 5: return "EL BW";
			case 6: return "AZ BW";
			case 7: return "UPDATE";
			case 8: return "POWER";
		}
		return "???";
	}

	//------------------------------------------------------------------------------------------------
	protected string ManualParamValue(int index, float value)
	{
		switch (index)
		{
			case 0: return value.ToString(-1, 1) + " dB";
			case 1: return value.ToString(-1, 2);
			case 2: return value.ToString(-1, 0) + " rpm";
			case 3:
			{
				if (value >= 1000.0)
					return (value / 1000.0).ToString(-1, 1) + " km";
				return value.ToString(-1, 0) + " m";
			}
			case 4: return value.ToString(-1, 1) + " deg";
			case 5: return value.ToString(-1, 1) + " deg";
			case 6: return value.ToString(-1, 1) + " deg";
			case 7: return value.ToString(-1, 2) + " s";
			case 8:
			{
				if (value >= 1000000.0)
					return (value / 1000000.0).ToString(-1, 1) + " MW";
				return (value / 1000.0).ToString(-1, 0) + " kW";
			}
		}
		return value.ToString();
	}

	//------------------------------------------------------------------------------------------------
	protected string PadParamName(string name)
	{
		while (name.Length() < 9)
			name = name + " ";
		return name;
	}

	// PPI note only — key glyphs come from official SCR_InputButton NavHints.
	protected void UpdateContextHint()
	{
		GBRS_RadarStationHudWidgets widgets = GBRS_RadarStationHud.GetWidgets();
		if (!widgets)
			return;

		if (!widgets.m_wPpiHint)
			return;

		string hint = HINT_CONTEXT;
		if (m_ActiveMode == MODE_WLR)
			hint = HINT_WLR;
		else if (m_ActiveMode == MODE_LOCK)
			hint = HINT_LOCK;
		else if (m_ActiveMode == MODE_MANUAL)
			hint = HINT_MANUAL;

		widgets.m_wPpiHint.SetText(hint);
	}

	//------------------------------------------------------------------------------------------------
	protected void RefreshNavHintGlyphs()
	{
		GBRS_RadarStationHudWidgets widgets = GBRS_RadarStationHud.GetWidgets();
		if (!widgets)
			return;

		InputManager inputManager = GetGame().GetInputManager();
		EInputDeviceType device = EInputDeviceType.KEYBOARD;
		if (inputManager)
			device = inputManager.GetLastUsedInputDevice();

		RefreshInputButton(widgets.m_wHintTabPrev, ACTION_TAB_PREV, device);
		RefreshInputButton(widgets.m_wHintTabNext, ACTION_TAB_NEXT, device);
		RefreshInputButton(widgets.m_wHintSelect, ACTION_SELECT, device);
		RefreshInputButton(widgets.m_wHintClose, ACTION_CLOSE, device);
	}

	//------------------------------------------------------------------------------------------------
	protected void RefreshInputButton(Widget w, string actionName, EInputDeviceType device)
	{
		if (!w)
			return;

		SCR_InputButtonComponent button =
			SCR_InputButtonComponent.Cast(w.FindHandler(SCR_InputButtonComponent));
		if (!button)
			return;

		// forceUpdate=false avoids rebinding listeners / glyph rebuild spam.
		button.SetAction(actionName, device, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void StartDeviceListener()
	{
		if (m_bDeviceListenerBound)
			return;

		GetGame().OnInputDeviceUserChangedInvoker().Insert(OnInputDeviceChanged);
		m_bDeviceListenerBound = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void StopDeviceListener()
	{
		if (!m_bDeviceListenerBound)
			return;

		GetGame().OnInputDeviceUserChangedInvoker().Remove(OnInputDeviceChanged);
		m_bDeviceListenerBound = false;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnInputDeviceChanged(EInputDeviceType oldDevice, EInputDeviceType newDevice)
	{
		RefreshNavHintGlyphs();
		UpdateContextHint();
		FocusForCurrentDevice();
	}

	//------------------------------------------------------------------------------------------------
	protected void FocusForCurrentDevice()
	{
		InputManager inputManager = GetGame().GetInputManager();
		if (!inputManager)
			return;

		EInputDeviceType device = inputManager.GetLastUsedInputDevice();
		if (device != EInputDeviceType.GAMEPAD)
		{
			if (device != EInputDeviceType.KEYBOARD)
				return;
		}

		FocusModeTabIndex(m_iFocusedModeTab);
	}

	//------------------------------------------------------------------------------------------------
	protected void FocusModeTabIndex(int index)
	{
		GBRS_RadarStationHudWidgets widgets = GBRS_RadarStationHud.GetWidgets();
		if (!widgets)
			return;

		Widget target = null;
		if (index == 0)
			target = widgets.m_wModeTabPd;
		else if (index == 1)
			target = widgets.m_wModeTabWlr;
		else if (index == 2)
			target = widgets.m_wModeTabLock;
		else
			target = widgets.m_wModeTabManual;

		if (!target)
			return;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return;

		if (workspace.GetFocusedWidget() == target)
			return;

		workspace.SetFocusedWidget(target);
	}

	//------------------------------------------------------------------------------------------------
	protected void StartFeed()
	{
		if (m_bFeedScheduled)
			return;

		GetGame().GetCallqueue().CallLater(TickFeed, FEED_INTERVAL_MS, true);
		m_bFeedScheduled = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void StopFeed()
	{
		if (!m_bFeedScheduled)
			return;

		GetGame().GetCallqueue().Remove(TickFeed);
		m_bFeedScheduled = false;
	}

	//------------------------------------------------------------------------------------------------
	protected void TickFeed()
	{
		if (!m_bBound)
		{
			StopFeed();
			return;
		}

		if (!CanKeepOpen())
		{
			Close();
			return;
		}

		SyncWorkstationModeFromStation();
		FeedOnce();
	}

	//------------------------------------------------------------------------------------------------
	// Apply PD / WLR / LOCK UI only after the station reports the confirmed mode.
	protected void SyncWorkstationModeFromStation()
	{
		if (!m_Station)
			return;

		string liveMode = m_Station.GetWorkstationMode();
		if (liveMode == m_ActiveMode)
			return;

		if (liveMode != MODE_WLR && liveMode != MODE_LOCK
			&& liveMode != MODE_PD_SEARCH && liveMode != MODE_MANUAL)
			return;

		m_ActiveMode = liveMode;
		ClearPersist();
		m_LastClusterS = 0.0;
		m_DetectedInRange = 0;
		GBRS_RadarStationHud.SetMode(m_ActiveMode);

		m_iFocusedModeTab = 0;
		if (m_ActiveMode == MODE_WLR)
			m_iFocusedModeTab = 1;
		else if (m_ActiveMode == MODE_LOCK)
			m_iFocusedModeTab = 2;
		else if (m_ActiveMode == MODE_MANUAL)
			m_iFocusedModeTab = 3;

		if (m_ActiveMode == MODE_MANUAL)
			RefreshManualParamList();

		UpdateModeTabVisuals();
		UpdateContextHint();
	}

	//------------------------------------------------------------------------------------------------
	protected bool CanKeepOpen()
	{
		if (!m_Station)
			return false;

		if (!m_Station.IsPowered())
			return false;

		if (m_Station.IsDestroyedForPpi())
			return false;

		RDF_RadarComponent radar = m_Station.GetRadarComponent();
		if (!radar)
			return false;

		if (!radar.IsEnabled())
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void EnsurePersistBuffer()
	{
		if (!m_PersistPlots)
			m_PersistPlots = new array<ref RDF_RadarTarget>();
		if (!m_DisplayPlots)
			m_DisplayPlots = new array<ref RDF_RadarTarget>();
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearPersist()
	{
		if (m_PersistPlots)
			m_PersistPlots.Clear();
		if (m_DisplayPlots)
			m_DisplayPlots.Clear();
	}

	//------------------------------------------------------------------------------------------------
	protected float GetPersistLifeS()
	{
		float rpm = 15.0;
		if (m_Station)
			rpm = m_Station.GetScanRpm();

		float life = 3.0;
		if (rpm > 0.0)
			life = 60.0 / rpm;

		life = life * 1.05;
		if (life < PERSIST_SEC_MIN)
			life = PERSIST_SEC_MIN;
		if (life > PERSIST_SEC_MAX)
			life = PERSIST_SEC_MAX;
		return life;
	}

	//------------------------------------------------------------------------------------------------
	protected void IngestLivePlots(
		array<ref RDF_RadarTarget> live,
		RDF_RadarSettings settings,
		float nowS)
	{
		EnsurePersistBuffer();
		if (!live)
			return;

		int i = 0;
		while (i < live.Count())
		{
			RDF_RadarTarget src = live.Get(i);
			i = i + 1;
			if (!GBRS_RadarStationConfig.ShouldDisplayPlot(src, settings))
				continue;

			RDF_RadarTarget existing = FindPersistMatch(src);
			if (existing)
			{
				CopyPlot(src, existing, nowS);
				continue;
			}

			if (m_PersistPlots.Count() >= PERSIST_MAX_BLIPS)
				RemoveOldestPersist();

			RDF_RadarTarget created = new RDF_RadarTarget();
			CopyPlot(src, created, nowS);
			m_PersistPlots.Insert(created);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected RDF_RadarTarget FindPersistMatch(RDF_RadarTarget src)
	{
		if (!src || !m_PersistPlots)
			return null;

		int i = 0;
		if (src.m_ScattererId > 0)
		{
			while (i < m_PersistPlots.Count())
			{
				RDF_RadarTarget t = m_PersistPlots.Get(i);
				i = i + 1;
				if (!t)
					continue;
				if (t.m_ScattererId == src.m_ScattererId)
					return t;
			}
			return null;
		}

		i = 0;
		while (i < m_PersistPlots.Count())
		{
			RDF_RadarTarget t = m_PersistPlots.Get(i);
			i = i + 1;
			if (!t)
				continue;
			if (t.m_ScattererId > 0)
				continue;

			vector d = t.m_Position - src.m_Position;
			if (d.LengthSq() < 4.0)
				return t;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected void RemoveOldestPersist()
	{
		if (!m_PersistPlots || m_PersistPlots.Count() == 0)
			return;

		int oldest = 0;
		float oldestTime = 1.0e30;
		int i = 0;
		while (i < m_PersistPlots.Count())
		{
			RDF_RadarTarget t = m_PersistPlots.Get(i);
			if (t && t.m_Time < oldestTime)
			{
				oldestTime = t.m_Time;
				oldest = i;
			}
			i = i + 1;
		}

		m_PersistPlots.Remove(oldest);
	}

	//------------------------------------------------------------------------------------------------
	protected void CopyPlot(RDF_RadarTarget src, RDF_RadarTarget dst, float nowS)
	{
		if (!src || !dst)
			return;

		dst.m_Entity = src.m_Entity;
		dst.m_ScattererId = src.m_ScattererId;
		dst.m_Position = src.m_Position;
		dst.m_Distance = src.m_Distance;
		dst.m_Velocity = src.m_Velocity;
		dst.m_Type = src.m_Type;
		dst.m_Time = nowS;
		dst.m_AzimuthDeg = src.m_AzimuthDeg;
		dst.m_ElevationDeg = src.m_ElevationDeg;
		dst.m_RadialSpeedMs = src.m_RadialSpeedMs;
		dst.m_RcsM2 = src.m_RcsM2;
		dst.m_MeanRcsM2 = src.m_MeanRcsM2;
		dst.m_SwerlingModel = src.m_SwerlingModel;
		dst.m_AglM = src.m_AglM;
		dst.m_DemTerrainY = src.m_DemTerrainY;
		dst.m_ReceivedPowerW = src.m_ReceivedPowerW;
		dst.m_ProcessedPowerW = src.m_ProcessedPowerW;
		dst.m_DopplerHz = src.m_DopplerHz;
		dst.m_MtiGain = src.m_MtiGain;
		dst.m_DopplerBin = src.m_DopplerBin;
		dst.m_PrfIndex = src.m_PrfIndex;
		dst.m_RotorTipSpeedMs = src.m_RotorTipSpeedMs;
		dst.m_BladeCount = src.m_BladeCount;
		dst.m_RotorRcsFraction = src.m_RotorRcsFraction;
		dst.m_HubWidthMs = src.m_HubWidthMs;
		dst.m_RotorSidebandUsed = src.m_RotorSidebandUsed;
		dst.m_DemSurfaceClass = src.m_DemSurfaceClass;
		dst.m_DemSampleValid = src.m_DemSampleValid;
		dst.m_ClutterPowerW = src.m_ClutterPowerW;
		dst.m_ClutterToNoiseDb = src.m_ClutterToNoiseDb;
		dst.m_SnrDb = src.m_SnrDb;
		dst.m_Detected = true;
		dst.m_IsAnonymous = src.m_IsAnonymous;
		dst.m_IsFalsePlot = src.m_IsFalsePlot;
		dst.m_CfarPowerW = src.m_CfarPowerW;
		dst.m_LosBlocked = src.m_LosBlocked;
		dst.m_LosHitFraction = src.m_LosHitFraction;
		dst.m_MultipathFactor = src.m_MultipathFactor;
		dst.m_EmitFrequencyHz = src.m_EmitFrequencyHz;
		dst.m_EmitPeakPowerW = src.m_EmitPeakPowerW;
		dst.m_EmitAntennaGainDbi = src.m_EmitAntennaGainDbi;
		dst.m_EmitStrength = src.m_EmitStrength;
		dst.m_BeamName = src.m_BeamName;
		dst.m_ScanNumber = src.m_ScanNumber;
	}

	//------------------------------------------------------------------------------------------------
	protected void PrunePersist(float nowS, float lifeS)
	{
		if (!m_PersistPlots)
			return;

		int i = m_PersistPlots.Count() - 1;
		while (i >= 0)
		{
			RDF_RadarTarget t = m_PersistPlots.Get(i);
			if (!t || (nowS - t.m_Time) > lifeS)
				m_PersistPlots.Remove(i);
			i = i - 1;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected float PlotRangeM(RDF_RadarTarget t, vector origin)
	{
		if (!t)
			return 0.0;

		if (t.m_Distance > 0.0)
			return t.m_Distance;

		vector d = t.m_Position - origin;
		return d.Length();
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity ResolveClusterRoot(RDF_RadarTarget src, map<int, IEntity> rootCache)
	{
		if (!src || src.m_ScattererId <= 0)
			return null;

		// Anonymous plots must not cluster via scatterer→entity truth; that
		// re-introduces identity after KeepEntityTruth is stripped.
		if (src.m_IsAnonymous)
			return null;

		if (rootCache && rootCache.Contains(src.m_ScattererId))
			return rootCache.Get(src.m_ScattererId);

		IEntity root = null;
		RDF_RadarScatterer entry = RDF_RadarScattererRegistry.FindById(src.m_ScattererId);
		if (entry && entry.m_Entity)
		{
			root = entry.m_Entity.GetRootParent();
			if (!root)
				root = entry.m_Entity;
		}

		if (rootCache)
			rootCache.Set(src.m_ScattererId, root);

		return root;
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildClusteredDisplayPlots(vector origin, float rangeM)
	{
		EnsurePersistBuffer();
		m_DisplayPlots.Clear();
		m_DetectedInRange = 0;
		if (!m_PersistPlots || m_PersistPlots.Count() == 0)
			return;

		float rangeLimit = rangeM;
		if (rangeLimit <= 0.0)
			rangeLimit = 1.0e9;

		float gateSq = DISPLAY_CLUSTER_M * DISPLAY_CLUSTER_M;
		map<int, IEntity> rootCache = new map<int, IEntity>();
		array<IEntity> displayRoots = new array<IEntity>();

		int i = 0;
		while (i < m_PersistPlots.Count())
		{
			RDF_RadarTarget src = m_PersistPlots.Get(i);
			i = i + 1;
			if (!src)
				continue;

			float rng = PlotRangeM(src, origin);
			if (rng > rangeLimit)
				continue;

			IEntity srcRoot = ResolveClusterRoot(src, rootCache);

			int match = -1;
			int j = 0;
			while (j < m_DisplayPlots.Count())
			{
				RDF_RadarTarget kept = m_DisplayPlots.Get(j);
				if (kept)
				{
					IEntity keptRoot = null;
					if (j < displayRoots.Count())
						keptRoot = displayRoots.Get(j);

					if (srcRoot && keptRoot && srcRoot == keptRoot)
					{
						match = j;
						break;
					}

					if (!srcRoot && !keptRoot)
					{
						vector d = kept.m_Position - src.m_Position;
						if (d.LengthSq() <= gateSq)
						{
							match = j;
							break;
						}
					}
				}
				j = j + 1;
			}

			if (match < 0)
			{
				RDF_RadarTarget created = new RDF_RadarTarget();
				CopyPlot(src, created, src.m_Time);
				if (rng > 0.0)
					created.m_Distance = rng;
				m_DisplayPlots.Insert(created);
				displayRoots.Insert(srcRoot);
				continue;
			}

			RDF_RadarTarget winner = m_DisplayPlots.Get(match);
			if (!winner)
				continue;

			if (src.m_SnrDb > winner.m_SnrDb)
			{
				CopyPlot(src, winner, src.m_Time);
				if (rng > 0.0)
					winner.m_Distance = rng;
			}
		}

		m_DetectedInRange = m_DisplayPlots.Count();
		TrimDisplayBudget();
	}

	//------------------------------------------------------------------------------------------------
	protected void TrimDisplayBudget()
	{
		if (!m_DisplayPlots)
			return;

		while (m_DisplayPlots.Count() > DISPLAY_MAX_BLIPS)
		{
			int worst = 0;
			float worstSnr = 1.0e30;
			int i = 0;
			while (i < m_DisplayPlots.Count())
			{
				RDF_RadarTarget t = m_DisplayPlots.Get(i);
				if (t && t.m_SnrDb < worstSnr)
				{
					worstSnr = t.m_SnrDb;
					worst = i;
				}
				i = i + 1;
			}
			m_DisplayPlots.Remove(worst);
		}
	}

	//------------------------------------------------------------------------------------------------
	void FeedOnce()
	{
		if (!m_bBound)
			return;

		if (!m_Station)
			return;

		RDF_RadarComponent radar = m_Station.GetRadarComponent();
		if (!radar)
			return;

		RDF_RadarSensor sensor = radar.GetSensor();
		if (!sensor)
			return;

		if (!GBRS_RadarStationHud.IsVisible())
			GBRS_RadarStationHud.Attach(GetRootWidget(), m_Station.GetOwner());

		float nowS = System.GetTickCount() * 0.001;
		float lifeS = GetPersistLifeS();

		vector hudOrigin = m_Station.GetScanOriginWorld();
		vector hudForward = m_Station.GetScanForwardWorld();

		RDF_RadarScanContext ctx = sensor.GetScanContext();
		RDF_RadarSettings settings = sensor.GetSettings();

		float hudRange = 2000.0;
		if (settings)
			hudRange = settings.m_Range;

		if (ctx)
		{
			if (ctx.m_Origin.LengthSq() > 0.0001)
				hudOrigin = ctx.m_Origin;
			if (ctx.m_RangeM > 0.0)
				hudRange = ctx.m_RangeM;
		}

		IngestLivePlots(sensor.GetPlots(), settings, nowS);
		PrunePersist(nowS, lifeS);

		float clusterIntervalS = CLUSTER_INTERVAL_MS * 0.001;
		bool needCluster = false;
		if (m_DisplayPlots.Count() == 0)
			needCluster = true;
		if ((nowS - m_LastClusterS) >= clusterIntervalS)
			needCluster = true;

		if (needCluster)
		{
			BuildClusteredDisplayPlots(hudOrigin, hudRange);
			m_LastClusterS = nowS;
		}

		GBRS_RadarStationHud.SetDisplayRange(hudRange);
		GBRS_RadarStationHud.SetMode(m_ActiveMode);
		// RDF 1.0.0 ECCM decision status → PPI jam ring + list footer.
		GBRS_RadarStationHud.SetEccmStatus(sensor.GetEccmStatusShort());
		GBRS_RadarStationHud.FeedScan(
			m_DisplayPlots,
			hudOrigin,
			hudForward,
			hudRange,
			sensor.GetTracker(),
			m_DetectedInRange,
			sensor.GetLockManager());
	}
}
