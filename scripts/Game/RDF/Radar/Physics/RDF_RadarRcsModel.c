// In-game target RCS defaults, aspect scaling, and Swerling fluctuation.
// Engineering approximations aligned with tools/dem/rdf_radar_channel.py.
class RDF_RadarRcsModel
{
    static float GetDefaultRcsM2(ERDF_RadarTargetType targetType)
    {
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return 0.01;
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return 10.0;
        return 5.0;
    }

    static int GetDefaultSwerlingModel(ERDF_RadarTargetType targetType)
    {
        // 0 = non-fluctuating; 1/3 scan-to-scan; 2/4 pulse-to-pulse.
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return 0;
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return 1;
        return 1;
    }

    static float GetEntityRcsM2(
        IEntity entity,
        ERDF_RadarTargetType targetType)
    {
        float fallback = GetDefaultRcsM2(targetType);
        if (!entity)
            return fallback;

        vector mins;
        vector maxs;
        entity.GetBounds(mins, maxs);
        vector size = maxs - mins;
        return EstimateRcsFromExtents(
            Math.AbsFloat(size[0]),
            Math.AbsFloat(size[1]),
            Math.AbsFloat(size[2]),
            targetType);
    }

    //------------------------------------------------------------------------------------------------
    // Optical-region estimate from AABB extents (no entity query).
    static float EstimateRcsFromExtents(
        float sizeX,
        float sizeY,
        float sizeZ,
        ERDF_RadarTargetType targetType)
    {
        float fallback = GetDefaultRcsM2(targetType);
        float width = Math.AbsFloat(sizeX);
        float height = Math.AbsFloat(sizeY);
        float length = Math.AbsFloat(sizeZ);

        float projectedA = width * height;
        float projectedB = length * height;
        float projected = Math.Max(projectedA, projectedB);
        if (projected <= 0.01)
            return fallback;

        float estimate = projected * 0.25;
        return Math.Clamp(estimate, fallback * 0.25, 1000.0);
    }

    //------------------------------------------------------------------------------------------------
    // Wrap delta degrees into [-180, 180].
    static float WrapDeltaDeg(float deltaDeg)
    {
        float rel = deltaDeg;
        while (rel > 180.0)
            rel = rel - 360.0;
        while (rel < -180.0)
            rel = rel + 360.0;
        return rel;
    }

    //------------------------------------------------------------------------------------------------
    // Horizontal only: nose/tail brighter than broadside (0.35 .. 1.0).
    // yawDeg / losAzimuthDeg are world horizontal.
    static float AspectFactor(float yawDeg, float losAzimuthDeg)
    {
        float rel = WrapDeltaDeg(losAzimuthDeg - yawDeg);
        if (rel < 0.0)
            rel = -rel;

        float rad = rel * 0.0174532925199;
        float c = Math.Cos(rad);
        return 0.35 + 0.65 * Math.AbsFloat(c);
    }

    //------------------------------------------------------------------------------------------------
    // Elevation vs body pitch: horizon brighter along body axis, look-down mixes planform.
    // Returns 0.50 .. 1.0.
    static float ElevationFactor(float pitchDeg, float losElevationDeg)
    {
        float rel = WrapDeltaDeg(losElevationDeg - pitchDeg);
        if (rel < 0.0)
            rel = -rel;
        if (rel > 90.0)
            rel = 180.0 - rel;

        float rad = rel * 0.0174532925199;
        float c = Math.Cos(rad);
        return 0.50 + 0.50 * Math.AbsFloat(c);
    }

    //------------------------------------------------------------------------------------------------
    // Combined azimuth×elevation scalar when extents are unknown.
    static float AspectFactor3D(
        float yawDeg,
        float pitchDeg,
        float losAzimuthDeg,
        float losElevationDeg)
    {
        float az = AspectFactor(yawDeg, losAzimuthDeg);
        float el = ElevationFactor(pitchDeg, losElevationDeg);
        return az * el;
    }

    //------------------------------------------------------------------------------------------------
    // Body-relative unit weights for silhouette proxy (no roll).
    // Forward uses entity yaw/pitch; sizeX≈beam, sizeY≈height, sizeZ≈length
    // (matches existing 2D nose=X×Y / side=Z×Y convention).
    static void BodyLosWeights(
        float yawDeg,
        float pitchDeg,
        float losAzimuthDeg,
        float losElevationDeg,
        out float outForward,
        out float outSide,
        out float outTop)
    {
        float relAz = WrapDeltaDeg(losAzimuthDeg - yawDeg) * 0.0174532925199;
        float relEl = WrapDeltaDeg(losElevationDeg - pitchDeg) * 0.0174532925199;
        float ce = Math.Cos(relEl);
        float se = Math.Sin(relEl);
        float ca = Math.Cos(relAz);
        float sa = Math.Sin(relAz);
        outForward = Math.AbsFloat(ce * ca);
        outSide = Math.AbsFloat(ce * sa);
        outTop = Math.AbsFloat(se);
    }

