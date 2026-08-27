//------------------------------------------------------------------------------------------------
//! Formal GBRS radar workstation menu (PD SEARCH / WLR / LOCK).
//! MANUAL handlers stay compiled but are reserved until a training addon exists.
class GBRS_PersistPlot
{
	ref RDF_RadarTarget m_Target;
	float m_LastFreshS;
}

class GBRS_PpiZoomWheelHandler : ScriptedWidgetEventHandler
{
	GBRS_RadarStationMenu m_Menu;

	override bool OnMouseWheel(Widget w, int x, int y, int wheel)
	{
		if (!m_Menu)
			return false;

		if (wheel > 0)
			m_Menu.AdjustPpiZoom(-1);
		else if (wheel < 0)
			m_Menu.AdjustPpiZoom(1);

		return true;
	}

	override bool OnMouseButtonDown(Widget w, int x, int y, int button)
	{
		if (!m_Menu)
			return false;
		if (button != 0)
			return false;

		return m_Menu.TryLockPaintedTarget(x, y);
	}
}

class GBRS_RadarStationMenu : ChimeraMenuBase
{
	protected static const int FEED_INTERVAL_MS = 16;
	protected static const int CLUSTER_INTERVAL_MS = 100;
	protected static const int PERSIST_MAX_BLIPS = 512;
	protected static const int DISPLAY_MAX_BLIPS = 64;
	// Digital TWS afterglow: last few dwells only. Contacts live on the
	// RDF track file (coast), not as a phosphor trail of raw plots.
	protected static const float PLOT_AFTERGLOW_S = 0.45;
	protected static const float DISPLAY_CLUSTER_M = 120.0;
	protected static const float DISPLAY_CLUSTER_AZ_DEG = 4.0;
	protected static const float DISPLAY_CLUSTER_RANGE_M = 400.0;
	protected static const float PERSIST_SPATIAL_M = 80.0;
	protected static const string MODE_PD_SEARCH = GBRS_RadarStationConstants.MODE_PD_SEARCH;
	protected static const string MODE_WLR = GBRS_RadarStationConstants.MODE_WLR;
	protected static const string MODE_LOCK = GBRS_RadarStationConstants.MODE_LOCK;
	protected static const string MODE_MANUAL = GBRS_RadarStationConstants.MODE_MANUAL;
	protected static const string HINT_NOT_AVAILABLE = "Not available";
	protected static const string HINT_CONTEXT = "click contact to lock   Up/Dn PPI range";
	protected static const string HINT_WLR = "WLR launch/impact   Up/Dn PPI range";
	protected static const string HINT_LOCK = "click contact to lock   Up/Dn PPI range";
	protected static const float PPI_RANGE_MIN_M = 1000.0;
	protected static const int PPI_RANGE_STEP_COUNT = 15;

	protected GBRS_RadarStationComponent m_Station;
	protected bool m_bBound;
	protected bool m_bFeedScheduled;
	protected float m_LastClusterS;
	protected int m_DetectedInRange;
	protected ref array<ref GBRS_PersistPlot> m_PersistPlots;
	protected ref array<ref RDF_RadarTarget> m_DisplayPlots;
	protected float m_LastPersistCoastS;
	protected string m_ActiveMode;
	protected bool m_bDeviceListenerBound;
	protected bool m_bNavBound;
	protected int m_iFocusedModeTab;
	protected static const int MODE_TAB_COUNT = 4;
	protected static const string ACTION_TAB_PREV = "MenuTabLeft";
	protected static const string ACTION_TAB_NEXT = "MenuTabRight";
	protected static const string ACTION_SELECT = "MenuSelect";
	protected static const string ACTION_CLOSE = "MenuBack";
	protected static const string ACTION_PARAM_PREV = "MenuUp";
	protected static const string ACTION_PARAM_NEXT = "MenuDown";
	protected static const string ACTION_PARAM_DEC = "MenuLeft";
	protected static const string ACTION_PARAM_INC = "MenuRight";
	protected static const int MODE_NAV_COOLDOWN_MS = 180;
	protected bool m_bManualActionsBound;

	// MANUAL mode: focused parameter index (0..PARAM_COUNT-1).
	protected int m_iFocusedManualParam;

