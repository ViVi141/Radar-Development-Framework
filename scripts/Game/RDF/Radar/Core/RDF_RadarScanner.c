// Radar scanner - extends RDF_LidarScanner with electromagnetic physics.
// Overrides Scan() to produce RDF_RadarSample objects and apply the
// full radar equation pipeline after each geometry ray trace.
//
// WARNING: IN DEVELOPMENT — Do not use in production environments.
//
// Design note: Scan() does NOT call super.Scan() because the base implementation
// allocates RDF_LidarSample objects that cannot be downcast to RDF_RadarSample.
// Instead, the geometry tracing logic is replicated here, and RDF_RadarSample
// objects are created directly so that ApplyRadarPhysics() can populate them.
class RDF_RadarScanner : RDF_LidarScanner
{
    // Typed alias kept alongside the inherited m_Settings reference so that
    // radar-specific fields are accessible without repeated casts.
    protected ref RDF_RadarSettings m_RadarSettings;
    // Active waveform-mode processor; instantiated from m_RadarSettings.m_RadarMode.
    protected ref RDF_RadarMode m_RadarModeProcessor;

    // Parameter type matches parent RDF_LidarScanner(RDF_LidarSettings).
    // Pass an RDF_RadarSettings instance; any non-radar settings are ignored
    // and a fresh RDF_RadarSettings is created instead.
    void RDF_RadarScanner(RDF_LidarSettings settings = null)
    {
        RDF_RadarSettings radarSettings = RDF_RadarSettings.Cast(settings);
        if (radarSettings)
            m_RadarSettings = radarSettings;
        else
            m_RadarSettings = new RDF_RadarSettings();

        // Point the base-class reference at the same object.
        m_Settings = m_RadarSettings;

        // Default strategy: uniform sphere distribution.
        m_SampleStrategy = new RDF_UniformSampleStrategy();

        // Instantiate the mode processor matching the configured mode.
        InstantiateMode(m_RadarSettings.m_RadarMode);

        // Optional: load offline OS-CFAR table at scanner init when enabled
        if (m_RadarSettings.m_CfarUseOfflineTable && m_RadarSettings.m_CfarOfflineTablePath != string.Empty)
        {
            RDF_CFar.LoadOSMultiplierTableFromFile(m_RadarSettings.m_CfarOfflineTablePath);
        }
    }

    RDF_RadarSettings GetRadarSettings()
    {
        return m_RadarSettings;
    }

    // Switch the active waveform mode at runtime.
    void SetRadarMode(ERadarMode mode)
    {
        if (m_RadarSettings)
            m_RadarSettings.m_RadarMode = mode;
        InstantiateMode(mode);
    }

    RDF_RadarMode GetRadarModeProcessor()
    {
        return m_RadarModeProcessor;
    }

    // Create the concrete mode object for the given ERadarMode value.
    protected void InstantiateMode(ERadarMode mode)
    {
        switch (mode)
        {
            case ERadarMode.PULSE:
                m_RadarModeProcessor = new RDF_PulseRadarMode();
                break;
            case ERadarMode.CW:
                m_RadarModeProcessor = new RDF_CWRadarMode();
                break;
            case ERadarMode.FMCW:
                m_RadarModeProcessor = new RDF_FMCWRadarMode();
                break;
            case ERadarMode.PHASED_ARRAY:
                m_RadarModeProcessor = new RDF_PhasedArrayMode();
                break;
            default:
                m_RadarModeProcessor = new RDF_RadarMode();
        }
    }