    //------------------------------------------------------------------------------------------------
    // Extent-aware projected-area proxy (azimuth + elevation).
    // Legacy 2D callers may pass losElevationDeg=0 and pitchDeg=0.
    static float AspectRcsFromExtents(
        float meanRcsM2,
        float sizeX,
        float sizeY,
        float sizeZ,
        float yawDeg,
        float losAzimuthDeg)
    {
        return AspectRcsFromExtents3D(
            meanRcsM2,
            sizeX,
            sizeY,
            sizeZ,
            yawDeg,
            0.0,
            losAzimuthDeg,
            0.0);
    }

    //------------------------------------------------------------------------------------------------
    static float AspectRcsFromExtents3D(
        float meanRcsM2,
        float sizeX,
        float sizeY,
        float sizeZ,
        float yawDeg,
        float pitchDeg,
        float losAzimuthDeg,
        float losElevationDeg)
    {
        float fallback = meanRcsM2;
        if (fallback <= 0.0)
            fallback = 1.0;

        if (sizeX <= 0.01 && sizeY <= 0.01 && sizeZ <= 0.01)
        {
            return fallback * AspectFactor3D(
                yawDeg, pitchDeg, losAzimuthDeg, losElevationDeg);
        }

        float uF;
        float uS;
        float uT;
        BodyLosWeights(
            yawDeg,
            pitchDeg,
            losAzimuthDeg,
            losElevationDeg,
            uF,
            uS,
            uT);

        float height = sizeY;
        if (height < 0.1)
            height = 0.1;
        float length = sizeZ;
        if (length < 0.1)
            length = 0.1;
        float beam = sizeX;
        if (beam < 0.1)
            beam = 0.1;

        // Nose face beam×height, side length×height, planform beam×length.
        float projected = uF * beam * height + uS * length * height + uT * beam * length;
        float estimate = projected * 0.25;
        float lo = fallback * 0.2;
        float hi = fallback * 4.0;
        if (estimate < lo)
            estimate = lo;
        if (estimate > hi)
            estimate = hi;
        return estimate;
    }

    //------------------------------------------------------------------------------------------------
    // Swerling sample. Models 1/3 constant within a scan; 2/4 redraw each call.
    static float SampleSwerling(
        float meanRcsM2,
        int model,
        int seed,
        int scanNumber,
        int scattererId)
    {
        if (meanRcsM2 <= 0.0)
            return 0.0;
        if (model <= 0)
            return meanRcsM2;

        // Deterministic u in (0,1) from ids — stable within a scan for I/III.
        float u1 = HashUnit(seed, scattererId, scanNumber, 1);
        float u2 = HashUnit(seed, scattererId, scanNumber, 2);
        if (model == 2 || model == 4)
        {
            // Extra salt so pulse-group draws differ from scan-constant draws.
            u1 = HashUnit(seed, scattererId, scanNumber * 131 + 17, 3);
            u2 = HashUnit(seed, scattererId, scanNumber * 131 + 17, 4);
        }

        bool chi4 = false;
        if (model == 3 || model == 4)
            chi4 = true;

        if (chi4)
            return -0.5 * meanRcsM2 * (Math.Log(u1) + Math.Log(u2));

        return -meanRcsM2 * Math.Log(u1);
    }

    //------------------------------------------------------------------------------------------------
    // Map integers to (eps, 1].
    protected static float HashUnit(int seed, int idA, int idB, int channel)
    {
        int x = seed;
        x = x * 374761393 + idA * 668265263;
        x = x * 2246822519 + idB * 3266489917;
        x = x * 668265263 + channel * 1013904223;
        if (x < 0)
            x = -x;
        int rem = x - (x / 1000000) * 1000000;
        float u = rem / 1000000.0;
        if (u < 0.000001)
            u = 0.000001;
        if (u > 1.0)
            u = 1.0;
        return u;
    }

