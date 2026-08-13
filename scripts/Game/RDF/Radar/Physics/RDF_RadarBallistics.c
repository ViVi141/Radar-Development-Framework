// Ballistic integration aligned with Reforger ShellMoveComponent:
//   a = g - AirDrag * |v - w| * (v - w)
// Global wind from TimeAndWeatherManager (world-wide, not a wind field).
// Used for projectile track extrapolation and weapon-locating fixes.

class RDF_RadarGlobalWind
{
    float m_SpeedMs;
    // Velocity heading in degrees (wind blows towards), world XZ, 0 = +X.
    float m_DirectionDeg;
    float m_Vx;
    float m_Vz;

    void RDF_RadarGlobalWind()
    {
        m_SpeedMs = 0.0;
        m_DirectionDeg = 0.0;
        m_Vx = 0.0;
        m_Vz = 0.0;
    }

    void Set(float speedMs, float directionDeg)
    {
        m_SpeedMs = speedMs;
        m_DirectionDeg = directionDeg;
        float rad = directionDeg * Math.DEG2RAD;
        m_Vx = speedMs * Math.Cos(rad);
        m_Vz = speedMs * Math.Sin(rad);
    }
}

// One-shot TimeAndWeatherManager sample (global, not a spatial field).
class RDF_RadarWeatherSnapshot
{
    bool m_Valid;
    float m_WindSpeedMs;
    float m_WindDirectionDeg;
    // Engine rain / fog amounts in [0, 1].
    float m_RainIntensity;
    float m_FogAmount;

    void RDF_RadarWeatherSnapshot()
    {
        m_Valid = false;
        m_WindSpeedMs = 0.0;
        m_WindDirectionDeg = 0.0;
        m_RainIntensity = 0.0;
        m_FogAmount = 0.0;
    }

    RDF_RadarGlobalWind ToWind()
    {
        RDF_RadarGlobalWind wind = new RDF_RadarGlobalWind();
        wind.Set(m_WindSpeedMs, m_WindDirectionDeg);
        return wind;
    }
}

class RDF_RadarGroundHit
{
    float m_TimeOffsetS;
    vector m_Position;
    vector m_Velocity;
    bool m_Valid;

    void RDF_RadarGroundHit()
    {
        m_TimeOffsetS = 0.0;
        m_Position = "0 0 0";
        m_Velocity = "0 0 0";
        m_Valid = false;
    }
}

class RDF_RadarWlrFix
{
    vector m_LaunchPos;
    vector m_ImpactPos;
    float m_LaunchTimeS;
    float m_ImpactTimeS;
    float m_AnchorTimeS;
    bool m_LaunchValid;
    bool m_ImpactValid;
    // Multi-point vacuum fit diagnostics (real WLR-style gate).
    bool m_FitValid;
    float m_FitRmsM;
    int m_FitPointCount;
    float m_FitSpanS;

    void RDF_RadarWlrFix()
    {
        m_LaunchPos = "0 0 0";
        m_ImpactPos = "0 0 0";
        m_LaunchTimeS = 0.0;
        m_ImpactTimeS = 0.0;
        m_AnchorTimeS = 0.0;
        m_LaunchValid = false;
        m_ImpactValid = false;
        m_FitValid = false;
        m_FitRmsM = 0.0;
        m_FitPointCount = 0;
        m_FitSpanS = 0.0;
    }
}

// Vacuum ballistic state at an absolute anchor time (least-squares fit).
class RDF_RadarBallisticFitState
{
    float m_AnchorTimeS;
    vector m_Position;
    vector m_Velocity;
    float m_RmsM;
    int m_PointCount;
    float m_SpanS;
    bool m_Valid;

    void RDF_RadarBallisticFitState()
    {
        m_AnchorTimeS = 0.0;
        m_Position = "0 0 0";
        m_Velocity = "0 0 0";
        m_RmsM = 0.0;
        m_PointCount = 0;
        m_SpanS = 0.0;
        m_Valid = false;
    }
}

class RDF_RadarBallistics
{
    // Prefab prior: Ammo_Shell_82mm_HE_O832DU AirDrag.
    static const float AIR_DRAG_SHELL_82MM_HE = 0.000615;
    static const float GRAVITY_M_S2 = -9.81;
    static const float DEFAULT_DT_S = 0.05;
    static const float MAX_INTEGRATE_S = 90.0;

