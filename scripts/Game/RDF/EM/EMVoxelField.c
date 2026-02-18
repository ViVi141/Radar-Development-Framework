/**
 * EM Voxel Field — minimal PoC implementation (Phase 1)
 * - Sparse voxel storage using map<string, ref EMVoxelCell>
 * - World ↔ voxel index mapping
 * - TTL decay tick
 * - Basic APIs: InjectPowerAt, GetPowerAt, TickDecay, DebugDraw
 *
 * Notes:
 * - Keep implementation lightweight for server-side authoritative use.
 * - Cell key format: "x:y:z:cellSize" (integers)
 */

class EMVoxelCell
{
    float m_fTotalPower;      // linear watts
    int   m_iFrequencyMask;   // bitmask of bands
    vector m_vMainDir;
    float m_fNoiseFloor;      // linear watts

    // TTL (seconds) — when <= 0 cell should be removed
    float m_fTTL;

    void EMVoxelCell()
    {
        m_fTotalPower = 0.0;
        m_iFrequencyMask = 0;
        m_vMainDir = Vector(0,0,0);
        m_fNoiseFloor = 1e-12; // very small baseline (W)
        m_fTTL = 0.0;
    }

    void AddPower(float powerW, int freqMask, vector dir, float ttl)
    {
        m_fTotalPower += Math.Max(powerW, 0.0);
        m_iFrequencyMask |= freqMask;
        if (dir.Length() > 0.0)
            m_vMainDir = (m_vMainDir + dir).Normalized();
        m_fTTL = Math.Max(m_fTTL, ttl);
    }

    float GetPower()
    {
        return m_fTotalPower;
    }

    void TickDecay(float dt, float decayRatePerSec)
    {
        if (m_fTTL > 0.0)
            m_fTTL -= dt;

        // exponential-ish decay to avoid abrupt zeroing
        float factor = Math.Exp(-decayRatePerSec * dt);
        m_fTotalPower *= factor;
        if (m_fTotalPower < 1e-15)
            m_fTotalPower = 0.0;
    }

    bool IsExpired()
    {
        return m_fTTL <= 0.0 && m_fTotalPower <= 0.0;
    }
}

class EMVoxelField
{
    // singleton-like usage via static getter
    protected static ref EMVoxelField s_Instance;

    // sparse storage: key -> EMVoxelCell
    protected ref map<string, ref EMVoxelCell> m_Cells;

    // cellSize meters (uniform for PoC); future: LOD layers
    protected float m_CellSize;

    // decay parameters
    protected float m_DecayRatePerSec; // >0 : exponential decay

    void EMVoxelField(float cellSize = 50.0, float decayRatePerSec = 1.0)
    {
        m_Cells = new map<string, ref EMVoxelCell>();
        m_CellSize = Math.Max(cellSize, 1.0);
        m_DecayRatePerSec = Math.Max(decayRatePerSec, 0.0);
    }

    static EMVoxelField GetInstance()
    {
        if (!s_Instance)
            s_Instance = new EMVoxelField();
        return s_Instance;
    }

    // Utility: world position -> integer voxel coords
    protected vector GetVoxelCoord(vector pos)
    {
        int xi = Math.Floor(pos[0] / m_CellSize);
        int yi = Math.Floor(pos[1] / m_CellSize);
        int zi = Math.Floor(pos[2] / m_CellSize);
        return Vector(xi, yi, zi);
    }

    protected string MakeKeyFromCoord(vector coord)
    {
        int xi = coord[0];
        int yi = coord[1];
        int zi = coord[2];
        return xi.ToString() + ":" + yi.ToString() + ":" + zi.ToString() + ":" + Math.Round(m_CellSize).ToString();
    }

    // Basic injection API: add power (W) at world position into corresponding voxel
    void InjectPowerAt(vector worldPos, float powerW, int freqMask = 0, vector dir = null, float ttl = 1.0)
    {
        vector coord = GetVoxelCoord(worldPos);
        string key = MakeKeyFromCoord(coord);
        EMVoxelCell cell;
        if (!m_Cells.Contains(key))
        {
            cell = new EMVoxelCell();
            m_Cells.Insert(key, cell);
        }
        else
        {
            cell = m_Cells.Get(key);
        }

        if (!dir)
            dir = Vector(0,0,0);

        cell.AddPower(powerW, freqMask, dir, ttl);
    }

    // Inject power distributed along a ray (ray-marching PoC).
    // - start: world origin
    // - dir: normalized direction
    // - maxRange: length to march
    // - transmitPowerW: transmitter power (used only to scale injected energy)
    // - freqMask: frequency bitmask
    // - ttl: cell ttl (s)
    // - step: march step (m). If <= 0 uses half cell size.
    // - injectionFraction: fraction of transmitPower distributed into voxels (PoC tuning)
    void InjectAlongRay(vector start, vector dir, float maxRange, float transmitPowerW, int freqMask = 0, float ttl = 1.0, float step = -1.0, float injectionFraction = 1e-4)
    {
        if (step <= 0.0)
            step = Math.Max(m_CellSize * 0.5, 1.0);
        int steps = Math.Ceil(maxRange / step);
        if (steps <= 0)
            return;

        // accumulate 1/r^2 weights to normalize distribution
        array<float> weights = new array<float>(steps);
        float sumW = 0.0;
        for (int i = 0; i < steps; ++i)
        {
            float dist = (i + 0.5) * step;
            float w = 1.0 / (dist * dist + 1.0); // avoid singularity at r=0
            weights.InsertAt(i, w);
            sumW += w;
        }

        float totalInjected = transmitPowerW * injectionFraction;
        if (totalInjected <= 0.0 || sumW <= 0.0)
            return;

        for (int j = 0; j < steps; ++j)
        {
            float dist = (j + 0.5) * step;
            vector pos = start + (dir * dist);
            float contribution = (weights[j] / sumW) * totalInjected;
            InjectPowerAt(pos, contribution, freqMask, dir, ttl);
        }
    }

