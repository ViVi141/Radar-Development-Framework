// Core scan settings for radar (entity-first, visibility ray + optional NLOS).
class RDF_RadarSettings
{
    bool m_Enabled = true;
    float m_Range = 2000.0;
    float m_UpdateInterval = 0.2;
    float m_SectorHalfAngleDeg = 45.0;
    vector m_OriginOffset = "0 0 0";
    bool m_UseBoundsCenter = true;
    bool m_UseLocalOffset = true;
    bool m_IncludeVehicles = true;
    bool m_IncludeProjectiles = true;
    bool m_IncludeRadarEmitters = true;
    // When true, RADAR_EMITTER plots use one-way Friis ESM power (not skin R^4).
    bool m_EnableEsmReceive = false;
    // After each dwell, report SEARCH/TRACK/LOCK threats into RDF_RadarRwr.
    bool m_EnableRwrReporting = true;
    // Discovery sweep radius = m_Range * this factor.
    float m_ScattererDiscoveryRangeScale = 1.25;
    float m_ScattererDiscoveryIntervalS = 3.0;
    int m_ScattererClassifyPerTick = 24;
    int m_ScattererRefreshPerTick = 96;
    int m_ScattererMaxEntries = 512;
    int m_MaxTargets = 64;
    // Cap TraceMove calls per scan to bound hitch size in dense scenes.
    int m_MaxLosTracesPerScan = 48;
    // Cross-frame LOS smoothing (default on): limit TraceMove calls per Tick and
    // defer the overflow to a queue drained on later scans. Keeps one scan from
    // burning the whole per-scan budget in a single synchronous burst.
    bool m_EnableLosFrameQueue = true;
    // Max TraceMove calls per Tick when m_EnableLosFrameQueue is on.
    // 0 = fall back to m_MaxLosTracesPerScan (legacy one-shot behaviour).
    int m_LosTracesPerTick = 16;
    // Cap on deferred LOS queue length (scatterers awaiting a fresh LOS pass).
    int m_LosQueueMax = 128;
    // Adaptive budget governor (default on): the Sensor measures real scan wall
    // time and tick cadence each scan and auto-scales the per-tick LOS trace
    // budget (and WLR solve budget) to keep the server headroom. To pin a fixed
    // budget, set m_AdaptiveLosMinPerTick == m_AdaptiveLosMaxPerTick (same for
    // WLR) or disable the governor. Requires m_EnableLosFrameQueue for the LOS
    // channel to take effect.
    bool m_EnableAdaptiveBudget = true;
    // Target: scanMs should stay below this fraction of ONE SERVER FRAME
    // (frameMs = 1000 / GetFPS; 16.7 ms @ 60 tick, 8.3 ms @ 120 tick). 0.3
    // means a scan may consume at most ~30% of a single frame; the scan runs
    // every m_UpdateInterval but must not hitch the frame it lands on. The
    // overload branch (util > 1.0) must stay reachable, so keep this modest.
    float m_AdaptiveTargetScanFraction = 0.3;
    // EMA smoothing of measured scan ms / frame ms.
    float m_AdaptiveEmaAlpha = 0.3;
    // Clamp range for the adaptive per-tick LOS trace budget.
    int m_AdaptiveLosMinPerTick = 4;
    int m_AdaptiveLosMaxPerTick = 20;
    // Clamp range for the adaptive per-scan WLR solve budget.
    int m_AdaptiveWlrMinSolves = 1;
    int m_AdaptiveWlrMaxSolves = 6;
    // Step factors when overloaded (down) / idle (up).
    float m_AdaptiveStepDown = 0.85;
    float m_AdaptiveStepUp = 1.1;
    float m_MinDistance = 0.5;
    // When true, discovery also eclipses ranges inside Hardware Rmin = c·(τ+recovery)/2.
    bool m_EnablePulseBlindZone = true;
    ref RDF_RadarHardware m_Hardware;
    // Multi-channel RF (default off). m_Hardware is the active alias for the
    // scan pipeline; SelectBandForDwell points it at a channel without mutating
    // authored objects. One carrier per scan (highest-priority scheduled dwell).
    bool m_EnableMultiBand = false;
    ref array<ref RDF_RadarHardware> m_BandChannels;
    int m_SearchBandIndex = 0;
    int m_TrackBandIndex = 0;
    int m_FireControlBandIndex = 0;
    int m_ActiveBandIndex = 0;
    bool m_EnablePhysicalDetection = true;
    bool m_EnableMechanicalScan = false;
    // Mechanical-scan phase offset (radians) added to GetScanForward's
    // world-time angle. Lets mods resume a paused antenna from its frozen
    // bearing instead of snapping to the world-clock angle. 0 = stock
    // (world-time absolute). Mods should update it while the sensor is
    // disabled so the first scan after re-enable starts at the frozen angle.
    float m_ScanPhaseOffsetRad = 0.0;
    // When true, GetScanForward uses m_ScanPhaseOffsetRad as an absolute
    // world azimuth (0 = +X east, pi/2 = +Z north) and ignores worldTime*rpm.
    // Needed so a parked beam is not one frame late vs a spinning world clock
    // (narrow beams miss the target if the offset is applied after Scan).
    bool m_bScanAngleLocked = false;
    // Cold-war counter-battery / narrow-threat-sector scan: instead of full 360°
    // rotation or a fixed stare, the beam sweeps back and forth within a sector
    // centered on m_SectorSweepCenterRad. Used for WLR-style narrow-threat-
    // corridor search. Units are radians; rate in rad/s.
    bool m_SectorSweepEnabled = false;
    float m_SectorSweepCenterRad = 0.0;
    float m_SectorSweepHalfWidthRad = 0.785398; // default ±45°
    float m_SectorSweepRateRadS = 0.6;
    float m_DetectionSnrDb = 8.0;
    bool m_KeepUndetected = false;
    bool m_EnableDemClutter = true;
    int m_DemCacheMaxTiles = RDF_DemBakeConstants.RUNTIME_DEM_CACHE_MAX_TILES;
    int m_DemTileLoadsPerScan = RDF_DemBakeConstants.RUNTIME_DEM_LOADS_PER_SCAN;
    // Load whole SURF/DEM pack into RAM on first scan (avoids mid-play tile hitch).
    bool m_DemPreloadAll = RDF_DemBakeConstants.RUNTIME_DEM_PRELOAD_ALL;
    float m_DemClutterScale = 1.0;
    // When Trace is blocked, still attempt a weakened ground-bounce path.
    bool m_EnableNlosMultipath = true;
    // |Gamma| for NLOS-only bounce power (~Gamma^2 * geometric path factor).
    float m_NlosReflectionAbs = 0.55;
    // Discard NLOS candidates weaker than this power scale.
    float m_NlosMinFactor = 0.008;
    // Skip bounce model for high-flying targets (AGL).
    float m_NlosMaxTargetAglM = 800.0;
    // Knife-edge diffraction over DEM/surface samples (still under NLOS gate).
    bool m_EnableKnifeEdgeDiffraction = true;
    // When true: up to two dominant edges cascaded (Deygout-lite).
    // When false: legacy single worst-edge knife only.
    bool m_EnableDualKnifeEdge = true;
    // Uniform samples along radar→target (plus Trace hitFraction candidate).
    int m_KnifeEdgeMaxSamples = 8;
    // Min |Δu| between the two dominant edges (ignore near-duplicate samples).
    float m_KnifeEdgeMinUSeparation = 0.10;
    // Subtract from obstacle height to reduce GetSurfaceY / DEM noise false blocks.
    float m_KnifeEdgeClearanceSlackM = 2.0;
    // Use baked ν→factor LUT for KnifeEdgeLinearFactor (default on).
    bool m_EnableKnifeEdgeLut = true;
    // Before TraceMove: sample DEM HEIGHT RAM along the ray. If terrain blocks,
    // skip Trace (entity occlusion still needs Trace when DEM is clear).
    bool m_EnableDemLosPrecheck = true;
    // Interior samples along radar→target for DEM precheck (endpoints skipped).
    int m_DemLosPrecheckSamples = 8;
    // Floor Gaussian two-way pattern to Hardware.m_SidelobeLevelDb^2.
    bool m_EnableSidelobeFloor = true;
    // Azimuth segmented pattern LUT (mainlobe + sidelobe peaks); default on.
    bool m_EnablePatternLut = true;
    // Apply Hardware.m_PolarizationMode match table × m_PolarizationFactor.
    bool m_EnablePolarizationMatch = true;
    // Fixed-site DEM polar path LUT (opt-in; needs profile SitePathLut.json).
    bool m_EnableSitePathLut = false;
    // Max radar-origin drift vs bake origin before site LUT is ignored.
    float m_SitePathMaxOriginDriftM = 5.0;
    // Optional receiver-side EW/noise injection after processing.
    float m_AdditionalNoisePowerW = 0.0;
    ref RDF_RadarEwStack m_EwStack;
    bool m_EnableCfarGate = true;
    int m_CfarGuardCells = 2;
    int m_CfarTrainingCells = 8;
    float m_CfarPfa = 0.000001;
    int m_RangeBinCount = 64;
    // Quantize + noise measurements; clear entity refs so plots are model-derived.
    bool m_EnableMeasurementSynthesis = true;
    // Debug only: also stamp plot.m_Entity (inverse algorithms must ignore it).
    // Prefer Sensor.GetDebugTruthEntity / track rebind for fire-control.
    bool m_KeepEntityTruth = false;
    // Measurement synthesis extras (applied when synthesis is on).
    // 0 = quantized / bias-only (or clean when biases are 0). Opt into noise via
    // SetMeasurementNoise(...) or ApplyGameplayFidelity(...).
    float m_MeasNoiseScale = 1.0;
    float m_MeasRangeBiasM = 0.0;
    float m_MeasAzimuthBiasDeg = 0.0;
    float m_MeasElevationBiasDeg = 0.0;
    float m_MeasDopplerBiasHz = 0.0;
    // Pluggable channel-error model (runs after CFAR, before Tracker/WLR).
    // Null → built-in RDF_RadarMeasurement.Synthesize fallback.
    ref RDF_RadarMeasurementModel m_MeasurementModel;
    // Phased-array dwell / resource management (TODO §9 S1). Layered, opt-in:
    // off keeps the classic scan-everything path. On, the Sensor schedules
    // FIRE_CONTROL / TRACK dwells within a beam-time budget; SEARCH still uses
    // the fair scan cursor. Offline mirror: tools/dem/rdf_radar_dwell.py.
    bool m_EnableDwellScheduler = false;
    float m_DwellBudgetMs = 20.0;
    float m_FireControlDwellPeriodS = 0.25;
    float m_FireControlDwellMs = 4.0;
    float m_TrackDwellPeriodS = 1.0;
    float m_TrackDwellMs = 2.0;
    // ECCM decision layer (TODO §9 S3). Layered, opt-in: off keeps the jammer
    // effects exactly as configured. On, the Sensor reads EW observables each
    // scan and auto-toggles SLB / frequency hop (next scan) / (reports) PRF /
    // burn-through. Offline mirror: tools/dem/rdf_radar_eccm.py.
    bool m_EnableEccmDecision = false;
    float m_EccmJnOnDb = 6.0;
    float m_EccmJnHysteresisDb = 2.0;
    float m_EccmSidelobeCouplingOn = 0.3;
    // JPDA soft association (TODO §9 S2). Layered, opt-in: off keeps the classic
    // nearest-neighbor (GNN) association. On, the tracker uses soft association.
    bool m_EnableJpda = false;
    float m_JpdaPd = 0.9;
    // NCTR from micro-Doppler (rotor / fan / fixed). Default off.
    bool m_EnableNctr = false;
    // RWR Friis intercept gate. Off = geometric (report if we detected them).
    bool m_EnableRwrFriis = false;
    float m_RwrAntennaGainDbi = 0.0;
    float m_RwrDetectSnrDb = 6.0;
    // Weapon-grade lock: 0 = no extra gate. Typical 0.55 when enabled.
    float m_WeaponGradeMinConfidence = 0.0;
    // Rain damps water σ⁰ (does not replace atmospheric rain loss).
    bool m_EnableRainSeaClutterDamp = false;
    float m_LastRainIntensity;
    // Trapping layer: stretch radio horizon. Default off.
    bool m_EnableAtmosphericDuct = false;
    float m_DuctHorizonScale = 2.0;
    // Low-AGL elevation bias toward the surface.
    bool m_EnableMultipathGlint = false;
    // Fill empty CFAR cells with thermal-noise samples (real Pfa behaviour).
    bool m_EnableCfarThermalFill = false;
    // Two-way atmospheric / rain loss on received power.
    bool m_EnableAtmosphericLoss = false;
    // <0 → auto from carrier frequency (clear-air fit).
    float m_AtmLossDbPerKmOneWay = -1.0;
    // Manual rain floor (always applied when atmospheric loss is on).
    float m_RainLossDbPerKmOneWay = 0.0;
    // When true, add rain/fog from TimeAndWeatherManager each scan.
    bool m_EnableWeatherDrivenRainLoss = false;
    // Extra one-way dB/km when GetRainIntensity() == 1.
    float m_RainLossDbPerKmAtFullIntensity = 0.5;
    // Extra one-way dB/km when GetFogAmount() == 1 (weak; fog is not rain).
    float m_FogLossDbPerKmAtFullFog = 0.05;
    // Clear-LOS two-ray multipath (power lobes). Not waveform simulation.
    // Default off — call EnableLosTwoRayMultipath(). Skipped for projectiles.
    bool m_EnableLosTwoRayMultipath = false;
    // Fixed Γ when surface dielectric is unavailable (negative = phase inversion).
    float m_LosTwoRayReflectionCoeff = -0.5;
    // Skip two-ray when target AGL exceeds this (m).
    float m_LosTwoRayMaxTargetAglM = 600.0;
    // Clamp two-ray power factor into [min, max].
    float m_LosTwoRayMinFactor = 0.08;
    float m_LosTwoRayMaxFactor = 4.0;
    // Prefer SurfaceTable dielectric → Fresnel Γ when DEM class is known.
    bool m_LosTwoRayUseSurfaceDielectric = true;
    // 4/3-Earth refraction: radio-horizon soft factor + elevation bias.
    bool m_EnableAtmosphericRefraction = false;
    // Effective-earth multiplier (4/3 ≈ standard tropospheric refraction).
    float m_EarthRadiusFactor = 1.333333;
    // Fold measured range into PRF unambiguous interval.
    bool m_EnableRangeAmbiguityFold = false;
    // Fold measured Doppler into ±PRF/2. Skipped when weapon-locate is on.
    bool m_EnableDopplerAmbiguityFold = false;
    // Plot-to-track association gates (meters / degrees).
    float m_TrackGateRangeM = 400.0;
    float m_TrackGateAzimuthDeg = 4.0;
    int m_TrackConfirmHits = 2;
    int m_TrackMaxMisses = 3;
    // On miss, coast filtered kinematics instead of only counting misses.
    // Default off so SEARCH/demo stay unchanged; PulseDoppler preset enables.
    bool m_TrackCoastOnMiss = false;
    // Extra miss budget / wider gates when last hit used a near-zero Doppler bin.
    bool m_TrackCoastOnDopplerNull = false;
    // Grow association gates by this fraction per consecutive miss while coasting.
    float m_TrackCoastGateGrowPerMiss = 0.25;
    // Drop coasting tracks after this many seconds (0 = disabled).
    float m_TrackCoastMaxSec = 0.0;
    // Runtime clutter-map EMA over DEM σ⁰ (per range–az cell).
    bool m_EnableClutterMap = false;
    // Rise rate (slow) when DEM clutter increases.
    float m_ClutterMapAlpha = 0.15;
    // Fall rate when DEM clutter drops (land→water). Default equals Alpha →
    // symmetric EMA (legacy behaviour). PulseDoppler sets a higher value.
    float m_ClutterMapAlphaDown = 0.15;
    // Azimuth bins for the clutter map grid (Scanner Configure).
    int m_ClutterMapAzBinCount = 36;
    // Average σ⁰ across a few DEM cells in the illuminated footprint
    // (sharper class boundaries than under-target point sample). Default OFF.
    bool m_EnableClutterFootprintMix = false;
    int m_ClutterFootprintSamples = 5;
    // Coarse range–Doppler map (default OFF). Amortized deposits only — not a
    // full RD cube. Enable explicitly when needed.
    bool m_EnableCoarseRd = false;
    int m_RdCellsPerScan = 32;
    float m_RdMapAlpha = 0.2;
    float m_RdDecayPerScan = 0.97;
    // Blend weight for coarse RD Peek into clutter noise (0 = ignore RD, 1 = RD only).
    float m_RdClutterBlend = 0.35;
    // Cockpit range–az clutter intensity surface (default OFF). Independent of
    // PhysicalDetect / ClutterMap: DEM σ⁰·A/R^4 for MFD display only.
    bool m_EnableRangeAzClutterSurface = false;
    int m_ClutterSurfaceAzBins = 64;
    int m_ClutterSurfaceRangeBins = 64;
    int m_ClutterSurfaceCellsPerScan = 96;
    float m_ClutterSurfaceDecayPerScan = 0.92;
    // <=0: use current scan sector half-angle / beam half-width.
    float m_ClutterSurfaceSectorHalfDeg = 0.0;
    // When true and DEM cell has column spans (non-SURF V3/CSV), knife-edge /
    // NLOS obstacle height uses column top Y (canopy / urban slabs).
    // SURF packs stay surface-only; leave false unless scene needs span data.
    // Span sampling may read DEM even when m_EnableDemClutter is false.
    bool m_EnableDemSpanOcclusion = false;
    // Projectile tracks: AirDrag + global wind extrapolation / WLR fixes.
    bool m_EnableBallisticPrediction = true;
    // Prefab prior (82mm HE O832DU). Override per ammo family later if needed.
    float m_ShellAirDrag = 0.000615;
    // Recompute launch/impact for confirmed projectile tracks each scan.
    bool m_EnableWeaponLocate = true;
    // Prefer DEM / live surface for WLR ground intersection (fallback = flat Y).
    bool m_EnableDemGroundForWlr = true;
    // Real-WLR style gates: enough hits + time span before reporting a fix.
    int m_WeaponLocateMinHits = 5;
    float m_WeaponLocateMinSpanS = 1.0;
    float m_WeaponLocateMaxFitRmsM = 80.0;
    int m_WeaponLocateFitWindow = 20;
    // Blend new launch/impact into the previous fix (1 = jump, 0 = freeze).
    float m_WeaponLocateSmoothAlpha = 0.35;
    // Cross-frame WLR solve budget (default on): cap ballistic solves per scan
    // and queue overflow tracks to later scans, so a mass barrage does not burn
    // 25-50 ms in one tick. 0 = unlimited (legacy: solve every eligible track
    // every scan).
    int m_WeaponLocateSolvesPerScan = 2;
    // Cap on the deferred WLR solve queue length.
    int m_WeaponLocateQueueMax = 16;