    // Main scan entry point.
    // Produces an array of RDF_RadarSample (stored as RDF_LidarSample for
    // compatibility). Each sample includes geometry + full radar-physics output.
    override void Scan(IEntity subject, array<ref RDF_LidarSample> outSamples)
    {
        if (!subject || !m_Settings || !m_Settings.m_Enabled)
            return;
        if (!outSamples)
            return;

        outSamples.Clear();

        World world = subject.GetWorld();
        if (!world)
        {
            world = GetGame().GetWorld();
            if (!world)
                return;
        }

        m_Settings.Validate();

        vector origin = GetSubjectOrigin(subject);
        int   rays    = Math.Max(m_Settings.m_RayCount, 1);
        float range   = Math.Clamp(m_Settings.m_Range, 0.1, 100000.0);

        // PoC: prepare EM voxel field + EM params for ray injection (Phase‑2)
        EMVoxelField evField = EMVoxelField.GetInstance();
        RDF_EMWaveParameters emParams = m_RadarSettings.m_EMWaveParams;

        // Subject orientation matrix for direction transforms.
        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        vector basisX = worldMat[0];
        vector basisY = worldMat[1];
        vector basisZ = worldMat[2];

        // Reuse trace parameter instance to reduce per-ray allocations.
        TraceParam param = new TraceParam();

        for (int i = 0; i < rays; i++)
        {
            // Build sample direction in local space, then transform to world space.
            vector dirLocal;
            if (m_SampleStrategy)
                dirLocal = m_SampleStrategy.BuildDirection(i, rays);
            else
                dirLocal = BuildUniformDirection(i, rays);

            vector dir  = (basisX * dirLocal[0]) + (basisY * dirLocal[1]) + (basisZ * dirLocal[2]);
            float  dlen = dir.Length();
            if (dlen > 0.0)
                dir = dir / dlen;

            // PoC Phase‑2: inject transmitted energy along the ray into EM voxel field
            if (evField && emParams)
            {
                // Main lobe injection
                evField.InjectAlongRay(origin, dir, range, emParams.m_TransmitPower, 0x1, 0.5);

                // Optional sidelobe sampling (PoC)
                if (m_RadarSettings && m_RadarSettings.m_EnableSidelobes && m_RadarSettings.m_SidelobeSampleCount > 0)
                {
                    int n = Math.Max(m_RadarSettings.m_SidelobeSampleCount, 1);
                    float sidelobeTotalFrac = Math.Clamp(m_RadarSettings.m_SidelobeFraction, 0.0, 1.0);
                    float perSamplePt = (emParams.m_TransmitPower * sidelobeTotalFrac) / (float)n;

                    // Convert dir -> az/el
                    float az = Math.Atan2(dir[2], dir[0]);
                    float el = Math.Atan2(dir[1], Math.Sqrt(dir[0]*dir[0] + dir[2]*dir[2]));

                    // radius for sidelobe (deg) — place sidelobes outside main beam
                    float sidelobeOffsetDeg = Math.Max(emParams.m_BeamwidthAzimuth * 1.5, 5.0);

                    for (int s = 0; s < n; ++s)
                    {
                        float theta = (2.0 * Math.PI) * (float)s / (float)n;
                        float offAz = az + Math.Cos(theta) * sidelobeOffsetDeg * Math.DEG2RAD;
                        float offEl = el + Math.Sin(theta) * (sidelobeOffsetDeg * 0.2) * Math.DEG2RAD; // small elevation spread

                        // convert back to direction vector
                        float cosEl = Math.Cos(offEl);
                        vector sdir = Vector(cosEl * Math.Cos(offAz), Math.Sin(offEl), cosEl * Math.Sin(offAz));
                        sdir = sdir.Normalized();

                        evField.InjectAlongRay(origin, sdir, range, perSamplePt, 0x1, 0.5);
                    }
                }
            }

            // Populate trace parameters.
            param.Start        = origin;
            param.End          = origin + (dir * range);
            param.Flags        = m_Settings.m_TraceFlags;
            param.LayerMask    = m_Settings.m_LayerMask;
            param.Exclude      = subject;
            param.TraceEnt     = null;
            param.SurfaceProps = null;
            param.ColliderName = string.Empty;

            float hitFraction = world.TraceMove(param, null);
            bool  hit         = (param.TraceEnt != null) || (param.SurfaceProps != null);

            vector hitPos = param.End;
            float  dist   = range;

            if (hit && hitFraction > 0.0)
            {
                dist   = hitFraction * range;
                hitPos = origin + (dir * dist);
                if (dist <= 0.0)
                {
                    hit    = false;
                    dist   = range;
                    hitPos = param.End;
                }
                // Treat max-range / far-plane as no hit (avoids sky or void reported as target).
                else if (dist >= range * 0.9999)
                {
                    hit = false;
                }
            }
            else
            {
                hit = false;
            }

            // Allocate as RDF_RadarSample so physics fields are available.
            RDF_RadarSample sample = new RDF_RadarSample();
            sample.m_Index        = i;
            sample.m_Hit          = hit;
            sample.m_Start        = origin;
            sample.m_End          = param.End;
            sample.m_Dir          = dir;
            sample.m_HitPos       = hitPos;
            sample.m_Distance     = dist;
            sample.m_Entity       = param.TraceEnt;
            sample.m_ColliderName = param.ColliderName;
            sample.m_Surface      = param.SurfaceProps;

            sample.m_EntityKind  = RDF_EntityPreClassifier.ClassifyEntity(sample.m_Entity);

            ApplyRadarPhysics(subject, sample);

            // PoC Phase‑2: when ray hit an entity/terrain, write the echo energy into voxel field
            if (sample.m_Hit)
            {
                float reflPower = sample.m_ReceivedPower;
                if (reflPower > 0.0)
                    EMVoxelField.GetInstance().InjectReflection(sample.m_HitPos, reflPower, 0x1, sample.m_Dir, 0.5);
            }

            outSamples.Insert(sample);
        }

        // Post-scan CFAR processing — optionally suppress local false alarms.
        if (m_RadarSettings && m_RadarSettings.m_EnableCFAR)
        {
            // Determine effective multiplier (support auto-scale for CA and OS)
            float effectiveMultiplier = m_RadarSettings.m_CfarMultiplier;
            if (m_RadarSettings.m_CfarAutoScale && m_RadarSettings.m_CfarTargetPfa > 0.0)
            {
                if (m_RadarSettings.m_CfarUseOrderStatistic)
                {
                    effectiveMultiplier = RDF_CFar.EstimateOSMultiplier(
                        m_RadarSettings.m_CfarWindowSize,
                        m_RadarSettings.m_CfarOrderRank,
                        m_RadarSettings.m_CfarTargetPfa,
                        2048);
                }
                else
                {
                    effectiveMultiplier = RDF_CFar.CalculateCAThresholdMultiplier(
                        m_RadarSettings.m_CfarWindowSize,
                        m_RadarSettings.m_CfarTargetPfa);
                }
                effectiveMultiplier = Math.Max(effectiveMultiplier, 1.0);
            }

            if (m_RadarSettings.m_CfarUseOrderStatistic)
            {
                RDF_CFar.ApplyOS_CFAR(outSamples,
                                      m_RadarSettings.m_CfarWindowSize,
                                      m_RadarSettings.m_CfarGuardAngleDeg,
                                      effectiveMultiplier,
                                      m_RadarSettings.m_CfarOrderRank);
            }
            else
            {
                RDF_CFar.ApplyCA_CFAR(outSamples,
                                      m_RadarSettings.m_CfarWindowSize,
                                      m_RadarSettings.m_CfarGuardAngleDeg,
                                      effectiveMultiplier);
            }
        }

        // Re-apply clutter and self-hit after CFAR so CFAR cannot re-enable terrain or platform.
        if (m_RadarSettings)
        {
            foreach (RDF_LidarSample baseSample : outSamples)
            {
                RDF_RadarSample s = RDF_RadarSample.Cast(baseSample);
                if (!s) continue;
                if (m_RadarSettings.m_EnableClutterFilter && s.m_Entity == null)
                    s.m_Hit = false;
                if (s.m_Entity != null && subject != null && IsHitOnRadarPlatform(s.m_Entity, subject))
                    s.m_Hit = false;
            }
        }
    }

