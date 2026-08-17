// Adaptive budget governor: the server's real main-thread headroom drives the
// per-tick LOS trace budget and the per-scan WLR solve budget, so a busy
// server degrades smoothly (fewer traces / solves) instead of hitching, and an
// idle server spends its spare frame time on fresher tracks.
//
// Signal: each ScanOnce reports its wall-clock cost. The governor reads the
// server's actual frame time via System.GetFPS() (10-frame average) and
// computes utilisation
//     frameMs  = 1000 / GetFPS()             (server frame budget)
//     util     = emaScanMs / (frameMs * m_AdaptiveTargetScanFraction)
// i.e. how much of ONE frame's budget the scan consumes. Overloaded
// (util > 1.0) → scale budgets down by m_AdaptiveStepDown (with hysteresis so
// one noisy frame does not thrash); idle (util < 0.25, scan well under ~5% of a
// frame) → scale up by m_AdaptiveStepUp, clamped to the configured min/max.
// Budgets are written back into the shared RDF_RadarSettings so the Scanner
// (per Scan) and Tracker (per ScanOnce ConfigureFromSettings) pick them up
// immediately.
//
// Note: the denominator is the *server frame* (16.7 ms @ 60 tick, 8.3 ms @ 120
// tick), not the scan interval. A scan runs every m_UpdateInterval (e.g. 200 ms)
// but must stay a small fraction of a single frame or that frame hitches.
class RDF_RadarBudgetGovernor
{
    // ---- config (copied from RDF_RadarSettings at Update) ----
    protected bool m_Enabled;
    protected float m_TargetScanFraction;
    protected float m_EmaAlpha;
    protected int m_LosMinPerTick;
    protected int m_LosMaxPerTick;
    protected int m_WlrMinSolves;
    protected int m_WlrMaxSolves;
    protected float m_StepDown;
    protected float m_StepUp;

    // ---- state ----
    protected bool m_HasBaseline;
    protected float m_EmaScanMs;
    protected float m_EmaFrameMs;
    protected int m_LosBudget;
    protected int m_WlrBudget;

    // Fallback frame time when GetFPS() is unavailable or degenerate (60 tick).
    protected static const float FALLBACK_FRAME_MS = 16.7;
    // GetFPS() < this is treated as degenerate.
    protected static const float MIN_FPS = 5.0;

    // Reset damping / budgets (e.g. after Configure with new settings).
    void Reset()
    {
        m_HasBaseline = false;
        m_EmaScanMs = 0.0;
        m_EmaFrameMs = 0.0;
        m_LosBudget = 0;
        m_WlrBudget = 0;
    }

    void Configure(RDF_RadarSettings settings)
    {
        if (!settings)
            return;
        m_Enabled = settings.m_EnableAdaptiveBudget;
        m_TargetScanFraction = settings.m_AdaptiveTargetScanFraction;
        m_EmaAlpha = settings.m_AdaptiveEmaAlpha;
        m_LosMinPerTick = settings.m_AdaptiveLosMinPerTick;
        m_LosMaxPerTick = settings.m_AdaptiveLosMaxPerTick;
        m_WlrMinSolves = settings.m_AdaptiveWlrMinSolves;
        m_WlrMaxSolves = settings.m_AdaptiveWlrMaxSolves;
        m_StepDown = settings.m_AdaptiveStepDown;
        m_StepUp = settings.m_AdaptiveStepUp;
        if (!m_HasBaseline)
        {
            m_LosBudget = settings.m_LosTracesPerTick;
            m_WlrBudget = settings.m_WeaponLocateSolvesPerScan;
        }
    }

    bool IsEnabled()
    {
        return m_Enabled;
    }

    // Server frame time in ms (damped). Reads System.GetFPS() (10-frame
    // average); falls back to 16.7 ms when degenerate.
    protected float ResolveFrameMs()
    {
        float fps = System.GetFPS();
        if (fps < MIN_FPS)
            return FALLBACK_FRAME_MS;
        float frameMs = 1000.0 / fps;
        if (frameMs < 1.0)
            frameMs = 1.0;
        return frameMs;
    }

    // Feed one scan sample and let the governor adjust settings budgets.
    // scanWallMs: wall clock of the last ScanOnce (ms).
    void Update(RDF_RadarSettings settings, float scanWallMs)
    {
        if (!m_Enabled || !settings)
            return;
        if (m_TargetScanFraction <= 0.0)
            return;

        float frameMs = ResolveFrameMs();

        if (!m_HasBaseline)
        {
            m_HasBaseline = true;
            m_EmaScanMs = scanWallMs;
            m_EmaFrameMs = frameMs;
            m_LosBudget = settings.m_LosTracesPerTick;
            m_WlrBudget = settings.m_WeaponLocateSolvesPerScan;
            return; // first sample: only baseline, no decision yet
        }

        // Damped EMA (defends against one-hit spikes).
        m_EmaScanMs = m_EmaScanMs * (1.0 - m_EmaAlpha) + scanWallMs * m_EmaAlpha;
        m_EmaFrameMs = m_EmaFrameMs * (1.0 - m_EmaAlpha) + frameMs * m_EmaAlpha;
        if (m_EmaFrameMs < 1.0)
            m_EmaFrameMs = 1.0;

        float budgetMs = m_EmaFrameMs * m_TargetScanFraction;
        if (budgetMs < 1.0)
            budgetMs = 1.0;
        float util = m_EmaScanMs / budgetMs;

        // Hysteresis: only react when clearly outside the working band, so a
        // single slow frame (or fast frame) does not flip budgets back/forth.
        // Overload at util > 1.0 (scan consumes more than its share of a frame).
        // Idle-raise only when util < 0.25 — with m_AdaptiveTargetScanFraction
        // = 0.3 that means scanMs must stay below ~7.5% of a frame before we
        // spend more. The 0.25..1.0 band holds, so a scene that is already
        // fully detected does not ratchet the LOS budget to the max and waste
        // TraceMoves; real overload still sheds.
        if (util > 1.0)
        {
            // Overloaded: shed load.
            int los = Math.Floor(m_LosBudget * m_StepDown);
            if (los < m_LosMinPerTick)
                los = m_LosMinPerTick;
            m_LosBudget = los;

            int wlr = Math.Floor(m_WlrBudget * m_StepDown);
            if (wlr < m_WlrMinSolves)
                wlr = m_WlrMinSolves;
            m_WlrBudget = wlr;
        }
        else if (util < 0.25)
        {
            // Clearly idle: spend spare time on fresher tracks.
            int los = Math.Ceil(m_LosBudget * m_StepUp);
            if (los > m_LosMaxPerTick)
                los = m_LosMaxPerTick;
            m_LosBudget = los;

            int wlr = Math.Ceil(m_WlrBudget * m_StepUp);
            if (wlr > m_WlrMaxSolves)
                wlr = m_WlrMaxSolves;
            m_WlrBudget = wlr;
        }
        // else: inside the working band → keep current budgets.

        // Write back so Scanner (per Scan) and Tracker (ConfigureFromSettings)
        // pick the new budgets up immediately.
        settings.m_LosTracesPerTick = m_LosBudget;
        settings.m_WeaponLocateSolvesPerScan = m_WlrBudget;
    }

    int GetLosBudget()
    {
        return m_LosBudget;
    }

    int GetWlrBudget()
    {
        return m_WlrBudget;
    }
}