    protected static ref RDF_DemRuntimeCache s_DemCache;
    protected static bool s_UseDemGround = true;

    static void SetDemCache(RDF_DemRuntimeCache demCache)
    {
        s_DemCache = demCache;
    }

    static void SetUseDemGround(bool enable)
    {
        s_UseDemGround = enable;
    }

    static bool GetUseDemGround()
    {
        return s_UseDemGround;
    }

    static RDF_DemRuntimeCache GetOrCreateDemCache()
    {
        if (!s_DemCache)
            s_DemCache = new RDF_DemRuntimeCache();
        return s_DemCache;
    }

    // Prefer baked RDF DEM; fall back to live world surface, then flat Y.
    static float SampleGroundYM(float worldX, float worldZ, float fallbackYM)
    {
        if (!s_UseDemGround)
            return fallbackYM;

        RDF_DemRuntimeCache cache = GetOrCreateDemCache();
        if (cache)
        {
            RDF_DemRuntimeCellSample demSample;
            if (cache.TrySampleAt(worldX, worldZ, demSample))
            {
                if (demSample && demSample.m_Valid)
                    return demSample.m_TerrainY;
            }
        }

        BaseWorld world = GetGame().GetWorld();
        if (world)
            return world.GetSurfaceY(worldX, worldZ);
        return fallbackYM;
    }

    // Sample the world's single global wind. Map UI adds +180 for display,
    // so the API angle is treated as velocity heading (blows towards).
    static RDF_RadarGlobalWind SampleGlobalWind()
    {
        RDF_RadarWeatherSnapshot snap = SampleWorldWeather();
        return snap.ToWind();
    }

    //------------------------------------------------------------------------------------------------
    // Wind + rain + fog from TimeAndWeatherManager (one call per scan preferred).
    static RDF_RadarWeatherSnapshot SampleWorldWeather()
    {
        RDF_RadarWeatherSnapshot snap = new RDF_RadarWeatherSnapshot();
        ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
        if (!world)
            return snap;
        TimeAndWeatherManagerEntity weather = world.GetTimeAndWeatherManager();
        if (!weather)
            return snap;

        snap.m_Valid = true;
        snap.m_WindSpeedMs = weather.GetWindSpeed();
        snap.m_WindDirectionDeg = weather.GetWindDirection();
        snap.m_RainIntensity = Math.Clamp(weather.GetRainIntensity(), 0.0, 1.0);
        snap.m_FogAmount = Math.Clamp(weather.GetFogAmount(), 0.0, 1.0);
        return snap;
    }

    static void AccelWithDrag(
        vector velocity,
        float airDrag,
        RDF_RadarGlobalWind wind,
        float gravityMs2,
        out vector outAccel)
    {
        float wx = 0.0;
        float wz = 0.0;
        if (wind)
        {
            wx = wind.m_Vx;
            wz = wind.m_Vz;
        }
        float rx = velocity[0] - wx;
        float ry = velocity[1];
        float rz = velocity[2] - wz;
        float airspeed = Math.Sqrt(rx * rx + ry * ry + rz * rz);
        float scale = airDrag * airspeed;
        outAccel = Vector(-scale * rx, gravityMs2 - scale * ry, -scale * rz);
    }

    // RK2 step. direction encoded in the sign of stepS.
    static void IntegrateStep(
        vector pos,
        vector vel,
        float stepS,
        float airDrag,
        RDF_RadarGlobalWind wind,
        float gravityMs2,
        out vector outPos,
        out vector outVel)
    {
        vector accel;
        AccelWithDrag(vel, airDrag, wind, gravityMs2, accel);
        vector velMid = vel + accel * (0.5 * stepS);
        vector accelMid;
        AccelWithDrag(velMid, airDrag, wind, gravityMs2, accelMid);
        outPos = pos + velMid * stepS;
        outVel = vel + accelMid * stepS;
    }

