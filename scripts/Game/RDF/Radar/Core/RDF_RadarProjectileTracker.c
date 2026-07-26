// Holds trajectory and alpha-beta state for one measured radar track.
// Association is plot-driven (range / azimuth gates), not entity identity.
// Projectile tracks extrapolate with Reforger-style AirDrag + global wind and
// can solve weapon-locating launch / impact points.
class RDF_RadarTrack
{
    int m_TrackId;
    IEntity m_Entity;
    ref array<vector> m_Positions = new array<vector>();
    ref array<vector> m_Velocities = new array<vector>();
    ref array<float> m_Times = new array<float>();
    static const int MAX_POINTS = 32;
    vector m_FilteredPosition;
    vector m_FilteredVelocity;
    float m_FilteredRangeM;
    float m_FilteredAzimuthDeg;
    float m_FilteredElevationDeg;
    float m_FilteredRangeRateMs;
    float m_LastSnrDb;
    int m_HitCount;
    int m_MissCount;
    int m_LastScanNumber = -1;
    bool m_Confirmed;
    float m_LastUpdateTime = -1.0;
    ERDF_RadarTargetType m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
    // Ballistic prior (ShellMoveComponent.AirDrag). <=0 disables drag path.
    float m_AirDrag = RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE;
    bool m_UseBallisticPrediction = true;
    float m_GroundYM = 0.0;
    bool m_GroundYValid;
    ref RDF_RadarWlrFix m_LastWlrFix;

    void Push(vector pos, vector vel, float time)
    {
        m_Positions.Insert(pos);
        m_Velocities.Insert(vel);
        m_Times.Insert(time);
        while (m_Positions.Count() > MAX_POINTS)
        {
            m_Positions.Remove(0);
            m_Velocities.Remove(0);
            m_Times.Remove(0);
        }
    }

    float GetLastTime()
    {
        if (m_Times.Count() == 0)
            return m_LastUpdateTime;
        return m_Times.Get(m_Times.Count() - 1);
    }

