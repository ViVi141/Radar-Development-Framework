// Station-to-station datalink track summaries + light IFF (not crypto IFF).
// Distinct from WeaponBridge midcourse "datalink" uplink.
enum ERDF_RadarIff
{
    RDF_IFF_UNKNOWN,
    RDF_IFF_FRIEND,
    RDF_IFF_FOE,
    RDF_IFF_NEUTRAL
}

class RDF_RadarDatalinkTrack
{
    int m_SourceRadarId;
    int m_LocalTrackId;
    vector m_WorldPos;
    vector m_Velocity;
    float m_RangeM;
    float m_AzimuthDeg;
    float m_ElevationDeg;
    float m_RangeRateMs;
    float m_SnrDb;
    ERDF_RadarTargetType m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
    ERDF_RadarIff m_Iff = ERDF_RadarIff.RDF_IFF_UNKNOWN;
    float m_TimeS;
    vector m_RadarOrigin;
    bool m_WlrLaunchValid;
    vector m_WlrLaunchPos;
    bool m_WlrImpactValid;
    vector m_WlrImpactPos;
}

class RDF_RadarFusedTrack
{
    int m_FusedId;
    vector m_WorldPos;
    vector m_Velocity;
    float m_SnrDb;
    ERDF_RadarTargetType m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
    ERDF_RadarIff m_Iff = ERDF_RadarIff.RDF_IFF_UNKNOWN;
    float m_TimeS;
    int m_ContributorCount;
    int m_ContributorRadarId0;
    int m_ContributorRadarId1;
    int m_ContributorTrackId0;
    int m_ContributorTrackId1;
    bool m_CrossFixUsed;
    bool m_WlrLaunchValid;
    vector m_WlrLaunchPos;
    bool m_WlrImpactValid;
    vector m_WlrImpactPos;
}

// Mods override Resolve() to map entity/faction → IFF.
class RDF_RadarIffResolver
{
    ERDF_RadarIff Resolve(IEntity radarSubject, RDF_RadarTrack track)
    {
        return ERDF_RadarIff.RDF_IFF_UNKNOWN;
    }
}

class RDF_RadarDefaultIffResolver : RDF_RadarIffResolver
{
    override ERDF_RadarIff Resolve(IEntity radarSubject, RDF_RadarTrack track)
    {
        return ERDF_RadarIff.RDF_IFF_UNKNOWN;
    }
}
