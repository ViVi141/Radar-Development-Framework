// Fire-control snapshot for weapon / missile scripts.
// Filled by RDF_RadarWeaponBridge from Sensor lock (or ARM aim).
class RDF_RadarFireSolution
{
    bool m_Valid;
    bool m_CanAuthorizeFire;
    bool m_IsArm;
    bool m_Radiating;
    ERDF_RadarLockState m_LockState;
    int m_TrackId;
    IEntity m_Target;
    vector m_AimPos;
    vector m_AimVel;
    float m_RangeM;
    float m_AzimuthDeg;

    void RDF_RadarFireSolution()
    {
        Clear();
    }

    void Clear()
    {
        m_Valid = false;
        m_CanAuthorizeFire = false;
        m_IsArm = false;
        m_Radiating = false;
        m_LockState = ERDF_RadarLockState.RDF_RADAR_LOCK_SEARCH;
        m_TrackId = -1;
        m_Target = null;
        m_AimPos = "0 0 0";
        m_AimVel = "0 0 0";
        m_RangeM = 0.0;
        m_AzimuthDeg = 0.0;
    }
}

// Stable bridge: RadarSensor / RadarComponent lock → weapon fire solution.
// Mod weapons should prefer this over calling LockManager fields directly.
class RDF_RadarWeaponBridge
{
    protected RDF_RadarSensor m_Sensor;
    protected RDF_RadarComponent m_RadarComponent;
    // When true, only TRACKING authorizes launch (COAST still valid for midcourse uplink).
    protected bool m_RequireTrackingForFire;
    // When true, prefer GetArmAim if the lock is an emitter track.
    protected bool m_PreferArmAim;

    void RDF_RadarWeaponBridge()
    {
        m_Sensor = null;
        m_RadarComponent = null;
        m_RequireTrackingForFire = true;
        m_PreferArmAim = true;
    }

    void SetRequireTrackingForFire(bool requireTracking)
    {
        m_RequireTrackingForFire = requireTracking;
    }

    void SetPreferArmAim(bool preferArm)
    {
        m_PreferArmAim = preferArm;
    }

    void BindSensor(RDF_RadarSensor sensor)
    {
        m_Sensor = sensor;
        m_RadarComponent = null;
    }

    void BindRadarComponent(RDF_RadarComponent radar)
    {
        m_RadarComponent = radar;
        m_Sensor = null;
        if (radar)
            m_Sensor = radar.GetSensor();
    }

    // Find RDF_RadarComponent on owner (or parent hierarchy one level).
    bool BindFromOwner(IEntity owner)
    {
        m_Sensor = null;
        m_RadarComponent = null;
        if (!owner)
            return false;

        RDF_RadarComponent radar = RDF_RadarComponent.Cast(
            owner.FindComponent(RDF_RadarComponent));
        if (radar)
        {
            BindRadarComponent(radar);
            return true;
        }

        IEntity parent = owner.GetParent();
        if (parent)
        {
            radar = RDF_RadarComponent.Cast(parent.FindComponent(RDF_RadarComponent));
            if (radar)
            {
                BindRadarComponent(radar);
                return true;
            }
        }
        return false;
    }

    RDF_RadarSensor GetSensor()
    {
        if (m_RadarComponent)
            return m_RadarComponent.GetSensor();
        return m_Sensor;
    }

