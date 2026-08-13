//------------------------------------------------------------------------------------------------
//! Visual settings for radar world-space debug / showcase drawing.
class RDF_RadarVisualSettings
{
    //! Legacy debug: origin→plot rays (noisy; off in showcase).
    bool m_DrawRays = false;
    //! Plot markers (spheres) at detected target positions.
    bool m_DrawPoints = true;
    //! Plot sphere radius in metres.
    float m_PointSize = 1.5;
    //! Alpha for debug rays when m_DrawRays is on.
    float m_RayAlpha = 0.35;
    //! RGB axis triad at radar origin.
    bool m_DrawOriginAxis = false;
    //! Axis triad length in metres.
    float m_OriginAxisLength = 1.0;

    //! Showcase: translucent search sector fan + leading sweep edge.
    bool m_DrawSectorSweep = true;
    //! Fan wedge count (clamped in visualizer).
    int m_SectorSweepSegments = 18;
    //! Sector fill alpha.
    float m_SectorSweepAlpha = 0.10;
    //! Sector edge / arc alpha.
    float m_SectorSweepEdgeAlpha = 0.55;
    //! Horizontal fan plane lift above scan origin (metres). Keep small so
    //! the sector reads as a ground slab, not a sheet floating in the sky.
    float m_SectorHeightM = 8.0;
    //! Extra Y added at the fan far edge. 0 = flat slab.
    float m_SectorFarTiltM = 0.0;
    //! Cap drawn sector / rings (0 = use radar range). Promo uses ~750 m so
    //! the fan fills the frame instead of a 3 km sliver.
    float m_SectorVisualRangeM = 0.0;
    //! If true, fan boresight follows the platform, not the mechanical beam.
    bool m_LockSectorToBoresight = false;
    //! Rotating sweep needle inside the sector (visual only, per presentation tick).
    bool m_AnimateSweepNeedle = false;
    float m_SweepNeedlePeriodS = 2.2;
    //! One plot sphere per entity (scatterer clouds otherwise swallow aircraft).
    bool m_CollapsePlotsByEntity = false;
    //! Hide EW deception plots. Inverse-path ANONYMOUS skin returns still draw.
    bool m_HideFalsePlots = false;
    //! World-marker radius for projectile plots (shells are tiny).
    float m_ProjectilePointSize = 8.0;

    //! Showcase: STT / lock beam toward locked aim point.
    bool m_DrawLockBeam = true;
    //! Lock beam alpha.
    float m_LockBeamAlpha = 0.45;
    //! Cone end radius at aim point (metres).
    float m_LockBeamEndRadiusM = 12.0;

    //! Showcase: phosphor afterglow for recent plots.
    bool m_DrawAfterglow = true;
    //! Afterglow lifetime in seconds.
    float m_AfterglowSec = 2.8;
    //! Afterglow sphere radius.
    float m_AfterglowPointSize = 1.0;
    //! Max afterglow blips in the ring buffer.
    int m_AfterglowMaxBlips = 160;

    //! Ground range rings (½ and full scan range) via CreateCircle.
    bool m_DrawRangeRings = true;
    //! Circle subdivisions for range rings (clamped ≤ 50).
    int m_RangeRingSegments = 32;

    //! WLR launch / impact markers + optional track ribbon.
    bool m_DrawWeaponLocate = true;
    //! Launch / impact sphere radius.
    float m_WeaponLocateMarkerSize = 4.0;
    //! Ground alert ring radius around launch / impact (counter-battery cue).
    float m_WeaponLocateAlertRadiusM = 80.0;
    //! Alert ring polyline segments (independent of m_RangeRingSegments).
    int m_WeaponLocateAlertSegments = 12;
    //! Draw subsampled projectile track history ribbons.
    bool m_DrawTrackRibbon = false;
    //! Subsample track history when drawing ribbon (1 = every sample).
    int m_TrackRibbonStride = 2;

    //------------------------------------------------------------------------------------------------
    //! Demo / promo defaults: volume beam + afterglow + lock, not raw rays.
    void ApplyShowcaseDefaults()
    {
        m_DrawRays = false;
        m_DrawPoints = true;
        m_PointSize = 1.5;
        m_DrawOriginAxis = false;
        m_DrawSectorSweep = true;
        m_SectorSweepSegments = 12;
        m_SectorHeightM = 8.0;
        m_SectorFarTiltM = 0.0;
        m_SectorVisualRangeM = 0.0;
        m_LockSectorToBoresight = false;
        m_AnimateSweepNeedle = false;
        m_CollapsePlotsByEntity = false;
        m_HideFalsePlots = false;
        m_ProjectilePointSize = 8.0;
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
    //! Promo reel: small markers, ground-hugging fan, smooth sweep, readable WLR.
    void ApplyPromoDefaults()
    {
        ApplyShowcaseDefaults();
        m_PointSize = 0.85;
        m_AfterglowPointSize = 0.32;
        m_AfterglowSec = 1.2;
        m_AfterglowMaxBlips = 28;
        m_LockBeamEndRadiusM = 4.2;
        m_LockBeamAlpha = 0.42;
        m_SectorSweepSegments = 16;
        m_SectorSweepAlpha = 0.13;
        m_SectorSweepEdgeAlpha = 0.72;
        m_SectorHeightM = 3.0;
        m_SectorFarTiltM = 0.0;
        m_SectorVisualRangeM = 750.0;
        m_LockSectorToBoresight = true;
        m_AnimateSweepNeedle = true;
        m_SweepNeedlePeriodS = 1.25;
        m_CollapsePlotsByEntity = true;
        m_HideFalsePlots = true;
        m_ProjectilePointSize = 0.55;
        m_DrawTrackRibbon = false;
        m_TrackRibbonStride = 1;
        m_DrawWeaponLocate = false;
        m_RangeRingSegments = 32;
    }

    //------------------------------------------------------------------------------------------------
    //! Minimal markers only (WLR on, sector / afterglow off).
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
