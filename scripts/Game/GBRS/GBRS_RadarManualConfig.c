//------------------------------------------------------------------------------------------------
//! Manual-mode radar parameters for the GBRS radar station workstation.
//!
//! The MANUAL workstation mode lets an operator tune radar behavior live:
//!   0 detection SNR gate (dB)
//!   1 DEM clutter scale
//!   2 scan RPM
//!   3 range (m)
//!   4 elevation boresight (deg)
//!   5 elevation beamwidth (deg)
//!   6 azimuth beamwidth (deg)
//!   7 update interval (s)
//!   8 peak power (W)
//!
//! All nine values are broadcast to peers (server-authoritative) and carried
//! in RplSave/RplLoad so JIP clients see the same tuning as everyone else.
class GBRS_RadarManualConfig
{
    // Defaults mirror the US RPL-5 SHORAD search preset so MANUAL starts
    // from a known-good operating point.
    float m_DetectionSnrDb = 8.0;
    float m_DemClutterScale = 1.0;
    float m_ScanRpm = 10.0;
    float m_RangeM = 7000.0;
    float m_ElevationBoresightDeg = 2.0;
    float m_ElevationBeamwidthDeg = 16.0;
    float m_AzimuthBeamwidthDeg = 2.5;
    float m_UpdateIntervalS = 0.02;
    float m_PeakPowerW = 120000.0;
    // Antenna stare bearing (0-360, north-up). -1 disables stare.
    float m_StareAzDeg = -1.0;

    static const int PARAM_COUNT = 10;

    //------------------------------------------------------------------------------------------------
    float GetParam(int index)
    {
        switch (index)
        {
            case 0: return m_DetectionSnrDb;
            case 1: return m_DemClutterScale;
            case 2: return m_ScanRpm;
            case 3: return m_RangeM;
            case 4: return m_ElevationBoresightDeg;
            case 5: return m_ElevationBeamwidthDeg;
            case 6: return m_AzimuthBeamwidthDeg;
            case 7: return m_UpdateIntervalS;
            case 8: return m_PeakPowerW;
            case 9: return m_StareAzDeg;
        }
        return 0.0;
    }

    //------------------------------------------------------------------------------------------------
    void SetParam(int index, float value)
    {
        switch (index)
        {
            case 0: m_DetectionSnrDb = value; break;
            case 1: m_DemClutterScale = value; break;
            case 2: m_ScanRpm = value; break;
            case 3: m_RangeM = value; break;
            case 4: m_ElevationBoresightDeg = value; break;
            case 5: m_ElevationBeamwidthDeg = value; break;
            case 6: m_AzimuthBeamwidthDeg = value; break;
            case 7: m_UpdateIntervalS = value; break;
            case 8: m_PeakPowerW = value; break;
            case 9: m_StareAzDeg = value; break;
        }
    }

    //------------------------------------------------------------------------------------------------
    // Clamp a candidate value for one parameter to its sane operating range.
    static float ClampParam(int index, float value)
    {
        switch (index)
        {
            case 0: // detection SNR gate dB
                if (value < -20.0) return -20.0;
                if (value > 30.0) return 30.0;
                return value;
            case 1: // DEM clutter scale
                if (value < 0.0) return 0.0;
                if (value > 2.0) return 2.0;
                return value;
            case 2: // scan RPM
                if (value < 1.0) return 1.0;
                if (value > 30.0) return 30.0;
                return value;
            case 3: // range m
                if (value < 1000.0) return 1000.0;
                if (value > 15000.0) return 15000.0;
                return value;
            case 4: // elevation boresight deg
                if (value < -5.0) return -5.0;
                if (value > 85.0) return 85.0;
                return value;
            case 5: // elevation beamwidth deg
                if (value < 1.0) return 1.0;
                if (value > 60.0) return 60.0;
                return value;
            case 6: // azimuth beamwidth deg
                if (value < 0.5) return 0.5;
                if (value > 60.0) return 60.0;
                return value;
            case 7: // update interval s
                if (value < 0.01) return 0.01;
                if (value > 2.0) return 2.0;
                return value;
            case 8: // peak power W
                if (value < 10000.0) return 10000.0;
                if (value > 2000000.0) return 2000000.0;
                return value;
            case 9: // stare bearing: -1 = disabled, else 0-360
                if (value < 0.0) return -1.0;
                if (value >= 360.0) return 0.0;
                return value;
        }
        return value;
    }

    //------------------------------------------------------------------------------------------------
    // Step size (per adjustment press) for one parameter.
    static float StepParam(int index)
    {
        switch (index)
        {
            case 0: return 1.0;        // 1 dB
            case 1: return 0.05;       // clutter scale
            case 2: return 1.0;        // 1 RPM
            case 3: return 500.0;      // 500 m
            case 4: return 1.0;        // 1 deg
            case 5: return 1.0;        // 1 deg
            case 6: return 0.5;        // 0.5 deg
            case 7: return 0.02;       // 20 ms
            case 8: return 50000.0;    // 50 kW
            case 9: return 5.0;        // 5 deg
        }
        return 1.0;
    }

    //------------------------------------------------------------------------------------------------
    void WriteRpl(ScriptBitWriter writer)
    {
        writer.WriteFloat(m_DetectionSnrDb);
        writer.WriteFloat(m_DemClutterScale);
        writer.WriteFloat(m_ScanRpm);
        writer.WriteFloat(m_RangeM);
        writer.WriteFloat(m_ElevationBoresightDeg);
        writer.WriteFloat(m_ElevationBeamwidthDeg);
        writer.WriteFloat(m_AzimuthBeamwidthDeg);
        writer.WriteFloat(m_UpdateIntervalS);
        writer.WriteFloat(m_PeakPowerW);
        writer.WriteFloat(m_StareAzDeg);
    }

    //------------------------------------------------------------------------------------------------
    void ReadRpl(ScriptBitReader reader)
    {
        float v;
        reader.ReadFloat(v); m_DetectionSnrDb = v;
        reader.ReadFloat(v); m_DemClutterScale = v;
        reader.ReadFloat(v); m_ScanRpm = v;
        reader.ReadFloat(v); m_RangeM = v;
        reader.ReadFloat(v); m_ElevationBoresightDeg = v;
        reader.ReadFloat(v); m_ElevationBeamwidthDeg = v;
        reader.ReadFloat(v); m_AzimuthBeamwidthDeg = v;
        reader.ReadFloat(v); m_UpdateIntervalS = v;
        reader.ReadFloat(v); m_PeakPowerW = v;
        reader.ReadFloat(v); m_StareAzDeg = v;
    }
}
