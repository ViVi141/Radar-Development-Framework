// Active-radar missile (ARH) style script guidance for demos.
// Official BaseMissileGuidance is unused; Launch(lockedTarget) alone does not home.
// Phases: boost thrust -> midcourse datalink/PN -> terminal seeker PN.
enum ERDF_RadarRocketPhase
{
    RDF_ROCKET_BOOST = 0,
    RDF_ROCKET_MIDCOURSE = 1,
    RDF_ROCKET_TERMINAL = 2,
    RDF_ROCKET_COAST = 3
}

//------------------------------------------------------------------------------------------------
class RDF_RadarRocketGuidanceState
{
    float m_LaunchWallS;
    float m_AgeS;
    ERDF_RadarRocketPhase m_Phase;

    float m_BoostDurationS;
    float m_GoActiveRangeM;
    float m_NavGainMid;
    float m_NavGainTerm;
    float m_MaxLateralAccelMs2;
    float m_BoostAccelMs2;
    float m_MaxSpeedMs;
    float m_MinSpeedMs;
    float m_LoftHeightM;
    float m_SeekerHalfConeDeg;
    float m_SeekerGraceS;
    float m_TerminalEnterAgeS;
    bool m_SeekerHasLock;
    bool m_Begun;

    //------------------------------------------------------------------------------------------------
    void InitDefaults()
    {
        m_LaunchWallS = 0.0;
        m_AgeS = 0.0;
        m_Phase = ERDF_RadarRocketPhase.RDF_ROCKET_BOOST;
        m_BoostDurationS = 1.8;
        m_GoActiveRangeM = 320.0;
        m_NavGainMid = 4.0;
        m_NavGainTerm = 5.0;
        m_MaxLateralAccelMs2 = 180.0;
        m_BoostAccelMs2 = 240.0;
        m_MaxSpeedMs = 480.0;
        m_MinSpeedMs = 60.0;
        m_LoftHeightM = 60.0;
        m_SeekerHalfConeDeg = 55.0;
        m_SeekerGraceS = 0.9;
        m_TerminalEnterAgeS = -1.0;
        m_SeekerHasLock = false;
        m_Begun = false;
    }

    //------------------------------------------------------------------------------------------------
    void Begin(float launchWallS)
    {
        InitDefaults();
        m_LaunchWallS = launchWallS;
        m_Begun = true;
        m_Phase = ERDF_RadarRocketPhase.RDF_ROCKET_BOOST;
    }

    //------------------------------------------------------------------------------------------------
    string GetPhaseName()
    {
        if (m_Phase == ERDF_RadarRocketPhase.RDF_ROCKET_BOOST)
            return "BOOST";
        if (m_Phase == ERDF_RadarRocketPhase.RDF_ROCKET_MIDCOURSE)
            return "MID";
        if (m_Phase == ERDF_RadarRocketPhase.RDF_ROCKET_TERMINAL)
            return "TERM";
        return "COAST";
    }
}