    // ---- Scan optimization thresholds (was hardcoded on Scanner / ReuseCache) ----
    float m_LosCacheMaxAgeS = 0.25;
    float m_LosCacheMaxOriginShiftM = 1.5;
    float m_LosCacheMaxTargetShiftM = 3.0;
    float m_TargetReuseMaxAgeS = 0.60;
    float m_PhysicalReuseMaxAgeS = 0.20;
    float m_PhysicalReuseMaxOriginShiftM = 2.0;
    float m_PhysicalReuseMaxTargetShiftM = 4.0;
    float m_PhysicalReuseMaxSpeedMs = 6.0;
    float m_PriorityNearRangeM = 800.0;
    float m_PriorityMidRangeM = 2200.0;
    float m_PriorityFastSpeedMs = 35.0;
    float m_PrioritySlowSpeedMs = 8.0;
    float m_PriorityBand1IntervalS = 0.10;
    float m_PriorityBand2IntervalS = 0.30;
    int m_FreshUpdateBudgetMin = 8;
    int m_FreshUpdateBudgetMax = 96;
    // Continue registry scan from last cutoff index for better fairness.
    bool m_FairScanCursor = true;

    // CA = cell-averaging; GO = greater-of; SO = smaller-of (clutter-edge modes).
    ERDF_CfarMode m_CfarMode = ERDF_CfarMode.RDF_CFAR_CA;

