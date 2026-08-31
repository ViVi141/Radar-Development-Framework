// Demo: official Computer_E_01 family as RTTexture clutter-surface display.
// Default prefab: RDF Computer_E_01_on_RT (inherits vanilla on + MaterialAssign RT screen).
// Also accepts vanilla Computer_E_01_on / Monitor_on / off and similar — runtime remap then.
[ComponentEditorProps(
    category: "GameScripted/RDF",
    description: "Range–az clutter surface on Computer_E_01_on / Monitor_on screen")]
class RDF_RadarClutterSurfaceDemoClass : ScriptComponentClass
{
}

class RDF_RadarClutterSurfaceDemo : ScriptComponent
{
    // Vanilla Computer_E_01 family (from data.pak resource DB).
    static const ResourceName VANILLA_COMPUTER_ON =
        "{A9DC69EC1CE466A0}Prefabs/Props/Civilian/Computer_E_01/Computer_E_01_on.et";
    static const ResourceName VANILLA_COMPUTER_OFF =
        "{BB7BC5E4B18C594E}Prefabs/Props/Civilian/Computer_E_01/Computer_E_01_off.et";
    static const ResourceName VANILLA_MONITOR_ON =
        "{2595F6426CDFDBBB}Prefabs/Props/Civilian/Computer_E_01/Computer_E_01_Monitor_on.et";
    static const ResourceName VANILLA_MONITOR_OFF =
        "{DF4B249BE5C403E2}Prefabs/Props/Civilian/Computer_E_01/Computer_E_01_Monitor_off.et";

    // RDF wrappers: same meshes, Computer_Screen_On → $rendertarget via MaterialAssign.
    static const ResourceName COMPUTER_ON_RT_PREFAB =
        "{D7A31F04C8E26B91}Prefabs/RDF/ClutterSurface/Computer_E_01_on_RT.et";
    static const ResourceName MONITOR_ON_RT_PREFAB =
        "{D7A31F04C8E26B90}Prefabs/RDF/ClutterSurface/Computer_E_01_Monitor_on_RT.et";

    static const ResourceName SCREEN_RT_MATERIAL =
        "{C1A84F02B7D35E91}RadarData/ClutterSurface/Computer_Screen_RT.emat";

    [Attribute("1", UIWidgets.CheckBox, "On init: spawn official computer and bind clutter surface")]
    protected bool m_bAutoStart;

    [Attribute("1", UIWidgets.CheckBox, "Spawn Computer_E_01_on (or Attribute prefab) in front of subject")]
    protected bool m_bSpawnOfficialComputer;

    [Attribute(
        "{D7A31F04C8E26B91}Prefabs/RDF/ClutterSurface/Computer_E_01_on_RT.et",
        UIWidgets.ResourceNamePicker,
        "Display prefab: Computer_E_01_on_RT / Monitor_on_RT / vanilla Computer_E_01_on|off|Monitor_on|off",
        "et")]
    protected ResourceName m_ComputerPrefab;

    [Attribute("1.6", UIWidgets.EditBox, "Spawn distance in front of subject (m)")]
    protected float m_fComputerDistanceM;

    [Attribute("1.15", UIWidgets.EditBox, "Height above player origin (m) — chest/eye; not ground")]
    protected float m_fSpawnHeightM;

    [Attribute("1", UIWidgets.EditBox, "Computer_E_01 / Monitor scale (1 = stock / original size)")]
    protected float m_fComputerScale;

    [Attribute("0", UIWidgets.CheckBox, "Fallback: workspace 2D preview if computer spawn/bind fails")]
    protected bool m_bWorkspacePreviewFallback;

    [Attribute("0", UIWidgets.CheckBox, "Bind RTTexture to this entity mesh instead of spawning a computer")]
    protected bool m_bUseOwnerAsScreenMesh;

