// Widget registry for RadarStationHUD.layout — bind every panel part by name.
// Geometry stays in the layout; this class only holds typed references.
class GBRS_RadarStationHudWidgets
{
    // Root / columns
    Widget m_wRoot;
    Widget m_wModeBar;
    Widget m_wModeTabs;
    Widget m_wModeTabPd;
    Widget m_wModeTabWlr;
    Widget m_wModeTabLock;
    Widget m_wModeTabManual;
    Widget m_wIntelTxBtn;
    Widget m_wNavHints;
    Widget m_wHintTabPrev;
    Widget m_wHintTabNext;
    Widget m_wHintSelect;
    Widget m_wHintClose;
    Widget m_wHintParamPrev;
    Widget m_wHintParamNext;
    Widget m_wHintParamDec;
    Widget m_wHintParamInc;
    Widget m_wStationRow;
    Widget m_wLeftColumn;
    Widget m_wOpticsPanel;
    Widget m_wAzElPanel;
    Widget m_wPpiPanel;
    Widget m_wListPanel;
    Widget m_wOpticsBezel;
    Widget m_wAzElBezel;
    Widget m_wPpiBezel;
    Widget m_wListBezel;
    Widget m_wModeBarBg;

    // Optics panel
    TextWidget m_wOpticsTitle;
    TextWidget m_wOpticsInfo;
    Widget m_wOpticsPlaceholder;
    RenderTargetWidget m_wOpticsRT;

    // Az/El panel
    TextWidget m_wAzElTitle;
    Widget m_wAzElPlotFrame;
    ImageWidget m_wAzElFaceBg;
    CanvasWidget m_wAzElCanvas;
    TextWidget m_wAzElEl0;
    TextWidget m_wAzElEl1;
    TextWidget m_wAzElEl2;
    TextWidget m_wAzElEl3;
    TextWidget m_wAzElEl4;
    TextWidget m_wAzElAz0;
    TextWidget m_wAzElAz1;
    TextWidget m_wAzElAz2;
    TextWidget m_wAzElAz3;
    TextWidget m_wAzElAz4;
    TextWidget m_wAzElAz5;
    TextWidget m_wAzElAz6;

    // PPI panel
    TextWidget m_wPpiTitle;
    TextWidget m_wPpiMode;
    TextWidget m_wPpiRange;
    TextWidget m_wPpiHint;
    Widget m_wPpiFaceHost;
    ImageWidget m_wPpiDiscBg;
    ImageWidget m_wPpiRing100;
    ImageWidget m_wPpiFaceImage;
    CanvasWidget m_wPpiCanvas;

    // Contacts panel
    TextWidget m_wListTitle;
    TextWidget m_wListHeader;
    TextWidget m_wListBody;
    TextWidget m_wListFooter;
    Widget m_wListTable;
    TextWidget m_wListHType;
    TextWidget m_wListBNr;
    TextWidget m_wListBAz;
    TextWidget m_wListBRng;
    TextWidget m_wListBAlt;
    TextWidget m_wListBSpd;
    TextWidget m_wListBType;
    TextWidget m_wListBSnr;