    // Apply the full radar physics pipeline to a single geometry sample.
    // Called immediately after the ray trace; modifies sample in place.
    protected void ApplyRadarPhysics(IEntity radarEntity, RDF_RadarSample sample)
    {
        if (!sample || !m_RadarSettings)
            return;

        RDF_EMWaveParameters emParams = m_RadarSettings.m_EMWaveParams;
        if (!emParams)
            return;

        sample.m_TransmitPower = emParams.m_TransmitPower;

        // 1. Propagation losses.
        sample.m_PathLoss = RDF_RadarPropagation.CalculateFSPL(
            sample.m_Distance,
            emParams.m_CarrierFrequency);

        if (m_RadarSettings.m_EnableAtmosphericModel)
        {
            sample.m_AtmosphericLoss = RDF_RadarPropagation.CalculateAtmosphericAttenuation(
                sample.m_Distance,
                emParams.m_CarrierFrequency,
                m_RadarSettings.m_Temperature,
                m_RadarSettings.m_Humidity);

            sample.m_RainAttenuation = RDF_RadarPropagation.CalculateRainAttenuation(
                sample.m_Distance,
                emParams.m_CarrierFrequency,
                m_RadarSettings.m_RainRate);
        }

        // 2. Target RCS.
        if (sample.m_Hit && m_RadarSettings.m_UseRCSModel)
        {
            // Resolve material name first so EstimateEntityRCS can use it.
            if (sample.m_Surface)
                sample.m_MaterialType = sample.m_Surface.GetName();

            sample.m_RadarCrossSection = RDF_RCSModel.EstimateEntityRCS(
                sample.m_Entity,
                sample.m_Start,
                sample.m_Dir,
                emParams.m_Wavelength,
                sample.m_MaterialType);

            sample.m_IncidenceAngle = CalculateIncidenceAngle(
                sample.m_Dir,
                GetSurfaceNormal(sample));

            if (m_RadarSettings.m_UseMaterialReflection && sample.m_Surface)
            {
                float reflectivity = RDF_RCSModel.GetMaterialReflectivity(sample.m_MaterialType);
                sample.m_ReflectionCoefficient = reflectivity;
            }
        }

        if (sample.m_Hit && sample.m_Entity != null && sample.m_RadarCrossSection <= 0)
            sample.m_RadarCrossSection = 0.1;

        // 3. Received power (radar equation).
        // NOTE: FSPL is already embedded in the radar equation via the R^4 and lambda^2 terms.
        // The 'L' parameter must only carry true system losses (hardware, polarisation, etc.)
        // that are NOT part of free-space propagation.  Atmospheric and rain terms are
        // one-way losses applied on top of the two-way path, so we include them here but
        // NOT the FSPL stored in sample.m_PathLoss.
        if (sample.m_Hit && sample.m_RadarCrossSection > 0)
        {
            float antennaGainLin  = RDF_RadarEquation.DBiToLinear(emParams.m_AntennaGain);
            float systemLossDB    = sample.m_AtmosphericLoss + sample.m_RainAttenuation
                                    + m_RadarSettings.m_SystemLossDB;
            float systemLossLin   = Math.Pow(10.0, systemLossDB / 10.0);

            sample.m_ReceivedPower = RDF_RadarEquation.CalculateReceivedPower(
                emParams.m_TransmitPower,
                antennaGainLin,
                emParams.m_Wavelength,
                sample.m_RadarCrossSection,
                sample.m_Distance,
                Math.Max(systemLossLin, 1.0));

            // Noise power assumes 1 MHz effective bandwidth.
            float noisePower = RDF_RadarEquation.CalculateNoisePower(
                1000000.0,
                emParams.m_NoiseFigure,
                290.0);

            sample.m_SignalToNoiseRatio = RDF_RadarEquation.CalculateSNR(
                sample.m_ReceivedPower,
                noisePower,
                1.0);
        }

        // 4. Time of flight, propagation delay, and two-way phase shift.
        sample.CalculateTimeOfFlight();
        sample.m_DelayTime = sample.m_TimeOfFlight;
        if (emParams.m_Wavelength > 0 && sample.m_Distance > 0)
        {
            // Two-way phase: phi = 4*PI*R / lambda
            sample.m_PhaseShift = (4.0 * Math.PI * sample.m_Distance) / emParams.m_Wavelength;
        }

        // 5. Doppler.
        if (m_RadarSettings.m_EnableDopplerProcessing && sample.m_Hit && sample.m_Entity)
        {
            sample.m_TargetVelocity = RDF_DopplerProcessor.CalculateRadialVelocity(
                radarEntity,
                sample.m_Entity);

            sample.m_DopplerFrequency = RDF_DopplerProcessor.CalculateDopplerShift(
                sample.m_TargetVelocity,
                emParams.m_CarrierFrequency);
        }

        // 5.5 Blind-speed suppression (optional PoC) — drop targets whose Doppler aliases to zero.
        if (m_RadarSettings.m_EnableBlindSpeedFilter && sample.m_Hit && m_RadarSettings.m_EnableDopplerProcessing)
        {
            float prf = emParams.m_PRF;
            float f0  = emParams.m_CarrierFrequency;
            if (RDF_DopplerProcessor.IsBlindSpeed(sample.m_TargetVelocity, prf, f0, m_RadarSettings.m_BlindSpeedToleranceHz))
                sample.m_Hit = false;
        }

        // Phase‑4 PoC: simple ground reflection / multipath injection into voxel field.
        // - If the ray points downward or hits terrain, create a reflected ray about the
        //   horizontal plane and inject a scaled portion of transmit energy into voxels.
        EMVoxelField evField = EMVoxelField.GetInstance();
        if (evField && emParams)
        {
            // Determine an approximate ground intersection and normal.
            vector groundNormal = Vector(0, 1, 0);
            vector reflectOrigin;
            bool doReflect = false;

            if (!sample.m_Hit && sample.m_Dir[1] < 0.0)
            {
                // intersect horizontal plane y=0 (simple ground plane approximation)
                float t = 0.0;
                if (sample.m_Start[1] > 0.0 && sample.m_Dir[1] < 0.0)
                {
                    t = (0.0 - sample.m_Start[1]) / sample.m_Dir[1];
                    if (t > 0.0)
                    {
                        reflectOrigin = sample.m_Start + sample.m_Dir * t;
                        doReflect = true;
                    }
                }
            }
            else if (sample.m_Hit && sample.m_Surface)
            {
                // If hit terrain-like surface, use hit position as reflection origin
                // (Material-based decision could be added later).
                reflectOrigin = sample.m_HitPos;
                doReflect = true;
            }

            if (doReflect)
            {
                // reflect direction about ground normal: r = d - 2*(d·n)*n
                vector d = sample.m_Dir;
                float dn = d[0]*groundNormal[0] + d[1]*groundNormal[1] + d[2]*groundNormal[2];
                vector rdir = d - (groundNormal * (2.0 * dn));
                rdir = rdir.Normalized();

                // reflection coefficient (use ground material approx)
                float reflCoeff = RDF_RCSModel.GetMaterialReflectivity("ground");
                if (reflCoeff <= 0.0)
                    reflCoeff = 0.2; // default weak ground reflection

                float reflPt = emParams.m_TransmitPower * Math.Clamp(reflCoeff, 0.0, 1.0) * 0.001; // small fraction

                // Inject along the reflected ray for a limited distance (PoC)
                evField.InjectAlongRay(reflectOrigin, rdir, Math.Min(m_Settings.m_Range, 2000.0), reflPt, 0x1, 0.5, -1.0, 1e-5);
            }
        }

        // 6. Range gate - only for terrain; keep entity hits for accuracy.
        if (sample.m_Hit && m_RadarSettings.m_MinRange > 0 && sample.m_Entity == null)
        {
            float trueRange = (sample.m_HitPos - sample.m_Start).Length();
            if (trueRange < m_RadarSettings.m_MinRange)
                sample.m_Hit = false;
        }

        // 7. Detection: known entities (pre-classified) always pass; others use threshold.
        if (sample.m_Hit)
        {
            float thresh = m_RadarSettings.m_DetectionThreshold;
            if (sample.m_Entity != null && sample.m_EntityKind != EEntityKind.ENTITY_UNKNOWN)
                thresh = 0.0;
            sample.m_Hit = sample.IsDetectable(thresh);
        }

        // 8. MTI clutter filter (binary pass/fail on minimum velocity).
        if (m_RadarSettings.m_EnableMTI && sample.m_Hit)
        {
            float minDopplerHz = RDF_DopplerProcessor.CalculateDopplerShift(
                m_RadarSettings.m_MinTargetVelocity,
                emParams.m_CarrierFrequency);

            if (!RDF_DopplerProcessor.IsMovingTarget(sample.m_DopplerFrequency, minDopplerHz))
                sample.m_Hit = false;
        }

        // 9. MTD: skip for known static/vehicle/building so they are not dropped (accuracy first).
        if (m_RadarSettings.m_EnableMTD && sample.m_Hit)
        {
            bool knownStatic = (sample.m_EntityKind == EEntityKind.ENTITY_VEHICLE
                || sample.m_EntityKind == EEntityKind.ENTITY_BUILDING
                || sample.m_EntityKind == EEntityKind.ENTITY_STATIC);
            if (!knownStatic)
            {
                float absVel    = Math.AbsFloat(sample.m_TargetVelocity);
                float threshold = m_RadarSettings.m_MinTargetVelocity * 3.0;
                if (absVel < threshold && threshold > 0)
                {
                    float penaltyDB = 15.0 * (1.0 - absVel / threshold);
                    sample.m_SignalToNoiseRatio -= penaltyDB;
                    float thresh = m_RadarSettings.m_DetectionThreshold;
                    if (sample.m_Entity != null && sample.m_EntityKind != EEntityKind.ENTITY_UNKNOWN)
                        thresh = 0.0;
                    if (!sample.IsDetectable(thresh))
                        sample.m_Hit = false;
                }
            }
        }

        // 10. Ground clutter filter - suppress terrain-only returns (no entity).
        // When enabled, any hit with no game entity is treated as clutter and not shown as target.
        if (m_RadarSettings.m_EnableClutterFilter && sample.m_Hit && sample.m_Entity == null)
            sample.m_Hit = false;

        // 11. Waveform-mode processing (range quantization, unambiguous range, beam losses).
        if (m_RadarModeProcessor)
            m_RadarModeProcessor.ProcessSample(sample, m_RadarSettings);

        // 12. Reject self-hit only; do not force entity to true (MTI/MTD must remain effective).
        if (sample.m_Entity != null && radarEntity != null && IsHitOnRadarPlatform(sample.m_Entity, radarEntity))
            sample.m_Hit = false;
        else if (sample.m_Entity != null && sample.m_SignalToNoiseRatio < 0)
            sample.m_SignalToNoiseRatio = 0;
    }

