#ifdef WORKBENCH
// Offline bake: enumerate placeable Vehicle/Character prefabs (+ projectiles)
// from the ResourceDatabase, spawn each once, measure extents/RCS, write CSV.
// Does NOT scan entities currently present in the world.
[WorkbenchPluginAttribute(
    name: "RDF Bake Radar Signatures",
    description: "Scan placeable Vehicle/Character (+ Projectile) prefabs and write $profile:RDF/Signatures/rdf_radar_signatures.csv",
    wbModules: {"WorldEditor"},
    awesomeFontCode: 0xF542)]
class RDF_RadarSignatureBakePlugin : WorkbenchPlugin
{
    [Attribute("", UIWidgets.EditBox, "Optional ResourceDatabase root path (empty = all loaded addons)")]
    protected string m_sRootPath;

    [Attribute("1", UIWidgets.CheckBox, "Include prefabs with ProjectileMoveComponent even if not PLACEABLE")]
    protected bool m_bIncludeProjectiles;

    [Attribute("0", UIWidgets.CheckBox, "Merge into existing CSV instead of replacing")]
    protected bool m_bMergeExisting;

    //------------------------------------------------------------------------------------------------
    override void Run()
    {
        string msg = "Enumerate PLACEABLE Vehicle/Character prefabs";
        if (m_bIncludeProjectiles)
            msg = msg + " (+ projectiles)";
        msg = msg + " from the resource database.\n";
        msg = msg + "Each prefab is spawned once off-map, measured, then deleted.\n";
        msg = msg + "Output: $profile:RDF/Signatures/rdf_radar_signatures.csv\n\n";
        msg = msg + "Open a world in World Editor first (do NOT run via Play ScriptDebugger).\n";
        msg = msg + "This does not scan live world entities.";

        if (!Workbench.ScriptDialog("RDF Bake Radar Signatures", msg, this))
            return;

        Bake();
    }

    //------------------------------------------------------------------------------------------------
    override void RunCommandline()
    {
        Bake();
        Workbench.Exit(0);
    }

    //------------------------------------------------------------------------------------------------
    [ButtonAttribute("Bake")]
    protected bool OK()
    {
        return true;
    }

    //------------------------------------------------------------------------------------------------
    [ButtonAttribute("Cancel")]
    protected bool Cancel()
    {
        return false;
    }

    //------------------------------------------------------------------------------------------------
    protected void Bake()
    {
        WorldEditor worldEditor = Workbench.GetModule(WorldEditor);
        if (!worldEditor)
        {
            Print("[RDF Radar Sig Bake] WorldEditor module missing.", LogLevel.ERROR);
            return;
        }

        WorldEditorAPI api = worldEditor.GetApi();
        if (!api)
        {
            Print("[RDF Radar Sig Bake] WorldEditorAPI missing.", LogLevel.ERROR);
            return;
        }

        BaseWorld world = api.GetWorld();
        if (!world)
        {
            Print("[RDF Radar Sig Bake] Open a world in World Editor first.", LogLevel.ERROR);
            return;
        }

        WBProgressDialog progress = new WBProgressDialog("RDF radar signature bake...", worldEditor);

        array<ResourceName> allPrefabs = new array<ResourceName>();
        SearchResourcesFilter filter = new SearchResourcesFilter();
        array<string> extensions = new array<string>();
        extensions.Insert("et");
        filter.fileExtensions = extensions;
        filter.recursive = true;
        if (m_sRootPath != "")
            filter.rootPath = m_sRootPath;

        ResourceDatabase.SearchResources(filter, allPrefabs.Insert);
        int allCount = allPrefabs.Count();
        Print("[RDF Radar Sig Bake] scanned .et count=" + allCount.ToString(), LogLevel.NORMAL);
        if (allCount == 0)
        {
            Print("[RDF Radar Sig Bake] SearchResources returned 0. Run this from World Editor Plugins menu (not Play / ScriptDebugger).", LogLevel.ERROR);
            return;
        }

        array<ResourceName> candidates = new array<ResourceName>();
        array<int> candidateTypes = new array<int>();
        float prevProgress = 0.0;
        for (int i = 0; i < allCount; i++)
        {
            ResourceName prefab = allPrefabs.Get(i);
            int typeHint;
            if (IsRadarBakeCandidate(prefab, typeHint))
            {
                candidates.Insert(prefab);
                candidateTypes.Insert(typeHint);
            }

            float curr = 0.0;
            if (allCount > 0)
                curr = (i + 1) / (allCount * 2.0);
            if (curr - prevProgress >= 0.01)
            {
                progress.SetProgress(curr);
                prevProgress = curr;
            }
        }

        int candidateCount = candidates.Count();
        Print("[RDF Radar Sig Bake] candidates=" + candidateCount.ToString(), LogLevel.NORMAL);
        if (candidateCount == 0)
        {
            Print("[RDF Radar Sig Bake] No candidates; refusing to clear/export empty table.", LogLevel.ERROR);
            return;
        }

        // Only clear after we know there is something to bake.
        if (!m_bMergeExisting)
            RDF_RadarSignatureLibrary.Clear();
        else
            RDF_RadarSignatureLibrary.EnsureLoadedPublic();

        int measured = 0;
        int failed = 0;
        for (int i = 0; i < candidateCount; i++)
        {
            ResourceName prefab = candidates.Get(i);
            ERDF_RadarTargetType targetType = IntToTargetType(candidateTypes.Get(i));
            if (MeasurePrefab(world, prefab, targetType))
                measured = measured + 1;
            else
                failed = failed + 1;

            float curr = 0.5;
            if (candidateCount > 0)
                curr = 0.5 + ((i + 1) / (candidateCount * 2.0));
            if (curr - prevProgress >= 0.01)
            {
                progress.SetProgress(curr);
                prevProgress = curr;
            }
        }

        if (measured == 0)
        {
            Print("[RDF Radar Sig Bake] Measured 0 prefabs; refusing empty export.", LogLevel.ERROR);
            return;
        }

        bool ok = RDF_RadarSignatureLibrary.ExportTable();
        progress.SetProgress(1.0);

        string summary = "[RDF Radar Sig Bake] done measured=" + measured.ToString();
        summary = summary + " failed=" + failed.ToString();
        summary = summary + " table=" + RDF_RadarSignatureLibrary.GetCount().ToString();
        if (ok)
            summary = summary + " -> " + RDF_RadarSignatureLibrary.SIG_FILE;
        else
            summary = summary + " EXPORT FAILED";
        Print(summary, LogLevel.NORMAL);
    }

