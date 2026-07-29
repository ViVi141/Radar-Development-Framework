// Attach next to RDF_RadarComponent on a vehicle / turret.
// Weapon scripts: FindComponent(RDF_RadarWeaponComponent) then TryGetFireSolution / GuideRocket.
[ComponentEditorProps(
    category: "GameScripted/RDF",
    description: "Radar fire-control bridge: lock aim for weapons / guided rockets")]
class RDF_RadarWeaponComponentClass : ScriptComponentClass
{
}

class RDF_RadarWeaponComponent : ScriptComponent
{
    protected ref RDF_RadarWeaponBridge m_Bridge;
    protected ref RDF_RadarFireSolution m_CachedSolution;
    protected bool m_RequireTrackingForFire;
    protected bool m_PreferArmAim;

    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        m_RequireTrackingForFire = true;
        m_PreferArmAim = true;
        m_Bridge = new RDF_RadarWeaponBridge();
        m_CachedSolution = new RDF_RadarFireSolution();
        Rebind(owner);
    }

    void SetRequireTrackingForFire(bool requireTracking)
    {
        m_RequireTrackingForFire = requireTracking;
        if (m_Bridge)
            m_Bridge.SetRequireTrackingForFire(requireTracking);
    }

    void SetPreferArmAim(bool preferArm)
    {
        m_PreferArmAim = preferArm;
        if (m_Bridge)
            m_Bridge.SetPreferArmAim(preferArm);
    }

    void Rebind(IEntity owner)
    {
        if (!m_Bridge)
            m_Bridge = new RDF_RadarWeaponBridge();
        m_Bridge.SetRequireTrackingForFire(m_RequireTrackingForFire);
        m_Bridge.SetPreferArmAim(m_PreferArmAim);
        if (!owner)
            owner = GetOwner();
        m_Bridge.BindFromOwner(owner);
    }

    RDF_RadarWeaponBridge GetBridge()
    {
        if (!m_Bridge)
            Rebind(GetOwner());
        return m_Bridge;
    }

    bool CanAuthorizeFire()
    {
        RDF_RadarWeaponBridge bridge = GetBridge();
        if (!bridge)
            return false;
        return bridge.CanAuthorizeFire();
    }

    bool TryGetFireSolution(out RDF_RadarFireSolution solution)
    {
        RDF_RadarWeaponBridge bridge = GetBridge();
        if (!bridge)
        {
            if (!solution)
                solution = new RDF_RadarFireSolution();
            solution.Clear();
            return false;
        }
        return bridge.TryGetFireSolution(solution);
    }

    // Cache last solution for HUD / debug without reallocating every caller's local.
    RDF_RadarFireSolution GetCachedSolution()
    {
        if (!m_CachedSolution)
            m_CachedSolution = new RDF_RadarFireSolution();
        TryGetFireSolution(m_CachedSolution);
        return m_CachedSolution;
    }

    bool TryGetMidcourseAim(out vector aimPos, out vector aimVel, out IEntity target)
    {
        RDF_RadarWeaponBridge bridge = GetBridge();
        if (!bridge)
        {
            aimPos = "0 0 0";
            aimVel = "0 0 0";
            target = null;
            return false;
        }
        return bridge.TryGetMidcourseAim(aimPos, aimVel, target);
    }

    // Drive an in-flight rocket with ARH sample guidance + live midcourse uplink.
    // Returns false if no aim or MissileMoveComponent missing.
    bool GuideRocket(
        IEntity rocket,
        RDF_RadarRocketGuidanceState state,
        float dt,
        float nowS)
    {
        if (!rocket || !state)
            return false;

        vector aimPos;
        vector aimVel;
        IEntity target;
        if (!TryGetMidcourseAim(aimPos, aimVel, target))
            return false;

        return RDF_RadarRocketGuidance.Update(rocket, aimPos, aimVel, dt, nowS, state);
    }

    string GetStatusShort()
    {
        RDF_RadarWeaponBridge bridge = GetBridge();
        if (!bridge)
            return "FIRE NOBRIDGE";
        return bridge.GetStatusShort();
    }

    // Convenience: resolve from any entity that owns this component.
    static RDF_RadarWeaponComponent FindOn(IEntity owner)
    {
        if (!owner)
            return null;
        RDF_RadarWeaponComponent comp = RDF_RadarWeaponComponent.Cast(
            owner.FindComponent(RDF_RadarWeaponComponent));
        if (comp)
            return comp;
        IEntity parent = owner.GetParent();
        if (!parent)
            return null;
        return RDF_RadarWeaponComponent.Cast(parent.FindComponent(RDF_RadarWeaponComponent));
    }
}
