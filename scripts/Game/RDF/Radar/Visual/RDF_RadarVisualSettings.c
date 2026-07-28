// Visual settings for radar world-space debug / showcase drawing.
class RDF_RadarVisualSettings
{
    // Legacy debug: origin→plot rays (noisy; off in showcase).
    bool m_DrawRays = false;
    bool m_DrawPoints = true;
    float m_PointSize = 1.5;
    float m_RayAlpha = 0.35;
    bool m_DrawOriginAxis = false;
    float m_OriginAxisLength = 1.0;

    // Showcase: translucent search sector fan + leading sweep edge.
    bool m_DrawSectorSweep = true;
    int m_SectorSweepSegments = 18;
    float m_SectorSweepAlpha = 0.10;
    float m_SectorSweepEdgeAlpha = 0.55;
    // Lift fan so it reads above terrain (meters along world up).
    float m_SectorHeightM = 40.0;

    // Showcase: STT / lock beam toward locked aim point.
    bool m_DrawLockBeam = true;
    float m_LockBeamAlpha = 0.45;
    float m_LockBeamEndRadiusM = 12.0;

    // Showcase: phosphor afterglow for recent plots.
    bool m_DrawAfterglow = true;
    float m_AfterglowSec = 2.8;
    float m_AfterglowPointSize = 1.0;
    int m_AfterglowMaxBlips = 160;

    // Ground range rings (½ and full scan range). One Shape per ring (line strip).
    bool m_DrawRangeRings = true;
    int m_RangeRingSegments = 32;

    // WLR launch / impact markers + optional track ribbon.
    bool m_DrawWeaponLocate = true;
    float m_WeaponLocateMarkerSize = 4.0;
    // Ground alert ring radius around launch / impact (counter-battery cue).
    float m_WeaponLocateAlertRadiusM = 80.0;
    // Alert ring polyline segments (independent of m_RangeRingSegments).
    int m_WeaponLocateAlertSegments = 12;
    bool m_DrawTrackRibbon = true;
    // Subsample track history when drawing ribbon (1 = every sample).
    int m_TrackRibbonStride = 2;

    //------------------------------------------------------------------------------------------------
    // Demo / promo defaults: volume beam + afterglow + lock, not raw rays.
    void ApplyShowcaseDefaults()
    {
        m_DrawRays = false;
        m_DrawPoints = true;
        m_PointSize = 1.5;
        m_DrawOriginAxis = false;
        m_DrawSectorSweep = true;
        m_SectorSweepSegments = 12;
        m_DrawLockBeam = true;
        m_DrawAfterglow = true;
        m_AfterglowMaxBlips = 80;
        m_DrawRangeRings = true;
        m_RangeRingSegments = 24;
        m_DrawWeaponLocate = true;
        m_WeaponLocateMarkerSize = 4.0;
        m_WeaponLocateAlertRadiusM = 80.0;
        m_WeaponLocateAlertSegments = 12;
        // Ribbon off by default — one polyline per track is cheap, but history
        // still grows; enable explicitly when debugging projectile paths.
        m_DrawTrackRibbon = false;
        m_TrackRibbonStride = 2;
    }

    //------------------------------------------------------------------------------------------------
    void ApplyMinimalDefaults()
    {
        m_DrawRays = false;
        m_DrawPoints = false;
        m_DrawOriginAxis = false;
        m_DrawSectorSweep = false;
        m_DrawLockBeam = false;
        m_DrawAfterglow = false;
        m_DrawRangeRings = false;
        m_DrawWeaponLocate = true;
        m_WeaponLocateMarkerSize = 1.5;
        m_WeaponLocateAlertSegments = 12;
        m_DrawTrackRibbon = false;
    }
}

class RDF_RadarAfterglowBlip
{
    vector m_Pos;
    float m_BirthS;
    float m_R;
    float m_G;
    float m_B;
}