    bool IsProjectileTrack()
    {
        if (m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return true;
        return false;
    }

    // Extrapolate Cartesian position at an absolute world time (seconds).
    // Projectiles: gravity + AirDrag + global wind. Others: constant velocity.
    vector PredictAt(float worldTimeSec)
    {
        float lastTime = GetLastTime();
        if (lastTime < 0.0)
            return m_FilteredPosition;
        float dt = worldTimeSec - lastTime;
        if (dt < 0.0)
            dt = 0.0;

        if (!m_UseBallisticPrediction || !IsProjectileTrack() || m_AirDrag <= 0.0)
            return m_FilteredPosition + m_FilteredVelocity * dt;

        RDF_RadarGlobalWind wind = RDF_RadarBallistics.SampleGlobalWind();
        return RDF_RadarBallistics.IntegrateForDuration(
            m_FilteredPosition,
            m_FilteredVelocity,
            dt,
            m_AirDrag,
            wind,
            RDF_RadarBallistics.GRAVITY_M_S2);
    }

    // Extrapolate polar range / azimuth relative to a radar origin.
    void PredictPolarAt(
        float worldTimeSec,
        vector radarOrigin,
        out float outRangeM,
        out float outAzimuthDeg,
        out float outElevationDeg,
        out float outRangeRateMs)
    {
        vector pred = PredictAt(worldTimeSec);
        vector delta = pred - radarOrigin;
        outRangeM = delta.Length();
        if (outRangeM < 0.001)
        {
            outRangeM = m_FilteredRangeM;
            outAzimuthDeg = m_FilteredAzimuthDeg;
            outElevationDeg = m_FilteredElevationDeg;
            outRangeRateMs = m_FilteredRangeRateMs;
            return;
        }
        outAzimuthDeg = Math.Atan2(delta[2], delta[0]) * 57.2957795;
        float horizontal = Math.Sqrt(delta[0] * delta[0] + delta[2] * delta[2]);
        outElevationDeg = Math.Atan2(delta[1], Math.Max(0.001, horizontal)) * 57.2957795;
        outRangeRateMs = m_FilteredRangeRateMs;
    }

    // Back/forward-project to flat ground for weapon locating.
    // Prefer the apex sample (max Y) when history is available.
    RDF_RadarWlrFix SolveWeaponLocate(float groundYM)
    {
        RDF_RadarWlrFix empty = new RDF_RadarWlrFix();
        if (!IsProjectileTrack())
            return empty;
        if (m_HitCount < 3)
            return empty;
        if (m_AirDrag <= 0.0)
            return empty;

        vector anchorPos = m_FilteredPosition;
        vector anchorVel = m_FilteredVelocity;
        float anchorTime = GetLastTime();
        if (anchorTime < 0.0)
            return empty;

        if (m_Positions && m_Positions.Count() > 0)
        {
            int apex = 0;
            float bestY = m_Positions.Get(0)[1];
            for (int i = 1; i < m_Positions.Count(); i++)
            {
                float y = m_Positions.Get(i)[1];
                if (y > bestY)
                {
                    bestY = y;
                    apex = i;
                }
            }
            anchorPos = m_Positions.Get(apex);
            if (m_Velocities && m_Velocities.Count() > apex)
                anchorVel = m_Velocities.Get(apex);
            if (m_Times && m_Times.Count() > apex)
                anchorTime = m_Times.Get(apex);
        }

        RDF_RadarGlobalWind wind = RDF_RadarBallistics.SampleGlobalWind();
        RDF_RadarWlrFix fix = RDF_RadarBallistics.SolveLaunchAndImpact(
            anchorPos,
            anchorVel,
            groundYM,
            anchorTime,
            m_AirDrag,
            wind,
            RDF_RadarBallistics.GRAVITY_M_S2);
        m_LastWlrFix = fix;
        m_GroundYM = groundYM;
        m_GroundYValid = true;
        return fix;
    }

    void FilterUpdate(
        RDF_RadarTarget target,
        float alpha,
        float beta,
        int confirmHits)
    {
        if (!target)
            return;

        m_Type = target.m_Type;
        float lastTime = GetLastTime();
        if (lastTime < 0.0)
        {
            m_FilteredPosition = target.m_Position;
            m_FilteredVelocity = target.m_Velocity;
            m_FilteredRangeM = target.m_Distance;
            m_FilteredAzimuthDeg = target.m_AzimuthDeg;
            m_FilteredElevationDeg = target.m_ElevationDeg;
            m_FilteredRangeRateMs = target.m_RadialSpeedMs;
        }
        else
        {
            float dt = Math.Max(0.001, target.m_Time - lastTime);
            float predRange = m_FilteredRangeM + m_FilteredRangeRateMs * dt;
            float residualRange = target.m_Distance - predRange;
            m_FilteredRangeM = predRange + residualRange * alpha;
            m_FilteredRangeRateMs = m_FilteredRangeRateMs + residualRange * (beta / dt);

            float azErr = NormalizeAngleDeg(target.m_AzimuthDeg - m_FilteredAzimuthDeg);
            m_FilteredAzimuthDeg = m_FilteredAzimuthDeg + azErr * alpha;
            float elErr = target.m_ElevationDeg - m_FilteredElevationDeg;
            m_FilteredElevationDeg = m_FilteredElevationDeg + elErr * alpha;

            vector prediction = m_FilteredPosition + m_FilteredVelocity * dt;
            vector residual = target.m_Position - prediction;
            m_FilteredPosition = prediction + residual * alpha;
            m_FilteredVelocity = m_FilteredVelocity + residual * (beta / dt);
        }

        m_LastSnrDb = target.m_SnrDb;
        m_HitCount = m_HitCount + 1;
        m_MissCount = 0;
        m_LastScanNumber = target.m_ScanNumber;
        m_LastUpdateTime = target.m_Time;
        if (m_HitCount >= confirmHits)
            m_Confirmed = true;
        else
            m_Confirmed = false;
        if (target.m_Entity)
            m_Entity = target.m_Entity;
        Push(m_FilteredPosition, m_FilteredVelocity, target.m_Time);
    }

    static float NormalizeAngleDeg(float deg)
    {
        while (deg > 180.0)
            deg = deg - 360.0;
        while (deg < -180.0)
            deg = deg + 360.0;
        return deg;
    }
}

// Multi-frame plot-driven radar tracker (nearest-neighbor + alpha-beta).
// Legacy class name remains API-compatible.
class RDF_RadarProjectileTracker
{
    protected ref array<ref RDF_RadarTrack> m_Tracks = new array<ref RDF_RadarTrack>();
    protected float m_PruneAgeSec = 30.0;
    protected float m_Alpha = 0.5;
    protected float m_Beta = 0.2;
    protected float m_GateRangeM = 400.0;
    protected float m_GateAzimuthDeg = 4.0;
    protected int m_ConfirmHits = 2;
    protected int m_MaxMisses = 3;
    protected int m_NextTrackId = 1;
    protected vector m_LastRadarOrigin = "0 0 0";
    protected bool m_EnableBallisticPrediction = true;
    protected float m_ShellAirDrag = RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE;
    protected bool m_EnableWeaponLocate = true;
    protected int m_WeaponLocateMinHits = 3;

