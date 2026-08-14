// Phased-array dwell / resource management scheduler (TODO §9 S1).
// Offline mirror: tools/dem/rdf_radar_dwell.py — keep behaviour identical
// (the Python golden test is the drift guard for this class).
//
// A phased-array radar shares a finite beam-time budget between competing
// dwells: SEARCH (discover), TRACK (revisit), FIRE_CONTROL (locked target).
// Scheduling rule: class priority (fire-control > track > search), then
// earliest deadline first (EDF), hard budget cap. Skipped tasks whose deadline
// has passed are reported as late so the caller may coast / degrade them.

enum ERDF_DwellKind
{
    RDF_DWELL_SEARCH,
    RDF_DWELL_TRACK,
    RDF_DWELL_FIRE_CONTROL
}

// One dwell task competing for beam-time budget.
class RDF_DwellTask
{
    ERDF_DwellKind m_Kind;
    int m_TaskId;
    float m_PeriodS;
    float m_DwellMs;
    float m_DeadlineS;
    float m_LastScheduledS = -1000000000.0;
}

// Earliest-deadline-first with class-priority dominance, budget-capped.
class RDF_DwellScheduler
{
    protected static const float ONE_SHOT_DEADLINE_S = 1000000000.0;
    protected float m_BudgetMs;

    void RDF_DwellScheduler(float budgetMs = 0.0)
    {
        m_BudgetMs = budgetMs;
    }

    void SetBudgetMs(float budgetMs)
    {
        m_BudgetMs = budgetMs;
    }

    float GetBudgetMs()
    {
        return m_BudgetMs;
    }

    // Class priority: fire-control > track > search (hard dominance).
    static int ClassPriority(ERDF_DwellKind kind)
    {
        if (kind == ERDF_DwellKind.RDF_DWELL_FIRE_CONTROL)
            return 2;
        if (kind == ERDF_DwellKind.RDF_DWELL_TRACK)
            return 1;
        return 0;
    }

    // Schedule within budget. Scheduled tasks land in outScheduled (execution
    // order); skipped tasks whose deadline passed land in outLate (deadline miss).
    void Plan(
        notnull array<ref RDF_DwellTask> tasks,
        float nowS,
        notnull array<ref RDF_DwellTask> outScheduled,
        notnull array<ref RDF_DwellTask> outLate)
    {
        outScheduled.Clear();
        outLate.Clear();

        // Copy, then selection sort by (priority desc, deadline asc, id asc).
        // Small N; mirrors RDF_RadarProjectileTracker pair-cost sort.
        array<ref RDF_DwellTask> ordered = new array<ref RDF_DwellTask>();
        for (int i = 0; i < tasks.Count(); i++)
        {
            if (tasks.Get(i))
                ordered.Insert(tasks.Get(i));
        }
        for (int a = 0; a < ordered.Count(); a++)
        {
            int best = a;
            for (int b = a + 1; b < ordered.Count(); b++)
            {
                if (TaskLess(ordered.Get(b), ordered.Get(best)))
                    best = b;
            }
            if (best != a)
            {
                RDF_DwellTask tmp = ordered.Get(a);
                ordered.Set(a, ordered.Get(best));
                ordered.Set(best, tmp);
            }
        }

        float spent = 0.0;
        for (int k = 0; k < ordered.Count(); k++)
        {
            RDF_DwellTask t = ordered.Get(k);
            if (spent + t.m_DwellMs > m_BudgetMs)
            {
                if (t.m_DeadlineS <= nowS)
                    outLate.Insert(t);
                continue;
            }
            outScheduled.Insert(t);
            spent = spent + t.m_DwellMs;
        }
    }

    // Mark serviced; next revisit is service-relative (deadline = now + period).
    static void Advance(RDF_DwellTask task, float nowS)
    {
        if (!task)
            return;
        task.m_LastScheduledS = nowS;
        if (task.m_PeriodS <= 0.0)
        {
            task.m_DeadlineS = ONE_SHOT_DEADLINE_S;
            return;
        }
        task.m_DeadlineS = nowS + task.m_PeriodS;
    }

    // True when lhs should sort before rhs.
    protected static bool TaskLess(RDF_DwellTask lhs, RDF_DwellTask rhs)
    {
        int lp = ClassPriority(lhs.m_Kind);
        int rp = ClassPriority(rhs.m_Kind);
        if (lp != rp)
            return lp > rp;
        if (lhs.m_DeadlineS != rhs.m_DeadlineS)
            return lhs.m_DeadlineS < rhs.m_DeadlineS;
        return lhs.m_TaskId < rhs.m_TaskId;
    }
}
