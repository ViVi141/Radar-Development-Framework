/**
 * EM Voxel Field  -- Phase 1 --   *  Advanced Spatial Optimization Rewrite
 *
 * Architecture: Two-Level Chunked Grid
 * -----------------------------------------------------------------------------
 *   -- Macro (Chunk) : 200 m^3 cell  -> key-space 100 per km axis  (manageable)
 *   -- Micro (Voxel) : 10 m^3 cell  ->20^3 = 8 000 voxels per chunk  (10 m precision)
 *   -- SOA storage in EMChunk  -- flat parallel arrays, zero per-voxel object alloc
 *   -- Lazy Ray Evaluation  -- passive sensors query EMRayDescriptor list, zero write cost
 *   -- Trilinear interpolation  -- sub-voxel (cm-level) perceived precision on read
 *   -- Temporal LoD  -- update priority controls macro vs. micro stepping step size
 *   -- Chunk TTL  -- idle chunks freed automatically; max-chunk budget enforced
 *   -- Morton-code utility  -- exposed as static helpers for external cache-friendly use
 *
 * Memory: 100 active 200 m chunks  ~= 100 x (8000 x 3 x 4 B)  ~= 9.6 MB
 * CPU:    iteration only over active chunks; lazy rays O(sensors x rays) not O(map)
 */

// -----------------------------------------------------------------------------
// Constants  (int constants as enum; float constants are static members of EMVoxelField)
// -----------------------------------------------------------------------------
enum EMFieldInts
{
    EM_CHUNK_DIM   = 20,    // voxels per axis per chunk
    EM_CHUNK_CELLS = 8000   // EM_CHUNK_DIM^3
}

// Temporal LoD priorities  -- controls ray-march step multiplier.
enum EMUpdatePriority
{
    HIGH   = 0,  // tracking / locked radar    -- micro step = 1 x EM_MICRO_SIZE
    MEDIUM = 1,  // scanning radar              -- micro step = 2 x EM_MICRO_SIZE
    LOW    = 2,  // comm / weather / jammer     -- micro step = 5 x EM_MICRO_SIZE
}

// -----------------------------------------------------------------------------
// EMVoxelCell  -- lightweight query-result struct (backward-compatible public API)
// The actual storage lives in EMChunk SOA arrays; this struct is filled on demand.
// -----------------------------------------------------------------------------
class EMVoxelCell
{
    float  m_fTotalPower;     // linear watts
    int    m_iFrequencyMask;  // bitmask of bands
    vector m_vMainDir;        // dominant energy direction
    float  m_fNoiseFloor;     // linear watts
    float  m_fTTL;            // remaining lifetime (s)

    // Signal descriptors  -- populated by GetSignalDescriptorAt only
    int   m_iWaveformType;    // 0=Pulse,1=CW,2=FMCW,3=LPI, -1 unset
    float m_fEstPRF;          // Hz, 0 if unavailable
    float m_fLastPulseWidth;  // s
    float m_fLastFrequency;   // Hz

    void EMVoxelCell()
    {
        m_fTotalPower    = 0.0;
        m_iFrequencyMask = 0;
        m_vMainDir       = Vector(0, 0, 0);
        m_fNoiseFloor    = 1e-12;
        m_fTTL           = 0.0;
        m_iWaveformType  = -1;
        m_fEstPRF        = 0.0;
        m_fLastPulseWidth = 0.0;
        m_fLastFrequency  = 0.0;
    }

    float GetPower()     { return m_fTotalPower; }
    bool  IsExpired()    { return m_fTTL <= 0.0 && m_fTotalPower <= 0.0; }
}

// -----------------------------------------------------------------------------
// EMCellSignalDescriptor  -- sparse pulse history per micro-cell (only for cells
// that record pulse metadata; stored in EMChunk.m_Descriptors sparse map)
// -----------------------------------------------------------------------------
class EMCellSignalDescriptor
{
    ref array<float> m_Timestamps;   // world-time of each pulse (s)
    ref array<float> m_PulseWidths;  // pulse width per pulse (s)
    ref array<float> m_Frequencies;  // carrier frequency (Hz)
    int              m_iWaveform;    // 0=Pulse,1=CW,2=FMCW,3=LPI,-1=unknown
    static const int HISTORY = 32;

    void EMCellSignalDescriptor()
    {
        m_Timestamps  = new array<float>();
        m_PulseWidths = new array<float>();
        m_Frequencies = new array<float>();
        m_iWaveform   = -1;
    }

    void Record(float ts, float pw, float freq, int waveform)
    {
        m_Timestamps.Insert(ts);
        m_PulseWidths.Insert(pw);
        m_Frequencies.Insert(freq);
        if (waveform >= 0) m_iWaveform = waveform;
        while (m_Timestamps.Count() > HISTORY) { m_Timestamps.Remove(0); m_PulseWidths.Remove(0); m_Frequencies.Remove(0); }
    }

    float EstimatePRF()
    {
        if (m_Timestamps.Count() < 2) return 0.0;
        float sum = 0.0;
        int n = m_Timestamps.Count();
        for (int i = 1; i < n; ++i) sum += m_Timestamps[i] - m_Timestamps[i - 1];
        float avg = sum / (float)(n - 1);
        if (avg > 0.0) return 1.0 / avg;
        return 0.0;
    }

    float LastPulseWidth()
    {
        if (m_PulseWidths.Count() > 0)
            return m_PulseWidths.Get(m_PulseWidths.Count() - 1);
        return 0.0;
    }

    float LastFrequency()
    {
        if (m_Frequencies.Count() > 0)
            return m_Frequencies.Get(m_Frequencies.Count() - 1);
        return 0.0;
    }
}

// -----------------------------------------------------------------------------
// EMChunk  -- SOA micro-storage for one 200 m^3 chunk (20^3 = 8 000 voxels)
//
// SOA layout avoids per-voxel object allocation; all 8 000 slots are allocated
// together when the chunk is first activated, then reused until the chunk TTL
// expires.  Flat index: ix + EMFieldInts.EM_CHUNK_DIM*(iy + EMFieldInts.EM_CHUNK_DIM*iz)
// -----------------------------------------------------------------------------
class EMChunk
{
    // -- SOA arrays (size EMFieldInts.EM_CHUNK_CELLS, lazy-allocated) ---------------------
    ref array<float>  m_Powers;     // W per cell
    ref array<int>    m_FreqMasks;  // frequency bitmask
    ref array<float>  m_DirsX;     // dominant direction X component
    ref array<float>  m_DirsY;
    ref array<float>  m_DirsZ;
    ref array<float>  m_TTLs;       // remaining TTL per cell (s)

