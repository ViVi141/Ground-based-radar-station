// Debug helpers for GBRS radar detection diagnosis (console Print).
// Tracks overall nearest AND nearest Vehicle separately — mounted Hydra ammo
// and pad infantry otherwise steal the "nearest" line and hide airframes.
class GBRS_RadarDebugProbe
{
    vector m_Origin;
    vector m_Forward;
    float m_CosHalf;
    float m_RangeSq;
    float m_MinDistSq;
    IEntity m_Exclude;

    int m_CandidateCount;
    int m_InConeCount;
    float m_NearestDist;
    float m_NearestDot;
    float m_NearestElDeg;
    string m_NearestType;
    string m_NearestName;

    int m_VehicleCount;
    int m_VehicleInConeCount;
    float m_NearestVehicleDist;
    float m_NearestVehicleDot;
    float m_NearestVehicleElDeg;
    string m_NearestVehicleName;
    vector m_NearestVehicleLosEnd;
    IEntity m_NearestVehicleEntity;
    bool m_bHasNearestVehicle;

    void Reset(
        vector origin,
        vector forward,
        float coneHalfDeg,
        float rangeM,
        float minDistM,
        IEntity exclude)
    {
        m_Origin = origin;
        m_Forward = forward;
        float flen = m_Forward.Length();
        if (flen > 0.001)
            m_Forward = m_Forward * (1.0 / flen);
        else
            m_Forward = Vector(1.0, 0.0, 0.0);

        float halfRad = coneHalfDeg * 0.017453292519943295;
        m_CosHalf = Math.Cos(halfRad);
        m_RangeSq = rangeM * rangeM;
        m_MinDistSq = minDistM * minDistM;
        m_Exclude = exclude;

        m_CandidateCount = 0;
        m_InConeCount = 0;
        m_NearestDist = 1.0e30;
        m_NearestDot = -2.0;
        m_NearestElDeg = 0.0;
        m_NearestType = "-";
        m_NearestName = "-";

        m_VehicleCount = 0;
        m_VehicleInConeCount = 0;
        m_NearestVehicleDist = 1.0e30;
        m_NearestVehicleDot = -2.0;
        m_NearestVehicleElDeg = 0.0;
        m_NearestVehicleName = "-";
        m_NearestVehicleLosEnd = "0 0 0";
        m_NearestVehicleEntity = null;
        m_bHasNearestVehicle = false;
    }

    bool FilterEntity(IEntity ent)
    {
        if (!ent)
            return false;
        if (ent == m_Exclude)
            return false;
        return RDF_RadarEntityClassifier.IsRadarCandidate(ent);
    }

    bool CollectEntity(IEntity ent)
    {
        if (!ent)
            return true;
        if (ent == m_Exclude)
            return true;

        vector pos = RDF_RadarScanGeometry.GetEntityLosEnd(ent);
        vector toTarget = pos - m_Origin;
        float distSq = toTarget.LengthSq();
        if (distSq < m_MinDistSq || distSq > m_RangeSq)
            return true;

        m_CandidateCount = m_CandidateCount + 1;
        float dist = Math.Sqrt(distSq);
        vector toNorm = toTarget * (1.0 / dist);

        float horiz = Math.Sqrt(toNorm[0] * toNorm[0] + toNorm[2] * toNorm[2]);
        float elDeg = Math.Atan2(toNorm[1], Math.Max(0.001, horiz)) * Math.RAD2DEG;
        float dot = -2.0;
        if (horiz > 0.001)
        {
            float invHoriz = 1.0 / horiz;
            // Match RDF scanner dwell filtering: test azimuth separately
            // from elevation beams.
            dot = m_Forward[0] * toNorm[0] * invHoriz
                + m_Forward[2] * toNorm[2] * invHoriz;
        }

        bool inCone = false;
        if (dot >= m_CosHalf)
        {
            inCone = true;
            m_InConeCount = m_InConeCount + 1;
        }

        if (dist < m_NearestDist)
        {
            m_NearestDist = dist;
            m_NearestDot = dot;
            m_NearestElDeg = elDeg;
            m_NearestType = DescribeType(ent);
            m_NearestName = DescribeName(ent);
        }

        if (Vehicle.Cast(ent))
        {
            m_VehicleCount = m_VehicleCount + 1;
            if (inCone)
                m_VehicleInConeCount = m_VehicleInConeCount + 1;

            if (dist < m_NearestVehicleDist)
            {
                m_NearestVehicleDist = dist;
                m_NearestVehicleDot = dot;
                m_NearestVehicleElDeg = elDeg;
                m_NearestVehicleName = DescribeName(ent);
                m_NearestVehicleLosEnd = pos;
                m_NearestVehicleEntity = ent;
                m_bHasNearestVehicle = true;
            }
        }

        return true;
    }

    protected string DescribeType(IEntity ent)
    {
        if (RDF_RadarEntityClassifier.IsProjectile(ent))
            return "projectile";
        if (Vehicle.Cast(ent))
            return "vehicle";
        if (ChimeraCharacter.Cast(ent))
            return "character";
        if (RDF_RadarEntityClassifier.IsVehicleOrCharacter(ent))
            return "veh/char";
        return "other";
    }

    protected string DescribeName(IEntity ent)
    {
        EntityPrefabData prefabData = ent.GetPrefabData();
        if (!prefabData)
            return ent.ToString();

        ResourceName prefabName = prefabData.GetPrefabName();
        string path = prefabName;
        int slash = path.LastIndexOf("/");
        if (slash >= 0 && slash + 1 < path.Length())
            path = path.Substring(slash + 1, path.Length() - slash - 1);
        return path;
    }
}