//------------------------------------------------------------------------------------------------
class RDF_RadarRocketGuidance
{
    //------------------------------------------------------------------------------------------------
    // One tick of ARH guidance. aimVel may be Zero if unknown (PN still works via LOS rate).
    static bool Update(
        IEntity rocket,
        vector aimPos,
        vector aimVel,
        float dt,
        float nowS,
        RDF_RadarRocketGuidanceState state)
    {
        if (!rocket || !state || !state.m_Begun)
            return false;
        if (dt <= 0.0)
            dt = 0.05;

        MissileMoveComponent missile = MissileMoveComponent.Cast(
            rocket.FindComponent(MissileMoveComponent));
        if (!missile)
            return false;

        vector pos = rocket.GetOrigin();
        vector vel = missile.GetVelocity();
        float speed = vel.Length();
        if (speed < 0.1)
            speed = state.m_MinSpeedMs;

        vector velDir = vel;
        if (speed > 0.1)
            velDir = vel * (1.0 / speed);
        else
            velDir = Vector(0.0, 0.0, 1.0);

        state.m_AgeS = nowS - state.m_LaunchWallS;
        if (state.m_AgeS < 0.0)
            state.m_AgeS = 0.0;

        vector toAim = aimPos - pos;
        float rangeM = toAim.Length();
        UpdatePhase(state, rangeM);

        vector steerAim = aimPos;
        float navGain = state.m_NavGainMid;
        float maxLatA = state.m_MaxLateralAccelMs2;
        float boostAlongA = 0.0;
        bool applyPn = true;

        if (state.m_Phase == ERDF_RadarRocketPhase.RDF_ROCKET_BOOST)
        {
            boostAlongA = state.m_BoostAccelMs2;
            steerAim = BuildLoftAim(pos, aimPos, aimVel, speed, state.m_LoftHeightM, 0.45);
            if (state.m_LoftHeightM <= 0.01)
            {
                // Intercept / no-loft: full midcourse gain during short boost.
                navGain = state.m_NavGainMid;
                maxLatA = state.m_MaxLateralAccelMs2 * 0.9;
            }
            else
            {
                navGain = state.m_NavGainMid * 0.6;
                maxLatA = state.m_MaxLateralAccelMs2 * 0.65;
            }
        }
        else if (state.m_Phase == ERDF_RadarRocketPhase.RDF_ROCKET_MIDCOURSE)
        {
            steerAim = BuildLoftAim(pos, aimPos, aimVel, speed, state.m_LoftHeightM, 0.25);
            navGain = state.m_NavGainMid;
            maxLatA = state.m_MaxLateralAccelMs2 * 0.9;
            boostAlongA = 0.0;
        }
        else if (state.m_Phase == ERDF_RadarRocketPhase.RDF_ROCKET_TERMINAL)
        {
            // Last ~100m: pure pursuit on body — lead overshoots on closing rockets.
            if (rangeM < 100.0)
                steerAim = aimPos;
            else
                steerAim = PredictIntercept(pos, aimPos, aimVel, speed);
            navGain = state.m_NavGainTerm;
            maxLatA = state.m_MaxLateralAccelMs2;
            boostAlongA = 0.0;

            vector los = steerAim - pos;
            float losLen = los.Length();
            if (losLen < 0.5)
                return true;
            vector losDir = los * (1.0 / losLen);
            float cosCone = Math.Cos(state.m_SeekerHalfConeDeg * Math.DEG2RAD);
            float align = vector.Dot(velDir, losDir);
            float termAge = state.m_AgeS - state.m_TerminalEnterAgeS;
            if (align < cosCone)
            {
                state.m_SeekerHasLock = false;
                // Grace: keep PN while seeker settles after go-active.
                if (termAge >= state.m_SeekerGraceS)
                {
                    state.m_Phase = ERDF_RadarRocketPhase.RDF_ROCKET_COAST;
                    applyPn = false;
                }
            }
            else
            {
                state.m_SeekerHasLock = true;
            }
        }
        else
        {
            // Coast: try seeker reacquire if target re-enters cone.
            vector losCoast = aimPos - pos;
            float losCoastLen = losCoast.Length();
            if (losCoastLen > 1.0 && rangeM <= state.m_GoActiveRangeM * 1.25)
            {
                vector losDirCoast = losCoast * (1.0 / losCoastLen);
                float cosConeCoast = Math.Cos(state.m_SeekerHalfConeDeg * Math.DEG2RAD);
                if (vector.Dot(velDir, losDirCoast) >= cosConeCoast)
                {
                    state.m_Phase = ERDF_RadarRocketPhase.RDF_ROCKET_TERMINAL;
                    state.m_SeekerHasLock = true;
                    steerAim = PredictIntercept(pos, aimPos, aimVel, speed);
                    navGain = state.m_NavGainTerm;
                    maxLatA = state.m_MaxLateralAccelMs2;
                    applyPn = true;
                }
                else
                {
                    applyPn = false;
                    boostAlongA = 0.0;
                }
            }
            else
            {
                applyPn = false;
                boostAlongA = 0.0;
            }
        }

        vector aCmd = Vector(0.0, 0.0, 0.0);
        if (applyPn)
        {
            aCmd = ComputePnAccel(pos, vel, steerAim, aimVel, navGain);
            aCmd = LimitLateralAccel(aCmd, velDir, maxLatA);
        }

        if (boostAlongA > 0.0)
            aCmd = aCmd + velDir * boostAlongA;

        vector newVel = vel + aCmd * dt;
        float newSpeed = newVel.Length();
        if (newSpeed < state.m_MinSpeedMs && state.m_Phase != ERDF_RadarRocketPhase.RDF_ROCKET_COAST)
        {
            if (newSpeed > 0.1)
                newVel = newVel * (state.m_MinSpeedMs / newSpeed);
            else
                newVel = velDir * state.m_MinSpeedMs;
            newSpeed = state.m_MinSpeedMs;
        }
        if (newSpeed > state.m_MaxSpeedMs)
        {
            newVel = newVel * (state.m_MaxSpeedMs / newSpeed);
            newSpeed = state.m_MaxSpeedMs;
        }

        missile.SetVelocity(newVel);

        if (newSpeed > 0.5)
        {
            vector flyDir = newVel * (1.0 / newSpeed);
            vector basis[4];
            Math3D.DirectionAndUpMatrix(flyDir, vector.Up, basis);
            basis[3] = pos;
            rocket.SetTransform(basis);
        }
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected static void UpdatePhase(RDF_RadarRocketGuidanceState state, float rangeM)
    {
        if (state.m_AgeS < state.m_BoostDurationS)
        {
            state.m_Phase = ERDF_RadarRocketPhase.RDF_ROCKET_BOOST;
            return;
        }

        // Allow COAST -> TERMINAL reacquire via Update() cone check; do not force overwrite.
        if (state.m_Phase == ERDF_RadarRocketPhase.RDF_ROCKET_COAST)
            return;

        if (rangeM <= state.m_GoActiveRangeM)
        {
            if (state.m_Phase != ERDF_RadarRocketPhase.RDF_ROCKET_TERMINAL)
                state.m_TerminalEnterAgeS = state.m_AgeS;
            state.m_Phase = ERDF_RadarRocketPhase.RDF_ROCKET_TERMINAL;
            return;
        }

        state.m_Phase = ERDF_RadarRocketPhase.RDF_ROCKET_MIDCOURSE;
    }

    //------------------------------------------------------------------------------------------------
    // True PN: a = N * Vc * (omega x losHat), omega = (r x vRel) / |r|^2.
    protected static vector ComputePnAccel(
        vector pos,
        vector vel,
        vector aimPos,
        vector aimVel,
        float navGain)
    {
        vector r = aimPos - pos;
        float r2 = r.LengthSq();
        if (r2 < 1.0)
            return Vector(0.0, 0.0, 0.0);

        float rLen = Math.Sqrt(r2);
        vector losHat = r * (1.0 / rLen);
        vector vRel = aimVel - vel;
        float vc = -vector.Dot(vRel, losHat);
        if (vc < 1.0)
            vc = 1.0;

        vector omega = SCR_Math3D.Cross(r, vRel, false) * (1.0 / r2);
        vector aCmd = SCR_Math3D.Cross(omega, losHat, false) * (navGain * vc);
        return aCmd;
    }

    //------------------------------------------------------------------------------------------------
    protected static vector LimitLateralAccel(vector aCmd, vector velDir, float maxLatA)
    {
        float along = vector.Dot(aCmd, velDir);
        vector aLat = aCmd - velDir * along;
        float mag = aLat.Length();
        if (mag <= 0.001)
            return Vector(0.0, 0.0, 0.0);
        if (mag > maxLatA)
            aLat = aLat * (maxLatA / mag);
        return aLat;
    }

    //------------------------------------------------------------------------------------------------
    // Closing-speed lead: tGo = range / (Vm - Vt·los). Stable for inbound intercepts.
    protected static vector PredictIntercept(
        vector pos,
        vector aimPos,
        vector aimVel,
        float missileSpeed)
    {
        vector r = aimPos - pos;
        float rangeM = r.Length();
        if (rangeM < 1.0)
            return aimPos;

        float vm = missileSpeed;
        if (vm < 40.0)
            vm = 40.0;

        vector losHat = r * (1.0 / rangeM);
        float closing = vm - vector.Dot(aimVel, losHat);
        if (closing < 8.0)
            closing = 8.0;

        float tGo = rangeM / closing;
        if (tGo < 0.05)
            tGo = 0.05;
        if (tGo > 20.0)
            tGo = 20.0;
        return aimPos + aimVel * tGo;
    }

    //------------------------------------------------------------------------------------------------
    // Midcourse loft: raise aim by loftFrac * loftHeight scaled with remaining range.
    // loftHeightM ~ 0 skips loft and uses pure collision-course lead (intercept demos).
    protected static vector BuildLoftAim(
        vector pos,
        vector aimPos,
        vector aimVel,
        float missileSpeed,
        float loftHeightM,
        float loftFrac)
    {
        vector pred = PredictIntercept(pos, aimPos, aimVel, missileSpeed);
        if (loftHeightM <= 0.01)
            return pred;

        float rangeM = vector.Distance(pos, aimPos);
        float loftScale = rangeM / 900.0;
        if (loftScale > 1.0)
            loftScale = 1.0;
        if (loftScale < 0.0)
            loftScale = 0.0;
        pred[1] = pred[1] + loftHeightM * loftFrac * loftScale;
        return pred;
    }
}
