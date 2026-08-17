// Optional receiver-side EW effects. Effects contribute processed noise power;
// the scanner remains independent of concrete jammer types.
class RDF_RadarEwEffect
{
    float GetAdditionalNoisePowerW(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware)
    {
        return 0.0;
    }

    void CollectFalsePlots(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware,
        float worldTimeS,
        notnull array<ref RDF_RadarFalsePlot> outPlots)
    {
    }
}

class RDF_RadarFalsePlot
{
    float m_RangeM;
    float m_AzimuthDeg;
    float m_ElevationDeg;
    float m_PowerW;
    float m_RangeRateMs;
    // World time (seconds) when this false plot was created — range walk-off
    // must be measured from creation, not from absolute world time zero.
    float m_StartTimeS = -1.0;
}

// Burn-through under noise jamming: Reff = R0 · (N / (N+J))^(1/4).
// thermalNoiseW and jamNoiseW must be in the same domain (RF or both post-PG).
class RDF_RadarEwBurnThrough
{
    static float RangeM(
        float cleanInstrumentedRangeM,
        float thermalNoiseW,
        float jamNoiseW)
    {
        float r0 = cleanInstrumentedRangeM;
        if (r0 < 1.0)
            r0 = 1.0;
        float n = thermalNoiseW;
        if (n < 0.0)
            n = 0.0;
        float j = jamNoiseW;
        if (j < 0.0)
            j = 0.0;
        float denom = n + j;
        if (denom <= 0.0)
            return r0;
        float factor = n / denom;
        if (factor <= 0.0)
            return 0.0;
        if (factor >= 1.0)
            return r0;
        return r0 * Math.Pow(factor, 0.25);
    }
}

class RDF_RadarNoiseJammerEffect : RDF_RadarEwEffect
{
    bool m_Enabled = true;
    vector m_Position;
    float m_ErpW = 10000.0;
    float m_BandwidthHz = 5000000.0;
    // Default SEARCH_AVG + deeper sidelobe: playable partial suppression for
    // rotating search. Use ConfigurePhysicsBeam() for stare / hard fidelity.
    ERDF_NoiseJamCoupling m_CouplingMode = ERDF_NoiseJamCoupling.RDF_JAM_COUPLE_SEARCH_AVG;
    float m_SidelobeLevelDb = -40.0;
    // Scales received jammer power after coupling (gameplay soft knob).
    float m_CouplingGain = 1.0;
    // SEARCH_AVG duty override; <0 → az_beamwidth_deg / 360.
    float m_SearchDutyOverride = -1.0;
    // Sidelobe blanking: drop sidelobe-only jam coupling (default off — keeps
    // SEARCH_AVG soft path; enable for harder anti-jam).
    bool m_EnableSlb = false;

    // Instantaneous beam coupling (legacy hard path).
    void ConfigurePhysicsBeam(float sidelobeLevelDb = -25.0)
    {
        m_CouplingMode = ERDF_NoiseJamCoupling.RDF_JAM_COUPLE_BEAM;
        m_SidelobeLevelDb = sidelobeLevelDb;
        m_CouplingGain = 1.0;
        m_SearchDutyOverride = -1.0;
    }

    // Rotating-search average coupling (default soft path).
    void ConfigureGameplaySearchAvg(
        float sidelobeLevelDb = -40.0,
        float couplingGain = 1.0)
    {
        m_CouplingMode = ERDF_NoiseJamCoupling.RDF_JAM_COUPLE_SEARCH_AVG;
        m_SidelobeLevelDb = sidelobeLevelDb;
        m_CouplingGain = couplingGain;
        m_SearchDutyOverride = -1.0;
    }

    // Only inject when the scan beam points at the jammer.
    void ConfigureMainlobeOnly()
    {
        m_CouplingMode = ERDF_NoiseJamCoupling.RDF_JAM_COUPLE_MAINLOBE_ONLY;
        m_CouplingGain = 1.0;
        m_SearchDutyOverride = -1.0;
    }

    void EnableSlb(bool enable)
    {
        m_EnableSlb = enable;
    }

