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

    // Sample the world's single global wind. Map UI adds +180 for display,
    // so the API angle is treated as velocity heading (blows towards).
    static RDF_RadarGlobalWind SampleGlobalWind()
    {
        RDF_RadarGlobalWind wind = new RDF_RadarGlobalWind();
        ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
        if (!world)
            return wind;
        TimeAndWeatherManagerEntity weather = world.GetTimeAndWeatherManager();
        if (!weather)
            return wind;
        float speed = weather.GetWindSpeed();
        float dirDeg = weather.GetWindDirection();
        wind.Set(speed, dirDeg);
        return wind;
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

        float y = pos[1];
        float vy = vel[1];
        if (y <= groundYM + 0.05)
        {
            if (!backward && vy <= 0.0)
            {
                hit.m_Valid = true;
                hit.m_TimeOffsetS = 0.0;
                hit.m_Position = pos;
                hit.m_Position[1] = groundYM;
                hit.m_Velocity = vel;
                return hit;
            }
            if (backward && vy >= 0.0)
            {
                hit.m_Valid = true;
                hit.m_TimeOffsetS = 0.0;
                hit.m_Position = pos;
                hit.m_Position[1] = groundYM;
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
            float yPrev = p[1];
            vector pPrev = p;
            vector vPrev = v;
            float tPrev = t;

            vector nextP;
            vector nextV;
            IntegrateStep(p, v, step, airDrag, wind, gravityMs2, nextP, nextV);
            p = nextP;
            v = nextV;
            t = t + step;

            bool crossedDown = false;
            if (yPrev > groundYM && p[1] <= groundYM)
                crossedDown = true;
            bool crossedUp = false;
            if (yPrev < groundYM && p[1] >= groundYM)
                crossedUp = true;
            if (!crossedDown && !crossedUp)
                continue;

            float denom = yPrev - p[1];
            float alpha = 1.0;
            if (Math.AbsFloat(denom) >= 0.000000001)
                alpha = (yPrev - groundYM) / denom;
            if (alpha < 0.0)
                alpha = 0.0;
            if (alpha > 1.0)
                alpha = 1.0;

            hit.m_Valid = true;
            hit.m_TimeOffsetS = tPrev + alpha * step;
            hit.m_Position[0] = pPrev[0] + (p[0] - pPrev[0]) * alpha;
            hit.m_Position[1] = groundYM;
            hit.m_Position[2] = pPrev[2] + (p[2] - pPrev[2]) * alpha;
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