    // Sparse signal descriptors  -- only allocated for cells that receive pulse data
    ref map<int, ref EMCellSignalDescriptor> m_Descriptors;

    // Chunk-level metadata
    float m_fLastModified;  // world time of last write
    float m_fChunkTTL;      // seconds before automatic release when all cells idle
    int   m_iNonZero;       // running count of cells with power > 0 (fast "is empty" check)

    void EMChunk()
    {
        m_fLastModified = 0.0;
        m_fChunkTTL     = 0.0;
        m_iNonZero      = 0;
        // SOA arrays allocated lazily in Allocate()
    }

    bool IsAllocated()
    {
        return m_Powers != null;
    }

    // Initialize SOA arrays for EMFieldInts.EM_CHUNK_CELLS slots (called once on first write).
    void Allocate()
    {
        if (IsAllocated()) return;
        m_Powers    = new array<float>();
        m_FreqMasks = new array<int>();
        m_DirsX     = new array<float>();
        m_DirsY     = new array<float>();
        m_DirsZ     = new array<float>();
        m_TTLs      = new array<float>();
        for (int i = 0; i < EMFieldInts.EM_CHUNK_CELLS; ++i)
        {
            m_Powers.Insert(0.0);
            m_FreqMasks.Insert(0);
            m_DirsX.Insert(0.0);
            m_DirsY.Insert(0.0);
            m_DirsZ.Insert(0.0);
            m_TTLs.Insert(0.0);
        }
        m_Descriptors = new map<int, ref EMCellSignalDescriptor>();
    }

    // Flat cell index from local voxel coordinates (0..19 per axis).
    static int CellIndex(int lx, int ly, int lz)
    {
        return lx + EMFieldInts.EM_CHUNK_DIM * (ly + EMFieldInts.EM_CHUNK_DIM * lz);
    }

    // Add power contribution at local cell index.
    void AddPower(int idx, float powerW, int freqMask, float dx, float dy, float dz, float ttl, float worldTime)
    {
        if (idx < 0 || idx >= EMFieldInts.EM_CHUNK_CELLS) return;
        Allocate();

        float prev = m_Powers.Get(idx);
        float next = prev + Math.Max(powerW, 0.0);
        m_Powers.Set(idx, next);
        m_FreqMasks.Set(idx, m_FreqMasks.Get(idx) | freqMask);

        // Weighted blend direction toward new contribution
        float dlen = Math.Sqrt(dx*dx + dy*dy + dz*dz);
        if (dlen > 0.0)
        {
            float rx = m_DirsX.Get(idx) + dx / dlen;
            float ry = m_DirsY.Get(idx) + dy / dlen;
            float rz = m_DirsZ.Get(idx) + dz / dlen;
            float rlen = Math.Sqrt(rx*rx + ry*ry + rz*rz);
            if (rlen > 0.0) { m_DirsX.Set(idx, rx/rlen); m_DirsY.Set(idx, ry/rlen); m_DirsZ.Set(idx, rz/rlen); }
        }

        m_TTLs.Set(idx, Math.Max(m_TTLs.Get(idx), ttl));
        if (prev <= 0.0 && next > 0.0) m_iNonZero++;

        m_fLastModified = worldTime;
        m_fChunkTTL     = Math.Max(m_fChunkTTL, ttl + 1.0);
    }

    float GetPower(int idx)
    {
        if (!IsAllocated() || idx < 0 || idx >= EMFieldInts.EM_CHUNK_CELLS) return 0.0;
        return m_Powers.Get(idx);
    }

    vector GetDir(int idx)
    {
        if (!IsAllocated() || idx < 0 || idx >= EMFieldInts.EM_CHUNK_CELLS) return Vector(0,0,0);
        return Vector(m_DirsX.Get(idx), m_DirsY.Get(idx), m_DirsZ.Get(idx));
    }

    int GetFreqMask(int idx)
    {
        if (!IsAllocated() || idx < 0 || idx >= EMFieldInts.EM_CHUNK_CELLS) return 0;
        return m_FreqMasks.Get(idx);
    }

    // Record pulse descriptor at cell index.
    void RecordPulse(int idx, float ts, float pw, float freq, int waveform)
    {
        if (!IsAllocated() || idx < 0 || idx >= EMFieldInts.EM_CHUNK_CELLS) return;
        if (!m_Descriptors) m_Descriptors = new map<int, ref EMCellSignalDescriptor>();
        if (!m_Descriptors.Contains(idx))
            m_Descriptors.Insert(idx, new EMCellSignalDescriptor());
        EMCellSignalDescriptor d = m_Descriptors.Get(idx);
        if (d) d.Record(ts, pw, freq, waveform);
    }

    EMCellSignalDescriptor GetDescriptor(int idx)
    {
        if (!m_Descriptors || !m_Descriptors.Contains(idx)) return null;
        return m_Descriptors.Get(idx);
    }

    // Decay all cells; returns true if the chunk is now completely empty.
    bool TickDecay(float dt, float baseRate, float outSectorMul, bool inSector)
    {
        if (!IsAllocated()) return true;

        float rate;
        if (inSector) rate = baseRate;
        else          rate = baseRate * outSectorMul;
        float factor = Math.Pow(2.718281828, -rate * dt);
        m_iNonZero = 0;

        for (int i = 0; i < EMFieldInts.EM_CHUNK_CELLS; ++i)
        {
            float p = m_Powers.Get(i);
            if (p <= 0.0) continue;

            float t = m_TTLs.Get(i) - dt;
            float newP = p * factor;
            if (newP < 1e-15 || t <= 0.0)
            {
                m_Powers.Set(i, 0.0);
                m_TTLs.Set(i, 0.0);
            }
            else
            {
                m_Powers.Set(i, newP);
                m_TTLs.Set(i, t);
                m_iNonZero++;
            }
        }

        m_fChunkTTL -= dt;
        return m_iNonZero == 0 && m_fChunkTTL <= 0.0;
    }