    //------------------------------------------------------------------------------------------------
    // Scale rotor sidebands by main-rotor disk aspect vs LOS elevation.
    // Horizon (edge-on disk) → 1; steep look-down/up (face-on) → ~0.2.
    static float RotorDiskAspectScale(float losElevationDeg)
    {
        float el = losElevationDeg;
        if (el < 0.0)
            el = -el;
        if (el > 90.0)
            el = 180.0 - el;
        if (el < 0.0)
            el = 0.0;
        if (el > 90.0)
            el = 90.0;

        float rad = el * 0.0174532925199;
        float edgeOn = Math.Cos(rad);
        if (edgeOn < 0.0)
            edgeOn = -edgeOn;
        float scale = 0.2 + 0.8 * edgeOn;
        if (scale < 0.2)
            scale = 0.2;
        if (scale > 1.0)
            scale = 1.0;
        return scale;
    }

    //------------------------------------------------------------------------------------------------
    // Doppler spectrum lines for MTD: body radial line (+ optional rotor sidebands).
    // powers are relative RCS weights (sum need not be 1; MaxMtdSpectrumGain normalizes).
    // blade_count drives interior harmonics at tip*(h/blades); elevation scales sidebands.
    static void FillDopplerSpectrum(
        RDF_RadarTarget target,
        float wavelengthM,
        float bodyDopplerHz,
        notnull array<float> outDopplerHz,
        notnull array<float> outPowers)
    {
        outDopplerHz.Clear();
        outPowers.Clear();
        outDopplerHz.Insert(bodyDopplerHz);
        outPowers.Insert(1.0);

        if (!target)
            return;
        if (wavelengthM <= 0.0)
            return;

        // Rotor / micro-Doppler sidebands from signature (0 tip speed → none).
        float tipMs = target.m_RotorTipSpeedMs;
        float rotorFrac = target.m_RotorRcsFraction;
        int blades = target.m_BladeCount;
        if (tipMs <= 0.0)
            return;
        if (rotorFrac <= 0.0)
            return;
        if (blades < 2)
            blades = 2;
        if (blades > 8)
            blades = 8;

        float aspectScale = RotorDiskAspectScale(target.m_ElevationDeg);
        float sideScale = aspectScale;

        float bodyFrac = 1.0 - rotorFrac;
        if (bodyFrac < 0.05)
            bodyFrac = 0.05;
        outPowers.Set(0, bodyFrac);

        // Tip Doppler extremes ±2·v_tip/λ about the body line (approaching / receding).
        float tipDopplerHz = RDF_RadarClutterModel.DopplerHz(tipMs, wavelengthM);
        float tipShare = 0.55;
        float harmShare = 0.30;
        float hubShare = 0.15;
        float tipPower = rotorFrac * tipShare * 0.5 * sideScale;
        outDopplerHz.Insert(bodyDopplerHz + tipDopplerHz);
        outPowers.Insert(tipPower);
        outDopplerHz.Insert(bodyDopplerHz - tipDopplerHz);
        outPowers.Insert(tipPower);

        // Interior blade harmonics at tip*(h/N), h=1..floor(N/2)-omit tip.
        int maxHarm = blades / 2;
        if (maxHarm > 3)
            maxHarm = 3;
        int harmLines = 0;
        for (int h = 1; h <= maxHarm; h++)
        {
            if (h >= blades)
                break;
            float harmMs = tipMs * (h * 1.0 / blades);
            if (harmMs < tipMs * 0.12)
                continue;
            if (harmMs > tipMs * 0.92)
                continue;
            harmLines = harmLines + 1;
        }
        if (harmLines < 1)
            harmLines = 1;
        float harmPowerEach = rotorFrac * harmShare * sideScale / (harmLines * 2.0);
        for (int h2 = 1; h2 <= maxHarm; h2++)
        {
            if (h2 >= blades)
                break;
            float harmMs2 = tipMs * (h2 * 1.0 / blades);
            if (harmMs2 < tipMs * 0.12)
                continue;
            if (harmMs2 > tipMs * 0.92)
                continue;
            float harmFd = RDF_RadarClutterModel.DopplerHz(harmMs2, wavelengthM);
            outDopplerHz.Insert(bodyDopplerHz + harmFd);
            outPowers.Insert(harmPowerEach);
            outDopplerHz.Insert(bodyDopplerHz - harmFd);
            outPowers.Insert(harmPowerEach);
        }

        // Optional hub / blade-flash mid line (weaker, near body + hub width).
        float hubMs = target.m_HubWidthMs;
        if (hubMs > 0.0)
        {
            float hubFd = RDF_RadarClutterModel.DopplerHz(hubMs, wavelengthM);
            float hubPower = rotorFrac * hubShare * 0.5 * sideScale;
            outDopplerHz.Insert(bodyDopplerHz + hubFd);
            outPowers.Insert(hubPower);
            outDopplerHz.Insert(bodyDopplerHz - hubFd);
            outPowers.Insert(hubPower);
        }
    }
}
