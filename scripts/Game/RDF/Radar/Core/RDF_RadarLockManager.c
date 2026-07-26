// Lock lifecycle state (search -> acquire -> track, with coast on brief loss).
enum ERDF_RadarLockState
{
    RDF_RADAR_LOCK_SEARCH,     // no target held; optionally auto-acquiring
    RDF_RADAR_LOCK_ACQUIRING,  // candidate held, waiting for confirm hits
    RDF_RADAR_LOCK_TRACKING,   // stable lock
    RDF_RADAR_LOCK_COAST       // target briefly missing; predicting before drop
}

// Turns tracker output into a single "current locked target" with a
// search -> acquire -> track -> coast state machine and break-lock rules.
// Consumers (weapon / fire control) read GetLockedTarget each frame.
// Selection is track-driven; entity link is best-effort (debug/gameplay only).
class RDF_RadarLockManager
{
    protected static const float RAD_TO_DEG = 57.29577951;

    // Config.
    protected bool m_AutoAcquire;
    protected float m_MaxLockRangeM;          // <=0 uses the scan context range
    protected float m_LockSectorHalfAngleDeg; // <=0 disables sector gating
    protected int m_AcquireHits;              // hits before ACQUIRING -> TRACKING
    protected float m_CoastMaxSec;            // coast time before dropping lock
    protected bool m_AllowVehicles;
    protected bool m_AllowProjectiles;
    protected bool m_AllowEmitters;

    // State.
    protected ERDF_RadarLockState m_State;
    protected int m_LockedTrackId;
    protected IEntity m_LockedEntity;
    protected vector m_LockedPosition;
    protected vector m_LockedVelocity;
    protected float m_LockedRangeM;
    protected float m_LockedAzimuthDeg;
    protected float m_LastUpdateTimeS;
    protected float m_CoastElapsedSec;
    protected vector m_LastOrigin;
    protected vector m_LastForward;
    protected float m_LastContextRangeM;

    void RDF_RadarLockManager()
    {
        m_AutoAcquire = true;
        m_MaxLockRangeM = 0.0;
        m_LockSectorHalfAngleDeg = 0.0;
        m_AcquireHits = 2;
        m_CoastMaxSec = 2.0;
        m_AllowVehicles = true;
        m_AllowProjectiles = false;
        m_AllowEmitters = false;
        ResetState();
    }

    protected void ResetState()
    {
        m_State = ERDF_RadarLockState.RDF_RADAR_LOCK_SEARCH;
        m_LockedTrackId = -1;
        m_LockedEntity = null;
        m_LockedPosition = "0 0 0";
        m_LockedVelocity = "0 0 0";
        m_LockedRangeM = 0.0;
        m_LockedAzimuthDeg = 0.0;
        m_LastUpdateTimeS = -1.0;
        m_CoastElapsedSec = 0.0;
    }

    // ---- Config setters ----

    void SetAutoAcquire(bool enabled)
    {
        m_AutoAcquire = enabled;
    }

    void SetMaxLockRange(float rangeM)
    {
        m_MaxLockRangeM = rangeM;
    }

    void SetLockSector(float halfAngleDeg)
    {
        m_LockSectorHalfAngleDeg = halfAngleDeg;
    }

    void SetAcquireHits(int hits)
    {
        m_AcquireHits = Math.Max(1, hits);
    }

    void SetCoastMaxSec(float seconds)
    {
        m_CoastMaxSec = Math.Max(0.0, seconds);
    }

    void SetTypeFilter(bool vehicles, bool projectiles, bool emitters)
    {
        m_AllowVehicles = vehicles;
        m_AllowProjectiles = projectiles;
        m_AllowEmitters = emitters;
    }

    // ---- Manual control ----

    // Force lock onto a specific track id (manual / HUD pick). Confirmed on next Update.
    bool LockTrackId(int trackId)
    {
        if (trackId < 0)
            return false;
        m_LockedTrackId = trackId;
        m_LockedEntity = null;
        m_State = ERDF_RadarLockState.RDF_RADAR_LOCK_ACQUIRING;
        m_CoastElapsedSec = 0.0;
        return true;
    }

    void Unlock()
    {
        ResetState();
    }

    // ---- Queries ----