    bool IsEmpty() { return m_iNonZero == 0; }
}

// -----------------------------------------------------------------------------
// EMRayDescriptor  -- lazy evaluation ray record
//
// Instead of writing energy into voxels at inject-time, callers may push a ray
// descriptor.  When a passive sensor calls GetPowerAt(), the field evaluates all
// live descriptors at that point (1/R^2 with Gaussian beam falloff) without any
// write.  Cost is O(sensors x live_rays), completely independent of map size.
// -----------------------------------------------------------------------------
class EMRayDescriptor
{
    vector m_vOrigin;       // emission origin
    vector m_vDir;          // unit direction
    float  m_fMaxRange;     // metres
    float  m_fPowerW;       // transmit power (W)
    int    m_iFreqMask;
    float  m_fExpiryTime;   // world time when this descriptor expires
    int    m_iWaveformType;
    float  m_fFrequency;    // Hz
    float  m_fPulseWidth;   // s
    float  m_fBeamHalfAngle;// degrees  -- Gaussian beam half-angle for falloff

    void EMRayDescriptor()
    {
        m_vOrigin        = Vector(0, 0, 0);
        m_vDir           = Vector(0, 0, 1);
        m_fMaxRange      = 0.0;
        m_fPowerW        = 0.0;
        m_iFreqMask      = 0;
        m_fExpiryTime    = 0.0;
        m_iWaveformType  = -1;
        m_fFrequency     = 0.0;
        m_fPulseWidth    = 0.0;
        m_fBeamHalfAngle = 5.0;
    }

    // Evaluate power contribution (W) at worldPos for a passive sensor.
    // Model: 1/R^2 path loss x Gaussian beam pattern.
    float EvaluateAt(vector worldPos)
    {
        if (m_fPowerW <= 0.0 || m_fMaxRange <= 0.0)
            return 0.0;

        vector rel = worldPos - m_vOrigin;
        float dist = rel.Length();
        if (dist < 0.1 || dist > m_fMaxRange)
            return 0.0;

        // Check if point is within beam cone (dot product against dir)
        vector relN = rel * (1.0 / dist);
        float cosA  = vector.Dot(relN, m_vDir);
        if (cosA <= 0.0)
            return 0.0; // behind emitter

        // Gaussian beam falloff: e^(-theta^2 / 2sigma^2), sigma = halfAngle/2
        float thetaDeg = Math.Acos(Math.Clamp(cosA, -1.0, 1.0)) * Math.RAD2DEG;
        float sigma    = Math.Max(m_fBeamHalfAngle * 0.5, 0.1);
        float beamGain = Math.Pow(2.718281828, -(thetaDeg * thetaDeg) / (2.0 * sigma * sigma));

        // Free-space path loss: P_r = P_t * beamGain / (4pi R^2)
        float pathLoss = 4.0 * 3.14159 * dist * dist;
        return m_fPowerW * beamGain / pathLoss;
    }

    bool IsExpired(float worldTime) { return worldTime >= m_fExpiryTime; }
}

// -----------------------------------------------------------------------------
// Phase 5: Sector Activation
// Records a conical scanning sector. Cells outside all registered active
// sectors will decay faster, capping memory to only scanned volumes.
// -----------------------------------------------------------------------------
class EMActiveSector
{
    vector m_vOrigin;
    float  m_fAzimuthDeg;       // centre azimuth (degrees, XZ plane)
    float  m_fHalfWidthDeg;     // half-angle of cone (degrees)
    float  m_fRange;            // max range (metres)
    float  m_fExpiryTime;       // world time (s) when this record expires

    void EMActiveSector(vector origin, float az, float halfWidth, float range, float expiryTime)
    {
        m_vOrigin       = origin;
        m_fAzimuthDeg   = az;
        m_fHalfWidthDeg = halfWidth;
        m_fRange        = range;
        m_fExpiryTime   = expiryTime;
    }

    // Returns true if worldPos falls inside this sector frustum.
    bool ContainsPoint(vector worldPos)
    {
        vector rel = worldPos - m_vOrigin;
        float d = rel.Length();
        if (d > m_fRange)
            return false;

        float az = Math.Atan2(rel[2], rel[0]) * Math.RAD2DEG;
        float diff = az - m_fAzimuthDeg;
        while (diff < -180.0) diff += 360.0;
        while (diff >  180.0) diff -= 360.0;
        return Math.AbsFloat(diff) <= m_fHalfWidthDeg;
    }
}

// -----------------------------------------------------------------------------
// Morton code helpers (static utilities)
//
// Maps 3D integer coordinates to a 1D Morton (Z-order) code so spatially
// adjacent voxels stay adjacent in memory / map iteration order.
// Improves cache locality when scanning a local neighbourhood of cells.
// Only lower 10 bits of each axis coordinate are packed (sufficient for
// EMFieldInts.EM_CHUNK_DIM=20 which requires 5 bits).
// -----------------------------------------------------------------------------
class EMMorton
{
    // Spread 10 bits of an integer into every 3rd bit position.
    protected static int Part1By2(int x)
    {
        x &= 0x000003ff;
        x  = (x | (x << 16)) & 0xff0000ff;
        x  = (x | (x <<  8)) & 0x0300f00f;
        x  = (x | (x <<  4)) & 0x030c30c3;
        x  = (x | (x <<  2)) & 0x09249249;
        return x;
    }

    // Encode local voxel coords (0..19 per axis) to Morton key.
    static int Encode(int x, int y, int z)
    {
        return Part1By2(x) | (Part1By2(y) << 1) | (Part1By2(z) << 2);
    }

    // Compact every 3rd bit back to a contiguous integer.
    protected static int Compact1By2(int x)
    {
        x &= 0x09249249;
        x  = (x | (x >>  2)) & 0x030c30c3;
        x  = (x | (x >>  4)) & 0x0300f00f;
        x  = (x | (x >>  8)) & 0xff0000ff;
        x  = (x | (x >> 16)) & 0x000003ff;
        return x;
    }

    static int DecodeX(int m) { return Compact1By2(m);       }
    static int DecodeY(int m) { return Compact1By2(m >> 1);  }
    static int DecodeZ(int m) { return Compact1By2(m >> 2);  }
}