    // 0..1: 1 = jammer fully in the mainlobe, 0 = fully in a sidelobe. Used by
    // the ECCM decision layer to pick sidelobe blanking vs frequency agility.
    // SEARCH_AVG has no instantaneous boresight, so it returns the scan duty
    // (the fraction of time the beam points at the jammer).
    float GetMainlobeFraction(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware)
    {
        if (m_CouplingMode == ERDF_NoiseJamCoupling.RDF_JAM_COUPLE_SEARCH_AVG)
        {
            float duty = m_SearchDutyOverride;
            if (duty < 0.0)
            {
                float beam = hardware.m_AzimuthBeamwidthDeg;
                if (beam < 0.1)
                    beam = 0.1;
                duty = beam / 360.0;
            }
            if (duty < 0.0)
                duty = 0.0;
            if (duty > 1.0)
                duty = 1.0;
            return duty;
        }

        vector delta = m_Position - radarOrigin;
        float rangeM = delta.Length();
        if (rangeM < 1.0)
            rangeM = 1.0;
        vector direction = delta * (1.0 / rangeM);
        float dot = scanForward[0] * direction[0]
            + scanForward[1] * direction[1]
            + scanForward[2] * direction[2];
        float halfBeamRad = hardware.m_AzimuthBeamwidthDeg * 0.5 * 0.01745329;
        if (dot >= Math.Cos(halfBeamRad))
            return 1.0;
        return 0.0;
    }

    protected float ResolveCoupling(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware)
    {
        float sideLin = RDF_RadarClutterModel.DbToLin(m_SidelobeLevelDb);
        if (sideLin < 0.0)
            sideLin = 0.0;

        if (m_CouplingMode == ERDF_NoiseJamCoupling.RDF_JAM_COUPLE_SEARCH_AVG)
        {
            float duty = m_SearchDutyOverride;
            if (duty < 0.0)
            {
                float beam = hardware.m_AzimuthBeamwidthDeg;
                if (beam < 0.1)
                    beam = 0.1;
                duty = beam / 360.0;
            }
            if (duty < 0.0)
                duty = 0.0;
            if (duty > 1.0)
                duty = 1.0;
            if (m_EnableSlb)
                return duty;
            return duty * 1.0 + (1.0 - duty) * sideLin;
        }

        vector delta = m_Position - radarOrigin;
        float rangeM = delta.Length();
        if (rangeM < 1.0)
            rangeM = 1.0;
        vector direction = delta * (1.0 / rangeM);
        float dot = scanForward[0] * direction[0]
            + scanForward[1] * direction[1]
            + scanForward[2] * direction[2];
        float halfBeamRad = hardware.m_AzimuthBeamwidthDeg * 0.5 * 0.01745329;
        float mainThreshold = Math.Cos(halfBeamRad);
        bool inMain = false;
        if (dot >= mainThreshold)
            inMain = true;

        if (m_CouplingMode == ERDF_NoiseJamCoupling.RDF_JAM_COUPLE_MAINLOBE_ONLY)
        {
            if (inMain)
                return 1.0;
            return 0.0;
        }

        // BEAM (default legacy).
        if (inMain)
            return 1.0;
        if (m_EnableSlb)
            return 0.0;
        return sideLin;
    }

    override float GetAdditionalNoisePowerW(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware)
    {
        if (!m_Enabled || !hardware || m_ErpW <= 0.0)
            return 0.0;
        if (m_CouplingGain <= 0.0)
            return 0.0;

        vector delta = m_Position - radarOrigin;
        float rangeM = delta.Length();
        if (rangeM < 1.0)
            rangeM = 1.0;

        float coupling = ResolveCoupling(radarOrigin, scanForward, hardware);
        if (coupling <= 0.0)
            return 0.0;

        float jammerBandwidth = Math.Max(1.0, m_BandwidthHz);
        float receiverBandwidth = Math.Max(1.0, hardware.m_BandwidthHz);
        float overlap = Math.Min(jammerBandwidth, receiverBandwidth);
        float fluxDensity = m_ErpW / (
            RDF_RadarClutterModel.FOUR_PI
            * rangeM
            * rangeM
            * jammerBandwidth);
        float wavelength = hardware.GetWavelengthM();
        float aperture = RDF_RadarClutterModel.DbToLin(
            hardware.m_AntennaGainDbi)
            * wavelength
            * wavelength
            / RDF_RadarClutterModel.FOUR_PI;
        return fluxDensity * aperture * overlap * coupling * m_CouplingGain;
    }
}

class RDF_RadarEwStack
{
    ref array<ref RDF_RadarEwEffect> m_Effects;

    void RDF_RadarEwStack()
    {
        m_Effects = new array<ref RDF_RadarEwEffect>();
    }

    void Add(RDF_RadarEwEffect effect)
    {
        if (effect)
            m_Effects.Insert(effect);
    }

    void Clear()
    {
        m_Effects.Clear();
    }

    float GetAdditionalNoisePowerW(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware)
    {
        float total = 0.0;
        for (int i = 0; i < m_Effects.Count(); i++)
        {
            RDF_RadarEwEffect effect = m_Effects.Get(i);
            if (effect)
            {
                total = total + effect.GetAdditionalNoisePowerW(
                    radarOrigin,
                    scanForward,
                    hardware);
            }
        }
        return total;
    }

