// Console / Script Debugger entry for range–az clutter on Computer_E_01 family.
//
// Usage (Workbench Script Console while Play is running):
//   RDF_RadarClutterSurfaceManualDemo.Start();
//   RDF_RadarClutterSurfaceManualDemo.Stop();
//
// Starts AutoRunner (SEARCH + clutter surface), spawns Computer_E_01_on_RT
// (vanilla Computer_E_01_on + RT screen MaterialAssign) in front of the player.
class RDF_RadarClutterSurfaceManualDemo
{
    // Default: RDF wrapper over official Computer_E_01_on.
    protected static const ResourceName COMPUTER_ON_RT_PREFAB =
        "{D7A31F04C8E26B91}Prefabs/RDF/ClutterSurface/Computer_E_01_on_RT.et";
    protected static const ResourceName MONITOR_ON_RT_PREFAB =
        "{D7A31F04C8E26B90}Prefabs/RDF/ClutterSurface/Computer_E_01_Monitor_on_RT.et";
    protected static const ResourceName VANILLA_COMPUTER_ON =
        "{A9DC69EC1CE466A0}Prefabs/Props/Civilian/Computer_E_01/Computer_E_01_on.et";
    protected static const ResourceName SCREEN_RT_MATERIAL =
        "{C1A84F02B7D35E91}RadarData/ClutterSurface/Computer_Screen_RT.emat";
    protected static const float TICK_MS = 50;
    protected static const float COMPUTER_DISTANCE_M = 1.6;
    // Relative to player origin (not ground) — chest/eye height for CRT viewing.
    protected static const float SPAWN_HEIGHT_M = 1.15;
    // Whole Computer_E_01 assembly scale (1 = stock / original size).
    protected static const float COMPUTER_SCALE = 1.0;

    protected static ref RDF_RadarClutterSurfaceScreen s_Screen;
    protected static IEntity s_ComputerRoot;
    protected static IEntity s_MonitorMesh;
    protected static bool s_Active;
    protected static bool s_PrevDemo;
    protected static bool s_PrevHud;
    protected static ResourceName s_PrefabOverride;

    // Optional: pass vanilla Computer_E_01_on / Monitor_on / off or Monitor_on_RT.
    static void Start()
    {
        StartWithPrefab(COMPUTER_ON_RT_PREFAB);
    }

    static void StartMonitorOnly()
    {
        StartWithPrefab(MONITOR_ON_RT_PREFAB);
    }

    static void StartVanillaOn()
    {
        StartWithPrefab(VANILLA_COMPUTER_ON);
    }