    // PPI / showcase: draw WLR launch & impact alert rings.
    bool m_EnableWlrHudAlerts = true;
    float m_WlrHudAlertRadiusM = 80.0;

    void RDF_RadarSettings()
    {
        m_Hardware = RDF_RadarHardware.CreateShorad();
        m_BandChannels = new array<ref RDF_RadarHardware>();
        m_EwStack = new RDF_RadarEwStack();
        m_MeasurementModel = new RDF_RadarDefaultMeasurementModel();
    }

    void Validate()
    {
        m_Range = Math.Clamp(m_Range, 1.0, 100000.0);
        m_UpdateInterval = Math.Max(0.05, m_UpdateInterval);
        m_SectorHalfAngleDeg = Math.Clamp(m_SectorHalfAngleDeg, 0.0, 180.0);
        m_MaxTargets = Math.Max(1, m_MaxTargets);
        m_MaxLosTracesPerScan = Math.Clamp(m_MaxLosTracesPerScan, 4, 512);
        m_LosTracesPerTick = Math.Clamp(m_LosTracesPerTick, 0, 512);
        m_LosQueueMax = Math.Clamp(m_LosQueueMax, 4, 4096);
        if (m_LosTracesPerTick > 0 && m_LosTracesPerTick > m_MaxLosTracesPerScan)
            m_LosTracesPerTick = m_MaxLosTracesPerScan;
        m_AdaptiveTargetScanFraction = Math.Clamp(m_AdaptiveTargetScanFraction, 0.1, 1.0);
        m_AdaptiveEmaAlpha = Math.Clamp(m_AdaptiveEmaAlpha, 0.05, 0.9);
        m_AdaptiveLosMinPerTick = Math.Clamp(m_AdaptiveLosMinPerTick, 1, 512);
        m_AdaptiveLosMaxPerTick = Math.Clamp(m_AdaptiveLosMaxPerTick, 1, 512);
        if (m_AdaptiveLosMaxPerTick < m_AdaptiveLosMinPerTick)
            m_AdaptiveLosMaxPerTick = m_AdaptiveLosMinPerTick;
        m_AdaptiveWlrMinSolves = Math.Clamp(m_AdaptiveWlrMinSolves, 0, 64);
        m_AdaptiveWlrMaxSolves = Math.Clamp(m_AdaptiveWlrMaxSolves, 0, 64);
        if (m_AdaptiveWlrMaxSolves < m_AdaptiveWlrMinSolves)
            m_AdaptiveWlrMaxSolves = m_AdaptiveWlrMinSolves;
        m_AdaptiveStepDown = Math.Clamp(m_AdaptiveStepDown, 0.5, 0.99);
        m_AdaptiveStepUp = Math.Clamp(m_AdaptiveStepUp, 1.01, 2.0);
        m_ScattererDiscoveryRangeScale = Math.Clamp(m_ScattererDiscoveryRangeScale, 1.0, 4.0);
        m_ScattererDiscoveryIntervalS = Math.Clamp(m_ScattererDiscoveryIntervalS, 0.25, 60.0);
        m_ScattererClassifyPerTick = Math.Clamp(m_ScattererClassifyPerTick, 1, 256);
        m_ScattererRefreshPerTick = Math.Clamp(m_ScattererRefreshPerTick, 1, 1024);
        m_ScattererMaxEntries = Math.Clamp(m_ScattererMaxEntries, 8, 4096);
        m_MinDistance = Math.Max(0.0, m_MinDistance);
        m_DetectionSnrDb = Math.Clamp(m_DetectionSnrDb, -100.0, 200.0);
        m_DemCacheMaxTiles = Math.Clamp(m_DemCacheMaxTiles, 1, 256);
        m_DemTileLoadsPerScan = Math.Clamp(m_DemTileLoadsPerScan, 0, 64);
        m_DemClutterScale = Math.Clamp(m_DemClutterScale, 0.0, 1000.0);
        m_NlosReflectionAbs = Math.Clamp(m_NlosReflectionAbs, 0.0, 1.0);
        m_NlosMinFactor = Math.Clamp(m_NlosMinFactor, 0.0, 1.0);
        m_NlosMaxTargetAglM = Math.Clamp(m_NlosMaxTargetAglM, 10.0, 20000.0);
        m_KnifeEdgeMaxSamples = Math.Clamp(m_KnifeEdgeMaxSamples, 2, 16);
        m_KnifeEdgeMinUSeparation = Math.Clamp(m_KnifeEdgeMinUSeparation, 0.05, 0.5);
        m_KnifeEdgeClearanceSlackM = Math.Clamp(m_KnifeEdgeClearanceSlackM, 0.0, 50.0);
        m_SitePathMaxOriginDriftM = Math.Clamp(m_SitePathMaxOriginDriftM, 0.5, 200.0);
        m_DemLosPrecheckSamples = Math.Clamp(m_DemLosPrecheckSamples, 2, 24);
        m_AdditionalNoisePowerW = Math.Max(0.0, m_AdditionalNoisePowerW);
        m_CfarGuardCells = Math.Clamp(m_CfarGuardCells, 0, 32);
        m_CfarTrainingCells = Math.Clamp(m_CfarTrainingCells, 2, 128);
        m_CfarPfa = Math.Clamp(m_CfarPfa, 0.00000001, 0.5);
        m_RangeBinCount = Math.Clamp(m_RangeBinCount, 8, 512);
        m_TrackGateRangeM = Math.Clamp(m_TrackGateRangeM, 10.0, 10000.0);
        m_TrackGateAzimuthDeg = Math.Clamp(m_TrackGateAzimuthDeg, 0.1, 90.0);
        m_TrackConfirmHits = Math.Clamp(m_TrackConfirmHits, 1, 16);
        m_TrackMaxMisses = Math.Clamp(m_TrackMaxMisses, 1, 32);
        m_TrackCoastGateGrowPerMiss = Math.Clamp(m_TrackCoastGateGrowPerMiss, 0.0, 5.0);
        m_TrackCoastMaxSec = Math.Clamp(m_TrackCoastMaxSec, 0.0, 120.0);
        m_ClutterMapAlpha = Math.Clamp(m_ClutterMapAlpha, 0.01, 1.0);
        m_ClutterMapAlphaDown = Math.Clamp(m_ClutterMapAlphaDown, 0.01, 1.0);
        m_ClutterMapAzBinCount = Math.Clamp(m_ClutterMapAzBinCount, 4, 180);
        m_ClutterFootprintSamples = Math.Clamp(m_ClutterFootprintSamples, 3, 9);
        m_RdCellsPerScan = Math.Clamp(m_RdCellsPerScan, 1, 512);
        m_RdMapAlpha = Math.Clamp(m_RdMapAlpha, 0.01, 1.0);
        m_RdDecayPerScan = Math.Clamp(m_RdDecayPerScan, 0.5, 1.0);
        m_ClutterSurfaceAzBins = Math.Clamp(m_ClutterSurfaceAzBins, 8, 128);
        m_ClutterSurfaceRangeBins = Math.Clamp(m_ClutterSurfaceRangeBins, 8, 128);
        m_ClutterSurfaceCellsPerScan = Math.Clamp(m_ClutterSurfaceCellsPerScan, 8, 512);
        m_ClutterSurfaceDecayPerScan = Math.Clamp(m_ClutterSurfaceDecayPerScan, 0.5, 1.0);
        m_ClutterSurfaceSectorHalfDeg = Math.Clamp(m_ClutterSurfaceSectorHalfDeg, 0.0, 180.0);
        m_ShellAirDrag = Math.Clamp(m_ShellAirDrag, 0.0, 1.0);
        m_WeaponLocateMinHits = Math.Clamp(m_WeaponLocateMinHits, 2, 32);
        m_WeaponLocateMinSpanS = Math.Clamp(m_WeaponLocateMinSpanS, 0.2, 30.0);
        m_WeaponLocateMaxFitRmsM = Math.Clamp(m_WeaponLocateMaxFitRmsM, 5.0, 500.0);
        m_WeaponLocateFitWindow = Math.Clamp(m_WeaponLocateFitWindow, 3, 64);
        m_WeaponLocateSmoothAlpha = Math.Clamp(m_WeaponLocateSmoothAlpha, 0.0, 1.0);
        m_WeaponLocateSolvesPerScan = Math.Clamp(m_WeaponLocateSolvesPerScan, 0, 64);
        m_WeaponLocateQueueMax = Math.Clamp(m_WeaponLocateQueueMax, 1, 256);
        m_LosCacheMaxAgeS = Math.Clamp(m_LosCacheMaxAgeS, 0.05, 5.0);
        m_LosCacheMaxOriginShiftM = Math.Clamp(m_LosCacheMaxOriginShiftM, 0.1, 50.0);
        m_LosCacheMaxTargetShiftM = Math.Clamp(m_LosCacheMaxTargetShiftM, 0.1, 100.0);
        m_TargetReuseMaxAgeS = Math.Clamp(m_TargetReuseMaxAgeS, 0.05, 10.0);
        m_PhysicalReuseMaxAgeS = Math.Clamp(m_PhysicalReuseMaxAgeS, 0.05, 5.0);
        m_PhysicalReuseMaxOriginShiftM = Math.Clamp(m_PhysicalReuseMaxOriginShiftM, 0.1, 50.0);
        m_PhysicalReuseMaxTargetShiftM = Math.Clamp(m_PhysicalReuseMaxTargetShiftM, 0.1, 100.0);
        m_PhysicalReuseMaxSpeedMs = Math.Clamp(m_PhysicalReuseMaxSpeedMs, 0.0, 100.0);
        m_PriorityNearRangeM = Math.Clamp(m_PriorityNearRangeM, 50.0, 50000.0);
        m_PriorityMidRangeM = Math.Clamp(m_PriorityMidRangeM, 100.0, 100000.0);
        if (m_PriorityMidRangeM < m_PriorityNearRangeM)
            m_PriorityMidRangeM = m_PriorityNearRangeM;
        m_PriorityFastSpeedMs = Math.Clamp(m_PriorityFastSpeedMs, 1.0, 500.0);
        m_PrioritySlowSpeedMs = Math.Clamp(m_PrioritySlowSpeedMs, 0.0, 200.0);
        m_PriorityBand1IntervalS = Math.Clamp(m_PriorityBand1IntervalS, 0.0, 5.0);
        m_PriorityBand2IntervalS = Math.Clamp(m_PriorityBand2IntervalS, 0.0, 10.0);
        m_FreshUpdateBudgetMin = Math.Clamp(m_FreshUpdateBudgetMin, 1, 256);
        m_FreshUpdateBudgetMax = Math.Clamp(m_FreshUpdateBudgetMax, 1, 512);
        if (m_FreshUpdateBudgetMax < m_FreshUpdateBudgetMin)
            m_FreshUpdateBudgetMax = m_FreshUpdateBudgetMin;
        m_WlrHudAlertRadiusM = Math.Clamp(m_WlrHudAlertRadiusM, 5.0, 2000.0);
        m_MeasNoiseScale = Math.Clamp(m_MeasNoiseScale, 0.0, 10.0);
        m_MeasRangeBiasM = Math.Clamp(m_MeasRangeBiasM, -500.0, 500.0);
        m_MeasAzimuthBiasDeg = Math.Clamp(m_MeasAzimuthBiasDeg, -5.0, 5.0);
        m_MeasElevationBiasDeg = Math.Clamp(m_MeasElevationBiasDeg, -5.0, 5.0);
        m_MeasDopplerBiasHz = Math.Clamp(m_MeasDopplerBiasHz, -500.0, 500.0);
        m_DwellBudgetMs = Math.Clamp(m_DwellBudgetMs, 0.0, 1000.0);
        m_FireControlDwellPeriodS = Math.Clamp(m_FireControlDwellPeriodS, 0.02, 60.0);
        m_FireControlDwellMs = Math.Clamp(m_FireControlDwellMs, 0.0, 100.0);
        m_TrackDwellPeriodS = Math.Clamp(m_TrackDwellPeriodS, 0.05, 60.0);
        m_TrackDwellMs = Math.Clamp(m_TrackDwellMs, 0.0, 100.0);
        m_EccmJnOnDb = Math.Clamp(m_EccmJnOnDb, -60.0, 60.0);
        m_EccmJnHysteresisDb = Math.Clamp(m_EccmJnHysteresisDb, 0.0, 30.0);
        m_EccmSidelobeCouplingOn = Math.Clamp(m_EccmSidelobeCouplingOn, 0.0, 1.0);
        m_JpdaPd = Math.Clamp(m_JpdaPd, 0.1, 1.0);
        m_RwrAntennaGainDbi = Math.Clamp(m_RwrAntennaGainDbi, -20.0, 20.0);
        m_RwrDetectSnrDb = Math.Clamp(m_RwrDetectSnrDb, -10.0, 30.0);
        m_WeaponGradeMinConfidence = Math.Clamp(m_WeaponGradeMinConfidence, 0.0, 1.0);
        m_LastRainIntensity = Math.Clamp(m_LastRainIntensity, 0.0, 1.0);
        m_DuctHorizonScale = Math.Clamp(m_DuctHorizonScale, 1.0, 8.0);
        m_AtmLossDbPerKmOneWay = Math.Clamp(m_AtmLossDbPerKmOneWay, -1.0, 5.0);
        m_RainLossDbPerKmOneWay = Math.Clamp(m_RainLossDbPerKmOneWay, 0.0, 20.0);
        m_RainLossDbPerKmAtFullIntensity = Math.Clamp(
            m_RainLossDbPerKmAtFullIntensity, 0.0, 20.0);
        m_FogLossDbPerKmAtFullFog = Math.Clamp(m_FogLossDbPerKmAtFullFog, 0.0, 5.0);
        m_LosTwoRayReflectionCoeff = Math.Clamp(m_LosTwoRayReflectionCoeff, -1.0, 1.0);
        m_LosTwoRayMaxTargetAglM = Math.Clamp(m_LosTwoRayMaxTargetAglM, 10.0, 5000.0);
        m_LosTwoRayMinFactor = Math.Clamp(m_LosTwoRayMinFactor, 0.01, 1.0);
        m_LosTwoRayMaxFactor = Math.Clamp(m_LosTwoRayMaxFactor, 1.0, 8.0);
        m_EarthRadiusFactor = Math.Clamp(m_EarthRadiusFactor, 0.5, 4.0);
        if (!m_Hardware)
            m_Hardware = RDF_RadarHardware.CreateShorad();
        if (!m_BandChannels)
            m_BandChannels = new array<ref RDF_RadarHardware>();
        if (m_EnableMultiBand)
        {
            if (m_BandChannels.Count() == 0)
                m_BandChannels.Insert(m_Hardware);
            int nCh = m_BandChannels.Count();
            if (nCh < 1)
                nCh = 1;
            m_SearchBandIndex = Math.ClampInt(m_SearchBandIndex, 0, nCh - 1);
            m_TrackBandIndex = Math.ClampInt(m_TrackBandIndex, 0, nCh - 1);
            m_FireControlBandIndex = Math.ClampInt(m_FireControlBandIndex, 0, nCh - 1);
            m_ActiveBandIndex = Math.ClampInt(m_ActiveBandIndex, 0, nCh - 1);
            for (int ci = 0; ci < m_BandChannels.Count(); ci++)
            {
                RDF_RadarHardware ch = m_BandChannels.Get(ci);
                if (ch)
                    ch.Validate();
            }
        }
        if (!m_EwStack)
            m_EwStack = new RDF_RadarEwStack();
        if (!m_MeasurementModel)
            m_MeasurementModel = new RDF_RadarDefaultMeasurementModel();
        m_Hardware.Validate();
    }