    protected ref RDF_RadarClutterSurfaceScreen m_Screen;
    protected RDF_RadarSensor m_Sensor;
    protected IEntity m_ComputerRoot;
    protected IEntity m_MonitorMesh;
    protected bool m_Active;
    protected bool m_OwnSensorTick;

    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        SetEventMask(owner, EntityEvent.FRAME);
        if (m_bAutoStart)
            GetGame().GetCallqueue().CallLater(Start, 500, false);
    }

    override void EOnFrame(IEntity owner, float timeSlice)
    {
        if (!m_Active || !m_Screen)
            return;

        if (m_OwnSensorTick && m_Sensor)
        {
            IEntity subject = ResolveSubject();
            if (subject)
                m_Sensor.Tick(subject, null);
        }

        if (m_Sensor)
            m_Screen.Update();
    }

    override void OnDelete(IEntity owner)
    {
        Stop();
        super.OnDelete(owner);
    }

    void Start()
    {
        if (m_Active)
            return;

        m_Sensor = ResolveSensor();
        if (!m_Sensor)
        {
            Print("[RDF CAS Demo] no sensor — starting AutoRunner SEARCH", LogLevel.WARNING);
            RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateDefault(64));
            RDF_RadarAutoRunner.SetDemoEnabled(true);
            m_Sensor = RDF_RadarAutoRunner.GetSensor();
            m_OwnSensorTick = false;
        }
        if (!m_Sensor)
        {
            Print("[RDF CAS Demo] failed to obtain RDF_RadarSensor", LogLevel.ERROR);
            return;
        }

        m_Sensor.EnableClutterSurface(true);
        m_Sensor.SetEnabled(true);

        if (!m_Screen)
            m_Screen = new RDF_RadarClutterSurfaceScreen();

        bool ok = false;
        if (m_bUseOwnerAsScreenMesh)
        {
            RemapScreenMaterials(GetOwner(), SCREEN_RT_MATERIAL);
            m_MonitorMesh = GetOwner();
            ok = m_Screen.EnableScreen(m_MonitorMesh, m_Sensor);
        }
        else if (m_bSpawnOfficialComputer)
        {
            ok = BindOfficialComputer(m_Sensor);
        }

        if (!ok && m_bWorkspacePreviewFallback)
        {
            Print("[RDF CAS Demo] falling back to workspace preview", LogLevel.WARNING);
            ok = m_Screen.EnablePreview(m_Sensor);
        }

        if (!ok)
        {
            Print("[RDF CAS Demo] failed to enable screen", LogLevel.ERROR);
            m_Sensor.EnableClutterSurface(false);
            return;
        }

        m_Active = true;
        Print("[RDF CAS Demo] clutter surface on Computer_E_01 family (call Stop / leave to tear down)", LogLevel.NORMAL);
    }

    void Stop()
    {
        if (m_Screen)
        {
            m_Screen.DisableScreen();
            m_Screen = null;
        }
        if (m_ComputerRoot)
        {
            SCR_EntityHelper.DeleteEntityAndChildren(m_ComputerRoot);
            m_ComputerRoot = null;
        }
        m_MonitorMesh = null;
        if (m_Sensor)
            m_Sensor.EnableClutterSurface(false);
        m_Active = false;
        m_OwnSensorTick = false;
    }

    bool IsSurfaceActive()
    {
        return m_Active;
    }

    protected bool BindOfficialComputer(RDF_RadarSensor sensor)
    {
        IEntity subject = ResolveSubject();
        if (!subject)
        {
            Print("[RDF CAS Demo] no subject to place computer", LogLevel.ERROR);
            return false;
        }

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        ResourceName prefabName = m_ComputerPrefab;
        if (prefabName == ResourceName.Empty)
            prefabName = COMPUTER_ON_RT_PREFAB;

        Resource prefabRes = Resource.Load(prefabName);
        if (!prefabRes)
        {
            Print(
                string.Format("[RDF CAS Demo] failed to load prefab %1 — try Computer_E_01_on_RT / vanilla on", prefabName),
                LogLevel.ERROR);
            return false;
        }

        vector mat[4];
        subject.GetWorldTransform(mat);
        vector fwd = mat[2];
        float flatLen = Math.Sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
        vector flatFwd;
        if (flatLen < 0.001)
            flatFwd = Vector(0.0, 0.0, 1.0);
        else
            flatFwd = Vector(fwd[0] / flatLen, 0.0, fwd[2] / flatLen);

        float dist = m_fComputerDistanceM;
        if (dist < 0.8)
            dist = 0.8;
        float heightM = m_fSpawnHeightM;
        if (heightM < 0.0)
            heightM = 0.0;

        vector flatPos = Vector(
            mat[3][0] + flatFwd[0] * dist,
            mat[3][1],
            mat[3][2] + flatFwd[2] * dist);
        // Viewing height relative to player (not ground snap).
        vector pos = Vector(flatPos[0], mat[3][1] + heightM, flatPos[2]);

        // Enfusion AnglesToMatrix is (yaw, pitch, roll) — do not put yaw in Y.
        // Screen faces local -Z; entity forward (+Z) = flatFwd so CRT faces the player.
        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.DirectionAndUpMatrix(flatFwd, vector.Up, spawnParams.Transform);
        spawnParams.Transform[3] = pos;

        m_ComputerRoot = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!m_ComputerRoot)
        {
            Print("[RDF CAS Demo] SpawnEntityPrefab Computer_E_01 family failed", LogLevel.ERROR);
            return false;
        }
        m_ComputerRoot.SetTransform(spawnParams.Transform);
        float scale = m_fComputerScale;
        if (scale < 0.25)
            scale = 0.25;
        if (scale > 8.0)
            scale = 8.0;
        if (Math.AbsFloat(scale - 1.0) > 0.001)
            m_ComputerRoot.SetScale(scale);
        ForceHighDetailLod(m_ComputerRoot);

        // Always force slot remap — prefab MaterialAssign alone often leaves stock CRT art.
        int remapped = RemapScreenMaterials(m_ComputerRoot, SCREEN_RT_MATERIAL);
        Print(string.Format("[RDF CAS Demo] screen slot remaps=%1", remapped), LogLevel.NORMAL);

        m_MonitorMesh = FindMonitorEntity(m_ComputerRoot);
        if (!m_MonitorMesh)
            m_MonitorMesh = m_ComputerRoot;
        remapped = remapped + ForceRemapComputerScreen(m_MonitorMesh, SCREEN_RT_MATERIAL);

        if (!m_Screen)
            m_Screen = new RDF_RadarClutterSurfaceScreen();
        return m_Screen.EnableScreen(m_MonitorMesh, sensor);
    }

    // Stock Computer_E_01 collapses the CRT into the keyboard on far LODs.
    protected void ForceHighDetailLod(IEntity ent)
    {
        if (!ent)
            return;
        ent.SetFixedLOD(0);
        IEntity child = ent.GetChildren();
        while (child)
        {
            ForceHighDetailLod(child);
            child = child.GetSibling();
        }
    }

    protected bool IsRtAssignedPrefab(ResourceName prefabName)
    {
        if (prefabName == COMPUTER_ON_RT_PREFAB)
            return true;
        if (prefabName == MONITOR_ON_RT_PREFAB)
            return true;
        string path = prefabName;
        path.ToLower();
        if (path.Contains("computer_e_01_on_rt"))
            return true;
        if (path.Contains("computer_e_01_monitor_on_rt"))
            return true;
        return false;
    }

    protected int ForceRemapComputerScreen(IEntity ent, ResourceName rtMat)
    {
        if (!ent)
            return 0;
        VObject mesh = ent.GetVObject();
        if (!mesh)
            return 0;
        string remap = string.Format("$remap 'Computer_Screen_On' '%1';", rtMat);
        ent.SetObject(mesh, remap);
        return 1;
    }

    // Remap Computer_Screen_On (+ any matching slots) to the RT material; leave chassis alone.
    protected int RemapScreenMaterials(IEntity ent, ResourceName rtMat)
    {
        if (!ent)
            return 0;

        int count = 0;
        VObject mesh = ent.GetVObject();
        if (mesh)
        {
            string materials[256];
            int numMats = mesh.GetMaterials(materials);
            string remap;
            bool hit = false;
            remap = string.Format("$remap 'Computer_Screen_On' '%1';", rtMat);
            for (int i = 0; i < numMats; i++)
            {
                string matName = materials[i];
                if (!IsComputerScreenMaterialName(matName))
                    continue;
                if (matName != "Computer_Screen_On")
                    remap = remap + string.Format("$remap '%1' '%2';", matName, rtMat);
                hit = true;
                Print(string.Format("[RDF CAS Demo] mat slot '%1' → RT", matName), LogLevel.NORMAL);
            }
            if (hit)
            {
                ent.SetObject(mesh, remap);
                count = count + 1;
            }
        }

        IEntity child = ent.GetChildren();
        while (child)
        {
            count = count + RemapScreenMaterials(child, rtMat);
            child = child.GetSibling();
        }
        return count;
    }

    protected bool IsComputerScreenMaterialName(string matName)
    {
        if (matName == string.Empty)
            return false;
        if (matName == "Computer_Screen_On")
            return true;
        string lower = matName;
        lower.ToLower();
        if (lower.Contains("computer_screen"))
            return true;
        if (lower.Contains("screen_on"))
            return true;
        if (lower.Contains("computer_e_01_screen"))
            return true;
        return false;
    }

    // Prefer the child that owns the monitor mesh (screen material present).
    protected IEntity FindMonitorEntity(IEntity root)
    {
        if (!root)
            return null;
        if (EntityHasScreenMaterial(root))
            return root;

        IEntity child = root.GetChildren();
        while (child)
        {
            IEntity found = FindMonitorEntity(child);
            if (found)
                return found;
            child = child.GetSibling();
        }
        return null;
    }

    protected bool EntityHasScreenMaterial(IEntity ent)
    {
        if (!ent)
            return false;
        VObject mesh = ent.GetVObject();
        if (!mesh)
            return false;
        string materials[256];
        int numMats = mesh.GetMaterials(materials);
        for (int i = 0; i < numMats; i++)
        {
            if (IsComputerScreenMaterialName(materials[i]))
                return true;
        }
        return false;
    }

    protected RDF_RadarSensor ResolveSensor()
    {
        IEntity owner = GetOwner();
        if (owner)
        {
            RDF_RadarComponent radar = RDF_RadarComponent.Cast(owner.FindComponent(RDF_RadarComponent));
            if (radar)
            {
                m_OwnSensorTick = false;
                return radar.GetSensor();
            }
            IEntity parent = owner.GetParent();
            if (parent)
            {
                radar = RDF_RadarComponent.Cast(parent.FindComponent(RDF_RadarComponent));
                if (radar)
                {
                    m_OwnSensorTick = false;
                    return radar.GetSensor();
                }
            }
        }

        RDF_RadarSensor fromRunner = RDF_RadarAutoRunner.GetSensor();
        if (fromRunner)
        {
            m_OwnSensorTick = false;
            return fromRunner;
        }
        return null;
    }

    protected IEntity ResolveSubject()
    {
        IEntity local = SCR_PlayerController.GetLocalControlledEntity();
        if (local)
            return local;
        return GetOwner();
    }
}