    ERDF_RadarLockState GetState()
    {
        return m_State;
    }

    string GetStateName()
    {
        if (m_State == ERDF_RadarLockState.RDF_RADAR_LOCK_ACQUIRING)
            return "ACQUIRING";
        if (m_State == ERDF_RadarLockState.RDF_RADAR_LOCK_TRACKING)
            return "TRACKING";
        if (m_State == ERDF_RadarLockState.RDF_RADAR_LOCK_COAST)
            return "COAST";
        return "SEARCH";
    }

    // Locked in a usable sense: actively tracking or briefly coasting.
    bool IsLocked()
    {
        if (m_State == ERDF_RadarLockState.RDF_RADAR_LOCK_TRACKING)
            return true;
        if (m_State == ERDF_RadarLockState.RDF_RADAR_LOCK_COAST)
            return true;
        return false;
    }

    bool HasTarget()
    {
        return m_LockedTrackId >= 0;
    }

    int GetLockedTrackId()
    {
        return m_LockedTrackId;
    }

    IEntity GetLockedEntity()
    {
        return m_LockedEntity;
    }

    vector GetLockedPosition()
    {
        return m_LockedPosition;
    }

    vector GetLockedVelocity()
    {
        return m_LockedVelocity;
    }

    float GetLockedRangeM()
    {
        return m_LockedRangeM;
    }

    float GetLockedAzimuthDeg()
    {
        return m_LockedAzimuthDeg;
    }

    // Convenience for weapon / fire-control code. Returns false when no usable lock.
    bool GetLockedTarget(out IEntity entity, out vector worldPos)
    {
        if (!IsLocked())
            return false;
        entity = m_LockedEntity;
        worldPos = m_LockedPosition;
        return true;
    }

    // ---- Main update: call once per completed scan ----

    void Update(
        array<ref RDF_RadarTrack> tracks,
        vector radarOrigin,
        vector radarForward,
        float contextRangeM,
        float worldTimeS)
    {
        m_LastOrigin = radarOrigin;
        m_LastForward = radarForward;
        m_LastContextRangeM = contextRangeM;

        RDF_RadarTrack locked = FindTrack(tracks, m_LockedTrackId);

        if (m_LockedTrackId >= 0)
        {
            if (locked)
            {
                UpdateFromTrack(locked, radarOrigin, radarForward, worldTimeS);
                return;
            }
            EnterOrContinueCoast(worldTimeS);
            return;
        }

        m_State = ERDF_RadarLockState.RDF_RADAR_LOCK_SEARCH;
        if (m_AutoAcquire)
        {
            RDF_RadarTrack best = SelectBest(tracks, radarOrigin, radarForward);
            if (best)
            {
                m_LockedTrackId = best.m_TrackId;
                m_State = ERDF_RadarLockState.RDF_RADAR_LOCK_ACQUIRING;
                m_CoastElapsedSec = 0.0;
                UpdateFromTrack(best, radarOrigin, radarForward, worldTimeS);
            }
        }
    }

    protected void UpdateFromTrack(
        RDF_RadarTrack track,
        vector radarOrigin,
        vector radarForward,
        float worldTimeS)
    {
        vector pos = track.m_FilteredPosition;
        vector delta = pos - radarOrigin;
        float rangeM = delta.Length();
        float azDeg = Math.Atan2(delta[2], delta[0]) * RAD_TO_DEG;

        if (!IsEligible(track, radarOrigin, radarForward))
        {
            Unlock();
            return;
        }

        m_LockedEntity = track.m_Entity;
        m_LockedPosition = pos;
        m_LockedVelocity = track.m_FilteredVelocity;
        m_LockedRangeM = rangeM;
        m_LockedAzimuthDeg = azDeg;
        m_LastUpdateTimeS = worldTimeS;
        m_CoastElapsedSec = 0.0;

        bool stable = track.m_Confirmed;
        if (track.m_HitCount < m_AcquireHits)
            stable = false;
        if (stable)
            m_State = ERDF_RadarLockState.RDF_RADAR_LOCK_TRACKING;
        else
            m_State = ERDF_RadarLockState.RDF_RADAR_LOCK_ACQUIRING;
    }