    //------------------------------------------------------------------------------------------------
    // Point m_Hardware at the channel for this dwell kind. No-op when multi-band
    // is off. Does not mutate authored channel objects.
    void SelectBandForDwell(ERDF_DwellKind kind)
    {
        if (!m_EnableMultiBand)
            return;
        if (!m_BandChannels)
            return;
        int n = m_BandChannels.Count();
        if (n <= 0)
            return;

        int idx = m_SearchBandIndex;
        if (kind == ERDF_DwellKind.RDF_DWELL_TRACK)
            idx = m_TrackBandIndex;
        else if (kind == ERDF_DwellKind.RDF_DWELL_FIRE_CONTROL)
            idx = m_FireControlBandIndex;
        if (idx < 0)
            idx = 0;
        if (idx >= n)
            idx = 0;

        RDF_RadarHardware channel = m_BandChannels.Get(idx);
        if (!channel)
            return;
        m_Hardware = channel;
        m_ActiveBandIndex = idx;
    }

    //------------------------------------------------------------------------------------------------
    // Opt-in: measurement noise / bias. scale=0 clears random noise (biases remain
    // unless zeroed via ClearMeasurementNoise).
    void SetMeasurementNoise(
        float scale,
        float rangeBiasM,
        float azBiasDeg,
        float elBiasDeg)
    {
        m_EnableMeasurementSynthesis = true;
        m_MeasNoiseScale = scale;
        m_MeasRangeBiasM = rangeBiasM;
        m_MeasAzimuthBiasDeg = azBiasDeg;
        m_MeasElevationBiasDeg = elBiasDeg;
    }