    static void IntegrateForDurationEx(
        vector pos,
        vector vel,
        float durationS,
        float airDrag,
        RDF_RadarGlobalWind wind,
        out vector outPos,
        out vector outVel,
        float gravityMs2,
        float dtS)
    {
        outPos = pos;
        outVel = vel;
        if (durationS <= 0.0)
            return;
        if (dtS < 0.0001)
            dtS = 0.0001;
        float t = 0.0;
        vector p = pos;
        vector v = vel;
        while (t + 0.000000000001 < durationS)
        {
            float step = dtS;
            if (t + step > durationS)
                step = durationS - t;
            vector nextP;
            vector nextV;
            IntegrateStep(p, v, step, airDrag, wind, gravityMs2, nextP, nextV);
            p = nextP;
            v = nextV;
            t = t + step;
        }
        outPos = p;
        outVel = v;
    }

    static vector IntegrateForDuration(
        vector pos,
        vector vel,
        float durationS,
        float airDrag,
        RDF_RadarGlobalWind wind,
        float gravityMs2 = GRAVITY_M_S2,
        float dtS = DEFAULT_DT_S)
    {
        vector outP;
        vector outV;
        IntegrateForDurationEx(
            pos, vel, durationS, airDrag, wind, outP, outV, gravityMs2, dtS);
        return outP;
    }

    protected static float SampleIntersectionGroundYM(
        float worldX,
        float worldZ,
        float fallbackYM,
        bool useWorldSurface)
    {
        if (useWorldSurface)
        {
            BaseWorld world = GetGame().GetWorld();
            if (world)
                return world.GetSurfaceY(worldX, worldZ);
            return fallbackYM;
        }
        return SampleGroundYM(worldX, worldZ, fallbackYM);
    }

    // Forward hit against live GetSurfaceY (not DEM / not a flat launch plane).
    static RDF_RadarGroundHit FindWorldSurfaceIntersection(
        vector pos,
        vector vel,
        float airDrag,
        RDF_RadarGlobalWind wind,
        float gravityMs2 = GRAVITY_M_S2)
    {
        float groundYM = pos[1];
        BaseWorld world = GetGame().GetWorld();
        if (world)
            groundYM = world.GetSurfaceY(pos[0], pos[2]);
        return FindGroundIntersection(
            pos,
            vel,
            groundYM,
            airDrag,
            wind,
            false,
            gravityMs2,
            MAX_INTEGRATE_S,
            DEFAULT_DT_S,
            true);
    }

    static RDF_RadarGroundHit FindGroundIntersection(
        vector pos,
        vector vel,
        float groundYM,
        float airDrag,
        RDF_RadarGlobalWind wind,
        bool backward,
        float gravityMs2 = GRAVITY_M_S2,
        float maxTimeS = MAX_INTEGRATE_S,
        float dtS = DEFAULT_DT_S,
        bool useWorldSurface = false)
    {
        RDF_RadarGroundHit hit = new RDF_RadarGroundHit();
        if (dtS < 0.0001)
            dtS = 0.0001;
        float direction = 1.0;
        if (backward)
            direction = -1.0;

        float ground0 = SampleIntersectionGroundYM(pos[0], pos[2], groundYM, useWorldSurface);
        float height0 = pos[1] - ground0;
        float vy = vel[1];
        if (height0 <= 0.05)
        {
            if (!backward && vy <= 0.0)
            {
                hit.m_Valid = true;
                hit.m_TimeOffsetS = 0.0;
                hit.m_Position = pos;
                hit.m_Position[1] = ground0;
                hit.m_Velocity = vel;
                return hit;
            }
            if (backward && vy >= 0.0)
            {
                hit.m_Valid = true;
                hit.m_TimeOffsetS = 0.0;
                hit.m_Position = pos;
                hit.m_Position[1] = ground0;
                hit.m_Velocity = vel;
                return hit;
            }
        }

        vector p = pos;
        vector v = vel;
        float t = 0.0;
        while (Math.AbsFloat(t) + 0.000000000001 < maxTimeS)
        {
            float step = direction * dtS;
            vector pPrev = p;
            vector vPrev = v;
            float tPrev = t;
            float groundPrev = SampleIntersectionGroundYM(
                pPrev[0], pPrev[2], groundYM, useWorldSurface);
            float heightPrev = pPrev[1] - groundPrev;

            vector nextP;
            vector nextV;
            IntegrateStep(p, v, step, airDrag, wind, gravityMs2, nextP, nextV);
            p = nextP;
            v = nextV;
            t = t + step;

            float ground = SampleIntersectionGroundYM(
                p[0], p[2], groundYM, useWorldSurface);
            float height = p[1] - ground;

            bool crossedDown = false;
            if (heightPrev > 0.0 && height <= 0.0)
                crossedDown = true;
            bool crossedUp = false;
            if (heightPrev < 0.0 && height >= 0.0)
                crossedUp = true;
            if (!crossedDown && !crossedUp)
                continue;

            float denom = heightPrev - height;
            float alpha = 1.0;
            if (Math.AbsFloat(denom) >= 0.000000001)
                alpha = heightPrev / denom;
            if (alpha < 0.0)
                alpha = 0.0;
            if (alpha > 1.0)
                alpha = 1.0;

            float hitX = pPrev[0] + (p[0] - pPrev[0]) * alpha;
            float hitZ = pPrev[2] + (p[2] - pPrev[2]) * alpha;
            float hitY = SampleIntersectionGroundYM(hitX, hitZ, groundYM, useWorldSurface);

            hit.m_Valid = true;
            hit.m_TimeOffsetS = tPrev + alpha * step;
            hit.m_Position[0] = hitX;
            hit.m_Position[1] = hitY;
            hit.m_Position[2] = hitZ;
            hit.m_Velocity[0] = vPrev[0] + (v[0] - vPrev[0]) * alpha;
            hit.m_Velocity[1] = vPrev[1] + (v[1] - vPrev[1]) * alpha;
            hit.m_Velocity[2] = vPrev[2] + (v[2] - vPrev[2]) * alpha;
            return hit;
        }
        return hit;
    }