    //------------------------------------------------------------------------------------------------
    // Prefab-source filter: PLACEABLE Vehicle/Character (or class inheritance),
    // and optionally projectiles. Does not require the prefab to be in the open world.
    protected bool IsRadarBakeCandidate(ResourceName prefab, out int outTypeHint)
    {
        outTypeHint = 0;
        if (prefab.IsEmpty())
            return false;

        Resource container = BaseContainerTools.LoadContainer(prefab);
        if (!container)
            return false;

        BaseResourceObject object = container.GetResource();
        if (!object)
            return false;

        IEntitySource entity = object.ToEntitySource();
        if (!entity)
            return false;

        bool hasProjectileMove = false;
        IEntityComponentSource editableComp = null;
        int componentCount = entity.GetComponentCount();
        for (int c = 0; c < componentCount; c++)
        {
            IEntityComponentSource component = entity.GetComponent(c);
            if (!component)
                continue;

            string className = component.GetClassName();
            typename compType = className.ToType();
            if (!compType)
            {
                // Fallback when ToType fails: string match on component class.
                if (className.IndexOf("ProjectileMove") >= 0)
                    hasProjectileMove = true;
                if (className.IndexOf("EditableEntity") >= 0)
                    editableComp = component;
                continue;
            }

            if (compType.IsInherited(ProjectileMoveComponent))
                hasProjectileMove = true;

            if (compType.IsInherited(SCR_EditableEntityComponent))
                editableComp = component;
        }

        if (hasProjectileMove && m_bIncludeProjectiles)
        {
            outTypeHint = 1;
            return true;
        }

        typename entityTypeName = entity.GetClassName().ToType();
        bool isVehicleClass = false;
        bool isCharacterClass = false;
        if (entityTypeName)
        {
            if (entityTypeName.IsInherited(Vehicle))
                isVehicleClass = true;
            if (entityTypeName.IsInherited(ChimeraCharacter))
                isCharacterClass = true;
        }

        bool placeable = false;
        EEditableEntityType editableType = EEditableEntityType.GENERIC;
        if (editableComp)
        {
            placeable = SCR_EditableEntityComponentClass.HasFlag(
                editableComp, EEditableEntityFlag.PLACEABLE);
            editableType = SCR_EditableEntityComponentClass.GetEntityType(editableComp);
        }

        if (placeable)
        {
            if (editableType == EEditableEntityType.VEHICLE
                || editableType == EEditableEntityType.CHARACTER
                || isVehicleClass
                || isCharacterClass)
            {
                outTypeHint = 0;
                return true;
            }
        }

        // Class inheritance alone: still bake (covers placeable variants typed GENERIC).
        if (isVehicleClass || isCharacterClass)
        {
            outTypeHint = 0;
            return true;
        }

        return false;
    }

    //------------------------------------------------------------------------------------------------
    protected bool MeasurePrefab(
        BaseWorld world,
        ResourceName prefab,
        ERDF_RadarTargetType typeHint)
    {
        Resource res = Resource.Load(prefab);
        if (!res || !res.IsValid())
            return false;

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        spawnParams.TransformMode = ETransformMode.WORLD;
        Math3D.MatrixIdentity4(spawnParams.Transform);
        spawnParams.Transform[3] = Vector(0, -5000, 0);

        IEntity entity = GetGame().SpawnEntityPrefab(res, world, spawnParams);
        if (!entity)
            return false;

        ERDF_RadarTargetType targetType = typeHint;
        if (RDF_RadarEntityClassifier.IsProjectile(entity))
            targetType = ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE;
        else if (RDF_RadarEntityClassifier.IsVehicleOrCharacter(entity))
            targetType = ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE;

        string key = prefab;
        RDF_RadarSignature sig = RDF_RadarSignatureLibrary.BakeFromSpawned(entity, key, targetType);
        delete entity;
        if (!sig)
            return false;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected ERDF_RadarTargetType IntToTargetType(int value)
    {
        if (value == 1)
            return ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE;
        if (value == 2)
            return ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER;
        if (value == 3)
            return ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
        return ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE;
    }
}
#endif
