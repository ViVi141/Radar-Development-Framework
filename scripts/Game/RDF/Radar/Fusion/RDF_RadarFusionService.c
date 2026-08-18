// Authority-side multi-radar association + optional dual-station cross-fix.
// Consumes DatalinkHub confirmed-track summaries only (no raw plots / Trace).
class RDF_RadarFusionService
{
    protected static ref RDF_RadarFusionService s_Instance;

    protected float m_AssocGateM = 350.0;
    protected float m_AssocSpeedGateMs = 80.0;
    protected float m_MinCrossFixAngleDeg = 15.0;
    protected int m_NextFusedId = 1;

    static RDF_RadarFusionService Get()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarFusionService();
        return s_Instance;
    }

    static void ResetForTests()
    {
        s_Instance = null;
    }

    void SetAssocGateM(float gateM)
    {
        m_AssocGateM = Math.Clamp(gateM, 10.0, 5000.0);
    }

    void SetAssocSpeedGateMs(float speedMs)
    {
        m_AssocSpeedGateMs = Math.Clamp(speedMs, 1.0, 500.0);
    }

    void SetMinCrossFixAngleDeg(float deg)
    {
        m_MinCrossFixAngleDeg = Math.Clamp(deg, 5.0, 90.0);
    }

    void UpdateFromHub(RDF_RadarDatalinkHub hub, float worldTimeS)
    {
        if (!hub)
            return;
        array<ref RDF_RadarDatalinkTrack> src = hub.GetAllTracks();
        array<ref RDF_RadarFusedTrack> fused = FuseTracks(src, worldTimeS);
        hub.SetFusedTracks(fused);
    }

    array<ref RDF_RadarFusedTrack> FuseTracks(
        array<ref RDF_RadarDatalinkTrack> tracks,
        float worldTimeS)
    {
        array<ref RDF_RadarFusedTrack> outFused = new array<ref RDF_RadarFusedTrack>();
        if (!tracks || tracks.Count() < 1)
            return outFused;

        array<bool> used = new array<bool>();
        for (int u = 0; u < tracks.Count(); u++)
            used.Insert(false);

        for (int i = 0; i < tracks.Count(); i++)
        {
            if (used.Get(i))
                continue;
            RDF_RadarDatalinkTrack a = tracks.Get(i);
            if (!a)
            {
                used.Set(i, true);
                continue;
            }
            used.Set(i, true);

            RDF_RadarDatalinkTrack b = null;
            int bIndex = -1;
            float bestDist2 = m_AssocGateM * m_AssocGateM;
            for (int j = i + 1; j < tracks.Count(); j++)
            {
                if (used.Get(j))
                    continue;
                RDF_RadarDatalinkTrack cand = tracks.Get(j);
                if (!cand)
                    continue;
                if (cand.m_SourceRadarId == a.m_SourceRadarId)
                    continue;
                if (!PassesAssociationGate(a, cand))
                    continue;
                vector dp = cand.m_WorldPos - a.m_WorldPos;
                float d2 = dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2];
                if (d2 < bestDist2)
                {
                    bestDist2 = d2;
                    b = cand;
                    bIndex = j;
                }
            }

            RDF_RadarFusedTrack fused = new RDF_RadarFusedTrack();
            fused.m_FusedId = m_NextFusedId;
            m_NextFusedId = m_NextFusedId + 1;
            fused.m_TimeS = worldTimeS;
            fused.m_Type = a.m_Type;
            fused.m_NctrClass = a.m_NctrClass;
            fused.m_NctrConfidence = a.m_NctrConfidence;
            fused.m_Confidence = a.m_Confidence;
            fused.m_ContributorRadarId0 = a.m_SourceRadarId;
            fused.m_ContributorTrackId0 = a.m_LocalTrackId;

            if (b && bIndex >= 0)
            {
                used.Set(bIndex, true);
                fused.m_ContributorCount = 2;
                fused.m_ContributorRadarId1 = b.m_SourceRadarId;
                fused.m_ContributorTrackId1 = b.m_LocalTrackId;
                fused.m_Iff = MergeIff(a.m_Iff, b.m_Iff);
                fused.m_NctrClass = MergeNctr(a.m_NctrClass, b.m_NctrClass);
                if (fused.m_NctrClass == ERDF_RadarNctrClass.RDF_NCTR_UNKNOWN)
                    fused.m_NctrConfidence = 0.0;
                else
                    fused.m_NctrConfidence = 0.5 * (a.m_NctrConfidence + b.m_NctrConfidence);
                fused.m_Confidence = 0.5 * (a.m_Confidence + b.m_Confidence);
                if (a.m_Type != b.m_Type)
                    fused.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;

                float wa = Math.Max(0.1, a.m_SnrDb + 20.0);
                float wb = Math.Max(0.1, b.m_SnrDb + 20.0);
                float wsum = wa + wb;
                fused.m_WorldPos = (a.m_WorldPos * wa + b.m_WorldPos * wb) * (1.0 / wsum);
                fused.m_Velocity = (a.m_Velocity * wa + b.m_Velocity * wb) * (1.0 / wsum);
                fused.m_SnrDb = 0.5 * (a.m_SnrDb + b.m_SnrDb);

                vector crossPos;
                if (TryCrossFixHorizontal(a, b, crossPos))
                {
                    fused.m_WorldPos = crossPos;
                    fused.m_CrossFixUsed = true;
                }

                MergeWlr(fused, a, b);
            }
            else
            {
                fused.m_ContributorCount = 1;
                fused.m_ContributorRadarId1 = -1;
                fused.m_ContributorTrackId1 = -1;
                fused.m_Iff = a.m_Iff;
                fused.m_NctrClass = a.m_NctrClass;
                fused.m_NctrConfidence = a.m_NctrConfidence;
                fused.m_Confidence = a.m_Confidence;
                fused.m_WorldPos = a.m_WorldPos;
                fused.m_Velocity = a.m_Velocity;
                fused.m_SnrDb = a.m_SnrDb;
                fused.m_CrossFixUsed = false;
                fused.m_WlrLaunchValid = a.m_WlrLaunchValid;
                fused.m_WlrLaunchPos = a.m_WlrLaunchPos;
                fused.m_WlrImpactValid = a.m_WlrImpactValid;
                fused.m_WlrImpactPos = a.m_WlrImpactPos;
            }

            outFused.Insert(fused);
        }

        return outFused;
    }

    protected bool PassesAssociationGate(
        RDF_RadarDatalinkTrack a,
        RDF_RadarDatalinkTrack b)
    {
        vector dp = b.m_WorldPos - a.m_WorldPos;
        float dist = Math.Sqrt(dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2]);
        if (dist > m_AssocGateM)
            return false;
        vector dv = b.m_Velocity - a.m_Velocity;
        float speed = Math.Sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
        if (speed > m_AssocSpeedGateMs)
            return false;
        return true;
    }

    protected ERDF_RadarIff MergeIff(ERDF_RadarIff a, ERDF_RadarIff b)
    {
        if (a == b)
            return a;
        if (a == ERDF_RadarIff.RDF_IFF_UNKNOWN)
            return b;
        if (b == ERDF_RadarIff.RDF_IFF_UNKNOWN)
            return a;
        return ERDF_RadarIff.RDF_IFF_UNKNOWN;
    }

    protected ERDF_RadarNctrClass MergeNctr(ERDF_RadarNctrClass a, ERDF_RadarNctrClass b)
    {
        if (a == b)
            return a;
        return ERDF_RadarNctrClass.RDF_NCTR_UNKNOWN;
    }

    protected void MergeWlr(
        RDF_RadarFusedTrack fused,
        RDF_RadarDatalinkTrack a,
        RDF_RadarDatalinkTrack b)
    {
        if (a.m_WlrLaunchValid)
        {
            fused.m_WlrLaunchValid = true;
            fused.m_WlrLaunchPos = a.m_WlrLaunchPos;
        }
        else if (b.m_WlrLaunchValid)
        {
            fused.m_WlrLaunchValid = true;
            fused.m_WlrLaunchPos = b.m_WlrLaunchPos;
        }
        if (a.m_WlrImpactValid)
        {
            fused.m_WlrImpactValid = true;
            fused.m_WlrImpactPos = a.m_WlrImpactPos;
        }
        else if (b.m_WlrImpactValid)
        {
            fused.m_WlrImpactValid = true;
            fused.m_WlrImpactPos = b.m_WlrImpactPos;
        }
    }

    // Horizontal bearing intersection of two radar→target rays.
    // Rejects when azimuth angle between stations is too acute.
    bool TryCrossFixHorizontal(
        RDF_RadarDatalinkTrack a,
        RDF_RadarDatalinkTrack b,
        out vector outPos)
    {
        outPos = "0 0 0";
        if (!a || !b)
            return false;

        vector o1 = a.m_RadarOrigin;
        vector o2 = b.m_RadarOrigin;
        float az1 = a.m_AzimuthDeg * 0.0174532925199;
        float az2 = b.m_AzimuthDeg * 0.0174532925199;
        float dx1 = Math.Cos(az1);
        float dz1 = Math.Sin(az1);
        float dx2 = Math.Cos(az2);
        float dz2 = Math.Sin(az2);

        float angleDeg = BearingSeparationDeg(a.m_AzimuthDeg, b.m_AzimuthDeg);
        if (angleDeg < m_MinCrossFixAngleDeg)
            return false;
        if (angleDeg > 180.0 - m_MinCrossFixAngleDeg)
            return false;

        float det = dx1 * dz2 - dz1 * dx2;
        if (Math.AbsFloat(det) < 0.0001)
            return false;

        float ox = o2[0] - o1[0];
        float oz = o2[2] - o1[2];
        float t = (ox * dz2 - oz * dx2) / det;
        float s = (ox * dz1 - oz * dx1) / det;
        if (t < 50.0 || s < 50.0)
            return false;

        float x = o1[0] + dx1 * t;
        float z = o1[2] + dz1 * t;
        float y = 0.5 * (a.m_WorldPos[1] + b.m_WorldPos[1]);
        outPos = Vector(x, y, z);
        return true;
    }

    protected float BearingSeparationDeg(float azA, float azB)
    {
        float d = azA - azB;
        while (d > 180.0)
            d = d - 360.0;
        while (d < -180.0)
            d = d + 360.0;
        if (d < 0.0)
            d = -d;
        return d;
    }
}
