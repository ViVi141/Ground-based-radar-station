//------------------------------------------------------------------------------------------------
//! CONTACT CRT — mode / power status + tabular track list from snapshot data.
class GBRS_ContactListPanel
{
    static const int MAX_ROWS = 18;

    protected TextWidget m_wStatus;
    protected TextWidget m_wBody;
    protected TextWidget m_wFooter;

    //------------------------------------------------------------------------------------------------
    void Bind(TextWidget status, TextWidget body, TextWidget footer)
    {
        m_wStatus = status;
        m_wBody = body;
        m_wFooter = footer;
        if (m_wStatus)
            m_wStatus.SetVisible(true);
        if (m_wBody)
            m_wBody.SetVisible(true);
        if (m_wFooter)
            m_wFooter.SetVisible(true);
    }

    //------------------------------------------------------------------------------------------------
    void Destroy()
    {
        m_wStatus = null;
        m_wBody = null;
        m_wFooter = null;
    }

    //------------------------------------------------------------------------------------------------
    void DrawIdle(string status)
    {
        if (m_wStatus)
            m_wStatus.SetText(status);
        if (m_wBody)
            m_wBody.SetText("(no contacts)");
        if (m_wFooter)
            m_wFooter.SetText("");
    }

    //------------------------------------------------------------------------------------------------
    void DrawFrame(
        GBRS_RadarStationComponent station,
        array<ref RDF_RadarTarget> plots,
        array<ref RDF_RadarTrack> tracks,
        array<ref GBRS_WlrPersistDisplay> wlr,
        vector origin,
        string mode,
        string eccm,
        int detectedTotal,
        int netOnline,
        bool opticsOn)
    {
        bool powered = false;
        if (station)
            powered = station.IsPowered();

        string opticsLabel = "OPT OFF";
        if (opticsOn)
            opticsLabel = "OPT ON";

        string status = mode;
        if (powered)
            status = status + "  PWR ON";
        else
            status = status + "  PWR OFF";
        status = status + "  ";
        status = status + opticsLabel;
        if (eccm != string.Empty)
        {
            status = status + "  ";
            status = status + eccm;
        }
        if (m_wStatus)
            m_wStatus.SetText(status);

        string body = "";
        int rows = 0;
        if (mode == GBRS_RadarStationConstants.MODE_WLR)
            rows = AppendWlrRows(wlr, origin, body);
        else
            rows = AppendTrackRows(tracks, origin, body);

        if (rows < MAX_ROWS && plots && mode != GBRS_RadarStationConstants.MODE_WLR)
            rows = rows + AppendPlotRows(plots, origin, body, rows);

        if (body == string.Empty)
            body = "(no contacts)";

        if (m_wBody)
            m_wBody.SetText(body);

        if (m_wFooter)
        {
            string foot = "DET ";
            foot = foot + detectedTotal.ToString();
            foot = foot + "  NET ";
            foot = foot + netOnline.ToString();
            foot = foot + "  ROW ";
            foot = foot + rows.ToString();
            m_wFooter.SetText(foot);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected int AppendTrackRows(array<ref RDF_RadarTrack> tracks, vector origin, inout string body)
    {
        if (!tracks)
            return 0;

        int rows = 0;
        foreach (RDF_RadarTrack tr : tracks)
        {
            if (!tr)
                continue;
            if (rows >= MAX_ROWS)
                break;

            vector d = tr.m_FilteredPosition - origin;
            float az = Math.Atan2(d[0], d[2]) * Math.RAD2DEG;
            if (az < 0.0)
                az = az + 360.0;
            float rngKm = d.Length() * 0.001;
            float altM = tr.m_FilteredPosition[1];
            float spd = tr.m_FilteredVelocity.Length();

            string line = "T";
            line = line + Pad2(tr.m_TrackId);
            line = line + "  ";
            line = line + az.ToString(1, 0);
            line = line + "  ";
            line = line + rngKm.ToString(1, 1);
            line = line + "  ";
            line = line + altM.ToString(1, 0);
            line = line + "  ";
            line = line + spd.ToString(1, 0);
            if (tr.m_Coasting)
                line = line + "  CST";
            else
                line = line + "  TRK";

            if (body != string.Empty)
                body = body + "\n";
            body = body + line;
            rows = rows + 1;
        }
        return rows;
    }

    //------------------------------------------------------------------------------------------------
    protected int AppendPlotRows(
        array<ref RDF_RadarTarget> plots,
        vector origin,
        inout string body,
        int already)
    {
        if (!plots)
            return 0;

        int rows = 0;
        foreach (RDF_RadarTarget t : plots)
        {
            if (!t)
                continue;
            if (already + rows >= MAX_ROWS)
                break;

            vector d = t.m_Position - origin;
            float az = Math.Atan2(d[0], d[2]) * Math.RAD2DEG;
            if (az < 0.0)
                az = az + 360.0;
            float rngKm = d.Length() * 0.001;
            float snr = t.m_SnrDb;

            string line = "P  ";
            line = line + az.ToString(1, 0);
            line = line + "  ";
            line = line + rngKm.ToString(1, 1);
            line = line + "  SNR ";
            line = line + snr.ToString(1, 1);

            if (body != string.Empty)
                body = body + "\n";
            body = body + line;
            rows = rows + 1;
        }
        return rows;
    }

    //------------------------------------------------------------------------------------------------
    protected int AppendWlrRows(
        array<ref GBRS_WlrPersistDisplay> wlr,
        vector origin,
        inout string body)
    {
        if (!wlr)
            return 0;

        int rows = 0;
        foreach (GBRS_WlrPersistDisplay entry : wlr)
        {
            if (!entry)
                continue;
            if (rows >= MAX_ROWS)
                break;
            if (!entry.m_HasLaunch && !entry.m_HasImpact && !entry.m_HasLive)
                continue;

            vector pos = origin;
            if (entry.m_HasLive)
                pos = entry.m_LivePos;
            else if (entry.m_HasLaunch)
                pos = entry.m_LaunchPos;
            else
                pos = entry.m_ImpactPos;

            vector d = pos - origin;
            float az = Math.Atan2(d[0], d[2]) * Math.RAD2DEG;
            if (az < 0.0)
                az = az + 360.0;
            float rngKm = d.Length() * 0.001;

            string id = entry.m_Id;
            if (id == string.Empty)
            {
                id = "W";
                id = id + Pad2(entry.m_TrackId);
            }

            string flags = "";
            if (entry.m_HasLaunch)
                flags = flags + "L";
            if (entry.m_HasImpact)
                flags = flags + "I";
            if (entry.m_HasLive)
                flags = flags + "V";

            string line = id;
            line = line + "  ";
            line = line + az.ToString(1, 0);
            line = line + "  ";
            line = line + rngKm.ToString(1, 1);
            line = line + "  ";
            line = line + flags;

            if (body != string.Empty)
                body = body + "\n";
            body = body + line;
            rows = rows + 1;
        }
        return rows;
    }

    //------------------------------------------------------------------------------------------------
    protected string Pad2(int value)
    {
        if (value < 0)
            value = 0;
        if (value < 10)
            return "0" + value.ToString();
        return value.ToString();
    }
}