    //------------------------------------------------------------------------------------------------
    // Caller floor (m_MinDistance) plus optional pulse-eclipsing Rmin.
    float GetEffectiveMinDistance()
    {
        float minDist = m_MinDistance;
        if (!m_EnablePulseBlindZone)
            return minDist;
        if (!m_Hardware)
            return minDist;

        float pulseMin = m_Hardware.GetMinDetectableRangeM();
        if (pulseMin > minDist)
            return pulseMin;
        return minDist;
    }

    //------------------------------------------------------------------------------------------------
    void ClearMeasurementNoise()
    {
        m_MeasNoiseScale = 0.0;
        m_MeasRangeBiasM = 0.0;
        m_MeasAzimuthBiasDeg = 0.0;
        m_MeasElevationBiasDeg = 0.0;
        m_MeasDopplerBiasHz = 0.0;
    }

    //------------------------------------------------------------------------------------------------
    // Opt-in gameplay packs: compose existing Enable* helpers for common roles.
    // Does not raise TraceMove budget. Does not enable JPDA / coarse RD / mesh RCS.
    // Map from product mode via RDF_RadarSensor.FidelityPresetForMode.
    void ApplyGameplayFidelity(ERDF_RadarFidelityPreset preset)
    {
        if (preset == ERDF_RadarFidelityPreset.RDF_FIDELITY_NONE)
            return;
        if (preset == ERDF_RadarFidelityPreset.RDF_FIDELITY_SHORAD)
        {
            ApplyShoradGameplayFidelity();
            return;
        }
        if (preset == ERDF_RadarFidelityPreset.RDF_FIDELITY_AIRBORNE)
        {
            ApplyAirborneGameplayFidelity();
            return;
        }
        if (preset == ERDF_RadarFidelityPreset.RDF_FIDELITY_WLR)
        {
            ApplyWlrGameplayFidelity();
            return;
        }
        if (preset == ERDF_RadarFidelityPreset.RDF_FIDELITY_ESM)
        {
            ApplyEsmGameplayFidelity();
            return;
        }
    }