    void CollectFalsePlots(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware,
        float worldTimeS,
        notnull array<ref RDF_RadarFalsePlot> outPlots)
    {
        for (int i = 0; i < m_Effects.Count(); i++)
        {
            RDF_RadarEwEffect effect = m_Effects.Get(i);
            if (!effect)
                continue;
            effect.CollectFalsePlots(
                radarOrigin,
                scanForward,
                hardware,
                worldTimeS,
                outPlots);
        }
    }

    // Worst-case mainlobe fraction across noise jammers (for ECCM decision).
    // Returns 1.0 when no noise jammer is active.
    float GetMinMainlobeFraction(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware)
    {
        float minFraction = 1.0;
        bool any = false;
        for (int i = 0; i < m_Effects.Count(); i++)
        {
            RDF_RadarNoiseJammerEffect jammer = RDF_RadarNoiseJammerEffect.Cast(
                m_Effects.Get(i));
            if (!jammer || !jammer.m_Enabled)
                continue;
            any = true;
            float fraction = jammer.GetMainlobeFraction(
                radarOrigin,
                scanForward,
                hardware);
            if (fraction < minFraction)
                minFraction = fraction;
        }
        if (!any)
            return 1.0;
        return minFraction;
    }

    // Toggle sidelobe blanking on every noise jammer (ECCM decision output).
    void SetSlbEnabled(bool enable)
    {
        for (int i = 0; i < m_Effects.Count(); i++)
        {
            RDF_RadarNoiseJammerEffect jammer = RDF_RadarNoiseJammerEffect.Cast(
                m_Effects.Get(i));
            if (jammer)
                jammer.EnableSlb(enable);
        }
    }
}

class RDF_RadarDeceptionJammerEffect : RDF_RadarEwEffect
{
    bool m_Enabled = true;
    ref array<ref RDF_RadarFalsePlot> m_FalsePlots;
    // World time (seconds) when the effect started producing plots. Plots added
    // without an explicit start time walk from here (so a static AddFalsePlot at
    // config time does not walk with absolute world time and fly out of range).
    float m_EffectStartTimeS = -1.0;

    void RDF_RadarDeceptionJammerEffect()
    {
        m_FalsePlots = new array<ref RDF_RadarFalsePlot>();
    }

    void AddFalsePlot(
        float rangeM,
        float azimuthDeg,
        float powerW,
        float rangeRateMs = 0.0,
        float elevationDeg = 0.0,
        float startTimeS = -1.0)
    {
        RDF_RadarFalsePlot p = new RDF_RadarFalsePlot();
        p.m_RangeM = rangeM;
        p.m_AzimuthDeg = azimuthDeg;
        p.m_ElevationDeg = elevationDeg;
        p.m_PowerW = powerW;
        p.m_RangeRateMs = rangeRateMs;
        p.m_StartTimeS = startTimeS;
        m_FalsePlots.Insert(p);
    }

    override void CollectFalsePlots(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware,
        float worldTimeS,
        notnull array<ref RDF_RadarFalsePlot> outPlots)
    {
        if (!m_Enabled || !m_FalsePlots)
            return;

        // Lazily anchor the effect's walk-off start on first production (the
        // effect is usually created at config time, before any scan runs).
        if (m_EffectStartTimeS < 0.0)
            m_EffectStartTimeS = worldTimeS;

        for (int i = 0; i < m_FalsePlots.Count(); i++)
        {
            RDF_RadarFalsePlot src = m_FalsePlots.Get(i);
            if (!src)
                continue;
            // Walk-off is measured from the plot's creation time (or the effect
            // start), not from absolute world time zero — a plot created at
            // t=300 s with an 18 m/s rate must sit at range+18*(t-300), not
            // range+18*t, which would be filtered by the scan range gate and
            // silently kill the effect mid-mission.
            float tRel = worldTimeS - m_EffectStartTimeS;
            if (src.m_StartTimeS >= 0.0)
                tRel = worldTimeS - src.m_StartTimeS;
            if (tRel < 0.0)
                tRel = 0.0;
            RDF_RadarFalsePlot dst = new RDF_RadarFalsePlot();
            dst.m_AzimuthDeg = src.m_AzimuthDeg;
            dst.m_ElevationDeg = src.m_ElevationDeg;
            dst.m_PowerW = src.m_PowerW;
            dst.m_RangeRateMs = src.m_RangeRateMs;
            dst.m_StartTimeS = src.m_StartTimeS;
            dst.m_RangeM = src.m_RangeM + src.m_RangeRateMs * tRel;
            if (dst.m_RangeM < 1.0)
                continue;
            outPlots.Insert(dst);
        }
    }
}

