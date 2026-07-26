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

    void RDF_RadarWlrFix()
    {
        m_LaunchPos = "0 0 0";
        m_ImpactPos = "0 0 0";
        m_LaunchTimeS = 0.0;
        m_ImpactTimeS = 0.0;
        m_AnchorTimeS = 0.0;
        m_LaunchValid = false;
        m_ImpactValid = false;
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

    static vector IntegrateForDuration(
        vector pos,
        vector vel,
        float durationS,
        float airDrag,
        RDF_RadarGlobalWind wind,
        float gravityMs2 = GRAVITY_M_S2,
        float dtS = DEFAULT_DT_S)
    {
        if (durationS <= 0.0)
            return pos;
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
        return p;
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
        float dtS = DEFAULT_DT_S)
    {
        RDF_RadarGroundHit hit = new RDF_RadarGroundHit();
        if (dtS < 0.0001)
            dtS = 0.0001;
        float direction = 1.0;
        if (backward)
            direction = -1.0;

        float ground0 = SampleGroundYM(pos[0], pos[2], groundYM);
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
            float groundPrev = SampleGroundYM(pPrev[0], pPrev[2], groundYM);
            float heightPrev = pPrev[1] - groundPrev;

            vector nextP;
            vector nextV;
            IntegrateStep(p, v, step, airDrag, wind, gravityMs2, nextP, nextV);
            p = nextP;
            v = nextV;
            t = t + step;

            float ground = SampleGroundYM(p[0], p[2], groundYM);
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
            float hitY = SampleGroundYM(hitX, hitZ, groundYM);

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
}