    //------------------------------------------------------------------------------------------------
    // Ground SHORAD / SEARCH / STARE: low-AGL multipath + weather + mild noise.
    protected void ApplyShoradGameplayFidelity()
    {
        ApplyCommonPropagationFidelity();
        SetMeasurementNoise(2.0, 2.0, 0.12, 0.08);
        EnableLosTwoRayMultipath();
        m_EnableMultipathGlint = true;
        m_EnableRainSeaClutterDamp = true;
        EnablePrfAmbiguityFolds(true, false);
        Validate();
    }

    //------------------------------------------------------------------------------------------------
    // Pulse-Doppler / air search: SHORAD pack + Doppler fold + NCTR + clutter sharpen.
    protected void ApplyAirborneGameplayFidelity()
    {
        ApplyCommonPropagationFidelity();
        SetMeasurementNoise(2.5, 3.0, 0.15, 0.12);
        EnableLosTwoRayMultipath();
        m_EnableMultipathGlint = true;
        m_EnableRainSeaClutterDamp = true;
        EnablePrfAmbiguityFolds(true, true);
        m_EnableNctr = true;
        EnableClutterSharpen(true, 0.15, 0.45, true);
        m_ClutterFootprintSamples = 5;
        Validate();
    }

    //------------------------------------------------------------------------------------------------
    // Counter-battery: path loss / refraction matter at long range; skip two-ray
    // (projectiles ignore it) and PRF folds (weapon-locate prefers unfolded range).
    protected void ApplyWlrGameplayFidelity()
    {
        ApplyCommonPropagationFidelity();
        SetMeasurementNoise(2.0, 5.0, 0.2, 0.15);
        m_EnableMultipathGlint = false;
        m_EnableRainSeaClutterDamp = false;
        EnablePrfAmbiguityFolds(false, false);
        Validate();
    }