    bool Init(Widget root)
    {
        if (!root)
            return false;

        m_wRoot = root;
        m_wModeBar = root.FindAnyWidget("ModeBar");
        m_wModeTabs = root.FindAnyWidget("ModeTabs");
        m_wModeTabPd = root.FindAnyWidget("ModeTabPd");
        m_wModeTabWlr = root.FindAnyWidget("ModeTabWlr");
        m_wModeTabLock = root.FindAnyWidget("ModeTabLock");
        m_wModeTabManual = root.FindAnyWidget("ModeTabManual");
        m_wIntelTxBtn = root.FindAnyWidget("IntelTxBtn");
        m_wNavHints = root.FindAnyWidget("NavHints");
        m_wHintTabPrev = root.FindAnyWidget("HintTabPrev");
        m_wHintTabNext = root.FindAnyWidget("HintTabNext");
        m_wHintSelect = root.FindAnyWidget("HintSelect");
        m_wHintClose = root.FindAnyWidget("HintClose");
        m_wHintParamPrev = root.FindAnyWidget("HintParamPrev");
        m_wHintParamNext = root.FindAnyWidget("HintParamNext");
        m_wHintParamDec = root.FindAnyWidget("HintParamDec");
        m_wHintParamInc = root.FindAnyWidget("HintParamInc");
        m_wStationRow = root.FindAnyWidget("StationRow");
        m_wLeftColumn = root.FindAnyWidget("LeftColumn");
        m_wOpticsBezel = root.FindAnyWidget("OpticsBezel");
        m_wAzElBezel = root.FindAnyWidget("AzElBezel");
        m_wPpiBezel = root.FindAnyWidget("PpiBezel");
        m_wListBezel = root.FindAnyWidget("ListBezel");
        m_wModeBarBg = root.FindAnyWidget("ModeBarBg");

        m_wOpticsPanel = root.FindAnyWidget("OpticsPanel");
        m_wOpticsTitle = TextWidget.Cast(root.FindAnyWidget("OpticsTitle"));
        m_wOpticsInfo = TextWidget.Cast(root.FindAnyWidget("OpticsInfo"));
        m_wOpticsPlaceholder = root.FindAnyWidget("OpticsPlaceholder");
        m_wOpticsRT = RenderTargetWidget.Cast(root.FindAnyWidget("OpticsRenderTarget"));

        m_wAzElPanel = root.FindAnyWidget("AzElPanel");
        m_wAzElTitle = TextWidget.Cast(root.FindAnyWidget("AzElTitle"));
        m_wAzElPlotFrame = root.FindAnyWidget("AzElPlotFrame");
        m_wAzElFaceBg = ImageWidget.Cast(root.FindAnyWidget("AzElFaceBg"));
        m_wAzElCanvas = CanvasWidget.Cast(root.FindAnyWidget("AzElCanvas"));
        m_wAzElEl0 = TextWidget.Cast(root.FindAnyWidget("AzElEl0"));
        m_wAzElEl1 = TextWidget.Cast(root.FindAnyWidget("AzElEl1"));
        m_wAzElEl2 = TextWidget.Cast(root.FindAnyWidget("AzElEl2"));
        m_wAzElEl3 = TextWidget.Cast(root.FindAnyWidget("AzElEl3"));
        m_wAzElEl4 = TextWidget.Cast(root.FindAnyWidget("AzElEl4"));
        m_wAzElAz0 = TextWidget.Cast(root.FindAnyWidget("AzElAz0"));
        m_wAzElAz1 = TextWidget.Cast(root.FindAnyWidget("AzElAz1"));
        m_wAzElAz2 = TextWidget.Cast(root.FindAnyWidget("AzElAz2"));
        m_wAzElAz3 = TextWidget.Cast(root.FindAnyWidget("AzElAz3"));
        m_wAzElAz4 = TextWidget.Cast(root.FindAnyWidget("AzElAz4"));
        m_wAzElAz5 = TextWidget.Cast(root.FindAnyWidget("AzElAz5"));
        m_wAzElAz6 = TextWidget.Cast(root.FindAnyWidget("AzElAz6"));

        m_wPpiPanel = root.FindAnyWidget("PpiPanel");
        m_wPpiTitle = TextWidget.Cast(root.FindAnyWidget("PpiTitle"));
        m_wPpiMode = TextWidget.Cast(root.FindAnyWidget("PpiMode"));
        m_wPpiRange = TextWidget.Cast(root.FindAnyWidget("PpiRange"));
        m_wPpiHint = TextWidget.Cast(root.FindAnyWidget("PpiHint"));
        m_wPpiFaceHost = root.FindAnyWidget("PpiFaceHost");
        m_wPpiDiscBg = ImageWidget.Cast(root.FindAnyWidget("PpiDiscBg"));
        m_wPpiRing100 = ImageWidget.Cast(root.FindAnyWidget("PpiRing100"));
        m_wPpiFaceImage = ImageWidget.Cast(root.FindAnyWidget("PpiFaceImage"));
        m_wPpiCanvas = CanvasWidget.Cast(root.FindAnyWidget("PpiCanvas"));

        m_wListPanel = root.FindAnyWidget("ListPanel");
        m_wListTitle = TextWidget.Cast(root.FindAnyWidget("ListTitle"));
        m_wListHeader = TextWidget.Cast(root.FindAnyWidget("ListHeader"));
        m_wListBody = TextWidget.Cast(root.FindAnyWidget("ListBody"));
        m_wListFooter = TextWidget.Cast(root.FindAnyWidget("ListFooter"));
        m_wListTable = root.FindAnyWidget("ListTable");
        m_wListHType = TextWidget.Cast(root.FindAnyWidget("ListH_Type"));
        m_wListBNr = TextWidget.Cast(root.FindAnyWidget("ListB_Nr"));
        m_wListBAz = TextWidget.Cast(root.FindAnyWidget("ListB_Az"));
        m_wListBRng = TextWidget.Cast(root.FindAnyWidget("ListB_Rng"));
        m_wListBAlt = TextWidget.Cast(root.FindAnyWidget("ListB_Alt"));
        m_wListBSpd = TextWidget.Cast(root.FindAnyWidget("ListB_Spd"));
        m_wListBType = TextWidget.Cast(root.FindAnyWidget("ListB_Type"));
        m_wListBSnr = TextWidget.Cast(root.FindAnyWidget("ListB_Snr"));

        bool ok = true;
        ok = Require(m_wOpticsPanel, "OpticsPanel") && ok;
        ok = Require(m_wAzElPanel, "AzElPanel") && ok;
        ok = Require(m_wPpiPanel, "PpiPanel") && ok;
        ok = Require(m_wListPanel, "ListPanel") && ok;
        ok = Require(m_wOpticsRT, "OpticsRenderTarget") && ok;
        ok = Require(m_wAzElCanvas, "AzElCanvas") && ok;
        ok = Require(m_wAzElFaceBg, "AzElFaceBg") && ok;
        ok = Require(m_wPpiCanvas, "PpiCanvas") && ok;
        ok = Require(m_wPpiFaceHost, "PpiFaceHost") && ok;
        ok = Require(m_wPpiDiscBg, "PpiDiscBg") && ok;
        ok = Require(m_wPpiRing100, "PpiRing100") && ok;
        ok = Require(m_wPpiFaceImage, "PpiFaceImage") && ok;
        ok = Require(m_wOpticsInfo, "OpticsInfo") && ok;
        ok = Require(m_wPpiMode, "PpiMode") && ok;
        ok = Require(m_wPpiRange, "PpiRange") && ok;
        ok = Require(m_wListBody, "ListBody") && ok;
        ok = Require(m_wListFooter, "ListFooter") && ok;
        ok = Require(m_wListTable, "ListTable") && ok;
        ok = Require(m_wListBNr, "ListB_Nr") && ok;
        ok = Require(m_wListBAz, "ListB_Az") && ok;
        ok = Require(m_wListBRng, "ListB_Rng") && ok;
        ok = Require(m_wListBAlt, "ListB_Alt") && ok;
        ok = Require(m_wListBSpd, "ListB_Spd") && ok;
        ok = Require(m_wListBType, "ListB_Type") && ok;
        ok = Require(m_wListBSnr, "ListB_Snr") && ok;
        return ok;
    }

