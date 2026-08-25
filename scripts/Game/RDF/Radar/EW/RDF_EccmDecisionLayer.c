// ECCM decision layer (TODO §9 S3).
// Offline mirror: tools/dem/rdf_radar_eccm.py — keep behaviour identical.
//
// Maps per-dwell EW observables (jammer-to-noise, sidelobe coupling, deception
// count, lock state) to ECCM responses (sidelobe blanking, PRF agility,
// frequency agility, burn-through). Noise-jam detection is hysteresis-guarded
// so the action set does not flip-flop at the threshold boundary.

class RDF_EccmDecisionLayer
{
    protected float m_JnOnDb;
    protected float m_JnOffDb;
    protected float m_SidelobeCouplingOn;
    protected bool m_JamActive;
    protected bool m_PrfActive;
    // Dwell-count hysteresis for PRF agility. A flickering deceptionCount
    // (0/1 every other dwell) would otherwise flip PRF agility on/off every
    // dwell. Keep PRF active until deception has been clear for this many
    // consecutive dwells.
    protected int m_PrfClearDwells;
    protected static const int PRF_CLEAR_DWELLS = 3;

    void RDF_EccmDecisionLayer(
        float jnOnDb = 6.0,
        float jnHysteresisDb = 2.0,
        float sidelobeCouplingOn = 0.3)
    {
        Configure(jnOnDb, jnHysteresisDb, sidelobeCouplingOn);
    }

    // Idempotent: sets thresholds only. Must NOT reset hysteresis state —
    // RunEccmDecision re-applies thresholds every scan.
    void Configure(
        float jnOnDb,
        float jnHysteresisDb,
        float sidelobeCouplingOn)
    {
        m_JnOnDb = jnOnDb;
        m_JnOffDb = jnOnDb - jnHysteresisDb;
        m_SidelobeCouplingOn = sidelobeCouplingOn;
    }

    // Hysteresis on noise-jam detection (sticky on/off). Within an active jam,
    // the sidelobe-coupling ratio selects SLB (sidelobe jam) vs frequency
    // agility (mainlobe jam). Deception triggers PRF agility, released when it
    // clears. A locked target under jam triggers burn-through.
    void Decide(
        float jnDb,
        float sidelobeCoupling,
        int deceptionCount,
        bool locked,
        out bool outEnableSlb,
        out bool outPrfAgility,
        out bool outFreqAgility,
        out bool outBurnThrough)
    {
        if (jnDb >= m_JnOnDb)
            m_JamActive = true;
        else if (jnDb < m_JnOffDb)
            m_JamActive = false;

        // PRF agility with dwell hysteresis: turn on immediately when any
        // deception plot is seen, but only turn off after PRF_CLEAR_DWELLS
        // consecutive clear dwells. Without this, a single flickering
        // deception plot flips PRF agility every dwell.
        if (deceptionCount > 0)
        {
            m_PrfActive = true;
            m_PrfClearDwells = 0;
        }
        else
        {
            m_PrfClearDwells = m_PrfClearDwells + 1;
            if (m_PrfClearDwells >= PRF_CLEAR_DWELLS)
                m_PrfActive = false;
        }

        outEnableSlb = false;
        outPrfAgility = m_PrfActive;
        outFreqAgility = false;
        outBurnThrough = false;
        if (m_JamActive)
        {
            if (sidelobeCoupling >= m_SidelobeCouplingOn)
                outEnableSlb = true;
            else
                outFreqAgility = true;
            if (locked)
                outBurnThrough = true;
        }
    }

    void Reset()
    {
        m_JamActive = false;
        m_PrfActive = false;
        m_PrfClearDwells = 0;
    }

    string GetStatusShort()
    {
        if (!m_JamActive && !m_PrfActive)
            return "eccm=0";
        string s = "eccm";
        if (m_JamActive)
            s = s + " jam";
        if (m_PrfActive)
            s = s + " prf";
        return s;
    }
}
