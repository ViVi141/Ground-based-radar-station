//------------------------------------------------------------------------------------------------
//! Operator knobs for MANUAL workstation mode.
//!
//! Only range, scan rate, elevation boresight, and stare bearing are exposed.
//! RF balance (SNR gate, clutter, beamwidth, power, dwell) stays on the
//! faction search preset so an operator cannot unbalance detection.
class GBRS_RadarManualConfig
{
    static const int PARAM_RANGE = 0;
    static const int PARAM_RPM = 1;
    static const int PARAM_EL_BORE = 2;
    static const int PARAM_STARE = 3;
    static const int PARAM_COUNT = 4;

    float m_RangeM = 12000.0;
    float m_ScanRpm = 10.0;
    float m_ElevationBoresightDeg = 2.0;
    float m_StareAzDeg = -1.0;

    // Hidden RF — copied from the faction search preset, not operator-tunable.
    float m_DetectionSnrDb = 8.0;
    float m_DemClutterScale = 1.0;
    float m_ElevationBeamwidthDeg = 16.0;
    float m_AzimuthBeamwidthDeg = 2.5;
    float m_UpdateIntervalS = 0.04;
    float m_PeakPowerW = 120000.0;
    float m_RangeMaxM = 12000.0;

    //------------------------------------------------------------------------------------------------
    void SeedFromFaction(EGBRS_RadarFactionPreset preset)
    {
        if (preset == EGBRS_RadarFactionPreset.USSR)
        {
            m_RangeM = 16000.0;
            m_RangeMaxM = 16000.0;
            m_ScanRpm = 6.0;
            m_ElevationBoresightDeg = 2.0;
            m_DetectionSnrDb = 5.0;
            m_DemClutterScale = 0.10;
            m_ElevationBeamwidthDeg = 16.0;
            m_AzimuthBeamwidthDeg = 6.0;
            m_UpdateIntervalS = 0.04;
            m_PeakPowerW = 350000.0;
            return;
        }

        m_RangeM = 12000.0;
        m_RangeMaxM = 12000.0;
        m_ScanRpm = 10.0;
        m_ElevationBoresightDeg = 2.0;
        m_DetectionSnrDb = 8.0;
        m_DemClutterScale = 1.0;
        m_ElevationBeamwidthDeg = 16.0;
        m_AzimuthBeamwidthDeg = 2.5;
        m_UpdateIntervalS = 0.04;
        m_PeakPowerW = 120000.0;
    }

    //------------------------------------------------------------------------------------------------
    float GetParam(int index)
    {
        if (index == PARAM_RANGE)
            return m_RangeM;
        if (index == PARAM_RPM)
            return m_ScanRpm;
        if (index == PARAM_EL_BORE)
            return m_ElevationBoresightDeg;
        if (index == PARAM_STARE)
            return m_StareAzDeg;
        return 0.0;
    }

    //------------------------------------------------------------------------------------------------
    void SetParam(int index, float value)
    {
        if (index == PARAM_RANGE)
            m_RangeM = value;
        else if (index == PARAM_RPM)
            m_ScanRpm = value;
        else if (index == PARAM_EL_BORE)
            m_ElevationBoresightDeg = value;
        else if (index == PARAM_STARE)
            m_StareAzDeg = value;
    }

    //------------------------------------------------------------------------------------------------
    static float ClampParam(int index, float value, float rangeMaxM)
    {
        if (index == PARAM_RANGE)
        {
            if (value < 1000.0)
                return 1000.0;
            if (value > rangeMaxM)
                return rangeMaxM;
            return value;
        }
        if (index == PARAM_RPM)
        {
            if (value < 1.0)
                return 1.0;
            if (value > 15.0)
                return 15.0;
            return value;
        }
        if (index == PARAM_EL_BORE)
        {
            if (value < -5.0)
                return -5.0;
            if (value > 45.0)
                return 45.0;
            return value;
        }
        if (index == PARAM_STARE)
        {
            if (value < 0.0)
                return -1.0;
            if (value >= 360.0)
                return 0.0;
            return value;
        }
        return value;
    }

    //------------------------------------------------------------------------------------------------
    static float StepParam(int index)
    {
        if (index == PARAM_RANGE)
            return 500.0;
        if (index == PARAM_RPM)
            return 1.0;
        if (index == PARAM_EL_BORE)
            return 1.0;
        if (index == PARAM_STARE)
            return 5.0;
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
        writer.WriteFloat(m_RangeMaxM);
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
        reader.ReadFloat(v); m_RangeMaxM = v;
    }
}