    RDF_RadarLockManager GetLockManager()
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return null;
        return sensor.GetLockManager();
    }

    // Launch gate: TRACKING by default; COAST/ACQUIRING optional.
    bool CanAuthorizeFire()
    {
        RDF_RadarLockManager lockMgr = GetLockManager();
        if (!lockMgr || !lockMgr.IsLocked())
            return false;

        ERDF_RadarLockState state = lockMgr.GetState();
        if (m_RequireTrackingForFire)
        {
            if (state == ERDF_RadarLockState.RDF_RADAR_LOCK_TRACKING)
                return true;
            return false;
        }

        if (state == ERDF_RadarLockState.RDF_RADAR_LOCK_TRACKING)
            return true;
        if (state == ERDF_RadarLockState.RDF_RADAR_LOCK_COAST)
            return true;
        return false;
    }

    // Fill a fire solution for HUD / launch / midcourse. Returns false if no usable lock.
    bool TryGetFireSolution(out RDF_RadarFireSolution solution)
    {
        if (!solution)
            solution = new RDF_RadarFireSolution();
        solution.Clear();

        RDF_RadarLockManager lockMgr = GetLockManager();
        if (!lockMgr || !lockMgr.IsLocked())
            return false;

        solution.m_LockState = lockMgr.GetState();
        solution.m_TrackId = lockMgr.GetLockedTrackId();
        solution.m_AimVel = lockMgr.GetLockedVelocity();
        solution.m_RangeM = lockMgr.GetLockedRangeM();
        solution.m_AzimuthDeg = lockMgr.GetLockedAzimuthDeg();
        solution.m_CanAuthorizeFire = CanAuthorizeFire();

        IEntity ent;
        vector aim;
        bool radiating;

        if (m_PreferArmAim)
        {
            RDF_RadarSensor sensor = GetSensor();
            if (sensor && sensor.GetArmAim(ent, aim, radiating))
            {
                solution.m_Valid = true;
                solution.m_IsArm = true;
                solution.m_Radiating = radiating;
                solution.m_Target = ent;
                solution.m_AimPos = aim;
                return true;
            }
        }

        if (!lockMgr.GetLockedTarget(ent, aim))
            return false;

        solution.m_Valid = true;
        solution.m_IsArm = false;
        solution.m_Radiating = false;
        solution.m_Target = ent;
        solution.m_AimPos = aim;
        return true;
    }

    // Midcourse datalink uplink (TRACKING or COAST). Does not require fire authorization.
    bool TryGetMidcourseAim(out vector aimPos, out vector aimVel, out IEntity target)
    {
        aimPos = "0 0 0";
        aimVel = "0 0 0";
        target = null;

        RDF_RadarFireSolution sol = new RDF_RadarFireSolution();
        if (!TryGetFireSolution(sol))
            return false;
        if (!sol.m_Valid)
            return false;

        aimPos = sol.m_AimPos;
        aimVel = sol.m_AimVel;
        target = sol.m_Target;
        return true;
    }

    // One-shot helper: owner → solution without keeping a bridge instance.
    static bool TryGetFireSolutionFromOwner(
        IEntity owner,
        out RDF_RadarFireSolution solution,
        bool requireTrackingForFire = true)
    {
        RDF_RadarWeaponBridge bridge = new RDF_RadarWeaponBridge();
        bridge.SetRequireTrackingForFire(requireTrackingForFire);
        if (!bridge.BindFromOwner(owner))
        {
            if (!solution)
                solution = new RDF_RadarFireSolution();
            solution.Clear();
            return false;
        }
        return bridge.TryGetFireSolution(solution);
    }

    // Near-range LiDAR fallback: closest hit entity / position (no lock state machine).
    static bool TryGetLidarAim(
        RDF_LidarSensor lidar,
        out IEntity target,
        out vector aimPos,
        out float rangeM)
    {
        target = null;
        aimPos = "0 0 0";
        rangeM = 0.0;
        if (!lidar)
            return false;

        RDF_LidarSample hit = lidar.GetClosestHit();
        if (!hit || !hit.m_Hit)
            return false;

        target = hit.m_Entity;
        aimPos = hit.m_HitPos;
        rangeM = hit.m_Distance;
        return true;
    }

    string GetStatusShort()
    {
        RDF_RadarFireSolution sol = new RDF_RadarFireSolution();
        if (!TryGetFireSolution(sol))
            return "FIRE NOLOCK";

        string armTag = "";
        if (sol.m_IsArm)
            armTag = " ARM";

        string auth = "HOLD";
        if (sol.m_CanAuthorizeFire)
            auth = "READY";

        string stateName = "SEARCH";
        RDF_RadarLockManager lockMgr = GetLockManager();
        if (lockMgr)
            stateName = lockMgr.GetStateName();

        return string.Format(
            "FIRE %1%2 id=%3 r=%4m %5",
            auth,
            armTag,
            sol.m_TrackId.ToString(),
            Math.Round(sol.m_RangeM).ToString(),
            stateName);
    }
}