    void ConfigureFromSettings(RDF_RadarSettings settings)
    {
        if (!settings)
            return;
        m_GateRangeM = settings.m_TrackGateRangeM;
        m_GateAzimuthDeg = settings.m_TrackGateAzimuthDeg;
        m_ConfirmHits = settings.m_TrackConfirmHits;
        m_MaxMisses = settings.m_TrackMaxMisses;
        m_EnableBallisticPrediction = settings.m_EnableBallisticPrediction;
        m_ShellAirDrag = settings.m_ShellAirDrag;
        m_EnableWeaponLocate = settings.m_EnableWeaponLocate;
        m_WeaponLocateMinHits = settings.m_WeaponLocateMinHits;
    }

    void SetFilterGains(float alpha, float beta)
    {
        m_Alpha = alpha;
        m_Beta = beta;
    }

    void ApplyBallisticConfig(RDF_RadarTrack track)
    {
        if (!track)
            return;
        track.m_UseBallisticPrediction = m_EnableBallisticPrediction;
        track.m_AirDrag = m_ShellAirDrag;
    }

    // Recompute launch/impact for confirmed projectile tracks using radar-site
    // altitude as the flat ground plane (good enough until DEM terrain Y).
    void RefreshWeaponLocates(float groundYM)
    {
        if (!m_EnableWeaponLocate)
            return;
        for (int i = 0; i < m_Tracks.Count(); i++)
        {
            RDF_RadarTrack track = m_Tracks.Get(i);
            if (!track)
                continue;
            if (!track.m_Confirmed)
                continue;
            if (!track.IsProjectileTrack())
                continue;
            if (track.m_HitCount < m_WeaponLocateMinHits)
                continue;
            ApplyBallisticConfig(track);
            track.SolveWeaponLocate(groundYM);
        }
    }

    void Update(array<ref RDF_RadarTarget> targets, float worldTimeSec)
    {
        UpdateWithOrigin(targets, worldTimeSec, "0 0 0");
    }

    void UpdateWithOrigin(
        array<ref RDF_RadarTarget> targets,
        float worldTimeSec,
        vector radarOrigin)
    {
        m_LastRadarOrigin = radarOrigin;
        if (!targets)
            return;

        array<ref RDF_RadarTarget> plots = new array<ref RDF_RadarTarget>();
        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t || !t.m_Detected)
                continue;
            plots.Insert(t);
        }

        array<bool> plotUsed = new array<bool>();
        for (int pInit = 0; pInit < plots.Count(); pInit++)
            plotUsed.Insert(false);

        array<bool> trackAssigned = new array<bool>();
        for (int tInit = 0; tInit < m_Tracks.Count(); tInit++)
            trackAssigned.Insert(false);

        // Cost triples stored as parallel arrays to avoid nested ref types.
        array<float> pairCosts = new array<float>();
        array<int> pairTrackIdx = new array<int>();
        array<int> pairPlotIdx = new array<int>();

        for (int ti = 0; ti < m_Tracks.Count(); ti++)
        {
            RDF_RadarTrack track = m_Tracks.Get(ti);
            if (!track)
                continue;
            if (track.m_MissCount > m_MaxMisses)
                continue;

            for (int pi = 0; pi < plots.Count(); pi++)
            {
                RDF_RadarTarget plot = plots.Get(pi);
                if (!plot)
                    continue;

                float dt = plot.m_Time - track.GetLastTime();
                if (dt < 0.0)
                    dt = 0.0;
                float predRange = track.m_FilteredRangeM + track.m_FilteredRangeRateMs * dt;
                float dRange = plot.m_Distance - predRange;
                if (dRange < 0.0)
                    dRange = -dRange;
                if (dRange > m_GateRangeM)
                    continue;

                float dAz = RDF_RadarTrack.NormalizeAngleDeg(
                    plot.m_AzimuthDeg - track.m_FilteredAzimuthDeg);
                if (dAz < 0.0)
                    dAz = -dAz;
                if (dAz > m_GateAzimuthDeg)
                    continue;

                float cost = dRange / Math.Max(m_GateRangeM, 1.0)
                    + dAz / Math.Max(m_GateAzimuthDeg, 0.1);
                pairCosts.Insert(cost);
                pairTrackIdx.Insert(ti);
                pairPlotIdx.Insert(pi);
            }
        }

