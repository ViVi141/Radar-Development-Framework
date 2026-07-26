// Packed scan-pass args. Enforce limits methods to 16 parameters; packing
// avoids that ceiling and keeps mutable budgets (LOS / fresh updates) shared.
class RDF_RadarScanPassContext
{
    IEntity m_Subject;
    BaseWorld m_World;
    vector m_Origin;
    vector m_Forward;
    float m_Range;
    float m_WorldTime;
    float m_WallTime;
    float m_MinDist;
    float m_MinDistSq;
    float m_RangeSq;
    float m_CosHalfAngle;
    int m_MaxTargets;
    ref TraceParam m_Param;
    int m_LosBudget;
    int m_LosUsed;
    int m_FreshBudget;
}