// -----------------------------------------------------------------------------
// EMVoxelField  -- Two-Level Chunked Grid, Lazy Rays, Trilinear Interpolation
// -----------------------------------------------------------------------------
class EMVoxelField
{
    // -- Singleton ------------------------------------------------------------
    protected static ref EMVoxelField s_Instance;

    // -- Two-level chunk storage -----------------------------------------------
    // Key: "CX:CY:CZ"  (chunk integer coordinates at EM_MACRO_SIZE resolution)
    protected ref map<string, ref EMChunk> m_Chunks;

    // -- Lazy ray list ---------------------------------------------------------
    protected ref array<ref EMRayDescriptor> m_Rays;

    // -- Decay / budget --------------------------------------------------------
    protected float m_DecayRatePerSec;
    protected int   m_MaxChunkBudget;     // max live chunks (0 = unlimited)
    protected float m_OutOfSectorDecayMul;

    // -- Sector activation -----------------------------------------------------
    protected ref array<ref EMActiveSector> m_ActiveSectors;

    // -- Server mode -----------------------------------------------------------
    protected bool m_ServerMode;

    // -- Float constants (file-scope const float not valid in EnforceScript) ---
    static const float EM_MICRO_SIZE = 10.0;   // metres per micro-voxel
    static const float EM_MACRO_SIZE = 200.0;  // metres per chunk


    // ---------------------------------------------------------------------
    // Constructor / Singleton
    // ---------------------------------------------------------------------

    void EMVoxelField(float decayRatePerSec = 1.0)
    {
        m_Chunks             = new map<string, ref EMChunk>();
        m_Rays               = new array<ref EMRayDescriptor>();
        m_ActiveSectors      = new array<ref EMActiveSector>();
        m_DecayRatePerSec    = Math.Max(decayRatePerSec, 0.0);
        m_MaxChunkBudget     = 256;       // 256 x 8000 cells ~=9.6 MB peak
        m_OutOfSectorDecayMul = 5.0;
        m_ServerMode         = true;
    }

    static EMVoxelField GetInstance()
    {
        if (!s_Instance)
            s_Instance = new EMVoxelField();
        return s_Instance;
    }

    void SetServerMode(bool isServer) { m_ServerMode = isServer; }
    bool IsServerMode()               { return m_ServerMode; }

    // ---------------------------------------------------------------------
    // Chunk coordinate helpers
    // ---------------------------------------------------------------------

    // World pos ->chunk integer coords
    protected void WorldToChunk(vector pos, out int cx, out int cy, out int cz)
    {
        cx = Math.Floor(pos[0] / EM_MACRO_SIZE);
        cy = Math.Floor(pos[1] / EM_MACRO_SIZE);
        cz = Math.Floor(pos[2] / EM_MACRO_SIZE);
    }

    // World pos ->local voxel coords within chunk (0..EMFieldInts.EM_CHUNK_DIM-1)
    protected void WorldToLocal(vector pos, out int lx, out int ly, out int lz)
    {
        float ox = pos[0] - Math.Floor(pos[0] / EM_MACRO_SIZE) * EM_MACRO_SIZE;
        float oy = pos[1] - Math.Floor(pos[1] / EM_MACRO_SIZE) * EM_MACRO_SIZE;
        float oz = pos[2] - Math.Floor(pos[2] / EM_MACRO_SIZE) * EM_MACRO_SIZE;
        lx = Math.Clamp(Math.Floor(ox / EM_MICRO_SIZE), 0, EMFieldInts.EM_CHUNK_DIM - 1);
        ly = Math.Clamp(Math.Floor(oy / EM_MICRO_SIZE), 0, EMFieldInts.EM_CHUNK_DIM - 1);
        lz = Math.Clamp(Math.Floor(oz / EM_MICRO_SIZE), 0, EMFieldInts.EM_CHUNK_DIM - 1);
    }

    // All three in one call
    protected void WorldToChunkLocal(vector pos, out int cx, out int cy, out int cz,
                                                  out int lx, out int ly, out int lz)
    {
        cx = Math.Floor(pos[0] / EM_MACRO_SIZE);
        cy = Math.Floor(pos[1] / EM_MACRO_SIZE);
        cz = Math.Floor(pos[2] / EM_MACRO_SIZE);
        float ox = pos[0] - cx * EM_MACRO_SIZE;
        float oy = pos[1] - cy * EM_MACRO_SIZE;
        float oz = pos[2] - cz * EM_MACRO_SIZE;
        lx = Math.Clamp(Math.Floor(ox / EM_MICRO_SIZE), 0, EMFieldInts.EM_CHUNK_DIM - 1);
        ly = Math.Clamp(Math.Floor(oy / EM_MICRO_SIZE), 0, EMFieldInts.EM_CHUNK_DIM - 1);
        lz = Math.Clamp(Math.Floor(oz / EM_MICRO_SIZE), 0, EMFieldInts.EM_CHUNK_DIM - 1);
    }

    protected string ChunkKey(int cx, int cy, int cz)
    {
        return cx.ToString() + ":" + cy.ToString() + ":" + cz.ToString();
    }

    // Centre of a local voxel in world space
    protected vector LocalToWorld(int cx, int cy, int cz, int lx, int ly, int lz)
    {
        return Vector(
            cx * EM_MACRO_SIZE + lx * EM_MICRO_SIZE + EM_MICRO_SIZE * 0.5,
            cy * EM_MACRO_SIZE + ly * EM_MICRO_SIZE + EM_MICRO_SIZE * 0.5,
            cz * EM_MACRO_SIZE + lz * EM_MICRO_SIZE + EM_MICRO_SIZE * 0.5);
    }

    // Get world time safely
    protected float WorldTime()
    {
        if (GetGame() && GetGame().GetWorld())
            return GetGame().GetWorld().GetWorldTime();
        return 0.0;
    }

    // Fetch (or create) a chunk
    protected EMChunk GetOrCreateChunk(int cx, int cy, int cz)
    {
        string key = ChunkKey(cx, cy, cz);
        if (!m_Chunks.Contains(key))
        {
            // Budget enforcement: evict oldest idle chunk when over limit
            if (m_MaxChunkBudget > 0 && m_Chunks.Count() >= m_MaxChunkBudget)
                EvictOldestChunk();
            m_Chunks.Insert(key, new EMChunk());
        }
        return m_Chunks.Get(key);
    }

