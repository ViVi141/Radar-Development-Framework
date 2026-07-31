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
    // Last associated plot Doppler bin (-1 unknown / TwoPulse).
    int m_LastDopplerBin = -1;
    // True while coasting on filtered velocity after a miss.
    bool m_Coasting;
    ERDF_RadarTargetType m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
    // Ballistic prior (ShellMoveComponent.AirDrag). <=0 disables drag path.
    float m_AirDrag = RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE;
    bool m_UseBallisticPrediction = true;
    float m_GroundYM = 0.0;
    bool m_GroundYValid;
    ref RDF_RadarWlrFix m_LastWlrFix;
    // Copied from tracker settings for WLR quality gates / smoothing.
    int m_WlrMinHits = 5;
    float m_WlrMinSpanS = 1.0;
    float m_WlrMaxFitRmsM = 80.0;
    int m_WlrFitWindow = 20;
    float m_WlrSmoothAlpha = 0.35;

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
        float hist = -1.0;
        if (m_Times.Count() > 0)
            hist = m_Times.Get(m_Times.Count() - 1);
        // Coast advances m_LastUpdateTime without Push; prefer the newer clock.
        if (m_LastUpdateTime > hist)
            return m_LastUpdateTime;
        return hist;
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

    // Real WLR-style: multi-point vacuum fit on track history, then AirDrag
    // integrate to DEM/surface for launch + impact. Soft-fails keep prior fix.
    // groundYM is the flat-plane fallback when DEM/surface sampling is disabled.
    RDF_RadarWlrFix SolveWeaponLocate(float groundYM)
    {
        RDF_RadarWlrFix empty = new RDF_RadarWlrFix();
        if (!IsProjectileTrack())
            return empty;
        if (m_HitCount < m_WlrMinHits)
            return empty;
        if (m_AirDrag <= 0.0)
            return empty;
        if (!m_Positions || !m_Times)
            return empty;
        if (m_Positions.Count() < m_WlrMinHits)
            return empty;

        RDF_RadarGlobalWind wind = RDF_RadarBallistics.SampleGlobalWind();
        RDF_RadarWlrFix fresh = RDF_RadarBallistics.SolveLaunchAndImpactFromHistory(
            m_Positions,
            m_Times,
            groundYM,
            m_AirDrag,
            wind,
            RDF_RadarBallistics.GRAVITY_M_S2,
            m_WlrMinHits,
            m_WlrMinSpanS,
            m_WlrMaxFitRmsM,
            m_WlrFitWindow);

        if (!fresh || !fresh.m_FitValid || (!fresh.m_LaunchValid && !fresh.m_ImpactValid))
        {
            // Keep previous reported fix when the new arc fails quality gates.
            if (m_LastWlrFix)
                return m_LastWlrFix;
            return empty;
        }

        if (m_LastWlrFix && m_WlrSmoothAlpha > 0.0 && m_WlrSmoothAlpha < 1.0)
        {
            float a = m_WlrSmoothAlpha;
            float b = 1.0 - a;
            if (fresh.m_LaunchValid && m_LastWlrFix.m_LaunchValid)
            {
                vector lp0 = m_LastWlrFix.m_LaunchPos;
                vector lp1 = fresh.m_LaunchPos;
                fresh.m_LaunchPos = Vector(
                    lp0[0] * b + lp1[0] * a,
                    lp0[1] * b + lp1[1] * a,
                    lp0[2] * b + lp1[2] * a);
                fresh.m_LaunchTimeS = (m_LastWlrFix.m_LaunchTimeS * b) + (fresh.m_LaunchTimeS * a);
            }
            if (fresh.m_ImpactValid && m_LastWlrFix.m_ImpactValid)
            {
                vector ip0 = m_LastWlrFix.m_ImpactPos;
                vector ip1 = fresh.m_ImpactPos;
                fresh.m_ImpactPos = Vector(
                    ip0[0] * b + ip1[0] * a,
                    ip0[1] * b + ip1[1] * a,
                    ip0[2] * b + ip1[2] * a);
                fresh.m_ImpactTimeS = (m_LastWlrFix.m_ImpactTimeS * b) + (fresh.m_ImpactTimeS * a);
            }
        }

        m_LastWlrFix = fresh;
        m_GroundYM = groundYM;
        m_GroundYValid = true;
        return fresh;
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
        m_Coasting = false;
        m_LastDopplerBin = target.m_DopplerBin;
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

    // Advance filtered state with velocity (and ballistic integrate for shells).
    void CoastTo(float worldTimeSec, vector radarOrigin)
    {
        float lastTime = GetLastTime();
        if (lastTime < 0.0)
            return;
        float dt = worldTimeSec - lastTime;
        if (dt <= 0.0)
            return;

        vector pred = PredictAt(worldTimeSec);
        m_FilteredPosition = pred;

        vector delta = pred - radarOrigin;
        float rangeM = delta.Length();
        if (rangeM > 0.001)
        {
            m_FilteredRangeM = rangeM;
            m_FilteredAzimuthDeg = Math.Atan2(delta[2], delta[0]) * 57.2957795;
            float horizontal = Math.Sqrt(delta[0] * delta[0] + delta[2] * delta[2]);
            m_FilteredElevationDeg = Math.Atan2(delta[1], Math.Max(0.001, horizontal)) * 57.2957795;
        }
        else
        {
            m_FilteredRangeM = m_FilteredRangeM + m_FilteredRangeRateMs * dt;
            if (m_FilteredRangeM < 0.0)
                m_FilteredRangeM = 0.0;
        }

        // Do not Push coast samples — history stays measurement-only (WLR / fit).
        m_LastUpdateTime = worldTimeSec;
        m_Coasting = true;
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
    protected int m_WeaponLocateMinHits = 5;
    protected float m_WeaponLocateMinSpanS = 1.0;
    protected float m_WeaponLocateMaxFitRmsM = 80.0;
    protected int m_WeaponLocateFitWindow = 20;
    protected float m_WeaponLocateSmoothAlpha = 0.35;
    protected bool m_CoastOnMiss = true;
    protected bool m_CoastOnDopplerNull = true;

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
        m_WeaponLocateMinSpanS = settings.m_WeaponLocateMinSpanS;
        m_WeaponLocateMaxFitRmsM = settings.m_WeaponLocateMaxFitRmsM;
        m_WeaponLocateFitWindow = settings.m_WeaponLocateFitWindow;
        m_WeaponLocateSmoothAlpha = settings.m_WeaponLocateSmoothAlpha;
        m_CoastOnMiss = settings.m_TrackCoastOnMiss;
        m_CoastOnDopplerNull = settings.m_TrackCoastOnDopplerNull;
        RDF_RadarBallistics.SetUseDemGround(settings.m_EnableDemGroundForWlr);
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
        track.m_WlrMinHits = m_WeaponLocateMinHits;
        track.m_WlrMinSpanS = m_WeaponLocateMinSpanS;
        track.m_WlrMaxFitRmsM = m_WeaponLocateMaxFitRmsM;
        track.m_WlrFitWindow = m_WeaponLocateFitWindow;
        track.m_WlrSmoothAlpha = m_WeaponLocateSmoothAlpha;
    }

    // Recompute launch/impact for confirmed projectile tracks.
    // groundYM is only the fallback plane; SampleGroundYM prefers DEM/surface.
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

                float lastT = track.m_LastUpdateTime;
                if (lastT < 0.0)
                    lastT = track.GetLastTime();
                float dt = plot.m_Time - lastT;
                if (dt < 0.0)
                    dt = 0.0;
                float gateRange = m_GateRangeM;
                float gateAz = m_GateAzimuthDeg;
                // Widen association while coasting through MTI / zero-Doppler nulls.
                if (m_CoastOnDopplerNull)
                {
                    bool dopplerNull = false;
                    if (track.m_LastDopplerBin == 0)
                        dopplerNull = true;
                    if (track.m_Coasting)
                        dopplerNull = true;
                    if (dopplerNull)
                    {
                        gateRange = gateRange * 1.5;
                        gateAz = gateAz * 1.5;
                    }
                }
                float predRange = track.m_FilteredRangeM + track.m_FilteredRangeRateMs * dt;
                float dRange = plot.m_Distance - predRange;
                if (dRange < 0.0)
                    dRange = -dRange;
                if (dRange > gateRange)
                    continue;

                float dAz = RDF_RadarTrack.NormalizeAngleDeg(
                    plot.m_AzimuthDeg - track.m_FilteredAzimuthDeg);
                if (dAz < 0.0)
                    dAz = -dAz;
                if (dAz > gateAz)
                    continue;

                float cost = dRange / Math.Max(gateRange, 1.0)
                    + dAz / Math.Max(gateAz, 0.1);
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
            if (!missed)
                continue;
            missed.m_MissCount = missed.m_MissCount + 1;
            if (m_CoastOnMiss)
                missed.CoastTo(worldTimeSec, m_LastRadarOrigin);
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

    // Drop every track. Used when a consumer restarts the sensor and must not
    // inherit tracks built from a previous configuration.
    void ClearTracks()
    {
        m_Tracks.Clear();
    }

    // Replace local tracks with authoritative network summaries (no re-association).
    void ApplySyncedTracks(array<ref RDF_RadarTrack> synced)
    {
        if (!m_Tracks)
            m_Tracks = new array<ref RDF_RadarTrack>();
        m_Tracks.Clear();
        if (!synced)
            return;
        for (int i = 0; i < synced.Count(); i++)
        {
            RDF_RadarTrack src = synced.Get(i);
            if (!src)
                continue;
            m_Tracks.Insert(src);
        }
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