    //------------------------------------------------------------------------------------------------
    // Receive-only ESM / DF: one-way atm + horizon + Friis RWR + optional site LUT.
    protected void ApplyEsmGameplayFidelity()
    {
        ApplyCommonPropagationFidelity();
        SetMeasurementNoise(1.5, 0.0, 0.25, 0.1);
        EnableLosTwoRayMultipath();
        m_EnableRwrFriis = true;
        EnablePatternAndSiteLuts(true, true);
        EnablePrfAmbiguityFolds(false, false);
        Validate();
    }

    //------------------------------------------------------------------------------------------------
    // Shared: clear-air + weather rain/fog, 4/3 refraction, CFAR thermal, pattern tables.
    protected void ApplyCommonPropagationFidelity()
    {
        EnableAtmosphericPathLoss(true);
        EnableAtmosphericRefraction();
        EnableCfarThermalFill(true);
        EnableAntennaPatternFidelity(true, true);
    }

    //------------------------------------------------------------------------------------------------
    // Opt-in: clear-air (+ optional weather rain/fog) path loss on received power.
    void EnableAtmosphericPathLoss(bool weatherDriven)
    {
        m_EnableAtmosphericLoss = true;
        m_EnableWeatherDrivenRainLoss = weatherDriven;
        m_AtmLossDbPerKmOneWay = -1.0;
    }