    // Simple wrapper for reflection / returned echo energy into a single voxel.
    void InjectReflection(vector pos, float reflectedPowerW, int freqMask = 0, vector dir = null, float ttl = 1.0)
    {
        InjectPowerAt(pos, reflectedPowerW, freqMask, dir, ttl);
    }

    // Read API: returns powerW (linear). Also outputs approximate mainDir and freqMask.
    float GetPowerAt(vector worldPos, out vector outMainDir, out int outFreqMask)
    {
        outMainDir = Vector(0,0,0);
        outFreqMask = 0;
        vector coord = GetVoxelCoord(worldPos);
        string key = MakeKeyFromCoord(coord);
        if (!m_Cells.Contains(key))
            return 0.0;
        EMVoxelCell cell = m_Cells.Get(key);
        outMainDir = cell.m_vMainDir;
        outFreqMask = cell.m_iFrequencyMask;
        return cell.GetPower();
    }

    // Tick to decay cells and purge expired ones
    void TickDecay(float dt)
    {
        if (dt <= 0.0)
            return;

        array<string> toRemove = new array<string>();
        for (int i = 0; i < m_Cells.Count(); ++i)
        {
            string k = m_Cells.GetKey(i);
            EMVoxelCell c = m_Cells.Get(k);
            c.TickDecay(dt, m_DecayRatePerSec);
            if (c.IsExpired())
                toRemove.Insert(k);
        }
        for (int j = 0; j < toRemove.Count(); ++j)
            m_Cells.Remove(toRemove[j]);
    }

    int GetActiveCellCount()
    {
        return m_Cells.Count();
    }

    // Debug draw: draw a small sphere per active cell (server only; cheap PoC)
    void DebugDrawAllCells(float duration = 1.0)
    {
        for (int i = 0; i < m_Cells.Count(); ++i)
        {
            string k = m_Cells.GetKey(i);
            // parse key back to coord
            array<string> parts = {}; k.Split(":", parts);
            if (parts.Count() < 4) continue;
            int xi = parts[0].ToInt();
            int yi = parts[1].ToInt();
            int zi = parts[2].ToInt();
            vector world = Vector(xi * m_CellSize + m_CellSize*0.5, yi * m_CellSize + m_CellSize*0.5, zi * m_CellSize + m_CellSize*0.5);
            EMVoxelCell cell = m_Cells.Get(k);
            float mag = Math.Log10(cell.m_fTotalPower + 1e-12) + 12.0; // small mapping to 0..12
            mag = Math.Clamp(mag, 0.1, 12.0);
            // engine debug draw: DrawSphere is used in other modules; fall back to Debug.DrawSphere if available
            Debug.DrawSphere(world, Math.Min(m_CellSize*0.4, 10.0), Color.FromARGB(180, 255, 64, 64), duration);
        }
    }

    // Simple clearing (test support)
    void Clear()
    {
        m_Cells.Clear();
    }

    // Inject a jammer source into nearby voxels (uniform distribution over sampled voxels).
    void InjectJammer(vector center, float powerW, float radius, int freqMask = 0, float ttl = 1.0, float sampleStep = -1.0)
    {
        if (powerW <= 0.0 || radius <= 0.0)
            return;
        if (sampleStep <= 0.0)
            sampleStep = m_CellSize;

        int xi0 = Math.Floor((center[0] - radius) / m_CellSize);
        int yi0 = Math.Floor((center[1] - radius) / m_CellSize);
        int zi0 = Math.Floor((center[2] - radius) / m_CellSize);
        int xi1 = Math.Floor((center[0] + radius) / m_CellSize);
        int yi1 = Math.Floor((center[1] + radius) / m_CellSize);
        int zi1 = Math.Floor((center[2] + radius) / m_CellSize);

        array<vector> samples = new array<vector>();
        for (int x = xi0; x <= xi1; ++x)
        {
            for (int y = yi0; y <= yi1; ++y)
            {
                for (int z = zi0; z <= zi1; ++z)
                {
                    vector wc = Vector(x * m_CellSize + m_CellSize * 0.5, y * m_CellSize + m_CellSize * 0.5, z * m_CellSize + m_CellSize * 0.5);
                    float d = (wc - center).Length();
                    if (d <= radius)
                        samples.Insert(wc);
                }
            }
        }

        if (samples.Count() == 0)
            return;

        float perCell = powerW / samples.Count();
        for (int i = 0; i < samples.Count(); ++i)
            InjectPowerAt(samples[i], perCell, freqMask, Vector(0,0,0), ttl);
    }
}