	protected float m_fLastModeNavS;
	protected float m_fLastIntelTxS;
	// 0 + !manual = follow RF max until the operator zooms the PPI.
	protected float m_PpiViewRangeM;
	protected bool m_bPpiZoomManual;
	protected bool m_bHasPpiSnapshot;
	protected vector m_PpiSnapOrigin;
	protected float m_PpiSnapScanAzDeg;
	protected float m_PpiSnapRangeM;
	protected string m_PpiSnapEccm;
	protected int m_PpiSnapDetectedTotal;
	protected int m_PpiSnapNetOnline;
	protected ref array<ref RDF_RadarTarget> m_PpiSnapPlots;
	protected ref array<ref RDF_RadarTrack> m_PpiSnapTracks;
	protected ref array<ref RDF_RadarFusedTrack> m_PpiSnapFused;
	protected ref array<ref GBRS_WlrPersistDisplay> m_PpiSnapWlr;
	protected int m_PpiSnapLockedTrackId;
	protected ref GBRS_PpiZoomWheelHandler m_PpiWheelHandler;
	protected Widget m_wPpiWheelHost;

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
	// Demo / operator override for the PPI range ring. 0 keeps following RF max.
	static void SetOpenMenuPpiViewRange(float rangeM)
	{
		if (rangeM <= 0.0)
			return;

		MenuManager menuManager = GetGame().GetMenuManager();
		if (!menuManager)
			return;

		MenuBase existing = menuManager.FindMenuByPreset(ChimeraMenuPreset.GBRS_RadarStationMenu);
		GBRS_RadarStationMenu openMenu = GBRS_RadarStationMenu.Cast(existing);
		if (!openMenu)
			return;

		openMenu.m_PpiViewRangeM = rangeM;
		openMenu.m_bPpiZoomManual = true;
		GBRS_RadarStationHud.SetDisplayRange(rangeM);
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
		ClearPpiSnapshot();
		m_PpiViewRangeM = 0.0;
		m_bPpiZoomManual = false;

		Widget root = GetRootWidget();
		IEntity opticsParent = station.GetOwner();
		GBRS_RadarStationHud.SetMode(m_ActiveMode);
		GBRS_RadarStationHud.Attach(root, opticsParent);
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

		GBRS_PlayerControllerNet.RequestSubscribePpi(station);
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
		ClearPpiSnapshot();
		m_PpiViewRangeM = 0.0;
		m_bPpiZoomManual = false;

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
		GBRS_RadarStationComponent station = m_Station;
		UnbindNavigation();
		UnbindPpiZoomWheel();
		StopDeviceListener();
		StopFeed();
		ClearPersist();
		ClearPpiSnapshot();
		m_bBound = false;
		m_Station = null;
		m_LastClusterS = 0.0;
		m_DetectedInRange = 0;
		m_PpiViewRangeM = 0.0;
		m_bPpiZoomManual = false;
		GBRS_RadarStationHud.Detach();
		if (station)
			GBRS_PlayerControllerNet.RequestUnsubscribePpi(station);
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
			MuteWLibSounds(widgets.m_wModeTabManual);

		if (widgets.m_wIntelTxBtn)
		{
			MuteWLibSounds(widgets.m_wIntelTxBtn);
			ScriptInvoker invIntel = ButtonActionComponent.GetOnAction(widgets.m_wIntelTxBtn, true);
			if (invIntel)
				invIntel.Insert(OnIntelTxBtn);
			widgets.m_wIntelTxBtn.SetColor(Color.FromRGBA(90, 255, 160, 255));
		}

		if (widgets.m_wOpticsToggleBtn)
		{
			MuteWLibSounds(widgets.m_wOpticsToggleBtn);
			ScriptInvoker invOptics = ButtonActionComponent.GetOnAction(widgets.m_wOpticsToggleBtn, true);
			if (invOptics)
				invOptics.Insert(OnOpticsToggleBtn);
			UpdateOpticsToggleVisual();
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

		if (widgets.m_wHintParamPrev)
		{
			SCR_InputButtonComponent paramPrev =
				SCR_InputButtonComponent.Cast(widgets.m_wHintParamPrev.FindHandler(SCR_InputButtonComponent));
			if (paramPrev)
			{
				MuteInputButtonSounds(paramPrev);
				paramPrev.m_OnActivated.Insert(OnNavParamPrev);
			}
		}

		if (widgets.m_wHintParamNext)
		{
			SCR_InputButtonComponent paramNext =
				SCR_InputButtonComponent.Cast(widgets.m_wHintParamNext.FindHandler(SCR_InputButtonComponent));
			if (paramNext)
			{
				MuteInputButtonSounds(paramNext);
				paramNext.m_OnActivated.Insert(OnNavParamNext);
			}
		}

		if (widgets.m_wHintParamDec)
		{
			SCR_InputButtonComponent paramDec =
				SCR_InputButtonComponent.Cast(widgets.m_wHintParamDec.FindHandler(SCR_InputButtonComponent));
			if (paramDec)
			{
				MuteInputButtonSounds(paramDec);
				paramDec.m_OnActivated.Insert(OnNavParamDec);
			}
		}

		if (widgets.m_wHintParamInc)
		{
			SCR_InputButtonComponent paramInc =
				SCR_InputButtonComponent.Cast(widgets.m_wHintParamInc.FindHandler(SCR_InputButtonComponent));
			if (paramInc)
			{
				MuteInputButtonSounds(paramInc);
				paramInc.m_OnActivated.Insert(OnNavParamInc);
			}
		}

		m_bNavBound = true;
		BindManualActions();
		BindPpiZoomWheel();
		RefreshNavHintGlyphs();
		UpdateNavParamHints();
	}

	//------------------------------------------------------------------------------------------------
	protected void BindManualActions()
	{
		if (m_bManualActionsBound)
			return;

		InputManager inputManager = GetGame().GetInputManager();
		if (!inputManager)
			return;

		inputManager.AddActionListener(ACTION_PARAM_PREV, EActionTrigger.DOWN, OnManualActionPrev);
		inputManager.AddActionListener(ACTION_PARAM_NEXT, EActionTrigger.DOWN, OnManualActionNext);
		inputManager.AddActionListener(ACTION_PARAM_DEC, EActionTrigger.DOWN, OnManualActionDec);
		inputManager.AddActionListener(ACTION_PARAM_INC, EActionTrigger.DOWN, OnManualActionInc);
		m_bManualActionsBound = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void UnbindManualActions()
	{
		if (!m_bManualActionsBound)
			return;

		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager)
		{
			inputManager.RemoveActionListener(ACTION_PARAM_PREV, EActionTrigger.DOWN, OnManualActionPrev);
			inputManager.RemoveActionListener(ACTION_PARAM_NEXT, EActionTrigger.DOWN, OnManualActionNext);
			inputManager.RemoveActionListener(ACTION_PARAM_DEC, EActionTrigger.DOWN, OnManualActionDec);
			inputManager.RemoveActionListener(ACTION_PARAM_INC, EActionTrigger.DOWN, OnManualActionInc);
		}

		m_bManualActionsBound = false;
	}

	//------------------------------------------------------------------------------------------------
	protected void BindPpiZoomWheel()
	{
		if (m_PpiWheelHandler)
			return;

		Widget host = GetRootWidget();
		if (!host)
			return;

		m_PpiWheelHandler = new GBRS_PpiZoomWheelHandler();
		m_PpiWheelHandler.m_Menu = this;
		host.AddHandler(m_PpiWheelHandler);
		m_wPpiWheelHost = host;
	}

	//------------------------------------------------------------------------------------------------
	protected void UnbindPpiZoomWheel()
	{
		if (m_PpiWheelHandler && m_wPpiWheelHost)
			m_wPpiWheelHost.RemoveHandler(m_PpiWheelHandler);

		if (m_PpiWheelHandler)
			m_PpiWheelHandler.m_Menu = null;

		m_PpiWheelHandler = null;
		m_wPpiWheelHost = null;
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
		UnbindManualActions();
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
	protected void OnIntelTxBtn(Widget w, float value, EActionTrigger reason)
	{
		if (!m_Station)
			return;
		if (!m_Station.IsPowered())
			return;

		float nowS = System.GetTickCount() * 0.001;
		if ((nowS - m_fLastIntelTxS) < 0.4)
			return;

		m_fLastIntelTxS = nowS;
		GBRS_PlayerControllerNet.RequestForceIntelTx(m_Station);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnOpticsToggleBtn(Widget w, float value, EActionTrigger reason)
	{
		GBRS_RadarStationHud.ToggleOpticsEnabled();
		UpdateOpticsToggleVisual();
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateOpticsToggleVisual()
	{
		GBRS_RadarStationHudWidgets widgets = GBRS_RadarStationHud.GetWidgets();
		if (!widgets)
			return;
		if (!widgets.m_wOpticsToggleBtn)
			return;

		bool on = GBRS_RadarStationHud.IsOpticsEnabled();
		if (on)
			widgets.m_wOpticsToggleBtn.SetColor(Color.FromRGBA(80, 210, 255, 255));
		else
			widgets.m_wOpticsToggleBtn.SetColor(Color.FromRGBA(58, 78, 74, 150));
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
		// Reserved: no operator-training addon yet.
		// m_iFocusedModeTab = 3;
		// ActivateFocusedModeTab();
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
		CycleModeTab(-1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnNavTabNext(SCR_InputButtonComponent button, string actionName)
	{
		CycleModeTab(1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnNavSelect(SCR_InputButtonComponent button, string actionName)
	{
		ActivateFocusedModeTab();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnNavParamPrev(SCR_InputButtonComponent button, string actionName)
	{
		OnParamOrZoomPrev();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnNavParamNext(SCR_InputButtonComponent button, string actionName)
	{
		OnParamOrZoomNext();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnNavParamDec(SCR_InputButtonComponent button, string actionName)
	{
		AdjustManualParam(-1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnNavParamInc(SCR_InputButtonComponent button, string actionName)
	{
		AdjustManualParam(1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnManualActionPrev()
	{
		OnParamOrZoomPrev();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnManualActionNext()
	{
		OnParamOrZoomNext();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnManualActionDec()
	{
		AdjustManualParam(-1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnManualActionInc()
	{
		AdjustManualParam(1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnParamOrZoomPrev()
	{
		if (m_ActiveMode == MODE_MANUAL)
			CycleManualParam(-1);
		else
			AdjustPpiZoom(-1);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnParamOrZoomNext()
	{
		if (m_ActiveMode == MODE_MANUAL)
			CycleManualParam(1);
		else
			AdjustPpiZoom(1);
	}

	//------------------------------------------------------------------------------------------------
	void AdjustPpiZoom(int direction)
	{
		if (direction == 0)
			return;

		if (!CanAcceptModeNav())
			return;

		float rfMax = GetRfRangeM();
		if (rfMax <= 0.0)
			rfMax = 7000.0;

		float minView = PPI_RANGE_MIN_M;
		if (minView > rfMax)
			minView = rfMax;

		float current = m_PpiViewRangeM;
		if (current <= 0.0)
			current = rfMax;

		int idx = NearestPpiRangeIndex(current);
		idx = idx + direction;
		if (idx < 0)
			idx = 0;
		if (idx >= PPI_RANGE_STEP_COUNT)
			idx = PPI_RANGE_STEP_COUNT - 1;

		float next = PpiRangeStepAt(idx);
		if (next > rfMax)
			next = rfMax;
		if (next < minView)
			next = minView;

		if (Math.AbsFloat(next - current) < 1.0)
			return;

		m_PpiViewRangeM = next;
		m_bPpiZoomManual = true;
		m_LastClusterS = 0.0;
		GBRS_RadarStationHud.SetDisplayRange(m_PpiViewRangeM);
	}

	//------------------------------------------------------------------------------------------------
	protected int NearestPpiRangeIndex(float rangeM)
	{
		int best = 0;
		float bestDiff = Math.AbsFloat(PpiRangeStepAt(0) - rangeM);
		int i = 1;
		while (i < PPI_RANGE_STEP_COUNT)
		{
			float diff = Math.AbsFloat(PpiRangeStepAt(i) - rangeM);
			if (diff < bestDiff)
			{
				best = i;
				bestDiff = diff;
			}
			i = i + 1;
		}
		return best;
	}

	//------------------------------------------------------------------------------------------------
	protected float PpiRangeStepAt(int index)
	{
		if (index <= 0)
			return 1000.0;
		if (index == 1)
			return 1500.0;
		if (index == 2)
			return 2000.0;
		if (index == 3)
			return 2500.0;
		if (index == 4)
			return 3000.0;
		if (index == 5)
			return 4000.0;
		if (index == 6)
			return 5000.0;
		if (index == 7)
			return 6000.0;
		if (index == 8)
			return 7000.0;
		if (index == 9)
			return 8000.0;
		if (index == 10)
			return 10000.0;
		if (index == 11)
			return 12000.0;
		if (index == 12)
			return 15000.0;
		if (index == 13)
			return 16000.0;
		return 18000.0;
	}

	//------------------------------------------------------------------------------------------------
	protected float GetRfRangeM()
	{
		if (!m_Station)
			return 0.0;

		RDF_RadarComponent radar = m_Station.GetRadarComponent();
		if (!radar)
			return 0.0;

		RDF_RadarSensor sensor = radar.GetSensor();
		if (!sensor)
			return 0.0;

		RDF_RadarSettings settings = sensor.GetSettings();
		if (settings)
			return settings.m_Range;

		return 0.0;
	}

	//------------------------------------------------------------------------------------------------
	protected float ResolvePpiViewRange(float rfRange)
	{
		if (rfRange <= 0.0)
			rfRange = 12000.0;

		float minView = PPI_RANGE_MIN_M;
		if (minView > rfRange)
			minView = rfRange;

		// Until the operator zooms, always track the live RF max (US 12 km /
		// USSR 16 km). An early FeedOnce used to lock onto a 2 km placeholder
		// before the first PPI snapshot arrived.
		if (!m_bPpiZoomManual || m_PpiViewRangeM <= 0.0)
		{
			m_PpiViewRangeM = rfRange;
			return m_PpiViewRangeM;
		}

		if (m_PpiViewRangeM > rfRange)
			m_PpiViewRangeM = rfRange;
		else if (m_PpiViewRangeM < minView)
			m_PpiViewRangeM = minView;

		return m_PpiViewRangeM;
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

		// LOCK on; MANUAL still reserved.
		int tabCount = 3;
		// int tabCount = MODE_TAB_COUNT;
		m_iFocusedModeTab = m_iFocusedModeTab + delta;
		while (m_iFocusedModeTab < 0)
			m_iFocusedModeTab = m_iFocusedModeTab + tabCount;
		while (m_iFocusedModeTab >= tabCount)
			m_iFocusedModeTab = m_iFocusedModeTab - tabCount;

		UpdateModeTabVisuals();
		FocusModeTabIndex(m_iFocusedModeTab);
		UpdateContextHint();
	}

	//------------------------------------------------------------------------------------------------
	// MANUAL mode: move the focused parameter up/down (MenuUp/MenuDown).
	protected void CycleManualParam(int delta)
	{
		if (m_ActiveMode != MODE_MANUAL)
			return;

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
		if (m_ActiveMode != MODE_MANUAL)
			return;

		if (!m_Station)
			return;

		if (!m_Station.IsManualMode())
			return;

		if (!m_Station.IsPowered())
			return;

		if (!CanAcceptModeNav())
			return;

		GBRS_RadarManualConfig cfg = m_Station.GetManualConfig();
		if (!cfg)
			return;

		int index = m_iFocusedManualParam;
		float current = cfg.GetParam(index);
		float step = GBRS_RadarManualConfig.StepParam(index);
		float next = current + step * direction;

		// STARE AZ: from OFF, any nudge parks at the live scan bearing.
		if (index == GBRS_RadarManualConfig.PARAM_STARE && current < 0.0)
			next = m_Station.GetLiveScanAngleDeg();

		if (!m_Station.ApplyManualParam(index, next))
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
		// MANUAL still reserved.
		// else if (m_iFocusedModeTab == 3)
		// 	nextMode = MODE_MANUAL;

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

		SetTabEmphasis(widgets.m_wModeTabPd, pdActive, Color.FromRGBA(70, 255, 170, 255));
		SetTabEmphasis(widgets.m_wModeTabWlr, wlrActive, Color.FromRGBA(255, 190, 70, 255));
		SetTabEmphasis(widgets.m_wModeTabLock, lockActive, Color.FromRGBA(255, 95, 80, 255));
		SetTabEmphasis(widgets.m_wModeTabManual, manualActive, Color.FromRGBA(80, 210, 255, 255));
	}

	//------------------------------------------------------------------------------------------------
	protected void SetTabEmphasis(Widget tab, bool active, Color activeColor)
	{
		if (!tab)
			return;

		if (active)
		{
			tab.SetColor(activeColor);
			return;
		}

		tab.SetColor(Color.FromRGBA(58, 78, 74, 150));
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

		// Keep the STARE AZ line in sync with the station's live stare state
		// (JIP / authority may have changed it without the config object).
		if (m_Station.IsAntennaStare())
			cfg.m_StareAzDeg = m_Station.GetAntennaStareAzDeg();
		else if (cfg.m_StareAzDeg >= 0.0)
			cfg.m_StareAzDeg = -1.0;

		if (m_iFocusedManualParam < 0)
			m_iFocusedManualParam = 0;
		if (m_iFocusedManualParam >= GBRS_RadarManualConfig.PARAM_COUNT)
			m_iFocusedManualParam = GBRS_RadarManualConfig.PARAM_COUNT - 1;

		string body = "";
		int i = 0;
		while (i < GBRS_RadarManualConfig.PARAM_COUNT)
		{
			string marker = "    ";
			if (i == m_iFocusedManualParam)
				marker = " >> ";

			string name = ManualParamName(i);
			float value = cfg.GetParam(i);
			string valueStr = ManualParamValue(i, value);

			string line = marker + PadParamName(name) + "  " + valueStr;
			if (body != "")
				body = body + "\n";
			body = body + line;
			i = i + 1;
		}

		GBRS_RadarStationHud.SetManualParamList(body, m_iFocusedManualParam);
		GBRS_RadarStationHud.SetManualParamFooter(
			"PARAM " + (m_iFocusedManualParam + 1).ToString()
			+ "/" + GBRS_RadarManualConfig.PARAM_COUNT.ToString()
			+ "  Up/Dn select  Left/Right value");
	}

	//------------------------------------------------------------------------------------------------
	protected string ManualParamName(int index)
	{
		switch (index)
		{
			case 0: return "RANGE";
			case 1: return "RPM";
			case 2: return "EL BORE";
			case 3: return "STARE AZ";
		}
		return "???";
	}

	//------------------------------------------------------------------------------------------------
	protected string ManualParamValue(int index, float value)
	{
		switch (index)
		{
			case 0:
			{
				if (value >= 1000.0)
					return (value / 1000.0).ToString(-1, 1) + " km";
				return value.ToString(-1, 0) + " m";
			}
			case 1: return value.ToString(-1, 0) + " rpm";
			case 2: return value.ToString(-1, 1) + " deg";
			case 3:
			{
				if (value < 0.0)
					return "OFF";
				return value.ToString(-1, 0) + " deg";
			}
		}
		return value.ToString();
	}

	//------------------------------------------------------------------------------------------------
	protected string PadParamName(string name)
	{
		while (name.Length() < 10)
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
			hint = "Up/Dn parameter   Left/Right value   wheel PPI range";

		widgets.m_wPpiHint.SetText(hint);
		UpdateNavParamHints();
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateNavParamHints()
	{
		GBRS_RadarStationHudWidgets widgets = GBRS_RadarStationHud.GetWidgets();
		if (!widgets)
			return;

		bool manual = false;
		if (m_ActiveMode == MODE_MANUAL)
			manual = true;

		string prevNextLabel = "PPI";
		if (manual)
			prevNextLabel = "Param";

		if (widgets.m_wHintParamPrev)
		{
			widgets.m_wHintParamPrev.SetVisible(true);
			SetInputButtonLabel(widgets.m_wHintParamPrev, prevNextLabel);
		}
		if (widgets.m_wHintParamNext)
		{
			widgets.m_wHintParamNext.SetVisible(true);
			SetInputButtonLabel(widgets.m_wHintParamNext, prevNextLabel);
		}
		if (widgets.m_wHintParamDec)
			widgets.m_wHintParamDec.SetVisible(manual);
		if (widgets.m_wHintParamInc)
			widgets.m_wHintParamInc.SetVisible(manual);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetInputButtonLabel(Widget w, string label)
	{
		if (!w)
			return;

		SCR_InputButtonComponent button =
			SCR_InputButtonComponent.Cast(w.FindHandler(SCR_InputButtonComponent));
		if (!button)
			return;

		button.SetLabel(label);
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
		RefreshInputButton(widgets.m_wHintParamPrev, ACTION_PARAM_PREV, device);
		RefreshInputButton(widgets.m_wHintParamNext, ACTION_PARAM_NEXT, device);
		RefreshInputButton(widgets.m_wHintParamDec, ACTION_PARAM_DEC, device);
		RefreshInputButton(widgets.m_wHintParamInc, ACTION_PARAM_INC, device);
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

		if (m_ActiveMode == MODE_MANUAL)
		{
			FocusManualList();
			return;
		}

		FocusModeTabIndex(m_iFocusedModeTab);
	}

	//------------------------------------------------------------------------------------------------
	protected void FocusManualList()
	{
		GBRS_RadarStationHudWidgets widgets = GBRS_RadarStationHud.GetWidgets();
		if (!widgets)
			return;

		Widget target = widgets.m_wListBody;
		if (!target)
			target = widgets.m_wListPanel;
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
		ClearPpiSnapshot();
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
		FocusForCurrentDevice();
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

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void EnsurePersistBuffer()
	{
		if (!m_PersistPlots)
			m_PersistPlots = new array<ref GBRS_PersistPlot>();
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
		if (m_ActiveMode == MODE_WLR)
			return 5.0;
		return PLOT_AFTERGLOW_S;
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

			RDF_RadarTarget existing = null;
			GBRS_PersistPlot held = FindPersistMatch(src);
			if (held)
				existing = held.m_Target;
			if (existing)
			{
				CopyPlot(src, existing, nowS);
				held.m_LastFreshS = nowS;
				continue;
			}

			if (m_PersistPlots.Count() >= PERSIST_MAX_BLIPS)
				RemoveOldestPersist();

			RDF_RadarTarget created = new RDF_RadarTarget();
			CopyPlot(src, created, nowS);
			GBRS_PersistPlot row = new GBRS_PersistPlot();
			row.m_Target = created;
			row.m_LastFreshS = nowS;
			m_PersistPlots.Insert(row);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected GBRS_PersistPlot FindPersistMatch(RDF_RadarTarget src)
	{
		if (!src || !m_PersistPlots)
			return null;

		int i = 0;
		if (src.m_ScattererId > 0)
		{
			while (i < m_PersistPlots.Count())
			{
				GBRS_PersistPlot row = m_PersistPlots.Get(i);
				i = i + 1;
				if (!row || !row.m_Target)
					continue;
				if (row.m_Target.m_ScattererId == src.m_ScattererId)
					return row;
			}
			return null;
		}

		i = 0;
		while (i < m_PersistPlots.Count())
		{
			GBRS_PersistPlot row = m_PersistPlots.Get(i);
			i = i + 1;
			if (!row || !row.m_Target)
				continue;
			if (row.m_Target.m_ScattererId > 0)
				continue;

			float gate = PERSIST_SPATIAL_M;
			vector d = row.m_Target.m_Position - src.m_Position;
			if (d.LengthSq() < (gate * gate))
				return row;
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
			GBRS_PersistPlot row = m_PersistPlots.Get(i);
			if (row && row.m_LastFreshS < oldestTime)
			{
				oldestTime = row.m_LastFreshS;
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
	protected void CoastPersist(float nowS)
	{
		if (!m_PersistPlots)
			return;

		m_LastPersistCoastS = nowS;
	}

	//------------------------------------------------------------------------------------------------
	protected void PrunePersist(float nowS, float lifeS)
	{
		if (!m_PersistPlots)
			return;

		int i = m_PersistPlots.Count() - 1;
		while (i >= 0)
		{
			GBRS_PersistPlot row = m_PersistPlots.Get(i);
			if (!row || !row.m_Target || (nowS - row.m_LastFreshS) > lifeS)
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
			GBRS_PersistPlot row = m_PersistPlots.Get(i);
			i = i + 1;
			if (!row || !row.m_Target)
				continue;

			RDF_RadarTarget src = row.m_Target;

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
					if (src.m_ScattererId > 0 && kept.m_ScattererId == src.m_ScattererId)
					{
						match = j;
						break;
					}

					IEntity keptRoot = null;
					if (j < displayRoots.Count())
						keptRoot = displayRoots.Get(j);

					if (srcRoot && keptRoot && srcRoot == keptRoot)
					{
						match = j;
						break;
					}

					vector d = kept.m_Position - src.m_Position;
					if (d.LengthSq() <= gateSq)
					{
						match = j;
						break;
					}

					if (PlotsSharePolarCell(kept, src, origin, rng))
					{
						match = j;
						break;
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
	protected bool PlotsSharePolarCell(
		RDF_RadarTarget a,
		RDF_RadarTarget b,
		vector origin,
		float rngB)
	{
		if (!a || !b)
			return false;

		vector da = a.m_Position - origin;
		vector db = b.m_Position - origin;
		float azA = Math.Atan2(da[0], da[2]) * Math.RAD2DEG;
		float azB = Math.Atan2(db[0], db[2]) * Math.RAD2DEG;
		float dAz = azA - azB;
		while (dAz > 180.0)
			dAz = dAz - 360.0;
		while (dAz < -180.0)
			dAz = dAz + 360.0;
		if (dAz < 0.0)
			dAz = -dAz;
		if (dAz > DISPLAY_CLUSTER_AZ_DEG)
			return false;

		float rngA = PlotRangeM(a, origin);
		float dRng = rngA - rngB;
		if (dRng < 0.0)
			dRng = -dRng;
		if (dRng > DISPLAY_CLUSTER_RANGE_M)
			return false;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	static void ApplyReplicatedSnapshot(
		RplId stationId,
		vector origin,
		float scanAzDeg,
		float rangeM,
		string eccm,
		array<int> packedInts,
		array<float> packedFloats)
	{
		MenuManager menuManager = GetGame().GetMenuManager();
		if (!menuManager)
			return;

		MenuBase existing = menuManager.FindMenuByPreset(ChimeraMenuPreset.GBRS_RadarStationMenu);
		GBRS_RadarStationMenu menu = GBRS_RadarStationMenu.Cast(existing);
		if (!menu)
			return;

		menu.StoreReplicatedSnapshot(
			stationId, origin, scanAzDeg, rangeM, eccm, packedInts, packedFloats);
	}

	//------------------------------------------------------------------------------------------------
	protected void StoreReplicatedSnapshot(
		RplId stationId,
		vector origin,
		float scanAzDeg,
		float rangeM,
		string eccm,
		array<int> packedInts,
		array<float> packedFloats)
	{
		if (!m_bBound || !m_Station)
			return;

		if (m_Station.GetStationRplId() != stationId)
			return;

		array<ref RDF_RadarTarget> plots;
		array<ref RDF_RadarTrack> tracks;
		array<ref RDF_RadarFusedTrack> fused;
		array<ref GBRS_WlrPersistDisplay> wlr;
		int detectedTotal;
		int netOnline;
		int lockedTrackId;
		if (!GBRS_PpiSnapshot.Unpack(
			packedInts, packedFloats, plots, tracks, fused, wlr, detectedTotal, netOnline, lockedTrackId))
			return;

		m_PpiSnapOrigin = origin;
		m_PpiSnapScanAzDeg = scanAzDeg;
		m_PpiSnapRangeM = rangeM;
		m_PpiSnapEccm = eccm;
		m_PpiSnapDetectedTotal = detectedTotal;
		m_PpiSnapNetOnline = netOnline;
		m_PpiSnapLockedTrackId = lockedTrackId;
		m_PpiSnapPlots = plots;
		m_PpiSnapTracks = tracks;
		m_PpiSnapFused = fused;
		m_PpiSnapWlr = wlr;
		m_bHasPpiSnapshot = true;
		GBRS_RadarStationHud.SetTrackCoastAnchor(System.GetTickCount() * 0.001);
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearPpiSnapshot()
	{
		m_bHasPpiSnapshot = false;
		m_PpiSnapOrigin = "0 0 0";
		m_PpiSnapScanAzDeg = 0.0;
		m_PpiSnapRangeM = 0.0;
		m_PpiSnapEccm = "";
		m_PpiSnapDetectedTotal = 0;
		m_PpiSnapNetOnline = 0;
		m_PpiSnapLockedTrackId = 0;
		if (m_PpiSnapPlots)
			m_PpiSnapPlots.Clear();
		if (m_PpiSnapTracks)
			m_PpiSnapTracks.Clear();
		if (m_PpiSnapFused)
			m_PpiSnapFused.Clear();
		if (m_PpiSnapWlr)
			m_PpiSnapWlr.Clear();
	}

	//------------------------------------------------------------------------------------------------
	bool TryLockPaintedTarget(int localX, int localY)
	{
		if (!m_bBound || !m_Station)
			return false;

		GBRS_RadarStationHudWidgets widgets = GBRS_RadarStationHud.GetWidgets();
		if (!widgets || !widgets.m_wPpiCanvas)
			return false;

		float canvasX;
		float canvasY;
		widgets.m_wPpiCanvas.GetScreenPos(canvasX, canvasY);
		float canvasW;
		float canvasH;
		widgets.m_wPpiCanvas.GetScreenSize(canvasW, canvasH);

		float hostX = 0.0;
		float hostY = 0.0;
		if (m_wPpiWheelHost)
			m_wPpiWheelHost.GetScreenPos(hostX, hostY);

		float px = (hostX + localX) - canvasX;
		float py = (hostY + localY) - canvasY;
		if (px < 0.0)
			return false;
		if (py < 0.0)
			return false;
		if (px > canvasW)
			return false;
		if (py > canvasH)
			return false;

		int trackId = GBRS_RadarStationHud.PickTrackIdAtCanvasPixels(px, py);
		if (trackId <= 0)
			return false;

		return m_Station.RequestLockTrack(trackId);
	}

	//------------------------------------------------------------------------------------------------
	void FeedOnce()
	{
		if (!m_bBound)
			return;

		if (!m_Station)
			return;

		if (!GBRS_RadarStationHud.IsVisible())
			GBRS_RadarStationHud.Attach(GetRootWidget(), m_Station.GetOwner());

		vector hudOrigin = m_Station.GetScanOriginWorld();
		// Sweep / wedge must track the live antenna every UI tick. Snapshot az
		// only arrives at PPI_SNAPSHOT_INTERVAL and makes the beam stutter.
		vector hudForward = m_Station.GetScanForwardWorld();
		// RF max from live settings (12 / 8 / 16 km). Never seed from the old
		// 2 km placeholder — that locked the PPI ring until the operator zoomed.
		float rfRange = GetRfRangeM();
		if (rfRange <= 0.0)
			rfRange = 12000.0;
		string eccm = "";
		array<ref RDF_RadarTarget> livePlots = null;
		array<ref RDF_RadarTrack> replicatedTracks = null;
		array<ref RDF_RadarFusedTrack> replicatedFused = null;
		array<ref GBRS_WlrPersistDisplay> replicatedWlr = null;
		int netOnline = -1;
		int detectedTotal = 0;

		if (m_bHasPpiSnapshot)
		{
			hudOrigin = m_PpiSnapOrigin;
			eccm = m_PpiSnapEccm;
			livePlots = m_PpiSnapPlots;
			replicatedTracks = m_PpiSnapTracks;
			replicatedFused = m_PpiSnapFused;
			replicatedWlr = m_PpiSnapWlr;
			netOnline = m_PpiSnapNetOnline;
			detectedTotal = m_PpiSnapDetectedTotal;
		}

		float viewRange = ResolvePpiViewRange(rfRange);
		if (m_bHasPpiSnapshot)
		{
			m_DisplayPlots = livePlots;
			m_DetectedInRange = detectedTotal;
		}
		else
		{
			// In local / Workbench scenarios the client is the authority and the
			// unreplicated snapshot has not arrived yet. Build the display list
			// from live RDF so the first feed already shows contacts.
			if (!m_DisplayPlots)
				m_DisplayPlots = new array<ref RDF_RadarTarget>();
			m_DisplayPlots.Clear();
			TickLocalPlots(detectedTotal, replicatedTracks);
		}

		GBRS_RadarStationHud.SetDisplayRange(viewRange);
		GBRS_RadarStationHud.SetMode(m_ActiveMode);
		GBRS_RadarStationHud.SetEccmStatus(eccm);
		int lockedTrackId = 0;
		if (m_bHasPpiSnapshot)
			lockedTrackId = m_PpiSnapLockedTrackId;
		GBRS_RadarStationHud.SetLockedTrackId(lockedTrackId);
		GBRS_RadarStationHud.FeedScan(
			m_DisplayPlots,
			hudOrigin,
			hudForward,
			viewRange,
			null,
			detectedTotal,
			null,
			replicatedTracks,
			replicatedFused,
			netOnline,
			replicatedWlr);
	}

	protected void TickLocalPlots(out int detectedTotal, out array<ref RDF_RadarTrack> tracks)
	{
		detectedTotal = 0;
		tracks = null;
		if (!m_Station)
			return;

		RDF_RadarComponent radar = m_Station.GetRadarComponent();
		if (!radar)
			return;

		RDF_RadarSensor sensor = radar.GetSensor();
		if (!sensor)
			return;

		RDF_RadarSettings settings = sensor.GetSettings();
		array<ref RDF_RadarTarget> live = sensor.GetPlots();
		if (!live)
			return;

		float nowS = System.GetTickCount() * 0.001;
		IngestLivePlots(live, settings, nowS);
		PrunePersist(nowS, GetPersistLifeS());
		BuildClusteredDisplayPlots(m_Station.GetScanOriginWorld(), GetRfRangeM());
		if (m_DisplayPlots)
			detectedTotal = m_DisplayPlots.Count();

		// Replicate tracks from the live sensor so the HUD track list is also
		// populated in local / single-player Workbench runs.
		RDF_RadarProjectileTracker tracker = sensor.GetTracker();
		if (tracker)
			tracks = tracker.GetAllTracks();
	}
}