    static RDF_RadarWlrFix SolveLaunchAndImpact(
        vector pos,
        vector vel,
        float groundYM,
        float anchorTimeS,
        float airDrag,
        RDF_RadarGlobalWind wind,
        float gravityMs2 = GRAVITY_M_S2)
    {
        RDF_RadarWlrFix fix = new RDF_RadarWlrFix();
        fix.m_AnchorTimeS = anchorTimeS;

        RDF_RadarGroundHit launch = FindGroundIntersection(
            pos, vel, groundYM, airDrag, wind, true, gravityMs2);
        if (launch && launch.m_Valid)
        {
            fix.m_LaunchValid = true;
            fix.m_LaunchPos = launch.m_Position;
            fix.m_LaunchTimeS = anchorTimeS + launch.m_TimeOffsetS;
        }

        RDF_RadarGroundHit impact = FindGroundIntersection(
            pos, vel, groundYM, airDrag, wind, false, gravityMs2);
        if (impact && impact.m_Valid)
        {
            fix.m_ImpactValid = true;
            fix.m_ImpactPos = impact.m_Position;
            fix.m_ImpactTimeS = anchorTimeS + impact.m_TimeOffsetS;
        }
        return fix;
    }

    // 2x2 normal equations for y = b0 + b1 * t.
    protected static bool SolveLinearTrend(
        int n,
        float sumT,
        float sumTT,
        float sumY,
        float sumTY,
        out float outB0,
        out float outB1)
    {
        outB0 = 0.0;
        outB1 = 0.0;
        if (n < 2)
            return false;
        float det = (n * sumTT) - (sumT * sumT);
        if (Math.AbsFloat(det) < 0.0000001)
            return false;
        outB0 = ((sumY * sumTT) - (sumT * sumTY)) / det;
        outB1 = ((n * sumTY) - (sumT * sumY)) / det;
        return true;
    }