    protected void EvictOldestChunk()
    {
        string oldest;
        float  oldestTime = 1e30;
        for (int i = 0; i < m_Chunks.Count(); ++i)
        {
            string k  = m_Chunks.GetKey(i);
            EMChunk c = m_Chunks.Get(k);
            if (c && c.m_fLastModified < oldestTime)
            {
                oldestTime = c.m_fLastModified;
                oldest     = k;
            }
        }
        if (oldest != string.Empty)
            m_Chunks.Remove(oldest);
    }

    // ---------------------------------------------------------------------
    // Occupancy bitmask (Macro-level skip acceleration)
    // Each bit represents one chunk (by array index, not spatially ordered).
    // Used in InjectAlongRay to skip empty macro-chunks during stepping.
    // ---------------------------------------------------------------------
    protected bool ChunkHasEnergy(string key)
    {
        if (!m_Chunks.Contains(key)) return false;
        EMChunk c = m_Chunks.Get(key);
        return c && !c.IsEmpty();
    }

    // ---------------------------------------------------------------------
    // WRITE  -- inject power at a single world position
    // ---------------------------------------------------------------------
    void InjectPowerAt(vector worldPos, float powerW, int freqMask = 0,
                       vector dir = "0 0 0", float ttl = 1.0)
    {
        if (!m_ServerMode || powerW <= 0.0) return;
        int cx, cy, cz, lx, ly, lz;
        WorldToChunkLocal(worldPos, cx, cy, cz, lx, ly, lz);
        EMChunk chunk = GetOrCreateChunk(cx, cy, cz);
        int idx = EMChunk.CellIndex(lx, ly, lz);
        chunk.AddPower(idx, powerW, freqMask, dir[0], dir[1], dir[2], ttl, WorldTime());
    }

    // ---------------------------------------------------------------------
    // WRITE  -- ray-march injection with Temporal-LoD step control
    //
    // Macro Stepping:  skip entire 200 m chunks when the occupancy bitmask
    //                  shows no energy (empty sky  -- zero work).
    // Micro Stepping:  once inside an active/near-target chunk, step at
    //                  EM_MICRO_SIZE x priority multiplier.
    // ---------------------------------------------------------------------
    void InjectAlongRay(vector start, vector dir, float maxRange,
                        float transmitPowerW, int freqMask = 0, float ttl = 1.0,
                        float injectionFraction = 1e-4,
                        float prf = -1.0, float pulseWidth = -1.0,
                        float frequency = -1.0, int waveformType = -1,
                        EMUpdatePriority priority = EMUpdatePriority.MEDIUM)
    {
        if (!m_ServerMode || transmitPowerW <= 0.0 || maxRange <= 0.0) return;

        // Step multipliers per priority
        float microMul;
        switch (priority)
        {
            case EMUpdatePriority.HIGH:   microMul = 1.0; break;
            case EMUpdatePriority.LOW:    microMul = 5.0; break;
            default:                      microMul = 2.0; break;  // MEDIUM
        }
        float microStep = EM_MICRO_SIZE * microMul;
        float macroStep = EM_MACRO_SIZE;

        float totalInjected = transmitPowerW * injectionFraction;
        if (totalInjected <= 0.0) return;

        float dist = 0.0;
        float wt   = WorldTime();

        while (dist < maxRange)
        {
            vector pos = start + dir * dist;

            // -- Macro skip: if this chunk has no energy and is far, jump a full chunk --
            int cx, cy, cz;
            WorldToChunk(pos, cx, cy, cz);
            string ckey = ChunkKey(cx, cy, cz);
            if (!ChunkHasEnergy(ckey) && dist > macroStep)
            {
                dist += macroStep;
                continue;
            }

            // -- Micro injection --
            float contribution = totalInjected * (microStep / maxRange);
            // 1/R^2 weighting (avoid singularity near origin)
            float weight = 1.0 / Math.Max(dist * dist, 1.0);
            float normaliser = totalInjected / Math.Max(maxRange / microStep, 1.0);
            InjectPowerAt(pos, weight * normaliser, freqMask, dir, ttl);

            // Record pulse descriptor where requested
            if (prf > 0.0)
            {
                int lx, ly, lz;
                WorldToLocal(pos, lx, ly, lz);
                EMChunk chunk = GetOrCreateChunk(cx, cy, cz);
                chunk.RecordPulse(EMChunk.CellIndex(lx, ly, lz), wt, pulseWidth, frequency, waveformType);
            }

            dist += microStep;
        }
    }

    // Convenience: push a lazy ray descriptor (zero write cost at inject time).
    // Passive sensors evaluate this list when they call GetPowerAt().
    void PushRay(vector origin, vector dir, float maxRange,
                 float transmitPowerW, int freqMask = 0, float ttlSec = 1.0,
                 int waveformType = -1, float frequency = 0.0, float pulseWidth = 0.0,
                 float beamHalfAngleDeg = 5.0)
    {
        if (!m_ServerMode || transmitPowerW <= 0.0) return;
        EMRayDescriptor r = new EMRayDescriptor();
        r.m_vOrigin        = origin;
        r.m_vDir           = dir.Normalized();
        r.m_fMaxRange      = maxRange;
        r.m_fPowerW        = transmitPowerW;
        r.m_iFreqMask      = freqMask;
        r.m_fExpiryTime    = WorldTime() + ttlSec;
        r.m_iWaveformType  = waveformType;
        r.m_fFrequency     = frequency;
        r.m_fPulseWidth    = pulseWidth;
        r.m_fBeamHalfAngle = beamHalfAngleDeg;
        m_Rays.Insert(r);
        // Bound the ray list to prevent unbounded growth
        while (m_Rays.Count() > 512) m_Rays.Remove(0);
    }

    void InjectReflection(vector pos, float reflectedPowerW, int freqMask = 0,
                          vector dir = "0 0 0", float ttl = 1.0)
    {
        InjectPowerAt(pos, reflectedPowerW, freqMask, dir, ttl);
    }

