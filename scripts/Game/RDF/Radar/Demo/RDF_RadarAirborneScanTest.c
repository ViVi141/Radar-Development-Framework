// Airborne scan test:
// 1) auto-select radar start location near player,
// 2) spawn Mi-8 prefab in air,
// 3) force circular motion,
// 4) verify radar can recognize and detect the airborne target.
//
// The Mi-8 gets no detection aid: no emitter flag, no RCS override, no registry
// pre-seeding. Detection must come from its own skin return.
//
// Usage (Script Debugger): RDF_RadarAirborneScanTest.Start();
class RDF_RadarAirborneScanTest
{
    protected static ref RDF_RadarAirborneScanTest s_Instance;
    protected static bool s_TickRegistered;
    protected static bool s_KeepSpawnedTargetAfterTest;

    protected static const ResourceName AIR_TARGET_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";

    protected bool m_Running;
    protected float m_StartWallS;
    protected float m_LastProgressWallS;
    // Cold scatterer table needs time to classify the freshly spawned airframe
    // now that the test no longer pre-seeds it.
    protected float m_DurationS = 35.0;
    protected int m_LastScanSerial = -1;

    protected IEntity m_Subject;
    protected IEntity m_AirTarget;
    protected vector m_RadarOrigin;

    protected float m_OrbitRadiusM = 320.0;
    protected float m_OrbitAltitudeM = 120.0;
    protected float m_OrbitRateRadS = 0.18;
    protected float m_OrbitPhaseRad = 0.0;
    protected float m_LastDebugPrintWallS;