    //------------------------------------------------------------------------------------------------
    void DisableAtmosphericPathLoss()
    {
        m_EnableAtmosphericLoss = false;
        m_EnableWeatherDrivenRainLoss = false;
    }

    //------------------------------------------------------------------------------------------------
    // Opt-in: CFAR thermal fill of empty range cells (real Pfa behaviour).
    void EnableCfarThermalFill(bool enable)
    {
        m_EnableCfarThermalFill = enable;
        if (enable)
            m_EnableCfarGate = true;
    }

    //------------------------------------------------------------------------------------------------
    // Opt-in: clear-LOS two-ray multipath power lobes (not waveform sim).
    void EnableLosTwoRayMultipath()
    {
        m_EnableLosTwoRayMultipath = true;
        m_LosTwoRayReflectionCoeff = -0.5;
        m_LosTwoRayMaxTargetAglM = 600.0;
        m_LosTwoRayMinFactor = 0.08;
        m_LosTwoRayMaxFactor = 4.0;
        m_LosTwoRayUseSurfaceDielectric = true;
    }

    //------------------------------------------------------------------------------------------------
    // Opt-in: 4/3-Earth refraction (horizon soft factor + elevation bias).
    void EnableAtmosphericRefraction()
    {
        m_EnableAtmosphericRefraction = true;
        m_EarthRadiusFactor = 1.333333;
    }

    //------------------------------------------------------------------------------------------------
    // Opt-in: fold measured range / Doppler into PRF unambiguous intervals.
    // Weapon-locate paths still skip Doppler fold at synthesize time.
    void EnablePrfAmbiguityFolds(bool foldRange, bool foldDoppler)
    {
        m_EnableRangeAmbiguityFold = foldRange;
        m_EnableDopplerAmbiguityFold = foldDoppler;
    }

    //------------------------------------------------------------------------------------------------
    // Off-axis pattern floor + polarization match tables (default on).
    void EnableAntennaPatternFidelity(bool sidelobeFloor, bool polarizationMatch)
    {
        m_EnableSidelobeFloor = sidelobeFloor;
        m_EnablePolarizationMatch = polarizationMatch;
    }

    //------------------------------------------------------------------------------------------------
    // Segmented pattern LUT + optional fixed-site DEM path table.
    void EnablePatternAndSiteLuts(bool patternLut, bool sitePathLut)
    {
        m_EnablePatternLut = patternLut;
        m_EnableSitePathLut = sitePathLut;
    }

    //------------------------------------------------------------------------------------------------
    // Clutter-boundary sharpening: asymmetric map EMA + optional footprint σ⁰ mix.
    void EnableClutterSharpen(
        bool clutterMap,
        float alphaUp,
        float alphaDown,
        bool footprintMix)
    {
        m_EnableClutterMap = clutterMap;
        m_ClutterMapAlpha = alphaUp;
        m_ClutterMapAlphaDown = alphaDown;
        m_EnableClutterFootprintMix = footprintMix;
    }

    //------------------------------------------------------------------------------------------------
    // Cockpit range–az clutter surface (display only; not detection).
    void EnableRangeAzClutterSurface(bool enable)
    {
        m_EnableRangeAzClutterSurface = enable;
    }

    //------------------------------------------------------------------------------------------------
    // AutoTest / Debugger helper: turn OFF optional fidelity extras that make
    // logic-loop assertions flaky. Not a gameplay "ideal tier" — mods should
    // Enable* only what they need. Does not force CFAR on/off.
    void StabilizeForRegression()
    {
        ClearMeasurementNoise();
        m_EnableCfarThermalFill = false;
        m_EnableAtmosphericLoss = false;
        m_EnableWeatherDrivenRainLoss = false;
        m_AtmLossDbPerKmOneWay = -1.0;
        m_RainLossDbPerKmOneWay = 0.0;
        m_EnableLosTwoRayMultipath = false;
        m_EnableAtmosphericRefraction = false;
        m_EnableRangeAmbiguityFold = false;
        m_EnableDopplerAmbiguityFold = false;
        m_EnableCoarseRd = false;
        m_EnableRangeAzClutterSurface = false;
        m_EnableDemSpanOcclusion = false;
        m_EnableClutterFootprintMix = false;
        m_EnableNctr = false;
        m_EnableRwrFriis = false;
        m_EnableRainSeaClutterDamp = false;
        m_EnableAtmosphericDuct = false;
        m_EnableMultipathGlint = false;
        m_WeaponGradeMinConfidence = 0.0;
    }

    //------------------------------------------------------------------------------------------------
    // Resolve effective one-way rain(+fog) loss for this scan.
    // Manual floor always applies; weather terms optional.
    // frequencyHz > 0 scales the weather rain term vs X-band 9 GHz (SHORAD unchanged).
    float ResolveRainLossDbPerKm(RDF_RadarWeatherSnapshot weather)
    {
        return ResolveRainLossDbPerKm(weather, 0.0);
    }

    //------------------------------------------------------------------------------------------------
    float ResolveRainLossDbPerKm(RDF_RadarWeatherSnapshot weather, float frequencyHz)
    {
        float rainDb = m_RainLossDbPerKmOneWay;
        if (!m_EnableWeatherDrivenRainLoss)
            return rainDb;
        if (!weather || !weather.m_Valid)
            return rainDb;

        float weatherRain = weather.m_RainIntensity * m_RainLossDbPerKmAtFullIntensity;
        if (frequencyHz > 0.0)
        {
            float xRef = RDF_RadarClutterModel.RainOneWayDbPerKm(25.0, 9000000000.0);
            float fRain = RDF_RadarClutterModel.RainOneWayDbPerKm(25.0, frequencyHz);
            if (xRef > 0.000001)
                weatherRain = weatherRain * (fRain / xRef);
        }

        rainDb = rainDb
            + weatherRain
            + weather.m_FogAmount * m_FogLossDbPerKmAtFullFog;
        if (rainDb < 0.0)
            rainDb = 0.0;
        if (rainDb > 20.0)
            rainDb = 20.0;
        return rainDb;
    }
}