    // Real WLR-style multi-point vacuum fit on Cartesian history.
    // Horizontal: constant velocity. Vertical: fixed gravity (g = gravityMs2).
    // Anchor state is evaluated at the last sample time in the window.
    static RDF_RadarBallisticFitState FitVacuumFromHistory(
        array<vector> positions,
        array<float> times,
        float gravityMs2 = GRAVITY_M_S2,
        int minPoints = 5,
        float minSpanS = 1.0,
        float maxFitRmsM = 80.0,
        int windowPoints = 20)
    {
        RDF_RadarBallisticFitState state = new RDF_RadarBallisticFitState();
        if (!positions || !times)
            return state;
        int count = positions.Count();
        if (times.Count() < count)
            count = times.Count();
        if (count < minPoints)
            return state;
        if (windowPoints < minPoints)
            windowPoints = minPoints;

        int end = count - 1;
        int start = end - windowPoints + 1;
        if (start < 0)
            start = 0;
        int n = end - start + 1;
        if (n < minPoints)
            return state;

        float tAnchor = times.Get(end);
        float span = tAnchor - times.Get(start);
        if (span < minSpanS)
            return state;

        float sumT = 0.0;
        float sumTT = 0.0;
        float sumX = 0.0;
        float sumTX = 0.0;
        float sumZ = 0.0;
        float sumTZ = 0.0;
        float sumYLin = 0.0;
        float sumTYLin = 0.0;

        for (int i = start; i <= end; i++)
        {
            float t = times.Get(i) - tAnchor;
            vector p = positions.Get(i);
            float yLin = p[1] - (0.5 * gravityMs2 * t * t);
            sumT = sumT + t;
            sumTT = sumTT + (t * t);
            sumX = sumX + p[0];
            sumTX = sumTX + (t * p[0]);
            sumZ = sumZ + p[2];
            sumTZ = sumTZ + (t * p[2]);
            sumYLin = sumYLin + yLin;
            sumTYLin = sumTYLin + (t * yLin);
        }

        float x0;
        float vx;
        float z0;
        float vz;
        float y0;
        float vy;
        if (!SolveLinearTrend(n, sumT, sumTT, sumX, sumTX, x0, vx))
            return state;
        if (!SolveLinearTrend(n, sumT, sumTT, sumZ, sumTZ, z0, vz))
            return state;
        if (!SolveLinearTrend(n, sumT, sumTT, sumYLin, sumTYLin, y0, vy))
            return state;

        float sumSq = 0.0;
        for (int j = start; j <= end; j++)
        {
            float t = times.Get(j) - tAnchor;
            vector p = positions.Get(j);
            float hx = x0 + vx * t;
            float hz = z0 + vz * t;
            float hy = y0 + vy * t + (0.5 * gravityMs2 * t * t);
            float dx = p[0] - hx;
            float dy = p[1] - hy;
            float dz = p[2] - hz;
            sumSq = sumSq + (dx * dx) + (dy * dy) + (dz * dz);
        }
        float rms = Math.Sqrt(sumSq / n);
        if (rms > maxFitRmsM)
            return state;

        state.m_Valid = true;
        state.m_AnchorTimeS = tAnchor;
        state.m_Position = Vector(x0, y0, z0);
        state.m_Velocity = Vector(vx, vy, vz);
        state.m_RmsM = rms;
        state.m_PointCount = n;
        state.m_SpanS = span;
        return state;
    }

    // Fit history → AirDrag integrate to launch/impact (DEM/surface aware).
    static RDF_RadarWlrFix SolveLaunchAndImpactFromHistory(
        array<vector> positions,
        array<float> times,
        float groundYM,
        float airDrag,
        RDF_RadarGlobalWind wind,
        float gravityMs2 = GRAVITY_M_S2,
        int minPoints = 5,
        float minSpanS = 1.0,
        float maxFitRmsM = 80.0,
        int windowPoints = 20)
    {
        RDF_RadarBallisticFitState fit = FitVacuumFromHistory(
            positions,
            times,
            gravityMs2,
            minPoints,
            minSpanS,
            maxFitRmsM,
            windowPoints);
        RDF_RadarWlrFix empty = new RDF_RadarWlrFix();
        if (!fit || !fit.m_Valid)
            return empty;

        RDF_RadarWlrFix fix = SolveLaunchAndImpact(
            fit.m_Position,
            fit.m_Velocity,
            groundYM,
            fit.m_AnchorTimeS,
            airDrag,
            wind,
            gravityMs2);
        fix.m_FitValid = true;
        fix.m_FitRmsM = fit.m_RmsM;
        fix.m_FitPointCount = fit.m_PointCount;
        fix.m_FitSpanS = fit.m_SpanS;
        return fix;
    }
}
