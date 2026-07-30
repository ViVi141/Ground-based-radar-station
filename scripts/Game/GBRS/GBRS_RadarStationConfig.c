// Faction-specific RDF_RadarSettings factories for ground radar stations.
class GBRS_RadarStationConfig
{
    // US RPL-5 style: medium-range search with mechanical scan at 15 RPM.
    static RDF_RadarSettings CreateUsSearch()
    {
        RDF_RadarSettings settings = RDF_RadarDemoConfig.CreateDefault(64);
        settings.m_Range = 4000.0;
        settings.m_SectorHalfAngleDeg = 180.0;
        settings.m_EnableMechanicalScan = true;
        settings.m_OriginOffset = "0 3.5 0";
        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_ScanRpm = 15.0;
            settings.m_Hardware.Validate();
        }
        settings.Validate();
        return settings;
    }

    // USSR TPN-19 / P-18-like: longer range, 6 RPM mechanical scan.
    static RDF_RadarSettings CreateUssrSearch()
    {
        RDF_RadarSettings settings = RDF_RadarDemoConfig.CreateP18Like(128);
        settings.m_OriginOffset = "0 2.5 0";
        if (settings.m_Hardware)
        {
            settings.m_Hardware.m_ScanRpm = 6.0;
            settings.m_Hardware.Validate();
        }
        settings.Validate();
        return settings;
    }
}