    protected void EnterOrContinueCoast(float worldTimeS)
    {
        if (m_State == ERDF_RadarLockState.RDF_RADAR_LOCK_ACQUIRING)
        {
            // Never promoted to a stable track; drop immediately.
            Unlock();
            return;
        }

        float dt = 0.0;
        if (m_LastUpdateTimeS >= 0.0)
            dt = worldTimeS - m_LastUpdateTimeS;
        if (dt < 0.0)
            dt = 0.0;
        m_CoastElapsedSec = m_CoastElapsedSec + dt;
        m_LastUpdateTimeS = worldTimeS;

        if (m_CoastElapsedSec > m_CoastMaxSec)
        {
            Unlock();
            return;
        }

        m_State = ERDF_RadarLockState.RDF_RADAR_LOCK_COAST;
        // Dead-reckon the last known state so weapons keep a usable aim point.
        m_LockedPosition = m_LockedPosition + m_LockedVelocity * dt;
    }

    // ---- Selection helpers ----

    // Eligible track ids for manual HUD selection (confirmed + filters + gates).
    array<int> GetEligibleTrackIds(
        array<ref RDF_RadarTrack> tracks,
        vector radarOrigin,
        vector radarForward)
    {
        array<int> ids = new array<int>();
        if (!tracks)
            return ids;
        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (!tr)
                continue;
            if (!IsEligible(tr, radarOrigin, radarForward))
                continue;
            ids.Insert(tr.m_TrackId);
        }
        return ids;
    }

    // Prefer nearest eligible track. Unconfirmed tracks may be acquired
    // (ACQUIRING); confirmed tracks with enough hits become TRACKING.
    protected RDF_RadarTrack SelectBest(
        array<ref RDF_RadarTrack> tracks,
        vector radarOrigin,
        vector radarForward)
    {
        if (!tracks)
            return null;
        RDF_RadarTrack best = null;
        float bestRange = 0.0;
        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (!tr)
                continue;
            if (tr.m_HitCount < 1)
                continue;
            if (!IsEligible(tr, radarOrigin, radarForward))
                continue;
            vector delta = tr.m_FilteredPosition - radarOrigin;
            float rangeM = delta.Length();
            if (!best || rangeM < bestRange)
            {
                best = tr;
                bestRange = rangeM;
            }
        }
        return best;
    }

    protected bool IsEligible(
        RDF_RadarTrack track,
        vector radarOrigin,
        vector radarForward)
    {
        if (!track)
            return false;
        if (!IsTypeAllowed(track.m_Type))
            return false;

        vector delta = track.m_FilteredPosition - radarOrigin;
        float rangeM = delta.Length();

        float maxRange = m_MaxLockRangeM;
        if (maxRange <= 0.0)
            maxRange = m_LastContextRangeM;
        if (maxRange > 0.0 && rangeM > maxRange)
            return false;

        if (m_LockSectorHalfAngleDeg > 0.0)
        {
            float targetAz = Math.Atan2(delta[2], delta[0]) * RAD_TO_DEG;
            float fwdAz = Math.Atan2(radarForward[2], radarForward[0]) * RAD_TO_DEG;
            float diff = RDF_RadarTrack.NormalizeAngleDeg(targetAz - fwdAz);
            if (diff < 0.0)
                diff = -diff;
            if (diff > m_LockSectorHalfAngleDeg)
                return false;
        }
        return true;
    }

    protected bool IsTypeAllowed(ERDF_RadarTargetType type)
    {
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE)
            return m_AllowVehicles;
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return m_AllowProjectiles;
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return m_AllowEmitters;
        // Anonymous plots: allow so measurement-synthesis tracks can still lock.
        return true;
    }

    protected RDF_RadarTrack FindTrack(array<ref RDF_RadarTrack> tracks, int trackId)
    {
        if (!tracks || trackId < 0)
            return null;
        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (tr && tr.m_TrackId == trackId)
                return tr;
        }
        return null;
    }

    string GetStatusShort()
    {
        string s = "LOCK " + GetStateName();
        if (m_LockedTrackId >= 0)
        {
            s = s + " id=" + m_LockedTrackId.ToString()
                + " r=" + Math.Round(m_LockedRangeM).ToString()
                + "m az=" + Math.Round(m_LockedAzimuthDeg).ToString();
        }
        return s;
    }
}