    // ---------------------------------------------------------------------
    // READ  -- power at a world position
    //
    // Returns the sum of:
    //   (a) Stored voxel power with trilinear interpolation over 8 neighbours.
    //   (b) Lazy ray contribution  -- each live EMRayDescriptor evaluated at pos.
    // ---------------------------------------------------------------------
    float GetPowerAt(vector worldPos, out vector outMainDir, out int outFreqMask)
    {
        outMainDir  = Vector(0, 0, 0);
        outFreqMask = 0;

        float wt = WorldTime();

        // -- (a) Trilinear interpolation over 8 micro-voxel neighbours --------
        // Sample point offset within its voxel (fractional part, 0 -- )
        int cx, cy, cz, lx, ly, lz;
        WorldToChunkLocal(worldPos, cx, cy, cz, lx, ly, lz);

        // Fractional position within the voxel
        float ox = worldPos[0] - (cx * EM_MACRO_SIZE + lx * EM_MICRO_SIZE);
        float oy = worldPos[1] - (cy * EM_MACRO_SIZE + ly * EM_MICRO_SIZE);
        float oz = worldPos[2] - (cz * EM_MACRO_SIZE + lz * EM_MICRO_SIZE);
        float tx = Math.Clamp(ox / EM_MICRO_SIZE, 0.0, 1.0);
        float ty = Math.Clamp(oy / EM_MICRO_SIZE, 0.0, 1.0);
        float tz = Math.Clamp(oz / EM_MICRO_SIZE, 0.0, 1.0);

        float storedPower = 0.0;
        // Iterate 2x2x2 neighbours
        for (int dz = 0; dz <= 1; ++dz)
        {
            for (int dy = 0; dy <= 1; ++dy)
            {
                for (int dx = 0; dx <= 1; ++dx)
                {
                    // Trilinear weight
                    float wx; if (dx == 0) wx = 1.0 - tx; else wx = tx;
                    float wy; if (dy == 0) wy = 1.0 - ty; else wy = ty;
                    float wz; if (dz == 0) wz = 1.0 - tz; else wz = tz;
                    float w  = wx * wy * wz;
                    if (w < 1e-6) continue;

                    // Neighbour local coords (may cross chunk boundary)
                    int nlx = lx + dx;
                    int nly = ly + dy;
                    int nlz = lz + dz;
                    int ncx = cx, ncy = cy, ncz = cz;
                    if (nlx >= EMFieldInts.EM_CHUNK_DIM) { nlx -= EMFieldInts.EM_CHUNK_DIM; ncx++; }
                    if (nly >= EMFieldInts.EM_CHUNK_DIM) { nly -= EMFieldInts.EM_CHUNK_DIM; ncy++; }
                    if (nlz >= EMFieldInts.EM_CHUNK_DIM) { nlz -= EMFieldInts.EM_CHUNK_DIM; ncz++; }

                    string nkey = ChunkKey(ncx, ncy, ncz);
                    if (!m_Chunks.Contains(nkey)) continue;
                    EMChunk nc = m_Chunks.Get(nkey);
                    if (!nc) continue;

                    int nidx = EMChunk.CellIndex(nlx, nly, nlz);
                    float p = nc.GetPower(nidx);
                    storedPower += p * w;

                    if (dx == 0 && dy == 0 && dz == 0)
                    {
                        // Main cell: capture direction and freq mask
                        outMainDir  = nc.GetDir(nidx);
                        outFreqMask = nc.GetFreqMask(nidx);
                    }
                }
            }
        }

        // -- (b) Lazy ray evaluation ----------------------------------------
        float rayPower = 0.0;
        for (int ri = m_Rays.Count() - 1; ri >= 0; --ri)
        {
            EMRayDescriptor ray = m_Rays.Get(ri);
            if (!ray || ray.IsExpired(wt))
            {
                m_Rays.Remove(ri);
                continue;
            }
            float contrib = ray.EvaluateAt(worldPos);
            rayPower += contrib;
            if (contrib > storedPower * 0.1 && outFreqMask == 0)
                outFreqMask = ray.m_iFreqMask;
        }

        return storedPower + rayPower;
    }

    // ---------------------------------------------------------------------
    // TICK  -- decay, sector pruning, chunk eviction
    // ---------------------------------------------------------------------
    void TickDecay(float dt)
    {
        if (dt <= 0.0) return;
        float wt = WorldTime();

        // 1. Expire stale active sectors
        bool hasSectors = false;
        if (m_ActiveSectors && m_ActiveSectors.Count() > 0)
        {
            for (int si = m_ActiveSectors.Count() - 1; si >= 0; --si)
            {
                EMActiveSector sec = m_ActiveSectors.Get(si);
                if (!sec || sec.m_fExpiryTime < wt)
                    m_ActiveSectors.Remove(si);
            }
            hasSectors = m_ActiveSectors.Count() > 0;
        }

        // 2. Expire stale lazy rays
        for (int ri = m_Rays.Count() - 1; ri >= 0; --ri)
        {
            EMRayDescriptor r = m_Rays.Get(ri);
            if (!r || r.IsExpired(wt)) m_Rays.Remove(ri);
        }

        // 3. Tick every active chunk
        array<string> toRemove = new array<string>();
        for (int ci = 0; ci < m_Chunks.Count(); ++ci)
        {
            string key   = m_Chunks.GetKey(ci);
            EMChunk chunk = m_Chunks.Get(key);
            if (!chunk) { toRemove.Insert(key); continue; }

            // Determine if this chunk overlaps any active sector
            // (use chunk world-centre for a single point test)
            array<string> parts = {};
            key.Split(":", parts, false);
            bool inSector = true;
            if (hasSectors && parts.Count() >= 3)
            {
                int ccx = parts.Get(0).ToInt();
                int ccy = parts.Get(1).ToInt();
                int ccz = parts.Get(2).ToInt();
                vector centre = Vector(
                    ccx * EM_MACRO_SIZE + EM_MACRO_SIZE * 0.5,
                    ccy * EM_MACRO_SIZE + EM_MACRO_SIZE * 0.5,
                    ccz * EM_MACRO_SIZE + EM_MACRO_SIZE * 0.5);
                inSector = IsInActiveSector(centre);
            }

            bool empty = chunk.TickDecay(dt, m_DecayRatePerSec, m_OutOfSectorDecayMul, inSector);
            if (empty) toRemove.Insert(key);
        }
        for (int ri2 = 0; ri2 < toRemove.Count(); ++ri2)
            m_Chunks.Remove(toRemove[ri2]);
    }