        // Selection sort by ascending cost (small N).
        for (int a = 0; a < pairCosts.Count(); a++)
        {
            int best = a;
            for (int b = a + 1; b < pairCosts.Count(); b++)
            {
                if (pairCosts.Get(b) < pairCosts.Get(best))
                    best = b;
            }
            if (best != a)
            {
                float tmpC = pairCosts.Get(a);
                pairCosts.Set(a, pairCosts.Get(best));
                pairCosts.Set(best, tmpC);
                int tmpT = pairTrackIdx.Get(a);
                pairTrackIdx.Set(a, pairTrackIdx.Get(best));
                pairTrackIdx.Set(best, tmpT);
                int tmpP = pairPlotIdx.Get(a);
                pairPlotIdx.Set(a, pairPlotIdx.Get(best));
                pairPlotIdx.Set(best, tmpP);
            }
        }

        for (int c = 0; c < pairCosts.Count(); c++)
        {
            int tiA = pairTrackIdx.Get(c);
            int piA = pairPlotIdx.Get(c);
            if (trackAssigned.Get(tiA) || plotUsed.Get(piA))
                continue;
            trackAssigned.Set(tiA, true);
            plotUsed.Set(piA, true);
            RDF_RadarTrack assigned = m_Tracks.Get(tiA);
            RDF_RadarTarget hit = plots.Get(piA);
            if (assigned && hit)
            {
                ApplyBallisticConfig(assigned);
                assigned.FilterUpdate(hit, m_Alpha, m_Beta, m_ConfirmHits);
            }
        }

        for (int tm = 0; tm < m_Tracks.Count(); tm++)
        {
            if (trackAssigned.Get(tm))
                continue;
            RDF_RadarTrack missed = m_Tracks.Get(tm);
            if (missed)
                missed.m_MissCount = missed.m_MissCount + 1;
        }

        for (int pn = 0; pn < plots.Count(); pn++)
        {
            if (plotUsed.Get(pn))
                continue;
            RDF_RadarTarget seed = plots.Get(pn);
            if (!seed)
                continue;
            RDF_RadarTrack born = new RDF_RadarTrack();
            born.m_TrackId = m_NextTrackId;
            m_NextTrackId = m_NextTrackId + 1;
            if (seed.m_Entity)
                born.m_Entity = seed.m_Entity;
            ApplyBallisticConfig(born);
            born.FilterUpdate(seed, m_Alpha, m_Beta, m_ConfirmHits);
            m_Tracks.Insert(born);
        }

        for (int j = m_Tracks.Count() - 1; j >= 0; j--)
        {
            RDF_RadarTrack tr = m_Tracks.Get(j);
            if (!tr)
            {
                m_Tracks.Remove(j);
                continue;
            }
            bool ageOut = false;
            if (worldTimeSec - tr.GetLastTime() > m_PruneAgeSec)
                ageOut = true;
            if (tr.m_MissCount > m_MaxMisses)
                ageOut = true;
            if (ageOut)
                m_Tracks.Remove(j);
        }
    }

    RDF_RadarTrack GetTrajectory(IEntity entity)
    {
        return FindTrack(entity);
    }

    RDF_RadarTrack GetTrackById(int trackId)
    {
        for (int i = 0; i < m_Tracks.Count(); i++)
        {
            RDF_RadarTrack tr = m_Tracks.Get(i);
            if (tr && tr.m_TrackId == trackId)
                return tr;
        }
        return null;
    }

    array<ref RDF_RadarTrack> GetAllTracks()
    {
        return m_Tracks;
    }

    vector GetLastRadarOrigin()
    {
        return m_LastRadarOrigin;
    }

    protected RDF_RadarTrack FindTrack(IEntity entity)
    {
        if (!entity)
            return null;
        for (int i = 0; i < m_Tracks.Count(); i++)
        {
            if (m_Tracks.Get(i).m_Entity == entity)
                return m_Tracks.Get(i);
        }
        return null;
    }
}