// Range walk-off / RGPO-style deception: false range walks outbound (or inbound).
class RDF_RadarRangeWalkOffEffect : RDF_RadarEwEffect
{
    bool m_Enabled = true;
    float m_BaseRangeM = 1200.0;
    float m_AzimuthDeg = 0.0;
    float m_ElevationDeg = 0.0;
    float m_PowerW = 0.000000000001;
    float m_RangeRateMs = 80.0;
    float m_StartTimeS = 0.0;
    float m_MaxRangeM = 8000.0;
    float m_MinRangeM = 50.0;

    override void CollectFalsePlots(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware,
        float worldTimeS,
        notnull array<ref RDF_RadarFalsePlot> outPlots)
    {
        if (!m_Enabled || m_PowerW <= 0.0)
            return;

        float tRel = worldTimeS - m_StartTimeS;
        if (tRel < 0.0)
            tRel = 0.0;
        float rangeM = m_BaseRangeM + m_RangeRateMs * tRel;
        if (rangeM < m_MinRangeM)
            return;
        if (rangeM > m_MaxRangeM)
            return;

        RDF_RadarFalsePlot p = new RDF_RadarFalsePlot();
        p.m_RangeM = rangeM;
        p.m_AzimuthDeg = m_AzimuthDeg;
        p.m_ElevationDeg = m_ElevationDeg;
        p.m_PowerW = m_PowerW;
        p.m_RangeRateMs = m_RangeRateMs;
        outPlots.Insert(p);
    }
}

// Angle scintillation: azimuth jitters around a base bearing each dwell.
class RDF_RadarAngleScintillationEffect : RDF_RadarEwEffect
{
    bool m_Enabled = true;
    float m_RangeM = 1500.0;
    float m_BaseAzimuthDeg = 0.0;
    float m_ElevationDeg = 0.0;
    float m_PowerW = 0.000000000001;
    float m_AzimuthJitterDeg = 3.0;
    float m_RangeRateMs = 0.0;
    float m_PhaseRad = 0.0;
    float m_JitterRateRadPerS = 4.5;

    override void CollectFalsePlots(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware,
        float worldTimeS,
        notnull array<ref RDF_RadarFalsePlot> outPlots)
    {
        if (!m_Enabled || m_PowerW <= 0.0)
            return;
        if (m_RangeM < 1.0)
            return;

        float phase = m_PhaseRad + worldTimeS * m_JitterRateRadPerS;
        float jitter = Math.Sin(phase) * m_AzimuthJitterDeg;

        RDF_RadarFalsePlot p = new RDF_RadarFalsePlot();
        p.m_RangeM = m_RangeM;
        p.m_AzimuthDeg = m_BaseAzimuthDeg + jitter;
        p.m_ElevationDeg = m_ElevationDeg;
        p.m_PowerW = m_PowerW;
        p.m_RangeRateMs = m_RangeRateMs;
        outPlots.Insert(p);
    }
}

// Intermittent false plot: only present for a duty-cycle fraction of each period.
class RDF_RadarIntermittentFalsePlotEffect : RDF_RadarEwEffect
{
    bool m_Enabled = true;
    float m_RangeM = 1800.0;
    float m_AzimuthDeg = 10.0;
    float m_ElevationDeg = 0.0;
    float m_PowerW = 0.000000000001;
    float m_RangeRateMs = 0.0;
    float m_PeriodS = 2.0;
    float m_DutyCycle = 0.35;
    float m_PhaseS = 0.0;

    override void CollectFalsePlots(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware,
        float worldTimeS,
        notnull array<ref RDF_RadarFalsePlot> outPlots)
    {
        if (!m_Enabled || m_PowerW <= 0.0)
            return;
        if (m_RangeM < 1.0)
            return;

        float period = m_PeriodS;
        if (period < 0.05)
            period = 0.05;
        float duty = m_DutyCycle;
        if (duty < 0.0)
            duty = 0.0;
        if (duty > 1.0)
            duty = 1.0;

        float phase = worldTimeS + m_PhaseS;
        float frac = phase / period;
        frac = frac - Math.Floor(frac);
        if (frac < 0.0)
            frac = frac + 1.0;
        if (frac > duty)
            return;

        RDF_RadarFalsePlot p = new RDF_RadarFalsePlot();
        p.m_RangeM = m_RangeM;
        p.m_AzimuthDeg = m_AzimuthDeg;
        p.m_ElevationDeg = m_ElevationDeg;
        p.m_PowerW = m_PowerW;
        p.m_RangeRateMs = m_RangeRateMs;
        outPlots.Insert(p);
    }
}