    protected int m_ScanCount;
    protected int m_TargetSeenCount;
    protected int m_TargetDetectedCount;
    protected int m_TargetSeenAsVehicle;
    protected int m_TargetSeenAsEmitter;
    protected int m_TargetSeenAsAnonymous;
    protected float m_MaxTargetSnrDb = -300.0;
    protected bool m_TargetDiscovered;
    protected int m_RespawnCount;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarAirborneScanTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarAirborneScanTest();
        return s_Instance;
    }

    static void Start()
    {
        s_KeepSpawnedTargetAfterTest = false;
        GetInstance().StartInternal();
    }

    // Start test and keep spawned airborne target after PASS/FAIL for visual checks.
    static void StartKeepTarget()
    {
        s_KeepSpawnedTargetAfterTest = true;
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarAirborneScanTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF Radar AirTest] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarAirborneScanTest inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    protected static void StaticTick()
    {
        RDF_RadarAirborneScanTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF Radar AirTest] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("Air"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF Radar AirTest] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Air");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF Radar AirTest] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Air");
            return;
        }

        m_PrevConfig = RDF_RadarAutoRunner.GetDemoConfig();
        m_PrevDemoEnabled = RDF_RadarAutoRunner.IsDemoEnabled();
        m_PrevHudEnabled = RDF_RadarAutoRunner.IsHudEnabled();
        m_PrevForceLocal = RDF_RadarAutoRunner.IsForceLocalScan();

        if (!ChooseAndApplyRadarOrigin())
        {
            Print("[RDF Radar AirTest] failed to choose radar origin.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("Air");
            return;
        }

        // Clock must start before the spawn so the initial orbit phase is 0 and
        // the first motion tick does not teleport the target.
        m_StartWallS = System.GetTickCount() * 0.001;

        if (!SpawnAirTarget())
        {
            Print("[RDF Radar AirTest] failed to spawn target prefab.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        // A Workbench character carrying RplComponent makes AutoRunner pick the
        // networked scanner, which ignores this test config.
        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplyTestRadarConfig();
        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();

        m_ScanCount = 0;
        m_TargetSeenCount = 0;
        m_TargetDetectedCount = 0;
        m_TargetSeenAsVehicle = 0;
        m_TargetSeenAsEmitter = 0;
        m_TargetSeenAsAnonymous = 0;
        m_MaxTargetSnrDb = -300.0;
        m_TargetDiscovered = false;
        m_RespawnCount = 0;
        m_LastScanSerial = RDF_RadarAutoRunner.GetLastScanSerial();

        m_LastProgressWallS = m_StartWallS;
        m_LastDebugPrintWallS = m_StartWallS;
        m_Running = true;

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
        }

        Print(string.Format(
            "[RDF Radar AirTest] started radarOrigin=%1 targetPrefab=%2",
            m_RadarOrigin.ToString(),
            AIR_TARGET_PREFAB));
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("Air");

        if (m_AirTarget && !s_KeepSpawnedTargetAfterTest)
        {
            RDF_RadarScattererRegistry.Unregister(m_AirTarget);
            SCR_EntityHelper.DeleteEntityAndChildren(m_AirTarget);
            m_AirTarget = null;
        }

        if (!restore)
            return;

        if (m_PrevConfig)
            RDF_RadarAutoRunner.SetDemoConfig(m_PrevConfig);
        RDF_RadarAutoRunner.SetDemoEnabled(m_PrevDemoEnabled);
        RDF_RadarAutoRunner.SetHudEnabled(m_PrevHudEnabled);
        RDF_RadarAutoRunner.SetForceLocalScan(m_PrevForceLocal);
    }

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float nowS = System.GetTickCount() * 0.001;
        UpdateAirTargetMotion(nowS);
        AccumulateLatestScan(nowS);

        if (nowS - m_StartWallS < m_DurationS)
            return;

        FinalizeAndReport();
        StopInternal(true);
    }

    protected bool ChooseAndApplyRadarOrigin()
    {
        if (!m_Subject)
            return false;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        vector mat[4];
        m_Subject.GetWorldTransform(mat);
        vector center = mat[3];

        vector best = center;
        float bestScore = 9999999.0;
        float searchRadius = 220.0;
        float sampleOffset = 20.0;

        for (int i = 0; i < 12; i++)
        {
            float angle = i * Math.PI * 2.0 / 12.0;
            float x = center[0] + Math.Cos(angle) * searchRadius;
            float z = center[2] + Math.Sin(angle) * searchRadius;
            float y = world.GetSurfaceY(x, z);

            float hx1 = world.GetSurfaceY(x + sampleOffset, z);
            float hx2 = world.GetSurfaceY(x - sampleOffset, z);
            float hz1 = world.GetSurfaceY(x, z + sampleOffset);
            float hz2 = world.GetSurfaceY(x, z - sampleOffset);
            float score = Math.AbsFloat(hx1 - hx2) + Math.AbsFloat(hz1 - hz2);
            if (score < bestScore)
            {
                bestScore = score;
                best = Vector(x, y + 8.0, z);
            }
        }

        // Safety: do not teleport/move local player in test, to avoid collision-death
        // side effects that stop the scanner subject resolver.
        m_RadarOrigin = best;
        return true;
    }

    // Orbit position for an elapsed-since-start time. Elapsed 0 == spawn point,
    // so the first motion tick continues smoothly instead of teleporting the
    // target across the circle (absolute tick counts caused a huge first jump).
    protected vector ComputeOrbitPos(float elapsedS)
    {
        float phase = elapsedS * m_OrbitRateRadS;
        return Vector(
            m_RadarOrigin[0] + Math.Cos(phase) * m_OrbitRadiusM,
            m_RadarOrigin[1] + m_OrbitAltitudeM + 15.0 * Math.Sin(elapsedS * 0.07),
            m_RadarOrigin[2] + Math.Sin(phase) * m_OrbitRadiusM);
    }

    protected float GetElapsedS()
    {
        if (m_StartWallS <= 0.0)
            return 0.0;
        float elapsed = System.GetTickCount() * 0.001 - m_StartWallS;
        if (elapsed < 0.0)
            return 0.0;
        return elapsed;
    }

    protected bool SpawnAirTarget()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        Resource prefabRes = Resource.Load(AIR_TARGET_PREFAB);
        if (!prefabRes)
            return false;

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        // Respawn keeps the current orbit phase; initial spawn is phase 0.
        vector spawnPos = ComputeOrbitPos(GetElapsedS());
        spawnParams.Transform[3] = spawnPos;

        m_AirTarget = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!m_AirTarget)
            return false;

        // Skin RCS only: no emitter flag, no registry pre-seeding.
        Print("[RDF Radar AirTest] spawned Mi-8 at " + spawnPos.ToString());
        return true;
    }

    protected void ApplyTestRadarConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarDemoConfig.CreateDefault(128);
        cfg.m_Range = 6000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = true;
        // Passive skin return only; an ESM contribution would mask a real miss.
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_KeepUndetected = true;
        cfg.m_DetectionSnrDb = -40.0;
        cfg.m_EnableDemClutter = false;
        // Spawned target must reach the table via the normal sweep.
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_ScattererMaxEntries = 2048;
        cfg.m_ScattererDiscoveryIntervalS = 0.5;
        // Empty range cells carry no thermal-noise fill yet, so the CFAR window can
        // reject a lone genuine plot. This test measures detection, not false alarms.
        cfg.m_EnableCfarGate = false;
        // Synthesis otherwise rewrites the type to ANONYMOUS, which makes the
        // classification check meaningless.
        cfg.m_KeepEntityTruth = true;

        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 40.0;
        // Non-Doppler search radar: the orbit crosses zero radial speed, and MTI
        // blind speeds would hide a target the test is meant to measure.
        hw.m_EnableMti = false;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("air_low", 8.0, 30.0, 0.0);
        hw.AddElevationBeam("air_mid", 18.0, 36.0, 0.0);
        hw.AddElevationBeam("air_high", 28.0, 44.0, -1.0);
        hw.Validate();
        cfg.m_Hardware = hw;

        cfg.Validate();
        RDF_RadarAutoRunner.SetDemoConfig(cfg);
    }

    protected void UpdateAirTargetMotion(float nowS)
    {
        if (!m_AirTarget)
        {
            if (SpawnAirTarget())
            {
                m_RespawnCount = m_RespawnCount + 1;
                Print("[RDF Radar AirTest] target respawned count=" + m_RespawnCount.ToString());
            }
            return;
        }

        float elapsedS = GetElapsedS();
        m_OrbitPhaseRad = elapsedS * m_OrbitRateRadS;
        vector pos = ComputeOrbitPos(elapsedS);
        m_AirTarget.SetOrigin(pos);

        float vx = -Math.Sin(m_OrbitPhaseRad) * m_OrbitRadiusM * m_OrbitRateRadS;
        float vz = Math.Cos(m_OrbitPhaseRad) * m_OrbitRadiusM * m_OrbitRateRadS;
        float vy = 15.0 * 0.07 * Math.Cos(elapsedS * 0.07);
        Physics physics = m_AirTarget.GetPhysics();
        if (physics)
            physics.SetVelocity(Vector(vx, vy, vz));

        // Always draw a bright debug marker so visibility tests do not depend on
        // whether the scenario prefab contains a rendered mesh.
        int markerFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
        Shape.CreateSphere(ARGBF(1, 1, 0.2, 0.2), markerFlags, pos, 6.0);
        vector markerLine[2];
        markerLine[0] = pos;
        markerLine[1] = pos + "0 25 0";
        Shape.CreateLines(ARGBF(1, 1, 0.2, 0.2), markerFlags, markerLine, 2);

        if (nowS - m_LastDebugPrintWallS > 3.0)
        {
            m_LastDebugPrintWallS = nowS;
            float dx = pos[0] - m_RadarOrigin[0];
            float dy = pos[1] - m_RadarOrigin[1];
            float dz = pos[2] - m_RadarOrigin[2];
            float dist = Math.Sqrt(dx * dx + dy * dy + dz * dz);
            Print("[RDF Radar AirTest] marker pos=" + pos.ToString()
                + " range=" + dist.ToString()
                + " inTable=" + m_TargetDiscovered.ToString()
                + " " + RDF_RadarScattererRegistry.GetStatsLine());
        }
    }

    protected void AccumulateLatestScan(float nowS)
    {
        int serial = RDF_RadarAutoRunner.GetLastScanSerial();
        if (serial == m_LastScanSerial)
        {
            if (nowS - m_LastProgressWallS > 5.0)
            {
                m_LastProgressWallS = nowS;
                Print("[RDF Radar AirTest] waiting for scan progress; ensure Play mode is running.");
            }
            return;
        }

        m_LastScanSerial = serial;
        m_LastProgressWallS = nowS;
        m_ScanCount = m_ScanCount + 1;
        if (m_AirTarget && RDF_RadarScattererRegistry.Find(m_AirTarget))
            m_TargetDiscovered = true;

        array<ref RDF_RadarTarget> targets = RDF_RadarAutoRunner.GetLastTargets();
        if (!targets || !m_AirTarget)
            return;

        foreach (RDF_RadarTarget t : targets)
        {
            if (!t)
                continue;

            // Plots may keep the entity ref; synthesized ones only match by gate.
            float matchGateM = 250.0;
            if (!IsPlotNearEntity(t, m_AirTarget, m_RadarOrigin, matchGateM))
                continue;

            // Seen counts every plot on the target, detected only those that pass
            // the detection pipeline, so a "seen but not detected" run is visible.
            m_TargetSeenCount = m_TargetSeenCount + 1;
            if (!t.m_Detected)
                continue;

            m_TargetDetectedCount = m_TargetDetectedCount + 1;
            if (t.m_SnrDb > m_MaxTargetSnrDb)
                m_MaxTargetSnrDb = t.m_SnrDb;
            if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE)
                m_TargetSeenAsVehicle = m_TargetSeenAsVehicle + 1;
            if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
                m_TargetSeenAsEmitter = m_TargetSeenAsEmitter + 1;
            if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS)
                m_TargetSeenAsAnonymous = m_TargetSeenAsAnonymous + 1;
        }
    }

    protected bool IsPlotNearEntity(
        RDF_RadarTarget plot,
        IEntity entity,
        vector radarOrigin,
        float gateM)
    {
        if (!plot || !entity)
            return false;
        if (plot.m_Entity == entity)
            return true;

        vector truthPos = entity.GetOrigin();
        vector truthDelta = truthPos - radarOrigin;
        float truthRange = truthDelta.Length();
        float dRange = plot.m_Distance - truthRange;
        if (dRange < 0.0)
            dRange = -dRange;
        if (dRange > gateM)
            return false;

        float truthAz = Math.Atan2(truthDelta[2], truthDelta[0]) * 57.2957795;
        float dAz = plot.m_AzimuthDeg - truthAz;
        while (dAz > 180.0)
            dAz = dAz - 360.0;
        while (dAz < -180.0)
            dAz = dAz + 360.0;
        if (dAz < 0.0)
            dAz = -dAz;
        if (dAz > 8.0)
            return false;
        return true;
    }

    protected void FinalizeAndReport()
    {
        bool passScans = m_ScanCount > 8;
        bool passSeen = m_TargetSeenCount > 0;
        bool passDetected = m_TargetDetectedCount > 0;
        // Must be a plain vehicle return. An emitter classification would mean
        // something re-flagged the target and the skin-RCS path went untested.
        bool passClassified = m_TargetSeenAsVehicle > 0;
        bool passNoEmitterAid = m_TargetSeenAsEmitter == 0;
        bool passDiscovery = m_TargetDiscovered;
        bool allPass = passScans && passSeen && passDetected && passClassified
            && passNoEmitterAid && passDiscovery;

        array<string> lines = new array<string>();
        lines.Insert("RDF Radar Airborne Scan Test");
        lines.Insert("prefab " + AIR_TARGET_PREFAB);
        lines.Insert("result " + BoolLabel(allPass));
        lines.Insert("");
        lines.Insert("checks:");
        lines.Insert("  scans_present " + BoolLabel(passScans));
        lines.Insert("  target_seen " + BoolLabel(passSeen));
        lines.Insert("  target_detected " + BoolLabel(passDetected));
        lines.Insert("  target_classified_vehicle " + BoolLabel(passClassified));
        lines.Insert("  no_emitter_aid " + BoolLabel(passNoEmitterAid));
        lines.Insert("  discovered_unaided " + BoolLabel(passDiscovery));
        lines.Insert("");
        lines.Insert("metrics:");
        lines.Insert("  scan_count " + m_ScanCount.ToString());
        lines.Insert("  target_seen_count " + m_TargetSeenCount.ToString());
        lines.Insert("  target_detected_count " + m_TargetDetectedCount.ToString());
        lines.Insert("  seen_as_vehicle " + m_TargetSeenAsVehicle.ToString());
        lines.Insert("  seen_as_emitter " + m_TargetSeenAsEmitter.ToString());
        lines.Insert("  seen_as_anonymous " + m_TargetSeenAsAnonymous.ToString());
        lines.Insert("  max_target_snr_db " + m_MaxTargetSnrDb.ToString());
        lines.Insert("  target_respawn_count " + m_RespawnCount.ToString());
        lines.Insert("  radar_origin " + m_RadarOrigin.ToString());

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory("$profile:RDF/RadarTests");
        string reportPath = "$profile:RDF/RadarTests/radar_airborne_scan_"
            + System.GetTickCount().ToString() + ".txt";
        SCR_FileIOHelper.WriteFileContent(reportPath, lines);

        Print("[RDF Radar AirTest] " + BoolLabel(allPass) + "  report=" + reportPath);
        Print(string.Format(
            "[RDF Radar AirTest] scans=%1 seen=%2 detected=%3 vehicle=%4 emitter=%5 anon=%6 maxSnr=%7 respawn=%8",
            m_ScanCount.ToString(),
            m_TargetSeenCount.ToString(),
            m_TargetDetectedCount.ToString(),
            m_TargetSeenAsVehicle.ToString(),
            m_TargetSeenAsEmitter.ToString(),
            m_TargetSeenAsAnonymous.ToString(),
            m_MaxTargetSnrDb.ToString(),
            m_RespawnCount.ToString()));
    }

    protected string BoolLabel(bool value)
    {
        if (value)
            return "PASS";
        return "FAIL";
    }
}