    protected bool Require(Widget w, string name)
    {
        if (w)
            return true;

        Print("[GBRS HUD] missing widget: " + name, LogLevel.ERROR);
        return false;
    }

    void Clear()
    {
        m_wRoot = null;
        m_wModeBar = null;
        m_wModeTabs = null;
        m_wModeTabPd = null;
        m_wModeTabWlr = null;
        m_wModeTabLock = null;
        m_wModeTabManual = null;
        m_wIntelTxBtn = null;
        m_wNavHints = null;
        m_wHintTabPrev = null;
        m_wHintTabNext = null;
        m_wHintSelect = null;
        m_wHintClose = null;
        m_wHintParamPrev = null;
        m_wHintParamNext = null;
        m_wHintParamDec = null;
        m_wHintParamInc = null;
        m_wStationRow = null;
        m_wLeftColumn = null;
        m_wOpticsPanel = null;
        m_wAzElPanel = null;
        m_wPpiPanel = null;
        m_wListPanel = null;
        m_wOpticsBezel = null;
        m_wAzElBezel = null;
        m_wPpiBezel = null;
        m_wListBezel = null;
        m_wModeBarBg = null;
        m_wOpticsTitle = null;
        m_wOpticsInfo = null;
        m_wOpticsPlaceholder = null;
        m_wOpticsRT = null;
        m_wAzElTitle = null;
        m_wAzElPlotFrame = null;
        m_wAzElFaceBg = null;
        m_wAzElCanvas = null;
        m_wAzElEl0 = null;
        m_wAzElEl1 = null;
        m_wAzElEl2 = null;
        m_wAzElEl3 = null;
        m_wAzElEl4 = null;
        m_wAzElAz0 = null;
        m_wAzElAz1 = null;
        m_wAzElAz2 = null;
        m_wAzElAz3 = null;
        m_wAzElAz4 = null;
        m_wAzElAz5 = null;
        m_wAzElAz6 = null;
        m_wPpiTitle = null;
        m_wPpiMode = null;
        m_wPpiRange = null;
        m_wPpiHint = null;
        m_wPpiFaceHost = null;
        m_wPpiDiscBg = null;
        m_wPpiRing100 = null;
        m_wPpiFaceImage = null;
        m_wPpiCanvas = null;
        m_wListTitle = null;
        m_wListHeader = null;
        m_wListBody = null;
        m_wListFooter = null;
        m_wListTable = null;
        m_wListHType = null;
        m_wListBNr = null;
        m_wListBAz = null;
        m_wListBRng = null;
        m_wListBAlt = null;
        m_wListBSpd = null;
        m_wListBType = null;
        m_wListBSnr = null;
    }
}