    static void StartWithPrefab(ResourceName prefab)
    {
        if (s_Active)
        {
            Print("[RDF CAS ManualDemo] already active — call Stop() first", LogLevel.WARNING);
            return;
        }

        s_PrefabOverride = prefab;
        if (s_PrefabOverride == ResourceName.Empty)
            s_PrefabOverride = COMPUTER_ON_RT_PREFAB;

        s_PrevDemo = RDF_RadarAutoRunner.IsDemoEnabled();
        s_PrevHud = RDF_RadarAutoRunner.IsHudEnabled();

        RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateDefault(64));
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.SetHudEnabled(false);
        RDF_RadarAutoRunner.StartAutoRun();

        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor)
        {
            Print("[RDF CAS ManualDemo] no AutoRunner sensor", LogLevel.ERROR);
            return;
        }

        sensor.EnableClutterSurface(true);
        sensor.SetEnabled(true);
        RDF_RadarSettings cfg = sensor.GetSettings();
        if (cfg)
        {
            cfg.m_EnableRangeAzClutterSurface = true;
            cfg.m_EnableDemClutter = true;
            cfg.Validate();
        }

        if (!BindOfficialComputer(sensor))
        {
            Print("[RDF CAS ManualDemo] computer bind failed — open Computer_E_01_on_RT.et once in Workbench", LogLevel.ERROR);
            sensor.EnableClutterSurface(false);
            return;
        }

        s_Active = true;
        GetGame().GetCallqueue().CallLater(StaticTick, TICK_MS, true);
        Print(string.Format("[RDF CAS ManualDemo] started — prefab %1", s_PrefabOverride));
        Print("[RDF CAS ManualDemo] stop: RDF_RadarClutterSurfaceManualDemo.Stop();");
    }

    static void Stop()
    {
        GetGame().GetCallqueue().Remove(StaticTick);
        s_Active = false;

        if (s_Screen)
        {
            s_Screen.DisableScreen();
            s_Screen = null;
        }
        if (s_ComputerRoot)
        {
            SCR_EntityHelper.DeleteEntityAndChildren(s_ComputerRoot);
            s_ComputerRoot = null;
        }
        s_MonitorMesh = null;
        s_PrefabOverride = ResourceName.Empty;

        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor)
            sensor.EnableClutterSurface(false);

        RDF_RadarAutoRunner.SetDemoEnabled(s_PrevDemo);
        RDF_RadarAutoRunner.SetHudEnabled(s_PrevHud);
        Print("[RDF CAS ManualDemo] stopped");
    }

    static bool IsActive()
    {
        return s_Active;
    }

    protected static void StaticTick()
    {
        if (!s_Active || !s_Screen)
            return;
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor)
            return;
        s_Screen.Update();
    }

    // Stock Computer_E_01 drops monitor detail into the keyboard at far LODs.
    // Lock LOD 0 on the whole hierarchy (same pattern as SCR_MapRTWBaseUI).
    protected static void ForceHighDetailLod(IEntity ent)
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

    protected static bool BindOfficialComputer(RDF_RadarSensor sensor)
    {
        IEntity subject = SCR_PlayerController.GetLocalControlledEntity();
        if (!subject)
        {
            Print("[RDF CAS ManualDemo] no local player — possess a character in Play first", LogLevel.ERROR);
            return false;
        }

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        Resource prefabRes = Resource.Load(s_PrefabOverride);
        if (!prefabRes)
        {
            Print(
                string.Format("[RDF CAS ManualDemo] failed to load %1", s_PrefabOverride),
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

        vector flatPos = Vector(
            mat[3][0] + flatFwd[0] * COMPUTER_DISTANCE_M,
            mat[3][1],
            mat[3][2] + flatFwd[2] * COMPUTER_DISTANCE_M);
        // Place in front of the player at viewing height (ignore ground snap).
        vector pos = Vector(flatPos[0], mat[3][1] + SPAWN_HEIGHT_M, flatPos[2]);

        // Enfusion AnglesToMatrix is (yaw, pitch, roll). Screen faces local -Z, so
        // entity +Z (= forward) points along flatFwd (away from player) → CRT toward player.
        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.DirectionAndUpMatrix(flatFwd, vector.Up, spawnParams.Transform);
        spawnParams.Transform[3] = pos;

        s_ComputerRoot = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!s_ComputerRoot)
        {
            Print("[RDF CAS ManualDemo] SpawnEntityPrefab failed", LogLevel.ERROR);
            return false;
        }
        s_ComputerRoot.SetTransform(spawnParams.Transform);
        if (Math.AbsFloat(COMPUTER_SCALE - 1.0) > 0.001)
            s_ComputerRoot.SetScale(COMPUTER_SCALE);
        ForceHighDetailLod(s_ComputerRoot);

        // Always force slot remap — prefab MaterialAssign alone often leaves stock CRT art.
        int remapped = RemapScreenMaterials(s_ComputerRoot, SCREEN_RT_MATERIAL);
        Print(string.Format("[RDF CAS ManualDemo] screen slot remaps=%1", remapped), LogLevel.NORMAL);

        s_MonitorMesh = FindMonitorEntity(s_ComputerRoot);
        if (!s_MonitorMesh)
            s_MonitorMesh = s_ComputerRoot;

        // Force remap again on the mesh entity that receives SetRenderTarget.
        remapped = remapped + ForceRemapComputerScreen(s_MonitorMesh, SCREEN_RT_MATERIAL);
        if (remapped < 1)
            DumpMaterials(s_ComputerRoot);

        s_Screen = new RDF_RadarClutterSurfaceScreen();
        return s_Screen.EnableScreen(s_MonitorMesh, sensor);
    }

    protected static bool IsRtAssignedPrefab(ResourceName prefabName)
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

    protected static int ForceRemapComputerScreen(IEntity ent, ResourceName rtMat)
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

    protected static int RemapScreenMaterials(IEntity ent, ResourceName rtMat)
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
            // Slot name used by Computer_E_01 Monitor.xob / MaterialAssign.
            remap = string.Format("$remap 'Computer_Screen_On' '%1';", rtMat);
            for (int i = 0; i < numMats; i++)
            {
                string matName = materials[i];
                if (!IsComputerScreenMaterialName(matName))
                    continue;
                if (matName != "Computer_Screen_On")
                    remap = remap + string.Format("$remap '%1' '%2';", matName, rtMat);
                hit = true;
                Print(string.Format("[RDF CAS ManualDemo] mat slot '%1' → RT", matName), LogLevel.NORMAL);
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

    protected static bool IsComputerScreenMaterialName(string matName)
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

    protected static void DumpMaterials(IEntity ent)
    {
        if (!ent)
            return;
        VObject mesh = ent.GetVObject();
        if (mesh)
        {
            string materials[256];
            int numMats = mesh.GetMaterials(materials);
            Print(string.Format("[RDF CAS ManualDemo] entity mats=%1", numMats), LogLevel.WARNING);
            for (int i = 0; i < numMats; i++)
                Print(string.Format("  [%1] %2", i, materials[i]), LogLevel.WARNING);
        }
        IEntity child = ent.GetChildren();
        while (child)
        {
            DumpMaterials(child);
            child = child.GetSibling();
        }
    }

    protected static IEntity FindMonitorEntity(IEntity root)
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

    protected static bool EntityHasScreenMaterial(IEntity ent)
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
}