    // ---------------------------------------------------------------------
    // Sector activation API (Phase 5  -- unchanged signatures)
    // ---------------------------------------------------------------------
    void RegisterActiveSector(vector origin, float azimuthDeg, float halfWidthDeg,
                               float range, float sectorTTL = 2.0)
    {
        if (!m_ActiveSectors)
            m_ActiveSectors = new array<ref EMActiveSector>();
        float expiry = WorldTime() + sectorTTL;
        m_ActiveSectors.Insert(new EMActiveSector(origin, azimuthDeg, halfWidthDeg, range, expiry));
        while (m_ActiveSectors.Count() > 64) m_ActiveSectors.Remove(0);
    }

    bool IsInActiveSector(vector worldPos)
    {
        if (!m_ActiveSectors || m_ActiveSectors.Count() == 0) return true;
        foreach (EMActiveSector s : m_ActiveSectors)
        {
            if (s && s.ContainsPoint(worldPos)) return true;
        }
        return false;
    }

    int GetActiveSectorCount()
    {
        if (!m_ActiveSectors) return 0;
        return m_ActiveSectors.Count();
    }

    void SetSectorParams(float outOfSectorDecayMul, int maxChunkBudget)
    {
        m_OutOfSectorDecayMul = Math.Max(outOfSectorDecayMul, 1.0);
        m_MaxChunkBudget      = Math.Max(maxChunkBudget, 0);
    }

    // ---------------------------------------------------------------------
    // legacy MarkSectorActive  -- preserved for backward compatibility
    // ---------------------------------------------------------------------
    void MarkSectorActive(vector origin, float azimuthDeg, float halfWidthDeg,
                          float range, float activityTTL)
    {
        RegisterActiveSector(origin, azimuthDeg, halfWidthDeg, range, activityTTL);
    }

    // ---------------------------------------------------------------------
    // Signal descriptor read
    // ---------------------------------------------------------------------
    bool GetSignalDescriptorAt(vector worldPos,
                                out float outEstPRF, out float outLastPulseWidth,
                                out int outWaveformType, out float outLastFrequency)
    {
        outEstPRF        = 0.0;
        outLastPulseWidth = 0.0;
        outWaveformType  = -1;
        outLastFrequency = 0.0;

        int cx, cy, cz, lx, ly, lz;
        WorldToChunkLocal(worldPos, cx, cy, cz, lx, ly, lz);
        string key = ChunkKey(cx, cy, cz);
        if (!m_Chunks.Contains(key)) return false;
        EMChunk chunk = m_Chunks.Get(key);
        if (!chunk) return false;

        EMCellSignalDescriptor desc = chunk.GetDescriptor(EMChunk.CellIndex(lx, ly, lz));
        if (!desc) return false;

        outEstPRF         = desc.EstimatePRF();
        outLastPulseWidth = desc.LastPulseWidth();
        outWaveformType   = desc.m_iWaveform;
        outLastFrequency  = desc.LastFrequency();
        return true;
    }

    // ---------------------------------------------------------------------
    // GetActiveCellCount  -- totals across all active chunks
    // ---------------------------------------------------------------------
    int GetActiveCellCount()
    {
        int total = 0;
        for (int i = 0; i < m_Chunks.Count(); ++i)
        {
            EMChunk c = m_Chunks.Get(m_Chunks.GetKey(i));
            if (c) total += c.m_iNonZero;
        }
        return total;
    }

    int GetActiveChunkCount() { return m_Chunks.Count(); }

    // ---------------------------------------------------------------------
    // GetTopCells  -- iterate chunks, collect non-zero cells within radius
    // ---------------------------------------------------------------------
    void GetTopCells(vector center, float radius, int maxCount,
                     out array<vector> outPositions, out array<float> outPowers)
    {
        outPositions = new array<vector>();
        outPowers    = new array<float>();
        if (maxCount <= 0 || radius <= 0.0) return;

        array<vector> cand    = new array<vector>();
        array<float>  candPow = new array<float>();

        for (int ci = 0; ci < m_Chunks.Count(); ++ci)
        {
            string key  = m_Chunks.GetKey(ci);
            EMChunk chunk = m_Chunks.Get(key);
            if (!chunk || chunk.IsEmpty()) continue;

            array<string> parts = {};
            key.Split(":", parts, false);
            if (parts.Count() < 3) continue;
            int ccx = parts.Get(0).ToInt();
            int ccy = parts.Get(1).ToInt();
            int ccz = parts.Get(2).ToInt();

            // Quick chunk AABB vs radius cull
            vector chunkMin = Vector(ccx * EM_MACRO_SIZE, ccy * EM_MACRO_SIZE, ccz * EM_MACRO_SIZE);
            vector chunkMax = chunkMin + Vector(EM_MACRO_SIZE, EM_MACRO_SIZE, EM_MACRO_SIZE);
            vector closest  = Vector(
                Math.Clamp(center[0], chunkMin[0], chunkMax[0]),
                Math.Clamp(center[1], chunkMin[1], chunkMax[1]),
                Math.Clamp(center[2], chunkMin[2], chunkMax[2]));
            if ((closest - center).Length() > radius) continue;

            // Iterate voxels within this chunk
            for (int lz = 0; lz < EMFieldInts.EM_CHUNK_DIM; ++lz)
            {
                for (int ly = 0; ly < EMFieldInts.EM_CHUNK_DIM; ++ly)
                {
                    for (int lx = 0; lx < EMFieldInts.EM_CHUNK_DIM; ++lx)
                    {
                        int idx = EMChunk.CellIndex(lx, ly, lz);
                        float p = chunk.GetPower(idx);
                        if (p <= 0.0) continue;

                        vector world = LocalToWorld(ccx, ccy, ccz, lx, ly, lz);
                        if ((world - center).Length() > radius) continue;

                        cand.Insert(world);
                        candPow.Insert(p);
                    }
                }
            }
        }

        int n = Math.Min(cand.Count(), maxCount);
        if (n == 0) return;

        // Partial descending sort by power
        for (int s = 0; s < n; ++s)
        {
            int best = s;
            for (int t = s + 1; t < candPow.Count(); ++t)
                if (candPow.Get(t) > candPow.Get(best)) best = t;
            vector tv = cand.Get(s);   cand.Set(s, cand.Get(best));     cand.Set(best, tv);
            float  tp = candPow.Get(s); candPow.Set(s, candPow.Get(best)); candPow.Set(best, tp);
        }
        for (int i = 0; i < n; ++i)
        {
            outPositions.Insert(cand.Get(i));
            outPowers.Insert(candPow.Get(i));
        }
    }