    // Returns true if hitEntity is the radar platform or a child/part of it.
    protected bool IsHitOnRadarPlatform(IEntity hitEntity, IEntity radarEntity)
    {
        if (!hitEntity || !radarEntity)
            return false;
        if (hitEntity == radarEntity)
            return true;
        IEntity cur = hitEntity;
        int guard = 0;
        while (cur && guard < 16)
        {
            IEntity parent = cur.GetParent();
            if (!parent)
                break;
            if (parent == radarEntity)
                return true;
            cur = parent;
            guard++;
        }
        return RDF_EntityPreClassifier.GetRootEntity(hitEntity) == radarEntity;
    }

    // Approximate surface normal from the ray direction (inward face).
    protected vector GetSurfaceNormal(RDF_RadarSample sample)
    {
        return Vector(-sample.m_Dir[0], -sample.m_Dir[1], -sample.m_Dir[2]);
    }

    // Angle of incidence (degrees) between the ray and the surface normal.
    protected float CalculateIncidenceAngle(vector rayDir, vector normal)
    {
        float cosAngle = Math.AbsFloat(vector.Dot(rayDir, normal));
        cosAngle = Math.Clamp(cosAngle, 0.0, 1.0);
        return Math.Acos(cosAngle) * Math.RAD2DEG;
    }
}