    // ---------------------------------------------------------------------
    // Jammer injection  -- backward-compatible signature
    // ---------------------------------------------------------------------
    void InjectJammer(vector center, float powerW, float radius,
                      int freqMask = 0, float ttl = 1.0, float sampleStep = -1.0)
    {
        if (powerW <= 0.0 || radius <= 0.0) return;
        float step;
        if (sampleStep > 0.0) step = sampleStep; else step = EM_MICRO_SIZE;
        float rSq   = radius * radius;

        int xi0 = Math.Floor((center[0] - radius) / EM_MICRO_SIZE);
        int yi0 = Math.Floor((center[1] - radius) / EM_MICRO_SIZE);
        int zi0 = Math.Floor((center[2] - radius) / EM_MICRO_SIZE);
        int xi1 = Math.Floor((center[0] + radius) / EM_MICRO_SIZE);
        int yi1 = Math.Floor((center[1] + radius) / EM_MICRO_SIZE);
        int zi1 = Math.Floor((center[2] + radius) / EM_MICRO_SIZE);

        array<vector> samples = new array<vector>();
        for (int x = xi0; x <= xi1; ++x)
        for (int y = yi0; y <= yi1; ++y)
        for (int z = zi0; z <= zi1; ++z)
        {
            vector wc = Vector(x * EM_MICRO_SIZE + EM_MICRO_SIZE * 0.5,
                               y * EM_MICRO_SIZE + EM_MICRO_SIZE * 0.5,
                               z * EM_MICRO_SIZE + EM_MICRO_SIZE * 0.5);
            vector rel = wc - center;
            if (rel[0]*rel[0] + rel[1]*rel[1] + rel[2]*rel[2] <= rSq)
                samples.Insert(wc);
        }

        if (samples.Count() == 0) return;
        float perCell = powerW / samples.Count();
        foreach (vector pos : samples)
            InjectPowerAt(pos, perCell, freqMask, Vector(0, 0, 0), ttl);
    }

    // ---------------------------------------------------------------------
    // Debug draw  -- color-mapped spheres per active MICRO-voxel
    // ---------------------------------------------------------------------
    static int PowerToDebugColor(float powerW)
    {
        if (powerW <= 0.0) return ARGB(80, 80, 80, 160);
        float dBm = 10.0 * Math.Log10(powerW * 1000.0);
        if (dBm < -90.0) return ARGB(160,  50,  50, 220);
        if (dBm < -60.0) return ARGB(180,  50, 200,  50);
        if (dBm < -30.0) return ARGB(200, 230, 200,  30);
                         return ARGB(220, 255,  50,  50);
    }

    void DebugDrawAllCells(float duration = 1.0)
    {
        DebugDrawColorMapped(200, duration);
    }

    void DebugDrawColorMapped(int maxCells = 200, float duration = 0.1)
    {
        if (m_Chunks.Count() == 0) return;

        // Collect candidates
        array<vector> cand    = new array<vector>();
        array<float>  candPow = new array<float>();
        array<vector> candDir = new array<vector>();

        for (int ci = 0; ci < m_Chunks.Count() && cand.Count() < maxCells * 4; ++ci)
        {
            string key   = m_Chunks.GetKey(ci);
            EMChunk chunk = m_Chunks.Get(key);
            if (!chunk || chunk.IsEmpty()) continue;
            array<string> parts = {};
            key.Split(":", parts, false);
            if (parts.Count() < 3) continue;
            int ccx = parts.Get(0).ToInt();
            int ccy = parts.Get(1).ToInt();
            int ccz = parts.Get(2).ToInt();

            for (int lz = 0; lz < EMFieldInts.EM_CHUNK_DIM; ++lz)
            for (int ly = 0; ly < EMFieldInts.EM_CHUNK_DIM; ++ly)
            for (int lx = 0; lx < EMFieldInts.EM_CHUNK_DIM; ++lx)
            {
                int   idx = EMChunk.CellIndex(lx, ly, lz);
                float p   = chunk.GetPower(idx);
                if (p <= 0.0) continue;
                cand.Insert(LocalToWorld(ccx, ccy, ccz, lx, ly, lz));
                candPow.Insert(p);
                candDir.Insert(chunk.GetDir(idx));
            }
        }

        int n = Math.Min(cand.Count(), maxCells);
        if (n == 0) return;

        // Top-n by power
        for (int s = 0; s < n; ++s)
        {
            int best = s;
            for (int t = s + 1; t < candPow.Count(); ++t)
                if (candPow.Get(t) > candPow.Get(best)) best = t;
            vector tv = cand.Get(s);    cand.Set(s, cand.Get(best));     cand.Set(best, tv);
            float  tp = candPow.Get(s); candPow.Set(s, candPow.Get(best)); candPow.Set(best, tp);
            vector td = candDir.Get(s); candDir.Set(s, candDir.Get(best)); candDir.Set(best, td);
        }

        float baseR   = 4.0;   // 10 m voxel ->4 m sphere radius
        float dirDotR = 1.2;

        for (int i = 0; i < n; ++i)
        {
            vector world = cand.Get(i);
            float  p     = candPow.Get(i);
            vector d     = candDir.Get(i);

            // Debug.DrawSphere not in EnforceScript; use Shape or SCR_DebugManager if needed
            // Debug.DrawSphere(world, baseR, PowerToDebugColor(p), duration);

            if (d.LengthSq() > 0.01)
            {
                vector tip = world + d.Normalized() * (baseR + dirDotR * 2.0);
                // Debug.DrawSphere(tip, dirDotR, ARGB(200, 255, 255, 255), duration);
            }

            if (IsInActiveSector(world))
            {
                // Debug.DrawSphere(world, baseR * 1.2, ARGB(55, 80, 255, 80), duration);
            }
        }
    }

    void Clear()
    {
        m_Chunks.Clear();
        m_Rays.Clear();
    }
}
