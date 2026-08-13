// Cinematic promo reel for screen recording.
// Linear script of the RDF sensor stack (not a feature gallery):
//
//   Act I  SEE — SEARCH
//     1 COLD      empty fan
//     2 SECTOR    crane slam, fan on the ground
//     3 GROUND    column in the fan (vehicle plots)
//     4 AIR       Mi-8 in the same sector
//   Act II COMMIT — same sector, then choose the mover
//     5 DOPPLER   mixed plots → PD drops the column, both helis stay
//     6 LOCK      lock beam on engage Mi-8
//     7 ENGAGE    Hydra70 into the locked Mi-8
//   Act III LOCATE — WLR
//     8 LAUNCH    mortar leaves the tube
//     9 TRACK     ride the shell; predicted arc already ahead
//    10 SOLVE     pull to god: origin + arc + impact
//    11 IMPACT    slam to the cyan circle
//   Act IV CONTESTED — picture dies, then listen, then shoot the emitter
//    12 EW        clean search returns → second Mi-8 jams, ghosts flood
//    13 ESM       stop painting; magenta listen to the jammer
//    14 ARM       Hydra along the listen beam into the jammer
//    15 COVERAGE  search picture restored
//
// Staged from live APIs. Not in this reel (need extra stations / terrain /
// UI the radar module does not own): dual-radar fusion, SAM salvo, RWR panel,
// DEM ridge NLOS, LiDAR sister module.
//
// Workbench Play → Script Debugger:
//   RDF_RadarPromoReel.Start();
//   RDF_RadarPromoReel.StartLoop();
//   RDF_RadarPromoReel.Stop();
class RDF_RadarPromoCamDriverClass : GenericEntityClass
{
}

class RDF_RadarPromoCamDriver : GenericEntity
{
    void RDF_RadarPromoCamDriver(IEntitySource src, IEntity parent)
    {
        SetFlags(EntityFlags.ACTIVE, false);
        SetEventMask(EntityEvent.POSTFRAME);
    }

    override void EOnPostFrame(IEntity owner, float timeSlice)
    {
        RDF_RadarPromoReel.GetInstance().OnPostFrame(timeSlice);
    }
}

class RDF_RadarPromoShot
{
    string m_Title;
    float m_DurationS;
    int m_CamStyle;
    int m_RadarMode;
    float m_FovDeg;
}

class RDF_RadarPromoReel
{
    protected static const int CAM_HOLD = 0;
    protected static const int CAM_CRANE = 1;
    protected static const int CAM_ORBIT = 2;
    protected static const int CAM_CHASE = 3;
    protected static const int CAM_SHELL = 4;
    protected static const int CAM_PULLBACK = 5;
    protected static const int CAM_LOW = 6;
    protected static const int CAM_LOCK = 7;
    protected static const int CAM_LAUNCH = 8;
    protected static const int CAM_ARC = 9;
    protected static const int CAM_IMPACT = 10;
    protected static const int CAM_ROCKET = 11;
    protected static const int CAM_JAM = 12;
    protected static const int CAM_ESM_LOOK = 13;
    protected static const int CAM_ARM = 14;

    protected static const int MODE_SEARCH = 0;
    protected static const int MODE_STARE = 1;
    protected static const int MODE_WLR = 2;
    protected static const int MODE_PD = 3;
    protected static const int MODE_EW = 4;
    protected static const int MODE_ESM = 5;

    protected static const ResourceName GROUND_PREFAB =
        "{259EE7B78C51B624}Prefabs/Vehicles/Wheeled/UAZ469/UAZ469.et";
    protected static const ResourceName AIR_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";
    protected static const ResourceName SHELL_PREFAB =
        "{98EC9C526AFBA282}Prefabs/Weapons/Ammo/Ammo_Shell_82mm_HE_O832DU.et";
    protected static const ResourceName ROCKET_PREFAB =
        "{ECD8628EBF7E5F6B}Prefabs/Weapons/Ammo/Ammo_Rocket_Hydra70.et";
    protected static const ResourceName MORTAR_PREFAB =
        "{D1FFE458E8AC4BDB}Prefabs/Weapons/Mortars/2B14/Mortar_2B14.et";
    protected static const ResourceName MORTAR_GUNNER_PREFAB =
        "{DCB41B3746FDD1BE}Prefabs/Characters/Factions/OPFOR/USSR_Army/Character_USSR_Rifleman.et";

    protected static const int GROUND_COUNT = 4;
    protected static const float UAZ_RANGE0_M = 95.0;
    protected static const float UAZ_RANGE_STEP_M = 50.0;
    protected static const float UAZ_LATERAL_M = 20.0;
    protected static const float AIR_ORBIT_RANGE_M = 400.0;
    protected static const float AIR_ORBIT_RADIUS_M = 80.0;
    protected static const float AIR_ORBIT_ALT_M = 95.0;
    protected static const float AIR_ORBIT_RATE_RAD_S = 0.30;
    protected static const float AIR_ORBIT_DEPTH_FRAC = 0.22;
    protected static const float AIR_JAM_PHASE = 3.14159265;
    protected static const float LAUNCH_FWD_M = 22.0;
    protected static const float LAUNCH_AGL_M = 8.0;
    protected static const float LOOK_SEARCH_FWD_M = 155.0;
    protected static const float LOOK_WIDE_FWD_M = 260.0;
    protected static const float SHELL_SPEED_COEF = 1.736;
    protected static const float SHELL_ELEVATION_DEG = 55.0;
    protected static const float SHELL_INIT_SPEED_MS = 76.0;
    protected static const float SHELL_FIRE_DELAY_S = 0.20;
    protected static const float ROCKET_FIRE_DELAY_S = 0.40;
    protected static const float ARM_FIRE_DELAY_S = 0.80;
    protected static const float ROCKET_LAUNCH_MS = 180.0;
    protected static const float PD_FILTER_S = 2.2;
    protected static const float JAM_ON_S = 2.8;
    protected static const float ESM_LISTEN_S = 1.5;
    protected static const float HELI_RELEASE_DELAY_S = 2.0;
    protected static const float HELI_ROCKET_HIT_M = 16.0;
    protected static const float PROMO_MIN_RANGE_M = 50.0;
    protected static const float SHELL_IMPACT_HOLD_S = 3.5;
    protected static const float SHELL_TOF_FALLBACK_S = 18.5;
    protected static const float TICK_MS = 33.0;

    protected static ref RDF_RadarPromoReel s_Instance;
    protected static bool s_TickRegistered;

    protected bool m_Running;
    protected bool m_Loop;
    protected float m_StartWallS;
    protected float m_ShotStartWallS;
    protected int m_ShotIndex;
    protected int m_AppliedRadarMode;
    protected int m_LastSlateSecond;

    protected IEntity m_RadarVan;
    protected IEntity m_CamDriver;
    protected CameraBase m_PromoCamera;
    protected CameraBase m_PrevCamera;
    protected vector m_CamEye;
    protected vector m_CamLookAt;
    protected float m_CamFovDeg;
    protected vector m_CamEyeDesired;
    protected vector m_CamLookDesired;
    protected float m_CamFovDesired;
    protected bool m_CamSmoothInit;
    protected int m_CamSmoothShot;
    protected IEntity m_AirTarget;
    protected IEntity m_JamHeli;
    protected ref array<IEntity> m_GroundTargets;
    protected ref array<IEntity> m_LiveShells;
    protected vector m_RadarAnchor;
    protected vector m_FlatFwd;
    protected vector m_FlatRight;
    protected vector m_OrbitCenter;
    protected vector m_LookAtSearch;
    protected vector m_LookAtWide;
    protected vector m_LaunchPos;
    protected vector m_ShellApexPos;
    protected bool m_ShellApexValid;
    protected IEntity m_ActiveShell;
    protected IEntity m_Mortar;
    protected ChimeraCharacter m_MortarGunner;
    protected IEntity m_MortarHeldShell;
    protected bool m_DidStartMortarLoad;
    protected IEntity m_Rocket;
    protected ref RDF_RadarRocketGuidanceState m_RocketGuide;
    protected bool m_DidFireRocket;
    protected bool m_DidFireArm;
    protected bool m_EmitterArmed;
    protected vector m_ShellImpactPos;
    protected bool m_ShellImpactValid;
    protected bool m_DidFireShell;
    protected bool m_DidLiveSolve;
    protected float m_ShellTofS;

    protected ref array<ref RDF_RadarPromoShot> m_Shots;
    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;
    protected IEntity m_PrevSubjectOverride;
    protected ref array<vector> m_ShellPath;
    protected ref array<vector> m_ShellLiveTrail;
    protected ref array<vector> m_RocketTrail;
    protected ref RDF_DebugShapeManager m_ShellOverlay;
    protected vector m_ShellMuzzleVel;
    protected vector m_HeliSmoothPos;
    protected vector m_HeliSmoothFwd;
    protected bool m_HeliSmoothInit;
    protected bool m_AirReleased;
    protected float m_AirHitWallS;
    protected vector m_JamSmoothPos;
    protected vector m_JamSmoothFwd;
    protected bool m_JamSmoothInit;
    protected bool m_JamReleased;
    protected float m_JamHitWallS;
    protected vector m_ConvoyLookPos;
    protected bool m_ConvoyLookInit;

    static RDF_RadarPromoReel GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarPromoReel();
        return s_Instance;
    }

    static void Start()
    {
        GetInstance().StartInternal(false);
    }

    static void StartLoop()
    {
        GetInstance().StartInternal(true);
    }

    static void Stop()
    {
        RDF_RadarPromoReel inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF Promo] stopped.");
    }

    static bool IsRunning()
    {
        RDF_RadarPromoReel inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    protected static void StaticTick()
    {
        RDF_RadarPromoReel inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    void RDF_RadarPromoReel()
    {
        m_GroundTargets = new array<IEntity>();
        m_LiveShells = new array<IEntity>();
        m_Shots = new array<ref RDF_RadarPromoShot>();
        m_ShellPath = new array<vector>();
        m_ShellLiveTrail = new array<vector>();
        m_RocketTrail = new array<vector>();
        m_ShellOverlay = new RDF_DebugShapeManager();
        m_AppliedRadarMode = -1;
        m_LastSlateSecond = -1;
        m_CamFovDeg = 48.0;
        m_CamFovDesired = 48.0;
        m_CamEye = "0 0 0";
        m_CamLookAt = "0 0 1";
        m_CamSmoothInit = false;
        m_CamSmoothShot = -1;
        m_ConvoyLookInit = false;
        m_ShellImpactValid = false;
        m_DidFireShell = false;
        m_DidStartMortarLoad = false;
        m_DidFireRocket = false;
        m_DidFireArm = false;
        m_EmitterArmed = false;
        m_DidLiveSolve = false;
        m_ShellApexValid = false;
        m_HeliSmoothInit = false;
        m_JamSmoothInit = false;
        m_AirReleased = false;
        m_JamReleased = false;
        m_AirHitWallS = -1.0;
        m_JamHitWallS = -1.0;
        m_ShellTofS = SHELL_TOF_FALLBACK_S;
    }

    protected void StartInternal(bool loop)
    {
        if (m_Running)
        {
            Print("[RDF Promo] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("Promo"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF Promo] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Promo");
            return;
        }

        m_Loop = loop;
        RememberPrevRunner();
        BuildShotList();
        if (!ChooseAnchor())
        {
            Print("[RDF Promo] failed to choose origin.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("Promo");
            return;
        }

        CraneEnds(m_CamEye, m_CamLookAt, 0.0);
        m_CamFovDeg = 48.0;
        m_CamEyeDesired = m_CamEye;
        m_CamLookDesired = m_CamLookAt;
        m_CamFovDesired = m_CamFovDeg;
        m_CamSmoothInit = false;
        m_CamSmoothShot = -1;
        m_ConvoyLookInit = false;

        if (!SpawnRadarVan())
        {
            Print("[RDF Promo] failed to spawn radar van.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        SpawnGroundTargets();
        if (!SpawnAirTargets())
            Print("[RDF Promo] Mi-8 spawn failed — continuing without air target.", LogLevel.WARNING);
        if (!SpawnPromoMortarCrew())
            Print("[RDF Promo] 2B14/AI spawn failed — shell will fall back to script launch.", LogLevel.WARNING);

        RDF_RadarAutoRunner.SetScanSubjectOverride(m_RadarVan);
        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplyRadarMode(MODE_SEARCH);
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor)
        {
            sensor.ResetSession();
            RDF_RadarLockManager lockMgr = sensor.GetLockManager();
            if (lockMgr)
            {
                lockMgr.SetAutoAcquire(true);
                lockMgr.SetTypeFilter(true, false, false);
                lockMgr.SetMaxLockRange(3500.0);
                lockMgr.SetLockSector(0.0);
                lockMgr.SetAcquireHits(2);
                lockMgr.SetCoastMaxSec(4.0);
            }
        }

        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();
        RDF_RadarVisualSettings vis = RDF_RadarAutoRunner.GetVisualSettings();
        if (vis)
            vis.ApplyPromoDefaults();
        RDF_RadarHUD.Show();
        RDF_RadarHUD.SetDisplayRangeCap(800.0);
        RDF_RadarHUD.SetModeOverride("PROMO | REC");

        if (!SpawnPromoCamera(world))
            Print("[RDF Promo] camera spawn failed — falling back to SetCameraEx.", LogLevel.WARNING);

        m_ShotIndex = 0;
        m_AppliedRadarMode = -1;
        m_StartWallS = System.GetTickCount() * 0.001;
        m_ShotStartWallS = m_StartWallS;
        m_LastSlateSecond = -1;
        m_Running = true;

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, TICK_MS, true);
        }

        Print("[RDF Promo] ========== START RECORDING NOW ==========");
        Print("[RDF Promo] 剧本: 发现 → 锁一架打一架 → 反炮兵 → 另一架干扰后改听并反辐射");
        Print("[RDF Promo] Stop() 结束。loop=" + loop.ToString());
        AnnounceShot();
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("Promo");
        ReleasePromoCamera();
        DeleteSpawned();
        m_RadarVan = null;
        m_AirTarget = null;
        m_JamHeli = null;
        m_ActiveShell = null;
        m_Rocket = null;
        m_RocketGuide = null;
        m_ShellImpactValid = false;
        m_DidFireShell = false;
        m_DidStartMortarLoad = false;
        m_DidFireRocket = false;
        m_DidFireArm = false;
        m_EmitterArmed = false;
        m_DidLiveSolve = false;
        m_HeliSmoothInit = false;
        m_JamSmoothInit = false;
        m_AirReleased = false;
        m_JamReleased = false;
        m_AirHitWallS = -1.0;
        m_JamHitWallS = -1.0;
        m_CamSmoothInit = false;
        m_ConvoyLookInit = false;
        RDF_RadarAutoRunner.SetScanSubjectOverride(m_PrevSubjectOverride);
        RDF_RadarHUD.SetModeOverride("");
        RDF_RadarHUD.SetDisplayRangeCap(0.0);
        if (m_ShellOverlay)
            m_ShellOverlay.Clear();
        RDF_RadarVisualSettings vis = RDF_RadarAutoRunner.GetVisualSettings();
        if (vis)
            vis.ApplyShowcaseDefaults();

        if (!restore)
            return;

        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor)
            sensor.ResetSession();
        if (m_PrevConfig)
            ApplySensorConfig(m_PrevConfig);
        RDF_RadarAutoRunner.SetDemoEnabled(m_PrevDemoEnabled);
        RDF_RadarAutoRunner.SetHudEnabled(m_PrevHudEnabled);
        RDF_RadarAutoRunner.SetForceLocalScan(m_PrevForceLocal);
    }

    protected void RememberPrevRunner()
    {
        m_PrevConfig = null;
        RDF_RadarSensor prevSensor = RDF_RadarAutoRunner.GetSensor();
        if (prevSensor)
            m_PrevConfig = prevSensor.GetSettings();
        m_PrevDemoEnabled = RDF_RadarAutoRunner.IsDemoEnabled();
        m_PrevHudEnabled = RDF_RadarAutoRunner.IsHudEnabled();
        m_PrevForceLocal = RDF_RadarAutoRunner.IsForceLocalScan();
        m_PrevSubjectOverride = RDF_RadarAutoRunner.GetScanSubjectOverride();
    }

    protected void ApplySensorConfig(RDF_RadarSettings cfg)
    {
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor || !cfg)
            return;
        sensor.SetForceLocalScan(true);
        sensor.Configure(cfg);
    }

    protected void BuildShotList()
    {
        m_Shots.Clear();
        m_Shots.Insert(MakeShot("1  COLD  空扇面", 3.0, CAM_HOLD, MODE_SEARCH, 56.0));
        m_Shots.Insert(MakeShot("2  SECTOR  扇面铺开", 5.0, CAM_CRANE, MODE_SEARCH, 48.0));
        m_Shots.Insert(MakeShot("3  GROUND  地面进扇", 4.5, CAM_LOW, MODE_SEARCH, 32.0));
        m_Shots.Insert(MakeShot("4  AIR  空中入画", 5.0, CAM_CHASE, MODE_SEARCH, 28.0));
        m_Shots.Insert(MakeShot("5  DOPPLER  滤掉地面", 7.0, CAM_CHASE, MODE_PD, 34.0));
        m_Shots.Insert(MakeShot("6  LOCK  锁定空中", 4.2, CAM_LOCK, MODE_STARE, 20.0));
        m_Shots.Insert(MakeShot("7  ENGAGE  制导交战", 8.0, CAM_ROCKET, MODE_STARE, 26.0));
        m_Shots.Insert(MakeShot("8  LAUNCH  弹丸离膛", 4.5, CAM_LAUNCH, MODE_WLR, 28.0));
        m_Shots.Insert(MakeShot("9  TRACK  弹道跟踪", 5.5, CAM_ARC, MODE_WLR, 34.0));
        m_Shots.Insert(MakeShot("10 SOLVE  解算", 6.5, CAM_SHELL, MODE_WLR, 64.0));
        m_Shots.Insert(MakeShot("11 IMPACT  落点确认", 3.5, CAM_IMPACT, MODE_WLR, 36.0));
        m_Shots.Insert(MakeShot("12 EW  画面被淹没", 8.0, CAM_JAM, MODE_EW, 42.0));
        m_Shots.Insert(MakeShot("13 ESM  改听辐射源", 6.5, CAM_ESM_LOOK, MODE_ESM, 32.0));
        m_Shots.Insert(MakeShot("14 ARM  顺着辐射打回去", 7.5, CAM_ARM, MODE_ESM, 26.0));
        m_Shots.Insert(MakeShot("15 COVERAGE  全图", 5.5, CAM_PULLBACK, MODE_SEARCH, 52.0));
    }

    protected RDF_RadarPromoShot MakeShot(
        string title,
        float durationS,
        int camStyle,
        int radarMode,
        float fovDeg)
    {
        RDF_RadarPromoShot shot = new RDF_RadarPromoShot();
        shot.m_Title = title;
        shot.m_DurationS = durationS;
        shot.m_CamStyle = camStyle;
        shot.m_RadarMode = radarMode;
        shot.m_FovDeg = fovDeg;
        return shot;
    }

    protected RDF_RadarPromoShot CurrentShot()
    {
        if (!m_Shots)
            return null;
        if (m_ShotIndex < 0)
            return null;
        if (m_ShotIndex >= m_Shots.Count())
            return null;
        return m_Shots.Get(m_ShotIndex);
    }

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float nowS = System.GetTickCount() * 0.001;
        float elapsedS = nowS - m_StartWallS;
        if (elapsedS < 0.0)
            elapsedS = 0.0;

        UpdateAirMotion(elapsedS);
        UpdateGroundMotion(elapsedS);
        UpdateShells();

        RDF_RadarPromoShot shot = CurrentShot();
        if (!shot)
        {
            FinishOrLoop();
            return;
        }

        float shotT = nowS - m_ShotStartWallS;
        if (shotT >= shot.m_DurationS)
        {
            AdvanceShot();
            shot = CurrentShot();
            if (!shot)
            {
                FinishOrLoop();
                return;
            }
            shotT = 0.0;
        }

        int wantMode = DesiredRadarMode(shot, shotT);
        if (m_AppliedRadarMode != wantMode)
        {
            ApplyRadarMode(wantMode);
            m_AppliedRadarMode = wantMode;
        }

        if (shot.m_CamStyle == CAM_LAUNCH)
            MaybeFireShell(shotT);
        if (shot.m_CamStyle == CAM_ROCKET)
            MaybeFireRocket(shotT);
        if (shot.m_CamStyle == CAM_ARM)
            MaybeFireArmRocket(shotT);
        UpdateRocketGuidance();
        IgnorePromoArtillery();
        IgnoreRocketChildren();
        if (m_AppliedRadarMode == MODE_STARE)
            ForceLockHeli();
        if (m_AppliedRadarMode == MODE_ESM)
            ForceLockArm();

        bool wantEmitter = false;
        if (shot.m_CamStyle == CAM_ESM_LOOK && shotT >= ESM_LISTEN_S)
            wantEmitter = true;
        if (shot.m_CamStyle == CAM_ARM)
            wantEmitter = true;
        if (wantEmitter)
            ArmPromoEmitter(true);
        else if (m_EmitterArmed)
            ArmPromoEmitter(false);

        UpdatePromoHudTitle(shot, shotT);
        DrawSlate(shot, shotT);
        DrawPromoOverlay(shot, shotT);
        RDF_RadarAutoRunner.RefreshPresentation(false);
        ApplyCameraForShot(shot, shotT, elapsedS);
    }

    void OnPostFrame(float timeSlice)
    {
        if (!m_Running)
            return;
        StepCameraSmooth(timeSlice);
        CommitCamera();
    }

    protected bool SpawnPromoCamera(BaseWorld world)
    {
        CameraManager mgr = GetGame().GetCameraManager();
        if (mgr)
            m_PrevCamera = mgr.CurrentCamera();

        m_PromoCamera = CameraBase.Cast(GetGame().SpawnEntity(CameraBase, world));
        m_CamDriver = GetGame().SpawnEntity(RDF_RadarPromoCamDriver, world);
        if (!m_PromoCamera)
            return false;

        m_PromoCamera.SetFOVDegree(48.0);
        m_PromoCamera.SetFarPlane(12000.0);
        m_PromoCamera.SetNearPlane(0.2);
        if (mgr)
            mgr.SetCamera(m_PromoCamera);
        return true;
    }

    protected void ReleasePromoCamera()
    {
        CameraManager mgr = GetGame().GetCameraManager();
        if (mgr && m_PrevCamera)
            mgr.SetCamera(m_PrevCamera);
        m_PrevCamera = null;

        if (m_PromoCamera)
        {
            delete m_PromoCamera;
            m_PromoCamera = null;
        }
        if (m_CamDriver)
        {
            delete m_CamDriver;
            m_CamDriver = null;
        }
    }

    protected void AdvanceShot()
    {
        m_ShotIndex = m_ShotIndex + 1;
        m_ShotStartWallS = System.GetTickCount() * 0.001;
        m_LastSlateSecond = -1;
        m_CamSmoothInit = false;
        if (CurrentShot())
            AnnounceShot();
    }

    protected void FinishOrLoop()
    {
        if (m_Loop)
        {
            BuildShotList();
            ClearEntityArray(m_LiveShells);
            ClearRocket();
            m_ActiveShell = null;
            m_DidFireShell = false;
            m_DidStartMortarLoad = false;
            m_DidFireRocket = false;
            m_DidFireArm = false;
            m_EmitterArmed = false;
            m_Rocket = null;
            m_RocketGuide = null;
            PrepareMortarHeldShell();
            m_DidLiveSolve = false;
            if (m_ShellLiveTrail)
                m_ShellLiveTrail.Clear();
            m_ShotIndex = 0;
            m_ShotStartWallS = System.GetTickCount() * 0.001;
            m_StartWallS = m_ShotStartWallS;
            m_AppliedRadarMode = -1;
            m_LastSlateSecond = -1;
            m_CamSmoothInit = false;
            m_ConvoyLookInit = false;
            Print("[RDF Promo] loop restart.");
            AnnounceShot();
            return;
        }

        Print("[RDF Promo] reel complete.");
        StopInternal(true);
    }

    protected void AnnounceShot()
    {
        RDF_RadarPromoShot shot = CurrentShot();
        if (!shot)
            return;
        Print("[RDF Promo] " + shot.m_Title
            + "  " + shot.m_DurationS.ToString() + "s");
        RDF_RadarHUD.SetModeOverride(shot.m_Title);
    }

    protected int DesiredRadarMode(RDF_RadarPromoShot shot, float shotT)
    {
        if (!shot)
            return MODE_SEARCH;
        if (shot.m_CamStyle == CAM_CHASE && shot.m_RadarMode == MODE_PD)
        {
            if (shotT < PD_FILTER_S)
                return MODE_SEARCH;
            return MODE_PD;
        }
        if (shot.m_CamStyle == CAM_JAM)
        {
            if (shotT < JAM_ON_S)
                return MODE_SEARCH;
            return MODE_EW;
        }
        return shot.m_RadarMode;
    }

    protected void UpdatePromoHudTitle(RDF_RadarPromoShot shot, float shotT)
    {
        if (!shot)
            return;
        string title = shot.m_Title;
        if (shot.m_CamStyle == CAM_CHASE && shot.m_RadarMode == MODE_PD)
        {
            if (shotT < PD_FILTER_S)
                title = "5  搜索  地面和空中混在一起";
            else
                title = "5  多普勒  地面被滤掉";
        }
        else if (shot.m_CamStyle == CAM_JAM)
        {
            if (shotT < JAM_ON_S)
                title = "12  搜索  画面回来了";
            else
                title = "12  干扰  真目标被抹掉";
        }
        else if (shot.m_CamStyle == CAM_ESM_LOOK)
        {
            if (shotT < ESM_LISTEN_S)
                title = "13  静默  停发";
            else
                title = "13  侦收  抓住辐射源";
        }
        else if (shot.m_CamStyle == CAM_ARM)
        {
            if (m_Rocket)
                title = "14  ARM  顺着辐射打回去";
            else
                title = "14  ARM  锁定辐射源";
        }
        RDF_RadarHUD.SetModeOverride(title);
    }

    protected void DrawSlate(RDF_RadarPromoShot shot, float shotT)
    {
        int remain = Math.Ceil(shot.m_DurationS - shotT);
        int slateSec = remain;
        if (slateSec == m_LastSlateSecond)
            return;
        m_LastSlateSecond = slateSec;
        if (m_ShotIndex != 0)
            return;
        Print("[RDF Promo] countdown " + slateSec.ToString());
    }

    protected string FormatOneDecimal(float v)
    {
        float rounded = Math.Round(v * 10.0) * 0.1;
        return rounded.ToString();
    }

    protected bool ChooseAnchor()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        vector center = "0 0 0";
        vector fwd = "0 0 1";
        IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (subject)
        {
            vector mat[4];
            subject.GetWorldTransform(mat);
            center = mat[3];
            fwd = mat[2];
        }
        else
        {
            vector camMat[4];
            world.GetCurrentCamera(camMat);
            center = camMat[3];
            fwd = camMat[2];
        }

        float flatLen = Math.Sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
        if (flatLen < 0.001)
            m_FlatFwd = Vector(0.0, 0.0, 1.0);
        else
            m_FlatFwd = Vector(fwd[0] / flatLen, 0.0, fwd[2] / flatLen);
        m_FlatRight = Vector(m_FlatFwd[2], 0.0, -m_FlatFwd[0]);

        float surfaceY = world.GetSurfaceY(center[0], center[2]);
        m_RadarAnchor = Vector(center[0], surfaceY + 0.6, center[2]);

        m_LookAtSearch = StagePoint(LOOK_SEARCH_FWD_M, 0.0, 10.0);
        m_LookAtWide = StagePoint(LOOK_WIDE_FWD_M, 0.0, 40.0);

        vector heliGround = StagePoint(AIR_ORBIT_RANGE_M, 0.0, 0.0);
        m_OrbitCenter = Vector(heliGround[0], heliGround[1] + AIR_ORBIT_ALT_M, heliGround[2]);

        m_LaunchPos = StagePoint(LAUNCH_FWD_M, 0.0, LAUNCH_AGL_M);
        PredictShellArc();
        PrintStageCard();
        return true;
    }

    protected vector ShellMuzzleVel()
    {
        float elevRad = SHELL_ELEVATION_DEG * Math.DEG2RAD;
        vector dir = Vector(
            m_FlatFwd[0] * Math.Cos(elevRad),
            Math.Sin(elevRad),
            m_FlatFwd[2] * Math.Cos(elevRad));
        return dir * (SHELL_INIT_SPEED_MS * SHELL_SPEED_COEF);
    }

    protected void PredictShellArc()
    {
        if (!ApplyTableImpact(null, SHELL_SPEED_COEF))
            ApplyWorldImpactSolve(m_LaunchPos, ShellMuzzleVel());
    }

    protected void ApplyPromoShellCharge(IEntity shell, ProjectileMoveComponent move)
    {
        if (!shell)
            return;

        SCR_MortarShellGadgetComponent gadget = SCR_MortarShellGadgetComponent.Cast(
            shell.FindComponent(SCR_MortarShellGadgetComponent));
        if (gadget)
        {
            int n = gadget.GetNumberOfChargeRingConfigurations();
            int i;
            for (i = 0; i < n; i++)
            {
                vector cfg = gadget.GetChargeRingConfig(i);
                if (Math.AbsFloat(cfg[1] - SHELL_SPEED_COEF) < 0.02)
                {
                    gadget.SetChargeRingConfig(i, true, false);
                    break;
                }
            }
        }

        if (move)
            move.SetBulletCoef(SHELL_SPEED_COEF);
    }

    protected IEntitySource GetShellPrefabSource()
    {
        Resource res = Resource.Load(SHELL_PREFAB);
        if (!res || !res.IsValid())
            return null;
        BaseResourceObject obj = res.GetResource();
        if (!obj)
            return null;
        return obj.ToEntitySource();
    }

    protected bool ApplyTableImpact(IEntity shell, float speedCoef)
    {
        float elevRad = SHELL_ELEVATION_DEG * Math.DEG2RAD;
        float tofS = 0.0;
        float distM = 0.0;
        if (shell)
            distM = BallisticTable.GetDistanceOfProjectile(
                elevRad, tofS, shell, speedCoef, false);
        else
        {
            IEntitySource src = GetShellPrefabSource();
            if (!src)
                return false;
            distM = BallisticTable.GetDistanceOfProjectileSource(
                elevRad, tofS, src, speedCoef, false);
        }

        if (tofS <= 1.0)
            return false;
        if (distM <= 80.0)
            return false;

        vector impact = Vector(
            m_LaunchPos[0] + m_FlatFwd[0] * distM,
            m_LaunchPos[1],
            m_LaunchPos[2] + m_FlatFwd[2] * distM);
        BaseWorld world = GetGame().GetWorld();
        if (world)
            impact[1] = world.GetSurfaceY(impact[0], impact[2]) + 0.4;

        m_ShellImpactPos = impact;
        m_ShellImpactValid = true;
        m_ShellTofS = tofS;
        RebuildShellPath(ShellMuzzleVel(), m_ShellTofS);
        Print("[RDF Promo] table dist="
            + FormatOneDecimal(distM)
            + "m tof="
            + FormatOneDecimal(tofS)
            + "s coef="
            + FormatOneDecimal(speedCoef));
        return true;
    }

    protected bool ApplyEngineSimImpact(ProjectileMoveComponent move, float speedMs)
    {
        if (!move)
            return false;
        if (speedMs < 10.0)
            return false;

        RDF_RadarGlobalWind wind = RDF_RadarBallistics.SampleGlobalWind();
        vector windVel = Vector(0.0, 0.0, 0.0);
        if (wind)
            windVel = Vector(wind.m_Vx, 0.0, wind.m_Vz);

        float groundY = m_LaunchPos[1];
        BaseWorld world = GetGame().GetWorld();
        if (world)
            groundY = world.GetSurfaceY(m_LaunchPos[0], m_LaunchPos[2]);

        float azDeg = Math.Atan2(m_FlatFwd[0], m_FlatFwd[2]) * Math.RAD2DEG;
        vector sim = move.GetProjectileSimulationResult(
            m_LaunchPos,
            speedMs,
            SHELL_ELEVATION_DEG,
            azDeg,
            windVel,
            groundY,
            true);
        float dx = sim[0] - m_LaunchPos[0];
        float dz = sim[2] - m_LaunchPos[2];
        float distM = Math.Sqrt(dx * dx + dz * dz);
        if (distM < 80.0)
            return false;

        if (world)
            sim[1] = world.GetSurfaceY(sim[0], sim[2]) + 0.4;
        m_ShellImpactPos = sim;
        m_ShellImpactValid = true;
        if (m_ShellTofS < 4.0)
            m_ShellTofS = SHELL_TOF_FALLBACK_S;
        RebuildShellPath(ShellMuzzleVel(), m_ShellTofS);
        Print("[RDF Promo] engine sim dist=" + FormatOneDecimal(distM) + "m");
        return true;
    }

    protected void ApplyWorldImpactSolve(vector pos, vector vel)
    {
        RDF_RadarGlobalWind wind = RDF_RadarBallistics.SampleGlobalWind();
        RDF_RadarGroundHit hit = RDF_RadarBallistics.FindWorldSurfaceIntersection(
            pos,
            vel,
            RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE,
            wind);
        if (hit && hit.m_Valid)
        {
            m_ShellImpactPos = hit.m_Position;
            m_ShellImpactValid = true;
            if (hit.m_TimeOffsetS > 4.0)
                m_ShellTofS = hit.m_TimeOffsetS;
        }

        vector pathVel = m_ShellMuzzleVel;
        if (pathVel.LengthSq() < 1.0)
            pathVel = ShellMuzzleVel();
        RebuildShellPath(pathVel, m_ShellTofS);
    }

    // Orange overlay: parabola that lands on the accurate impact, not AirDrag.
    protected void RebuildShellPath(vector muzzleVel, float tofS)
    {
        if (!m_ShellPath)
            m_ShellPath = new array<vector>();
        m_ShellPath.Clear();
        m_ShellMuzzleVel = muzzleVel;
        if (!m_ShellImpactValid)
            return;

        vector impact = m_ShellImpactPos;
        float dx = impact[0] - m_LaunchPos[0];
        float dz = impact[2] - m_LaunchPos[2];
        float distM = Math.Sqrt(dx * dx + dz * dz);
        if (distM < 10.0)
            distM = 10.0;

        float elevRad = SHELL_ELEVATION_DEG * Math.DEG2RAD;
        float apexBoost = distM * Math.Tan(elevRad) * 0.25;
        int segs = 32;
        int i;
        for (i = 0; i <= segs; i++)
        {
            float u = i / (segs * 1.0);
            vector p = m_LaunchPos + (impact - m_LaunchPos) * u;
            p[1] = m_LaunchPos[1] + (impact[1] - m_LaunchPos[1]) * u
                + 4.0 * apexBoost * u * (1.0 - u);
            m_ShellPath.Insert(p);
            if (i == 0)
                m_ShellApexPos = p;
            else if (p[1] >= m_ShellApexPos[1])
                m_ShellApexPos = p;
        }
        m_ShellApexValid = true;
    }

    protected vector RadarScanOrigin()
    {
        return m_RadarAnchor + Vector(0.0, 4.0, 0.0);
    }

    protected vector StagePoint(float fwdM, float rightM, float aglM)
    {
        vector pos = Vector(
            m_RadarAnchor[0] + m_FlatFwd[0] * fwdM + m_FlatRight[0] * rightM,
            m_RadarAnchor[1],
            m_RadarAnchor[2] + m_FlatFwd[2] * fwdM + m_FlatRight[2] * rightM);
        BaseWorld world = GetGame().GetWorld();
        if (world)
            pos[1] = world.GetSurfaceY(pos[0], pos[2]) + aglM;
        else
            pos[1] = m_RadarAnchor[1] + aglM;
        return pos;
    }

    protected vector LiftEye(vector eye)
    {
        return LiftEyeMin(eye, 4.0);
    }

    protected vector LiftEyeMin(vector eye, float aglM)
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return eye;
        float minY = world.GetSurfaceY(eye[0], eye[2]) + aglM;
        if (eye[1] < minY)
            eye[1] = minY;
        return eye;
    }

    protected void PrintStageCard()
    {
        float heliElevDeg = Math.Atan2(AIR_ORBIT_ALT_M, AIR_ORBIT_RANGE_M) * Math.RAD2DEG;
        float heliAzDeg = Math.Atan2(AIR_ORBIT_RADIUS_M, AIR_ORBIT_RANGE_M) * Math.RAD2DEG;
        float uazAzDeg = Math.Atan2(UAZ_LATERAL_M, UAZ_RANGE0_M) * Math.RAD2DEG;
        float impactRange = vector.Distance(m_RadarAnchor, m_ShellImpactPos);
        Print("[RDF Promo] stage uaz="
            + UAZ_RANGE0_M.ToString()
            + ".."
            + (UAZ_RANGE0_M + UAZ_RANGE_STEP_M * 3.0).ToString()
            + "m az<="
            + FormatOneDecimal(uazAzDeg)
            + "deg  heli="
            + AIR_ORBIT_RANGE_M.ToString()
            + "m elev="
            + FormatOneDecimal(heliElevDeg)
            + "deg swing=±"
            + FormatOneDecimal(heliAzDeg)
            + "deg");
        Print("[RDF Promo] shell tof="
            + FormatOneDecimal(m_ShellTofS)
            + "s impactRange="
            + FormatOneDecimal(impactRange)
            + "m  cameras keep set in front of radar");
    }

    protected bool SpawnRadarVan()
    {
        IEntity van = SpawnPrefabAt(GROUND_PREFAB, m_RadarAnchor);
        if (!van)
            return false;

        vector mat[4];
        Math3D.DirectionAndUpMatrix(m_FlatFwd, vector.Up, mat);
        mat[3] = m_RadarAnchor;
        van.SetTransform(mat);
        m_RadarVan = van;
        Print("[RDF Promo] radar van at " + m_RadarAnchor.ToString());
        return true;
    }

    protected void SpawnGroundTargets()
    {
        ClearEntityArray(m_GroundTargets);
        for (int i = 0; i < GROUND_COUNT; i++)
        {
            float rangeM = UAZ_RANGE0_M + i * UAZ_RANGE_STEP_M;
            float lateral = UAZ_LATERAL_M;
            if ((i / 2) * 2 == i)
                lateral = -UAZ_LATERAL_M;
            vector pos = StagePoint(rangeM, lateral, 0.5);
            IEntity ent = SpawnPrefabAt(GROUND_PREFAB, pos);
            if (ent)
            {
                m_GroundTargets.Insert(ent);
                OrientEntityAlong(ent, m_FlatFwd, pos);
                PreparePromoCarDrive(ent);
            }
        }
        Print("[RDF Promo] ground=" + m_GroundTargets.Count().ToString());
    }

    protected bool SpawnAirTargets()
    {
        bool ok = false;
        vector posA = ComputeOrbitPos(0.0, 0.0);
        m_AirTarget = SpawnPrefabAt(AIR_PREFAB, posA);
        if (m_AirTarget)
        {
            m_HeliSmoothPos = posA;
            m_HeliSmoothFwd = ComputeOrbitTangent(0.0, 0.0);
            m_HeliSmoothInit = true;
            m_AirReleased = false;
            m_AirHitWallS = -1.0;
            OrientEntityAlong(m_AirTarget, m_HeliSmoothFwd, posA);
            PreparePromoHeliRotors(m_AirTarget);
            Print("[RDF Promo] engage Mi-8 at " + posA.ToString());
            ok = true;
        }

        vector posB = ComputeOrbitPos(0.0, AIR_JAM_PHASE);
        m_JamHeli = SpawnPrefabAt(AIR_PREFAB, posB);
        if (m_JamHeli)
        {
            m_JamSmoothPos = posB;
            m_JamSmoothFwd = ComputeOrbitTangent(0.0, AIR_JAM_PHASE);
            m_JamSmoothInit = true;
            m_JamReleased = false;
            m_JamHitWallS = -1.0;
            OrientEntityAlong(m_JamHeli, m_JamSmoothFwd, posB);
            PreparePromoHeliRotors(m_JamHeli);
            Print("[RDF Promo] jam Mi-8 at " + posB.ToString());
        }
        else
            Print("[RDF Promo] jam Mi-8 spawn failed.", LogLevel.WARNING);
        return ok;
    }

    protected vector ComputeOrbitPos(float elapsedS, float phaseOffset)
    {
        float phase = elapsedS * AIR_ORBIT_RATE_RAD_S + phaseOffset;
        float lat = Math.Cos(phase) * AIR_ORBIT_RADIUS_M;
        float depth = Math.Sin(phase) * AIR_ORBIT_RADIUS_M * AIR_ORBIT_DEPTH_FRAC;
        return Vector(
            m_OrbitCenter[0] + m_FlatRight[0] * lat + m_FlatFwd[0] * depth,
            m_OrbitCenter[1] + 8.0 * Math.Sin(elapsedS * 0.07 + phaseOffset),
            m_OrbitCenter[2] + m_FlatRight[2] * lat + m_FlatFwd[2] * depth);
    }

    protected vector ComputeOrbitTangent(float elapsedS, float phaseOffset)
    {
        float phase = elapsedS * AIR_ORBIT_RATE_RAD_S + phaseOffset;
        vector t = m_FlatRight * (-Math.Sin(phase) * AIR_ORBIT_RADIUS_M)
            + m_FlatFwd * (Math.Cos(phase) * AIR_ORBIT_RADIUS_M * AIR_ORBIT_DEPTH_FRAC);
        float len = t.Length();
        if (len < 0.001)
            return m_FlatFwd;
        return t / len;
    }

    protected void StepHeliOrbit(
        IEntity heli,
        inout vector smoothPos,
        inout vector smoothFwd,
        inout bool smoothInit,
        float elapsedS,
        float phaseOffset)
    {
        if (!heli)
            return;

        vector target = ComputeOrbitPos(elapsedS, phaseOffset);
        vector wantTan = ComputeOrbitTangent(elapsedS, phaseOffset);
        if (!smoothInit)
        {
            smoothPos = heli.GetOrigin();
            smoothFwd = wantTan;
            smoothInit = true;
        }

        float follow = 0.12;
        vector prev = smoothPos;
        smoothPos = smoothPos + (target - smoothPos) * follow;
        smoothFwd = smoothFwd + (wantTan - smoothFwd) * 0.10;
        float fwdLen = smoothFwd.Length();
        if (fwdLen > 0.001)
            smoothFwd = smoothFwd / fwdLen;
        else
            smoothFwd = wantTan;

        float yaw = Math.Atan2(smoothFwd[0], smoothFwd[2]) * Math.RAD2DEG;
        float bank = 0.0;
        vector delta = smoothPos - prev;
        float side = delta[0] * smoothFwd[2] - delta[2] * smoothFwd[0];
        bank = Math.Clamp(-side * 18.0, -12.0, 12.0);

        vector mat[4];
        Math3D.AnglesToMatrix(Vector(yaw, 0.0, bank), mat);
        mat[3] = smoothPos;
        heli.SetTransform(mat);

        Physics physics = heli.GetPhysics();
        if (physics)
        {
            physics.EnableGravity(false);
            physics.SetVelocity(Vector(0.0, 0.0, 0.0));
            physics.SetAngularVelocity(Vector(0.0, 0.0, 0.0));
        }
    }

    protected void UpdateAirMotion(float elapsedS)
    {
        TickPromoHeli(
            m_AirTarget,
            m_AirReleased,
            m_AirHitWallS,
            m_HeliSmoothPos,
            m_HeliSmoothFwd,
            m_HeliSmoothInit,
            elapsedS,
            0.0);
        TickPromoHeli(
            m_JamHeli,
            m_JamReleased,
            m_JamHitWallS,
            m_JamSmoothPos,
            m_JamSmoothFwd,
            m_JamSmoothInit,
            elapsedS,
            AIR_JAM_PHASE);
    }

    protected void TickPromoHeli(
        IEntity heli,
        inout bool released,
        inout float hitWallS,
        inout vector smoothPos,
        inout vector smoothFwd,
        inout bool smoothInit,
        float elapsedS,
        float phaseOffset)
    {
        if (!heli)
            return;
        if (released)
            return;

        float nowS = System.GetTickCount() * 0.001;
        if (hitWallS < 0.0)
        {
            if (IsHeliStruck(heli))
                hitWallS = nowS;
        }
        if (hitWallS >= 0.0)
        {
            if (nowS - hitWallS >= HELI_RELEASE_DELAY_S)
            {
                ReleaseHeliControl(heli, smoothFwd);
                released = true;
                smoothInit = false;
                return;
            }
        }

        StepHeliOrbit(
            heli, smoothPos, smoothFwd, smoothInit, elapsedS, phaseOffset);
    }

    protected bool IsHeliStruck(IEntity heli)
    {
        if (!heli)
            return false;

        SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(heli);
        if (dmg)
        {
            if (dmg.IsDestroyed())
                return true;
            if (dmg.GetHealthScaled() < 0.85)
                return true;
        }

        if (!m_Rocket)
            return false;
        float dist = vector.Distance(m_Rocket.GetOrigin(), heli.GetOrigin());
        if (dist <= HELI_ROCKET_HIT_M)
            return true;
        return false;
    }

    protected void ReleaseHeliControl(IEntity heli, vector lastFwd)
    {
        if (!heli)
            return;

        HelicopterControllerComponent heliCtrl = HelicopterControllerComponent.Cast(
            heli.FindComponent(HelicopterControllerComponent));
        if (heliCtrl)
        {
            VehicleHelicopterSimulation hsim = heliCtrl.GetSimulation();
            if (hsim)
            {
                hsim.SetThrottle(0.0);
                if (hsim.EngineIsOn())
                    hsim.EngineStop();
            }
        }

        Physics physics = heli.GetPhysics();
        if (physics)
        {
            physics.EnableGravity(true);
            vector vel = lastFwd * (AIR_ORBIT_RATE_RAD_S * AIR_ORBIT_RADIUS_M);
            vel[1] = vel[1] - 4.0;
            physics.SetVelocity(vel);
            physics.SetAngularVelocity(Vector(0.15, 0.35, 0.20));
        }
        Print("[RDF Promo] heli released to physics");
    }

    protected IEntity SpawnPrefabAt(ResourceName prefabName, vector pos)
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return null;
        Resource prefabRes = Resource.Load(prefabName);
        if (!prefabRes)
            return null;

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = pos;
        IEntity ent = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (ent)
            ent.SetOrigin(pos);
        return ent;
    }

    protected bool SpawnPromoMortarCrew()
    {
        vector mortarPos = StagePoint(LAUNCH_FWD_M, 0.0, 0.05);
        IEntity mortar = SpawnPrefabAt(MORTAR_PREFAB, mortarPos);
        if (!mortar)
            return false;

        vector mat[4];
        Math3D.DirectionAndUpMatrix(m_FlatFwd, vector.Up, mat);
        mat[3] = mortarPos;
        mortar.SetTransform(mat);
        m_Mortar = mortar;

        TurretControllerComponent turret = TurretControllerComponent.Cast(
            mortar.FindComponent(TurretControllerComponent));
        if (turret)
        {
            turret.AssembleTurret();
            float pitchRad = SHELL_ELEVATION_DEG * Math.DEG2RAD;
            turret.SetAimingAngles(0.0, pitchRad);
        }

        HookMortarFired();
        RefreshMuzzleLaunchPos();
        PredictShellArc();

        vector gunnerPos = mortarPos + m_FlatRight * 1.45;
        IEntity gunnerEnt = SpawnPrefabAt(MORTAR_GUNNER_PREFAB, gunnerPos);
        ChimeraCharacter gunner = ChimeraCharacter.Cast(gunnerEnt);
        if (!gunner)
        {
            Print("[RDF Promo] gunner prefab is not ChimeraCharacter.", LogLevel.WARNING);
            return true;
        }

        vector toMortar = mortarPos - gunnerPos;
        OrientEntityAlong(gunner, toMortar, gunnerPos);
        m_MortarGunner = gunner;

        AIControlComponent aiControl = AIControlComponent.Cast(
            gunner.FindComponent(AIControlComponent));
        if (aiControl)
        {
            aiControl.ActivateAI();
            AIAgent agent = aiControl.GetAIAgent();
            if (agent)
                agent.PreventMaxLOD();
        }

        if (!PrepareMortarHeldShell())
            Print("[RDF Promo] failed to put O832DU in AI hands.", LogLevel.WARNING);

        IgnorePromoArtillery();
        Print("[RDF Promo] 2B14 + AI loader at " + mortarPos.ToString());
        return true;
    }

    protected bool PrepareMortarHeldShell()
    {
        if (!m_MortarGunner)
            return false;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        Resource prefabRes = Resource.Load(SHELL_PREFAB);
        if (!prefabRes || !prefabRes.IsValid())
            return false;

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = m_MortarGunner.GetOrigin() + Vector(0.0, 0.8, 0.0);
        IEntity shell = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!shell)
            return false;

        ApplyPromoShellCharge(shell, null);

        InventoryStorageManagerComponent inv = InventoryStorageManagerComponent.Cast(
            m_MortarGunner.FindComponent(InventoryStorageManagerComponent));
        if (inv)
            inv.TryInsertItem(shell);

        SCR_GadgetManagerComponent gadgetMgr = SCR_GadgetManagerComponent.GetGadgetManager(m_MortarGunner);
        if (gadgetMgr)
            gadgetMgr.SetGadgetMode(shell, EGadgetMode.IN_HAND, false);

        CharacterControllerComponent charCtrl = m_MortarGunner.GetCharacterController();
        SCR_MortarShellGadgetComponent gadget = SCR_MortarShellGadgetComponent.Cast(
            shell.FindComponent(SCR_MortarShellGadgetComponent));
        if (charCtrl && gadget)
            charCtrl.TakeGadgetInLeftHand(shell, gadget.GetAnimVariable(), false, true);

        m_MortarHeldShell = shell;
        return true;
    }

    protected void HookMortarFired()
    {
        if (!m_Mortar)
            return;
        SCR_MortarMuzzleComponent muzzle = SCR_MortarMuzzleComponent.Cast(
            m_Mortar.FindComponent(SCR_MortarMuzzleComponent));
        SCR_MuzzleEffectComponent fx;
        if (muzzle)
            fx = SCR_MuzzleEffectComponent.Cast(muzzle.FindComponent(SCR_MuzzleEffectComponent));
        if (!fx)
            fx = SCR_MuzzleEffectComponent.Cast(m_Mortar.FindComponent(SCR_MuzzleEffectComponent));
        if (!fx)
            return;
        fx.GetOnWeaponFired().Insert(OnPromoMortarFired);
    }

    protected void UnhookMortarFired()
    {
        if (!m_Mortar)
            return;
        SCR_MortarMuzzleComponent muzzle = SCR_MortarMuzzleComponent.Cast(
            m_Mortar.FindComponent(SCR_MortarMuzzleComponent));
        SCR_MuzzleEffectComponent fx;
        if (muzzle)
            fx = SCR_MuzzleEffectComponent.Cast(muzzle.FindComponent(SCR_MuzzleEffectComponent));
        if (!fx)
            fx = SCR_MuzzleEffectComponent.Cast(m_Mortar.FindComponent(SCR_MuzzleEffectComponent));
        if (!fx)
            return;
        fx.GetOnWeaponFired().Remove(OnPromoMortarFired);
    }

    protected void RefreshMuzzleLaunchPos()
    {
        if (!m_Mortar)
            return;
        TurretControllerComponent turret = TurretControllerComponent.Cast(
            m_Mortar.FindComponent(TurretControllerComponent));
        if (turret)
        {
            BaseWeaponManagerComponent weapons = turret.GetWeaponManager();
            if (weapons)
            {
                vector transform[4];
                weapons.GetCurrentMuzzleTransform(transform);
                m_LaunchPos = transform[3];
                return;
            }
        }
        m_LaunchPos = m_Mortar.GetOrigin() + Vector(0.0, 1.1, 0.0);
    }

    protected void MaybeFireShell(float shotT)
    {
        if (shotT < SHELL_FIRE_DELAY_S)
            return;
        if (m_DidFireShell)
            return;
        if (m_DidStartMortarLoad)
            return;
        m_DidStartMortarLoad = true;
        if (TryAiMortarFire())
            return;
        Print("[RDF Promo] AI mortar load failed — muzzle fallback.", LogLevel.WARNING);
        TryFireShellFromMuzzle();
    }

    protected bool TryAiMortarFire()
    {
        if (!m_Mortar || !m_MortarGunner)
            return false;

        SCR_MortarMuzzleComponent muzzle = SCR_MortarMuzzleComponent.Cast(
            m_Mortar.FindComponent(SCR_MortarMuzzleComponent));
        if (!muzzle)
            return false;

        if (!m_MortarHeldShell)
        {
            if (!PrepareMortarHeldShell())
                return false;
        }

        SCR_MortarShellGadgetComponent gadget = SCR_MortarShellGadgetComponent.Cast(
            m_MortarHeldShell.FindComponent(SCR_MortarShellGadgetComponent));
        if (!gadget)
            return false;

        ApplyPromoShellCharge(m_MortarHeldShell, null);
        RefreshMuzzleLaunchPos();
        muzzle.LoadShell(gadget, m_MortarGunner, -1, false);
        GetGame().GetCallqueue().CallLater(FallbackMuzzleLaunchIfNeeded, 2300, false);
        Print("[RDF Promo] AI loading 2B14");
        return true;
    }

    protected void FallbackMuzzleLaunchIfNeeded()
    {
        if (!m_Running)
            return;
        if (m_DidFireShell)
            return;
        Print("[RDF Promo] mortar anim missed fire — launching from 2B14 muzzle.", LogLevel.WARNING);
        TryFireShellFromMuzzle();
    }

    protected void TryFireShellFromMuzzle()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;
        if (m_DidFireShell)
            return;

        RefreshMuzzleLaunchPos();
        vector dir = PromoMuzzleDir();
        Resource prefabRes = Resource.Load(SHELL_PREFAB);
        if (!prefabRes || !prefabRes.IsValid())
            return;

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = m_LaunchPos;
        IEntity shell = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!shell)
            return;

        vector basis[4];
        Math3D.DirectionAndUpMatrix(dir, vector.Up, basis);
        basis[3] = m_LaunchPos;
        shell.SetTransform(basis);

        ProjectileMoveComponent move = ProjectileMoveComponent.Cast(
            shell.FindComponent(ProjectileMoveComponent));
        if (!move)
        {
            SCR_EntityHelper.DeleteEntityAndChildren(shell);
            return;
        }

        ApplyPromoShellCharge(shell, move);
        move.EnableSimulation(shell);
        move.SetBulletCoef(SHELL_SPEED_COEF);
        move.Launch(dir, vector.Zero, 1.0, shell, null, m_MortarGunner, null, null);
        BindLiveShell(shell, move);
    }

    protected void BindLiveShell(IEntity shell, ProjectileMoveComponent move)
    {
        if (!shell)
            return;
        m_LiveShells.Insert(shell);
        m_ActiveShell = shell;
        RDF_RadarScattererRegistry.Unignore(shell);
        m_DidFireShell = true;
        m_DidLiveSolve = false;
        if (m_ShellLiveTrail)
            m_ShellLiveTrail.Clear();

        vector dir = PromoMuzzleDir();
        float liveCoef = SHELL_SPEED_COEF;
        vector liveVel = dir * (SHELL_INIT_SPEED_MS * liveCoef);
        if (move)
        {
            liveCoef = move.GetBulletSpeedCoef();
            if (liveCoef < 0.2)
                liveCoef = SHELL_SPEED_COEF;
            liveVel = move.GetVelocity();
            float liveSpeed = liveVel.Length();
            if (liveSpeed < 10.0)
                liveVel = dir * (SHELL_INIT_SPEED_MS * liveCoef);
        }
        m_ShellMuzzleVel = liveVel;

        float liveSpeedBind = liveVel.Length();
        bool usedEngine = false;
        if (move)
            usedEngine = ApplyEngineSimImpact(move, liveSpeedBind);
        if (usedEngine)
            m_DidLiveSolve = true;
        else
            ApplyWorldImpactSolve(m_LaunchPos, liveVel);

        Print("[RDF Promo] fired coef="
            + FormatOneDecimal(liveCoef)
            + " speed="
            + FormatOneDecimal(liveVel.Length())
            + "m/s tof="
            + FormatOneDecimal(m_ShellTofS)
            + "s impact="
            + m_ShellImpactPos.ToString());

        RDF_RadarPromoShot shot = CurrentShot();
        if (shot)
            StretchShellCoverage();
    }

    protected void OnPromoMortarFired(IEntity effectEntity, BaseMuzzleComponent muzzle, IEntity projectileEntity)
    {
        if (!m_Running)
            return;
        if (!projectileEntity)
            return;
        if (m_DidFireShell)
            return;

        RefreshMuzzleLaunchPos();
        ProjectileMoveComponent move = ProjectileMoveComponent.Cast(
            projectileEntity.FindComponent(ProjectileMoveComponent));
        BindLiveShell(projectileEntity, move);
        Print("[RDF Promo] 2B14 muzzle fired live shell");
    }

    protected vector PromoMuzzleDir()
    {
        if (m_Mortar)
        {
            SCR_MortarMuzzleComponent muzzle = SCR_MortarMuzzleComponent.Cast(
                m_Mortar.FindComponent(SCR_MortarMuzzleComponent));
            if (muzzle)
            {
                vector dir = muzzle.GetMuzzleDirection();
                if (dir.LengthSq() > 0.01)
                    return dir;
            }
        }
        float elevRad = SHELL_ELEVATION_DEG * Math.DEG2RAD;
        return Vector(
            m_FlatFwd[0] * Math.Cos(elevRad),
            Math.Sin(elevRad),
            m_FlatFwd[2] * Math.Cos(elevRad));
    }

    protected float CurrentShotDuration()
    {
        RDF_RadarPromoShot shot = CurrentShot();
        if (!shot)
            return 0.0;
        return shot.m_DurationS;
    }

    protected void UpdateShells()
    {
        if (!m_LiveShells)
            return;
        for (int i = m_LiveShells.Count() - 1; i >= 0; i--)
        {
            IEntity shell = m_LiveShells.Get(i);
            if (shell)
                continue;
            m_LiveShells.Remove(i);
        }
        if (m_ActiveShell)
            return;
        if (m_LiveShells.Count() > 0)
            m_ActiveShell = m_LiveShells.Get(m_LiveShells.Count() - 1);
        else
            m_ActiveShell = null;
    }

    protected void MaybeFireRocket(float shotT)
    {
        if (shotT < ROCKET_FIRE_DELAY_S)
            return;
        if (m_DidFireRocket)
            return;
        if (m_Rocket)
            return;

        IEntity locked;
        vector aimPos;
        bool haveAim = false;
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor && sensor.GetLockedTarget(locked, aimPos))
            haveAim = true;
        else if (m_AirTarget)
        {
            locked = m_AirTarget;
            aimPos = FollowHeliPos();
            haveAim = true;
        }
        if (!haveAim)
            return;
        if (!TryFireRocket(aimPos, locked))
            return;
        m_DidFireRocket = true;
    }

    protected void MaybeFireArmRocket(float shotT)
    {
        if (shotT < ARM_FIRE_DELAY_S)
            return;
        if (m_DidFireArm)
            return;
        if (m_Rocket)
            return;

        IEntity locked;
        vector aimPos;
        bool haveAim = false;
        bool radiating;
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor && sensor.GetArmAim(locked, aimPos, radiating))
            haveAim = true;
        else if (m_JamHeli)
        {
            locked = m_JamHeli;
            aimPos = FollowJamHeliPos();
            haveAim = true;
        }
        if (!haveAim)
            return;
        if (!TryFireRocket(aimPos, locked))
            return;
        m_DidFireArm = true;
        Print("[RDF Promo] ARM along emitter beam");
    }

    protected bool TryFireRocket(vector aimPos, IEntity lockedTarget)
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;
        Resource prefabRes = Resource.Load(ROCKET_PREFAB);
        if (!prefabRes)
            return false;
        if (!prefabRes.IsValid())
            return false;

        vector launchPos = m_RadarAnchor + Vector(0.0, 4.5, 0.0) + m_FlatFwd * 6.0;
        vector toAim = aimPos - launchPos;
        float dist = toAim.Length();
        if (dist < 1.0)
            return false;
        vector dir = toAim * (1.0 / dist);

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = launchPos;
        IEntity rocket = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!rocket)
            return false;

        vector basis[4];
        Math3D.DirectionAndUpMatrix(dir, vector.Up, basis);
        basis[3] = launchPos;
        rocket.SetTransform(basis);

        MissileMoveComponent missile = MissileMoveComponent.Cast(
            rocket.FindComponent(MissileMoveComponent));
        if (!missile)
        {
            SCR_EntityHelper.DeleteEntityAndChildren(rocket);
            return false;
        }

        missile.EnableSimulation(rocket);
        missile.Launch(dir, vector.Zero, 1.0, rocket, null, m_RadarVan, lockedTarget, null);
        missile.SetVelocity(dir * ROCKET_LAUNCH_MS);

        m_Rocket = rocket;
        m_RocketGuide = new RDF_RadarRocketGuidanceState();
        m_RocketGuide.Begin(System.GetTickCount() * 0.001);
        if (!m_RocketTrail)
            m_RocketTrail = new array<vector>();
        m_RocketTrail.Clear();
        m_RocketTrail.Insert(launchPos);
        Print("[RDF Promo] Hydra70 engage range=" + FormatOneDecimal(dist) + "m");
        return true;
    }

    protected void UpdateRocketGuidance()
    {
        if (!m_Rocket)
            return;
        if (!m_RocketGuide)
            return;

        float dt = TICK_MS * 0.001;
        float nowS = System.GetTickCount() * 0.001;
        vector aimPos = FollowHeliPos();
        vector aimVel = m_HeliSmoothFwd * (AIR_ORBIT_RATE_RAD_S * AIR_ORBIT_RADIUS_M);
        RDF_RadarPromoShot shot = CurrentShot();
        bool armShot = false;
        if (shot)
        {
            if (shot.m_CamStyle == CAM_ARM)
                armShot = true;
        }
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (armShot)
        {
            aimPos = FollowJamHeliPos();
            aimVel = m_JamSmoothFwd * (AIR_ORBIT_RATE_RAD_S * AIR_ORBIT_RADIUS_M);
            IEntity armEnt;
            vector armPos;
            bool radiating;
            if (sensor && sensor.GetArmAim(armEnt, armPos, radiating))
                aimPos = armPos;
        }
        else
        {
            IEntity locked;
            vector lockPos;
            if (sensor && sensor.GetLockedTarget(locked, lockPos))
            {
                if (locked == m_AirTarget)
                    aimPos = lockPos;
            }
        }

        RDF_RadarRocketGuidance.Update(m_Rocket, aimPos, aimVel, dt, nowS, m_RocketGuide);

        vector rocketPos = m_Rocket.GetOrigin();
        if (!m_RocketTrail)
            m_RocketTrail = new array<vector>();
        if (m_RocketTrail.Count() == 0)
            m_RocketTrail.Insert(rocketPos);
        else
        {
            vector last = m_RocketTrail.Get(m_RocketTrail.Count() - 1);
            if (vector.DistanceSq(last, rocketPos) > 25.0)
                m_RocketTrail.Insert(rocketPos);
            while (m_RocketTrail.Count() > 40)
                m_RocketTrail.RemoveOrdered(0);
        }
    }

    protected void ClearRocket()
    {
        if (m_Rocket)
        {
            RDF_RadarScattererRegistry.Unregister(m_Rocket);
            SCR_EntityHelper.DeleteEntityAndChildren(m_Rocket);
            m_Rocket = null;
        }
        m_RocketGuide = null;
        if (m_RocketTrail)
            m_RocketTrail.Clear();
    }

    protected void ForceLockHeli()
    {
        if (!m_AirTarget)
            return;
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor)
            return;
        RDF_RadarLockManager lockMgr = sensor.GetLockManager();
        if (!lockMgr)
            return;

        IEntity locked;
        vector lockPos;
        if (sensor.GetLockedTarget(locked, lockPos))
        {
            if (locked == m_AirTarget)
                return;
        }

        array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
        if (!tracks)
            return;
        int i;
        for (i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (!tr)
                continue;
            if (tr.m_Entity != m_AirTarget)
                continue;
            lockMgr.LockTrackId(tr.m_TrackId);
            return;
        }
    }

    protected void ForceLockArm()
    {
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor)
            return;
        RDF_RadarLockManager lockMgr = sensor.GetLockManager();
        if (!lockMgr)
            return;

        IEntity armEnt;
        vector armPos;
        bool radiating;
        if (sensor.GetArmAim(armEnt, armPos, radiating))
        {
            if (!m_JamHeli)
                return;
            if (armEnt == m_JamHeli)
                return;
        }

        array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
        if (!tracks)
            return;
        int jamTrackId = -1;
        int emitterTrackId = -1;
        int i;
        for (i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (!tr)
                continue;
            if (m_JamHeli)
            {
                if (tr.m_Entity == m_JamHeli)
                    jamTrackId = tr.m_TrackId;
            }
            if (tr.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            {
                if (emitterTrackId < 0)
                    emitterTrackId = tr.m_TrackId;
            }
        }
        if (jamTrackId >= 0)
        {
            lockMgr.LockArmTrackId(jamTrackId);
            return;
        }
        if (emitterTrackId >= 0)
            lockMgr.LockArmTrackId(emitterTrackId);
    }

    protected void ArmPromoEmitter(bool on)
    {
        IEntity emitter = m_JamHeli;
        if (!emitter)
            emitter = m_AirTarget;
        if (!emitter)
            return;
        vector pos = FollowJamHeliPos();
        if (on)
        {
            RDF_RadarEmitterRegistry.RegisterWithRadio(
                emitter,
                pos,
                true,
                1.0,
                9000000000.0,
                120000.0,
                32.0);
            m_EmitterArmed = true;
            return;
        }
        RDF_RadarEmitterRegistry.SetEmitting(emitter, false);
        m_EmitterArmed = false;
    }

    protected void ApplyRadarMode(int mode)
    {
        RDF_RadarSettings cfg;
        if (mode == MODE_STARE)
            cfg = BuildStareConfig();
        else if (mode == MODE_WLR)
            cfg = BuildWlrConfig();
        else if (mode == MODE_PD)
            cfg = BuildPulseDopplerConfig();
        else if (mode == MODE_EW)
            cfg = BuildEwConfig();
        else if (mode == MODE_ESM)
            cfg = BuildEsmConfig();
        else
            cfg = BuildSearchConfig();

        if (mode == MODE_WLR)
            ClearRocket();

        ApplySensorConfig(cfg);
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor)
            sensor.ResetSession();
        if (mode == MODE_WLR)
            RDF_RadarHUD.SetDisplayRangeCap(2000.0);
        else if (mode == MODE_STARE)
            RDF_RadarHUD.SetDisplayRangeCap(420.0);
        else if (mode == MODE_PD)
            RDF_RadarHUD.SetDisplayRangeCap(640.0);
        else if (mode == MODE_ESM)
            RDF_RadarHUD.SetDisplayRangeCap(1400.0);
        else
            RDF_RadarHUD.SetDisplayRangeCap(720.0);
        RDF_RadarVisualSettings vis = RDF_RadarAutoRunner.GetVisualSettings();
        if (vis)
        {
            vis.m_HideFalsePlots = true;
            vis.m_DrawSectorSweep = true;
            vis.m_DrawRangeRings = true;
            if (mode == MODE_WLR)
            {
                vis.m_SectorVisualRangeM = 1400.0;
                vis.m_DrawTrackRibbon = true;
                vis.m_PointSize = 0.85;
                vis.m_ProjectilePointSize = 0.55;
                vis.m_CollapsePlotsByEntity = true;
                vis.m_DrawAfterglow = false;
                vis.m_DrawRays = false;
            }
            else if (mode == MODE_PD)
            {
                vis.m_SectorVisualRangeM = 900.0;
                vis.m_DrawTrackRibbon = true;
                vis.m_DrawAfterglow = true;
                vis.m_DrawRays = true;
                vis.m_DrawLockBeam = true;
                vis.m_PointSize = 1.6;
                vis.m_RayAlpha = 0.55;
                vis.m_LockBeamEndRadiusM = 14.0;
                vis.m_LockBeamAlpha = 0.55;
            }
            else if (mode == MODE_EW)
            {
                vis.m_HideFalsePlots = false;
                vis.m_SectorVisualRangeM = 900.0;
                vis.m_DrawRays = true;
                vis.m_DrawAfterglow = true;
                vis.m_DrawTrackRibbon = false;
                vis.m_PointSize = 2.2;
                vis.m_RayAlpha = 0.70;
            }
            else if (mode == MODE_ESM)
            {
                vis.m_SectorVisualRangeM = 1100.0;
                vis.m_DrawSectorSweep = false;
                vis.m_DrawRangeRings = false;
                vis.m_DrawRays = false;
                vis.m_DrawLockBeam = false;
                vis.m_DrawTrackRibbon = false;
                vis.m_DrawAfterglow = false;
                vis.m_PointSize = 3.2;
            }
            else
            {
                vis.m_SectorVisualRangeM = 750.0;
                vis.m_DrawTrackRibbon = false;
                vis.m_DrawRays = false;
                vis.m_PointSize = 0.85;
                vis.m_ProjectilePointSize = 0.55;
            }
            if (mode == MODE_STARE)
            {
                vis.m_LockBeamEndRadiusM = 22.0;
                vis.m_LockBeamAlpha = 0.82;
                vis.m_DrawLockBeam = true;
                vis.m_DrawRays = true;
                vis.m_RayAlpha = 0.45;
                vis.m_PointSize = 0.85;
                vis.m_ProjectilePointSize = 0.55;
                vis.m_CollapsePlotsByEntity = true;
                vis.m_DrawAfterglow = false;
            }
            else if (mode != MODE_PD)
            {
                if (mode != MODE_ESM)
                {
                    vis.m_LockBeamEndRadiusM = 4.2;
                    vis.m_LockBeamAlpha = 0.42;
                }
            }
        }
        if (sensor)
        {
            RDF_RadarLockManager lockMgr = sensor.GetLockManager();
            if (lockMgr)
            {
                if (mode == MODE_STARE)
                {
                    lockMgr.SetAutoAcquire(true);
                    lockMgr.SetTypeFilter(true, false, false);
                    lockMgr.SetArmRequireLiveEmitter(false);
                }
                else if (mode == MODE_ESM)
                {
                    lockMgr.SetAutoAcquire(true);
                    lockMgr.SetTypeFilter(false, false, true);
                    lockMgr.SetArmRequireLiveEmitter(true);
                    lockMgr.SetAcquireHits(1);
                    lockMgr.SetMaxLockRange(5000.0);
                    lockMgr.SetLockSector(0.0);
                }
                else
                {
                    lockMgr.SetArmRequireLiveEmitter(false);
                    lockMgr.SetTypeFilter(true, false, false);
                }
            }
        }
    }

    protected RDF_RadarSettings BuildSearchConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(128);
        cfg.m_Range = 3500.0;
        cfg.m_SectorHalfAngleDeg = 55.0;
        cfg.m_UpdateInterval = 0.15;
        cfg.m_MaxLosTracesPerScan = 96;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = true;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_EnableDemClutter = false;
        cfg.m_DetectionSnrDb = 0.0;
        cfg.m_KeepUndetected = false;
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableMechanicalScan = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.m_OriginOffset = Vector(0.0, 4.0, 0.0);
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_ScattererMaxEntries = 2048;
        cfg.m_ScattererDiscoveryIntervalS = 0.25;

        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 40.0;
        hw.m_ScanRpm = 0.0;
        hw.m_EnableMti = false;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("promo_gnd", 0.0, 28.0, 0.0);
        hw.AddElevationBeam("promo_low", 8.0, 30.0, 0.0);
        hw.AddElevationBeam("promo_mid", 18.0, 36.0, 0.0);
        hw.AddElevationBeam("promo_hi", 28.0, 40.0, -0.5);
        hw.Validate();
        cfg.m_Hardware = hw;
        cfg.m_KeepEntityTruth = true;
        cfg.m_MinDistance = PROMO_MIN_RANGE_M;
        cfg.Validate();
        return cfg;
    }

    protected RDF_RadarSettings BuildPulseDopplerConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreatePulseDopplerSettings(96);
        cfg.m_Range = 3000.0;
        cfg.m_SectorHalfAngleDeg = 42.0;
        cfg.m_UpdateInterval = 0.18;
        cfg.m_IncludeVehicles = false;
        cfg.m_IncludeProjectiles = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_EnableDemClutter = true;
        cfg.m_DetectionSnrDb = 2.0;
        cfg.m_KeepEntityTruth = true;
        cfg.m_KeepUndetected = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.m_OriginOffset = Vector(0.0, 4.0, 0.0);
        cfg.m_MaxLosTracesPerScan = 96;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_MinDistance = PROMO_MIN_RANGE_M;
        cfg.Validate();
        return cfg;
    }

    protected RDF_RadarSettings BuildEwConfig()
    {
        RDF_RadarSettings cfg = BuildSearchConfig();
        cfg.m_DetectionSnrDb = 8.0;
        cfg.m_KeepUndetected = false;
        if (!cfg.m_EwStack)
            cfg.m_EwStack = new RDF_RadarEwStack();
        cfg.m_EwStack.Clear();

        RDF_RadarNoiseJammerEffect jam = new RDF_RadarNoiseJammerEffect();
        jam.m_Enabled = true;
        jam.m_ErpW = 80000.0;
        jam.m_BandwidthHz = 8000000.0;
        jam.ConfigurePhysicsBeam(-18.0);
        if (m_JamHeli)
            jam.m_Position = FollowJamHeliPos();
        else if (m_AirTarget)
            jam.m_Position = FollowHeliPos();
        else
            jam.m_Position = StagePoint(380.0, 40.0, 40.0);
        cfg.m_EwStack.Add(jam);

        RDF_RadarDeceptionJammerEffect decoy = new RDF_RadarDeceptionJammerEffect();
        decoy.m_Enabled = true;
        decoy.AddFalsePlot(180.0, -22.0, 0.08, 12.0, 8.0);
        decoy.AddFalsePlot(260.0, -8.0, 0.08, 18.0, 10.0);
        decoy.AddFalsePlot(340.0, 6.0, 0.10, 8.0, 12.0);
        decoy.AddFalsePlot(420.0, 16.0, 0.08, 22.0, 9.0);
        decoy.AddFalsePlot(510.0, -14.0, 0.07, -6.0, 11.0);
        decoy.AddFalsePlot(620.0, 28.0, 0.09, 14.0, 8.0);
        cfg.m_EwStack.Add(decoy);
        cfg.Validate();
        return cfg;
    }

    protected RDF_RadarSettings BuildEsmConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateEsmSettings(96);
        cfg.m_Range = 5000.0;
        cfg.m_SectorHalfAngleDeg = 70.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_DetectionSnrDb = 0.0;
        cfg.m_KeepUndetected = false;
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableCfarGate = false;
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.m_OriginOffset = Vector(0.0, 4.0, 0.0);
        cfg.m_MaxLosTracesPerScan = 96;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_MinDistance = PROMO_MIN_RANGE_M;
        cfg.Validate();
        return cfg;
    }

    protected RDF_RadarSettings BuildStareConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateStareSettings(96);
        cfg.m_Range = 3000.0;
        cfg.m_SectorHalfAngleDeg = 40.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = false;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_DetectionSnrDb = 3.0;
        cfg.m_KeepUndetected = false;
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableCfarGate = false;
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.m_OriginOffset = Vector(0.0, 4.0, 0.0);
        cfg.m_MaxLosTracesPerScan = 96;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_ScattererDiscoveryIntervalS = 0.25;

        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 28.0;
        hw.m_ScanRpm = 0.0;
        hw.m_EnableMti = false;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("lock_low", 6.0, 16.0, 0.0);
        hw.AddElevationBeam("lock_mid", 14.0, 18.0, 0.0);
        hw.Validate();
        cfg.m_Hardware = hw;
        cfg.m_KeepEntityTruth = true;
        cfg.m_MinDistance = PROMO_MIN_RANGE_M;
        cfg.Validate();
        return cfg;
    }

    protected RDF_RadarSettings BuildWlrConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateWlrSettings(128);
        cfg.m_Range = 6000.0;
        cfg.m_SectorHalfAngleDeg = 70.0;
        cfg.m_UpdateInterval = 0.15;
        cfg.m_IncludeVehicles = false;
        cfg.m_IncludeProjectiles = true;
        cfg.m_EnableWeaponLocate = true;
        cfg.m_EnableWlrHudAlerts = true;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_EnableCfarGate = false;
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableDemGroundForWlr = false;
        cfg.m_DetectionSnrDb = -20.0;
        cfg.m_KeepEntityTruth = true;
        cfg.m_KeepUndetected = true;
        cfg.m_OriginOffset = Vector(0.0, 4.0, 0.0);
        cfg.m_MaxLosTracesPerScan = 96;
        cfg.m_ScattererDiscoveryIntervalS = 0.2;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_WeaponLocateMinHits = 4;
        cfg.m_WeaponLocateMinSpanS = 0.8;
        cfg.m_EnableBallisticPrediction = true;
        cfg.m_ShellAirDrag = RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE;
        cfg.m_MinDistance = PROMO_MIN_RANGE_M;
        cfg.Validate();
        return cfg;
    }

    protected void ApplyCameraForShot(RDF_RadarPromoShot shot, float shotT, float elapsedS)
    {
        float uLin = 0.0;
        if (shot.m_DurationS > 0.001)
            uLin = shotT / shot.m_DurationS;
        if (uLin < 0.0)
            uLin = 0.0;
        if (uLin > 1.0)
            uLin = 1.0;

        vector eye;
        vector lookAt;
        float fov = shot.m_FovDeg;
        int style = shot.m_CamStyle;
        float u = Smooth01(uLin);

        if (style == CAM_HOLD)
        {
            CraneEnds(eye, lookAt, 0.0);
            vector dir = lookAt - eye;
            float dirLen = dir.Length();
            if (dirLen > 0.05)
                eye = eye + (dir / dirLen) * (shotT * 1.4);
            fov = Math.Lerp(58.0, 52.0, u);
        }
        else if (style == CAM_CRANE)
        {
            float dive = EaseInCubic(uLin);
            CraneEnds(eye, lookAt, dive);
            fov = Math.Lerp(62.0, 34.0, dive);
        }
        else if (style == CAM_ORBIT)
        {
            FrontArcCamera(eye, lookAt, u, false);
        }
        else if (style == CAM_LOW)
        {
            ConvoyCamera(eye, lookAt, u);
            fov = Math.Lerp(36.0, 26.0, u);
        }
        else if (style == CAM_CHASE)
        {
            if (shot.m_RadarMode == MODE_PD)
            {
                DopplerCamera(eye, lookAt, elapsedS, u);
                fov = Math.Lerp(42.0, 34.0, u);
            }
            else
            {
                ChaseCamera(eye, lookAt, shotT, elapsedS, u);
                fov = Math.Lerp(22.0, 38.0, u);
            }
        }
        else if (style == CAM_LOCK)
        {
            float punch = EaseInCubic(uLin);
            LockPunchCamera(eye, lookAt, elapsedS, punch);
            fov = Math.Lerp(38.0, 22.0, punch);
        }
        else if (style == CAM_ROCKET)
        {
            RideRocketCamera(eye, lookAt, u, FollowHeliPos());
            fov = Math.Lerp(32.0, 24.0, u);
        }
        else if (style == CAM_LAUNCH)
        {
            LaunchCamera(eye, lookAt, u);
            fov = Math.Lerp(30.0, 22.0, u);
        }
        else if (style == CAM_ARC)
        {
            RideShellCamera(eye, lookAt, u);
            fov = Math.Lerp(30.0, 36.0, u);
        }
        else if (style == CAM_SHELL)
        {
            ShellCamera(eye, lookAt, shotT);
            fov = Math.Lerp(42.0, 66.0, EaseOutCubic(uLin));
        }
        else if (style == CAM_IMPACT)
        {
            float slam = EaseInCubic(uLin);
            ImpactCamera(eye, lookAt, slam);
            fov = Math.Lerp(48.0, 28.0, slam);
        }
        else if (style == CAM_JAM)
        {
            float jamU = 0.0;
            if (shotT > JAM_ON_S)
            {
                float span = shot.m_DurationS - JAM_ON_S;
                if (span > 0.001)
                    jamU = (shotT - JAM_ON_S) / span;
                if (jamU > 1.0)
                    jamU = 1.0;
                jamU = Smooth01(jamU);
            }
            JamCamera(eye, lookAt, jamU);
            fov = Math.Lerp(38.0, 32.0, jamU);
        }
        else if (style == CAM_ESM_LOOK)
        {
            float listenU = 0.0;
            if (shotT > ESM_LISTEN_S)
            {
                float span = shot.m_DurationS - ESM_LISTEN_S;
                if (span > 0.001)
                    listenU = (shotT - ESM_LISTEN_S) / span;
                if (listenU > 1.0)
                    listenU = 1.0;
                listenU = Smooth01(listenU);
            }
            EsmCamera(eye, lookAt, listenU);
            fov = Math.Lerp(40.0, 28.0, listenU);
        }
        else if (style == CAM_ARM)
        {
            if (m_Rocket)
            {
                RideRocketCamera(eye, lookAt, u, FollowJamHeliPos());
                fov = Math.Lerp(32.0, 24.0, u);
            }
            else
            {
                EsmCamera(eye, lookAt, 1.0);
                fov = 28.0;
            }
        }
        else
        {
            vector nearEye;
            vector nearLook;
            CraneEnds(nearEye, nearLook, 1.0);
            vector farEye = LiftEye(
                m_RadarAnchor - m_FlatFwd * 90.0 + m_FlatRight * 56.0 + Vector(0.0, 130.0, 0.0));
            vector farLook = m_LookAtWide;
            float pull = EaseOutCubic(uLin);
            eye = LerpVec(nearEye, farEye, pull);
            lookAt = LerpVec(nearLook, farLook, pull);
            fov = Math.Lerp(34.0, 58.0, pull);
        }

        ApplyWorldCamera(eye, lookAt, fov);
    }

    protected void ApplyWorldCamera(vector eye, vector lookAt, float fovDeg)
    {
        m_CamEyeDesired = eye;
        m_CamLookDesired = lookAt;
        m_CamFovDesired = fovDeg;
        if (!m_PromoCamera)
        {
            m_CamEye = eye;
            m_CamLookAt = lookAt;
            m_CamFovDeg = fovDeg;
            CommitCamera();
        }
    }

    protected float CameraFollowTau(int style)
    {
        if (style == CAM_CHASE)
            return 0.10;
        if (style == CAM_LOCK)
            return 0.06;
        if (style == CAM_LOW)
            return 0.12;
        if (style == CAM_LAUNCH)
            return 0.08;
        if (style == CAM_ARC)
            return 0.10;
        if (style == CAM_IMPACT)
            return 0.05;
        if (style == CAM_SHELL)
            return 0.12;
        if (style == CAM_ROCKET)
            return 0.08;
        if (style == CAM_ARM)
            return 0.08;
        if (style == CAM_JAM)
            return 0.10;
        if (style == CAM_ESM_LOOK)
            return 0.12;
        if (style == CAM_CRANE)
            return 0.08;
        return 0.06;
    }

    protected void StepCameraSmooth(float dt)
    {
        if (dt < 0.0)
            dt = 0.0;
        if (dt > 0.05)
            dt = 0.05;

        RDF_RadarPromoShot shot = CurrentShot();
        int style = CAM_HOLD;
        if (shot)
            style = shot.m_CamStyle;

        bool snap = !m_CamSmoothInit;
        if (m_CamSmoothShot != m_ShotIndex)
            snap = true;
        if (snap)
        {
            m_CamEye = m_CamEyeDesired;
            m_CamLookAt = m_CamLookDesired;
            m_CamFovDeg = m_CamFovDesired;
            m_CamSmoothInit = true;
            m_CamSmoothShot = m_ShotIndex;
            return;
        }

        float tau = CameraFollowTau(style);
        float a = dt / (tau + dt);
        m_CamEye = m_CamEye + (m_CamEyeDesired - m_CamEye) * a;
        m_CamLookAt = m_CamLookAt + (m_CamLookDesired - m_CamLookAt) * a;
        m_CamFovDeg = m_CamFovDeg + (m_CamFovDesired - m_CamFovDeg) * a;
    }

    protected vector FollowHeliPos()
    {
        if (m_AirReleased)
        {
            if (m_AirTarget)
                return m_AirTarget.GetOrigin();
            return m_OrbitCenter;
        }
        if (m_HeliSmoothInit)
            return m_HeliSmoothPos;
        if (m_AirTarget)
            return m_AirTarget.GetOrigin();
        return m_OrbitCenter;
    }

    protected vector FollowJamHeliPos()
    {
        if (m_JamReleased)
        {
            if (m_JamHeli)
                return m_JamHeli.GetOrigin();
            return FollowHeliPos();
        }
        if (m_JamSmoothInit)
            return m_JamSmoothPos;
        if (m_JamHeli)
            return m_JamHeli.GetOrigin();
        return FollowHeliPos();
    }

    protected void CommitCamera()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;

        vector dir = m_CamLookAt - m_CamEye;
        float len = dir.Length();
        if (len < 0.05)
            return;
        dir = dir / len;

        vector up = vector.Up;
        if (Math.AbsFloat(vector.Dot(dir, up)) > 0.97)
            up = "0 0 1";

        vector mat[4];
        Math3D.DirectionAndUpMatrix(dir, up, mat);
        mat[3] = m_CamEye;

        if (m_PromoCamera)
        {
            m_PromoCamera.SetTransform(mat);
            m_PromoCamera.SetFOVDegree(m_CamFovDeg);
            m_PromoCamera.ApplyTransform(0.0);
            CameraManager mgr = GetGame().GetCameraManager();
            if (mgr)
            {
                if (mgr.CurrentCamera() != m_PromoCamera)
                    mgr.SetCamera(m_PromoCamera);
            }
            return;
        }

        int idx = 0;
        CameraManager mgrFallback = GetGame().GetCameraManager();
        if (mgrFallback)
        {
            CameraBase cam = mgrFallback.CurrentCamera();
            if (cam)
                idx = cam.GetCameraIndex();
        }

        world.SetCameraVerticalFOV(idx, m_CamFovDeg);
        world.SetCameraEx(idx, mat);
    }

    protected void CraneEnds(out vector eye, out vector lookAt, float u)
    {
        vector high = LiftEye(
            m_RadarAnchor - m_FlatFwd * 130.0 + m_FlatRight * 42.0 + Vector(0.0, 98.0, 0.0));
        vector low = LiftEyeMin(
            m_RadarAnchor - m_FlatFwd * 12.0 + m_FlatRight * 4.5 + Vector(0.0, 3.8, 0.0),
            2.2);
        vector lookHigh = m_LookAtWide;
        vector lookLow = m_RadarAnchor + m_FlatFwd * 36.0 + Vector(0.0, 3.2, 0.0);
        eye = LerpVec(high, low, u);
        lookAt = LerpVec(lookHigh, lookLow, u);
    }

    protected void FrontArcCamera(out vector eye, out vector lookAt, float u, bool wide)
    {
        lookAt = m_LookAtSearch;
        float boom = 165.0;
        float height = 38.0;
        float yaw0 = -0.50;
        float yaw1 = 0.85;
        if (wide)
        {
            lookAt = m_LookAtWide;
            boom = 255.0;
            height = 72.0;
            yaw0 = -0.35;
            yaw1 = 0.75;
        }

        float yaw = Math.Lerp(yaw0, yaw1, u);
        float c = Math.Cos(yaw);
        float s = Math.Sin(yaw);
        vector boomDir = Vector(
            m_FlatFwd[0] * c + m_FlatRight[0] * s,
            0.0,
            m_FlatFwd[2] * c + m_FlatRight[2] * s);
        eye = LiftEye(lookAt - boomDir * boom + Vector(0.0, height, 0.0));
    }

    protected bool ShotDrawsShell(int style)
    {
        if (style == CAM_LAUNCH)
            return true;
        if (style == CAM_ARC)
            return true;
        if (style == CAM_SHELL)
            return true;
        if (style == CAM_IMPACT)
            return true;
        return false;
    }

    protected vector FalsePlotWorld(float rangeM, float azimuthDeg, float elevationDeg)
    {
        float az = azimuthDeg * Math.DEG2RAD;
        float el = elevationDeg * Math.DEG2RAD;
        vector flat = m_FlatFwd * Math.Cos(az) + m_FlatRight * Math.Sin(az);
        float flatLen = flat.Length();
        if (flatLen > 0.001)
            flat = flat * (1.0 / flatLen);
        else
            flat = m_FlatFwd;
        vector origin = RadarScanOrigin();
        return origin + flat * (rangeM * Math.Cos(el)) + Vector(0.0, rangeM * Math.Sin(el), 0.0);
    }

    protected void DrawPromoOverlay(RDF_RadarPromoShot shot, float shotT)
    {
        if (!shot)
            return;
        if (ShotDrawsShell(shot.m_CamStyle))
        {
            DrawShellOverlay();
            return;
        }

        if (!m_ShellOverlay)
            m_ShellOverlay = new RDF_DebugShapeManager();
        m_ShellOverlay.Clear();

        ShapeFlags flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP;
        int style = shot.m_CamStyle;
        if (shot.m_RadarMode == MODE_PD)
            DrawDopplerOverlay(flags, shotT);
        if (m_AppliedRadarMode == MODE_STARE)
            DrawLockConeOverlay(flags);
        if (style == CAM_ROCKET)
            DrawRocketOverlay(flags);
        if (style == CAM_ARM)
            DrawRocketOverlay(flags);
        if (m_AppliedRadarMode == MODE_EW)
            DrawEwOverlay(flags);
        if (m_AppliedRadarMode == MODE_ESM)
        {
            if (m_EmitterArmed)
                DrawEsmOverlay(flags);
        }
    }

    protected void DrawVelocityStreak(
        vector pos,
        vector velDir,
        float lengthM,
        float headR,
        int color,
        ShapeFlags flags)
    {
        float dirLen = velDir.Length();
        if (dirLen < 0.001)
            return;
        vector dir = velDir * (1.0 / dirLen);
        vector tail = pos - dir * lengthM;
        m_ShellOverlay.AddLine(pos, tail, color, flags);
        m_ShellOverlay.AddSphere(pos, headR, color, flags);
        m_ShellOverlay.AddSphere(pos - dir * (lengthM * 0.38), headR * 0.62, color, flags);
        m_ShellOverlay.AddSphere(tail, headR * 0.32, color, flags);
    }

    protected void DrawBeamCone(
        vector origin,
        vector aim,
        float endRadiusM,
        int coreColor,
        int wallColor,
        ShapeFlags flags)
    {
        vector toAim = aim - origin;
        float dist = toAim.Length();
        if (dist < 1.0)
            return;
        vector dir = toAim * (1.0 / dist);
        vector side = Vector(dir[2], 0.0, -dir[0]);
        float sideLen = side.Length();
        if (sideLen < 0.001)
            side = Vector(1.0, 0.0, 0.0);
        else
            side = side * (1.0 / sideLen);
        vector binormal = Vector(
            dir[1] * side[2] - dir[2] * side[1],
            dir[2] * side[0] - dir[0] * side[2],
            dir[0] * side[1] - dir[1] * side[0]);
        float binLen = binormal.Length();
        if (binLen > 0.001)
            binormal = binormal * (1.0 / binLen);

        m_ShellOverlay.AddLine(origin, aim, coreColor, flags);

        int spokes = 10;
        int i;
        for (i = 0; i < spokes; i++)
        {
            float ang = (Math.PI2 * i) / spokes;
            vector offset = side * (Math.Cos(ang) * endRadiusM)
                + binormal * (Math.Sin(ang) * endRadiusM);
            m_ShellOverlay.AddLine(origin, aim + offset, wallColor, flags);
        }

        int ring;
        for (ring = 1; ring <= 3; ring++)
        {
            float t = ring / 3.0;
            vector centre = origin + dir * (dist * t);
            float r = endRadiusM * t;
            m_ShellOverlay.AddCircleXZ(centre, r, wallColor, 16, flags);
        }
        m_ShellOverlay.AddSphere(aim, 4.8, coreColor, flags);
        m_ShellOverlay.AddSphere(aim, 2.0, ARGBF(1.0, 1.0, 0.92, 0.55), flags);
    }

    protected void DrawAlivePlot(vector pos, ShapeFlags flags)
    {
        vector drop = pos;
        BaseWorld world = GetGame().GetWorld();
        if (world)
            drop[1] = world.GetSurfaceY(pos[0], pos[2]) + 0.6;
        else
            drop[1] = m_RadarAnchor[1];
        m_ShellOverlay.AddSphere(pos, 4.2, ARGBF(0.9, 0.2, 1.0, 0.35), flags);
        m_ShellOverlay.AddLine(pos, drop, ARGBF(0.7, 0.3, 1.0, 0.4), flags);
        m_ShellOverlay.AddCircleXZ(drop, 5.0, ARGBF(0.65, 0.25, 1.0, 0.35), 10, flags);
    }

    protected void DrawClutterReject(vector pos, ShapeFlags flags)
    {
        vector drop = pos;
        BaseWorld world = GetGame().GetWorld();
        if (world)
            drop[1] = world.GetSurfaceY(pos[0], pos[2]) + 0.6;
        else
            drop[1] = m_RadarAnchor[1];
        int grey = ARGBF(0.7, 0.45, 0.45, 0.45);
        m_ShellOverlay.AddSphere(pos, 3.4, grey, flags);
        vector a = pos + m_FlatRight * 5.5 + Vector(0.0, 5.5, 0.0);
        vector b = pos - m_FlatRight * 5.5 - Vector(0.0, 5.5, 0.0);
        vector c = pos + m_FlatRight * 5.5 - Vector(0.0, 5.5, 0.0);
        vector d = pos - m_FlatRight * 5.5 + Vector(0.0, 5.5, 0.0);
        m_ShellOverlay.AddLine(a, b, grey, flags);
        m_ShellOverlay.AddLine(c, d, grey, flags);
        m_ShellOverlay.AddLine(pos, drop, ARGBF(0.4, 0.4, 0.4, 0.4), flags);
    }

    protected void DrawAirDopplerMark(
        vector pos,
        vector velDir,
        bool filtered,
        bool selected,
        ShapeFlags flags)
    {
        if (!filtered)
        {
            DrawAlivePlot(pos, flags);
            return;
        }

        vector dir = velDir;
        float dirLen = dir.Length();
        if (dirLen < 0.001)
            dir = m_FlatFwd;
        DrawVelocityStreak(
            pos,
            dir,
            48.0,
            4.2,
            ARGBF(0.95, 0.15, 0.95, 1.0),
            flags);
        if (selected)
            m_ShellOverlay.AddCircleXZ(
                pos,
                14.0,
                ARGBF(0.75, 0.2, 0.85, 1.0),
                16,
                flags);
    }

    protected void DrawDopplerOverlay(ShapeFlags flags, float shotT)
    {
        bool filtered = false;
        if (shotT >= PD_FILTER_S)
            filtered = true;

        DrawAirDopplerMark(
            FollowHeliPos() + Vector(0.0, 1.4, 0.0),
            m_HeliSmoothFwd,
            filtered,
            true,
            flags);
        if (m_JamHeli)
            DrawAirDopplerMark(
                FollowJamHeliPos() + Vector(0.0, 1.4, 0.0),
                m_JamSmoothFwd,
                filtered,
                false,
                flags);

        if (!m_GroundTargets)
            return;
        int i;
        for (i = 0; i < m_GroundTargets.Count(); i++)
        {
            IEntity ent = m_GroundTargets.Get(i);
            if (!ent)
                continue;
            vector pos = ent.GetOrigin() + Vector(0.0, 1.4, 0.0);
            if (filtered)
                DrawClutterReject(pos, flags);
            else
                DrawAlivePlot(pos, flags);
        }
    }

    protected void DrawLockConeOverlay(ShapeFlags flags)
    {
        vector origin = RadarScanOrigin();
        vector aim = FollowHeliPos();
        IEntity locked;
        vector lockPos;
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor && sensor.GetLockedTarget(locked, lockPos))
            aim = lockPos;
        DrawBeamCone(
            origin,
            aim,
            20.0,
            ARGBF(0.95, 1.0, 0.28, 0.12),
            ARGBF(0.55, 1.0, 0.22, 0.10),
            flags);
    }

    protected void DrawRocketOverlay(ShapeFlags flags)
    {
        vector rocketPos = m_RadarAnchor + m_FlatFwd * 8.0 + Vector(0.0, 6.0, 0.0);
        if (m_Rocket)
            rocketPos = m_Rocket.GetOrigin();
        m_ShellOverlay.AddSphere(rocketPos, 1.15, ARGBF(0.92, 1.0, 0.45, 0.08), flags);
        m_ShellOverlay.AddSphere(rocketPos, 0.45, ARGBF(1.0, 1.0, 0.85, 0.35), flags);
        if (m_RocketTrail && m_RocketTrail.Count() >= 2)
            m_ShellOverlay.AddPolyLine(m_RocketTrail, ARGBF(1.0, 1.0, 0.35, 0.08), flags);

        vector drop = rocketPos;
        BaseWorld world = GetGame().GetWorld();
        if (world)
            drop[1] = world.GetSurfaceY(rocketPos[0], rocketPos[2]) + 0.6;
        else
            drop[1] = m_RadarAnchor[1];
        m_ShellOverlay.AddLine(rocketPos, drop, ARGBF(0.55, 1.0, 0.7, 0.25), flags);
        m_ShellOverlay.AddCircleXZ(drop, 2.4, ARGBF(0.6, 1.0, 0.55, 0.12), 10, flags);
    }

    protected void DrawEwOverlay(ShapeFlags flags)
    {
        vector origin = RadarScanOrigin();
        vector heli = FollowJamHeliPos();
        m_ShellOverlay.AddSphere(heli, 10.0, ARGBF(0.72, 1.0, 0.45, 0.08), flags);
        m_ShellOverlay.AddSphere(heli, 4.5, ARGBF(0.95, 1.0, 0.22, 0.06), flags);
        m_ShellOverlay.AddLine(heli, origin, ARGBF(0.55, 1.0, 0.4, 0.08), flags);

        int ghostCount = 0;
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor)
        {
            array<ref RDF_RadarTarget> plots = sensor.GetPlots();
            if (plots)
            {
                int i;
                for (i = 0; i < plots.Count(); i++)
                {
                    RDF_RadarTarget t = plots.Get(i);
                    if (!t)
                        continue;
                    if (!t.m_IsFalsePlot)
                        continue;
                    DrawFalsePlotMarker(t.m_Position, origin, flags);
                    ghostCount = ghostCount + 1;
                }
            }
        }
        if (ghostCount > 0)
            return;

        DrawFalsePlotMarker(FalsePlotWorld(180.0, -22.0, 8.0), origin, flags);
        DrawFalsePlotMarker(FalsePlotWorld(260.0, -8.0, 10.0), origin, flags);
        DrawFalsePlotMarker(FalsePlotWorld(340.0, 6.0, 12.0), origin, flags);
        DrawFalsePlotMarker(FalsePlotWorld(420.0, 16.0, 9.0), origin, flags);
        DrawFalsePlotMarker(FalsePlotWorld(510.0, -14.0, 11.0), origin, flags);
        DrawFalsePlotMarker(FalsePlotWorld(620.0, 28.0, 8.0), origin, flags);
    }

    protected void DrawFalsePlotMarker(vector pos, vector origin, ShapeFlags flags)
    {
        vector drop = pos;
        BaseWorld world = GetGame().GetWorld();
        if (world)
            drop[1] = world.GetSurfaceY(pos[0], pos[2]) + 0.7;
        else
            drop[1] = m_RadarAnchor[1];
        m_ShellOverlay.AddSphere(pos, 7.0, ARGBF(0.88, 0.95, 0.95, 1.0), flags);
        m_ShellOverlay.AddSphere(pos, 3.0, ARGBF(1.0, 1.0, 1.0, 1.0), flags);
        m_ShellOverlay.AddLine(pos, drop, ARGBF(0.8, 0.9, 0.9, 1.0), flags);
        m_ShellOverlay.AddLine(origin, pos, ARGBF(0.35, 0.85, 0.85, 1.0), flags);
        m_ShellOverlay.AddCircleXZ(drop, 10.0, ARGBF(0.7, 0.9, 0.9, 1.0), 12, flags);
    }

    protected void DrawEsmOverlay(ShapeFlags flags)
    {
        vector origin = RadarScanOrigin();
        vector heli = FollowJamHeliPos();
        DrawBeamCone(
            origin,
            heli,
            16.0,
            ARGBF(0.95, 0.95, 0.15, 0.85),
            ARGBF(0.55, 0.85, 0.12, 0.75),
            flags);
        m_ShellOverlay.AddSphere(heli, 8.0, ARGBF(0.8, 0.95, 0.2, 0.85), flags);
        m_ShellOverlay.AddSphere(origin, 3.2, ARGBF(0.9, 0.9, 0.2, 0.8), flags);
    }

    protected void StretchShellCoverage()
    {
        float shotT = System.GetTickCount() * 0.001 - m_ShotStartWallS;
        if (shotT < 0.0)
            shotT = 0.0;
        float remain = 0.0;
        int i;
        for (i = m_ShotIndex; i < m_Shots.Count(); i++)
        {
            RDF_RadarPromoShot s = m_Shots.Get(i);
            if (!s)
                continue;
            if (s.m_RadarMode != MODE_WLR)
                break;
            remain = remain + s.m_DurationS;
        }
        remain = remain - shotT;
        float need = m_ShellTofS + SHELL_IMPACT_HOLD_S;
        float extra = need - remain;
        if (extra <= 0.0)
            return;
        for (i = m_Shots.Count() - 1; i >= m_ShotIndex; i--)
        {
            RDF_RadarPromoShot s = m_Shots.Get(i);
            if (!s)
                continue;
            if (s.m_RadarMode != MODE_WLR)
                continue;
            s.m_DurationS = s.m_DurationS + extra;
            Print("[RDF Promo] stretch impact hold +"
                + FormatOneDecimal(extra)
                + "s");
            return;
        }
    }

    protected void UpdateGroundMotion(float elapsedS)
    {
        if (!m_GroundTargets)
            return;
        int i;
        for (i = 0; i < m_GroundTargets.Count(); i++)
        {
            IEntity ent = m_GroundTargets.Get(i);
            if (!ent)
                continue;
            DrivePromoUaz(ent, i);
        }
    }

    protected void OrientEntityAlong(IEntity ent, vector fwd, vector pos)
    {
        if (!ent)
            return;
        vector dir = Vector(fwd[0], 0.0, fwd[2]);
        float len = dir.Length();
        if (len < 0.001)
            dir = Vector(0.0, 0.0, 1.0);
        else
            dir = dir / len;
        vector mat[4];
        Math3D.DirectionAndUpMatrix(dir, vector.Up, mat);
        mat[3] = pos;
        ent.SetTransform(mat);
    }

    protected void PreparePromoCarDrive(IEntity ent)
    {
        if (!ent)
            return;
        Vehicle veh = Vehicle.Cast(ent);
        if (veh)
        {
            VehicleControllerComponent controller = veh.GetVehicleController();
            if (controller)
            {
                if (!controller.IsEngineOn())
                    controller.ForceStartEngine();
            }
        }

        CarControllerComponent carCtrl = CarControllerComponent.Cast(
            ent.FindComponent(CarControllerComponent));
        if (!carCtrl)
            return;
        carCtrl.SetPersistentHandBrake(false);
        VehicleWheeledSimulation sim = carCtrl.GetSimulation();
        if (!sim)
            sim = VehicleWheeledSimulation.Cast(ent.FindComponent(VehicleWheeledSimulation));
        if (!sim)
            return;
        sim.ForceEnableSimulation();
        if (!sim.EngineIsOn())
            sim.EngineStart();
        sim.SetBreak(0.0, false);
        sim.SetClutch(1.0);
        sim.GearboxSetEfficiencyState(1.0);
        int gear = sim.GetGear();
        if (gear < 2)
            sim.SetGear(2);
    }

    protected void DrivePromoUaz(IEntity ent, int index)
    {
        PreparePromoCarDrive(ent);

        vector mat[4];
        ent.GetWorldTransform(mat);
        vector fwd = mat[2];
        fwd[1] = 0.0;
        float fwdLen = fwd.Length();
        if (fwdLen < 0.001)
            fwd = m_FlatFwd;
        else
            fwd = fwd / fwdLen;

        float cross = fwd[0] * m_FlatFwd[2] - fwd[2] * m_FlatFwd[0];
        float steer = -cross * 1.6;
        if (steer > 0.4)
            steer = 0.4;
        if (steer < -0.4)
            steer = -0.4;

        vector pos = mat[3];
        vector delta = pos - m_RadarAnchor;
        float lat = delta[0] * m_FlatRight[0] + delta[2] * m_FlatRight[2];
        float wantLat = UAZ_LATERAL_M;
        if ((index / 2) * 2 == index)
            wantLat = -UAZ_LATERAL_M;
        float latErr = lat - wantLat;
        steer = steer - latErr * 0.012;
        if (steer > 0.45)
            steer = 0.45;
        if (steer < -0.45)
            steer = -0.45;

        CarControllerComponent carCtrl = CarControllerComponent.Cast(
            ent.FindComponent(CarControllerComponent));
        if (carCtrl)
        {
            VehicleWheeledSimulation sim = carCtrl.GetSimulation();
            if (!sim)
                sim = VehicleWheeledSimulation.Cast(ent.FindComponent(VehicleWheeledSimulation));
            if (sim)
            {
                sim.SetBreak(0.0, false);
                sim.SetClutch(1.0);
                sim.SetThrottle(0.55);
                sim.SetSteering(steer);
            }
        }

        Physics phys = ent.GetPhysics();
        if (!phys)
            return;
        vector vel = phys.GetVelocity();
        float along = vel[0] * fwd[0] + vel[2] * fwd[2];
        if (along < 1.2)
        {
            vector crawl = fwd * 7.0;
            crawl[1] = vel[1];
            phys.SetVelocity(crawl);
        }
    }

    protected void PreparePromoHeliRotors(IEntity heli)
    {
        if (!heli)
            return;
        HelicopterControllerComponent heliCtrl = HelicopterControllerComponent.Cast(
            heli.FindComponent(HelicopterControllerComponent));
        if (!heliCtrl)
            return;
        VehicleHelicopterSimulation hsim = heliCtrl.GetSimulation();
        if (!hsim)
            return;
        if (!hsim.EngineIsOn())
            hsim.EngineStart();
        hsim.SetThrottle(0.45);
    }

    protected void ConvoyCamera(out vector eye, out vector lookAt, float u)
    {
        vector leadPos = StagePoint(Math.Lerp(110.0, 200.0, u), -UAZ_LATERAL_M, 1.2);
        if (m_GroundTargets)
        {
            if (m_GroundTargets.Count() > 1)
            {
                IEntity lead = m_GroundTargets.Get(1);
                if (lead)
                {
                    vector raw = lead.GetOrigin() + Vector(0.0, 1.15, 0.0);
                    if (!m_ConvoyLookInit)
                    {
                        m_ConvoyLookPos = raw;
                        m_ConvoyLookInit = true;
                    }
                    else
                    {
                        m_ConvoyLookPos = m_ConvoyLookPos + (raw - m_ConvoyLookPos) * 0.18;
                    }
                    leadPos = m_ConvoyLookPos;
                }
            }
        }

        vector vanLook = m_RadarAnchor + Vector(0.0, 2.4, 0.0);
        lookAt = LerpVec(leadPos, vanLook, 0.18 + u * 0.22);
        eye = leadPos + m_FlatFwd * Math.Lerp(14.0, 22.0, u)
            - m_FlatRight * Math.Lerp(9.0, 14.0, u)
            + Vector(0.0, Math.Lerp(1.5, 2.4, u), 0.0);
        eye = LiftEyeMin(eye, 1.15);
    }

    protected void ChaseCamera(
        out vector eye,
        out vector lookAt,
        float shotT,
        float elapsedS,
        float u)
    {
        vector heli = FollowHeliPos();
        vector tan = m_FlatFwd;
        if (m_HeliSmoothInit)
            tan = m_HeliSmoothFwd;
        else
            tan = ComputeOrbitTangent(elapsedS, 0.0);
        vector right = Vector(tan[2], 0.0, -tan[0]);
        float dist = Math.Lerp(16.0, 38.0, u);
        float height = Math.Lerp(4.2, 14.0, u);
        float side = Math.Lerp(6.0, 18.0, u);
        eye = heli - tan * dist + right * side + Vector(0.0, height, 0.0);
        lookAt = heli + Vector(0.0, 1.2, 0.0);
        eye = LiftEye(eye);
    }

    protected void LockPunchCamera(out vector eye, out vector lookAt, float elapsedS, float u)
    {
        vector origin = RadarScanOrigin();
        vector heli = FollowHeliPos();
        vector toHeli = heli - origin;
        float dist = toHeli.Length();
        if (dist < 8.0)
            dist = 8.0;
        vector dir = toHeli * (1.0 / dist);
        vector side = Vector(dir[2], 0.0, -dir[0]);
        float sideLen = side.Length();
        if (sideLen > 0.001)
            side = side * (1.0 / sideLen);
        else
            side = m_FlatRight;

        float along = Math.Lerp(dist * 0.10, dist * 0.72, u);
        float lateral = Math.Lerp(22.0, 7.0, u);
        float height = Math.Lerp(10.0, 4.0, u);
        eye = origin + dir * along + side * lateral + Vector(0.0, height, 0.0);
        lookAt = heli + Vector(0.0, 1.2, 0.0);
        eye = LiftEye(eye);
    }

    protected void DopplerCamera(out vector eye, out vector lookAt, float elapsedS, float u)
    {
        vector heli = FollowHeliPos();
        vector jam = FollowJamHeliPos();
        vector convoy = StagePoint(140.0, 0.0, 1.4);
        if (m_GroundTargets)
        {
            if (m_GroundTargets.Count() > 1)
            {
                IEntity lead = m_GroundTargets.Get(1);
                if (lead)
                    convoy = lead.GetOrigin() + Vector(0.0, 1.2, 0.0);
            }
        }

        vector eye0 = StagePoint(28.0, -92.0, 16.0);
        vector eye1 = StagePoint(110.0, -78.0, 38.0);
        eye = LerpVec(eye0, eye1, u);
        vector airMid = LerpVec(heli, jam, 0.42);
        vector look0 = convoy;
        vector look1 = LerpVec(airMid, convoy, 0.18);
        lookAt = LerpVec(look0, look1, u);
        eye = LiftEyeMin(eye, 2.0);
    }

    protected void JamCamera(out vector eye, out vector lookAt, float u)
    {
        vector sector = StagePoint(220.0, 8.0, 10.0);
        vector heli = FollowJamHeliPos();
        lookAt = LerpVec(sector, heli, u * 0.62);
        eye = StagePoint(
            Math.Lerp(55.0, 130.0, u),
            -102.0,
            Math.Lerp(22.0, 40.0, u));
        eye = LiftEye(eye);
    }

    protected void EsmCamera(out vector eye, out vector lookAt, float u)
    {
        vector sector = StagePoint(210.0, 0.0, 10.0);
        vector origin = RadarScanOrigin();
        vector heli = FollowJamHeliPos();
        vector mid = LerpVec(origin, heli, 0.58);
        lookAt = LerpVec(sector, mid, u);
        eye = StagePoint(
            Math.Lerp(70.0, 150.0, u),
            -98.0,
            Math.Lerp(20.0, 34.0, u));
        eye = LiftEye(eye);
    }

    protected void LaunchCamera(out vector eye, out vector lookAt, float u)
    {
        eye = StagePoint(LAUNCH_FWD_M - 6.5, -5.0, 1.45);
        lookAt = m_LaunchPos + Vector(0.0, 2.2 + u * 16.0, 0.0);
        if (m_Mortar)
            lookAt = LerpVec(m_Mortar.GetOrigin() + Vector(0.0, 1.4, 0.0), lookAt, u * 0.55);
        if (m_ActiveShell)
            lookAt = LerpVec(m_LaunchPos + Vector(0.0, 4.0, 0.0), m_ActiveShell.GetOrigin(), 0.35 + u * 0.65);
        eye = LiftEyeMin(eye, 1.1);
    }

    protected void RideShellCamera(out vector eye, out vector lookAt, float u)
    {
        vector shell = LiveShellPos(u);
        vector tan = ShellTangent();
        vector right = Vector(tan[2], 0.0, -tan[0]);
        float rightLen = right.Length();
        if (rightLen < 0.001)
            right = m_FlatRight;
        else
            right = right / rightLen;

        eye = shell - tan * Math.Lerp(12.0, 8.0, u)
            + right * Math.Lerp(22.0, 16.0, u)
            + Vector(0.0, Math.Lerp(5.0, 12.0, u), 0.0);
        lookAt = shell + tan * 40.0 + Vector(0.0, 4.0, 0.0);
        eye = LiftEyeMin(eye, 2.0);
    }

    protected void RideRocketCamera(out vector eye, out vector lookAt, float u, vector heli)
    {
        vector rocketPos = m_RadarAnchor + m_FlatFwd * 8.0 + Vector(0.0, 6.0, 0.0);
        if (m_Rocket)
            rocketPos = m_Rocket.GetOrigin();

        vector tan = m_FlatFwd;
        if (m_Rocket)
        {
            MissileMoveComponent missile = MissileMoveComponent.Cast(
                m_Rocket.FindComponent(MissileMoveComponent));
            if (missile)
            {
                vector vel = missile.GetVelocity();
                float speed = vel.Length();
                if (speed > 8.0)
                    tan = vel * (1.0 / speed);
            }
        }
        vector right = Vector(tan[2], 0.0, -tan[0]);
        float rightLen = right.Length();
        if (rightLen > 0.001)
            right = right * (1.0 / rightLen);
        else
            right = m_FlatRight;

        float along = vector.Dot(heli - rocketPos, tan);
        bool passed = false;
        if (along < 6.0)
            passed = true;

        eye = rocketPos - tan * Math.Lerp(22.0, 14.0, u)
            + right * 11.0
            + Vector(0.0, Math.Lerp(4.5, 7.0, u), 0.0);
        if (passed)
            lookAt = rocketPos + tan * 36.0;
        else
            lookAt = LerpVec(rocketPos + tan * 18.0, heli, 0.55);
        eye = LiftEyeMin(eye, 2.0);
    }

    protected void ArcCamera(out vector eye, out vector lookAt, float u)
    {
        RideShellCamera(eye, lookAt, u);
    }

    protected void ImpactCamera(out vector eye, out vector lookAt, float u)
    {
        vector impact = m_ShellImpactPos;
        if (!m_ShellImpactValid)
            impact = StagePoint(LAUNCH_FWD_M + 900.0, 0.0, 0.5);

        vector high = LiftEye(
            impact - m_FlatFwd * 70.0 + m_FlatRight * 22.0 + Vector(0.0, 90.0, 0.0));
        vector low = LiftEyeMin(
            impact - m_FlatFwd * 11.0 + m_FlatRight * 4.0 + Vector(0.0, 2.4, 0.0),
            1.4);
        eye = LerpVec(high, low, u);
        lookAt = impact + Vector(0.0, 1.2, 0.0);
        if (m_ActiveShell)
        {
            vector incoming = m_ActiveShell.GetOrigin();
            lookAt = LerpVec(incoming, impact, u * 0.72);
        }
    }

    protected void ShellCamera(out vector eye, out vector lookAt, float shotT)
    {
        vector impact = m_ShellImpactPos;
        if (!m_ShellImpactValid)
            impact = StagePoint(LAUNCH_FWD_M + 900.0, 0.0, 0.5);

        vector mid = (m_LaunchPos + impact) * 0.5;
        float dx = impact[0] - m_LaunchPos[0];
        float dz = impact[2] - m_LaunchPos[2];
        float span = Math.Sqrt(dx * dx + dz * dz);
        if (span < 200.0)
            span = 200.0;

        float apexY = mid[1] + 180.0;
        if (m_ShellApexValid)
            apexY = m_ShellApexPos[1];

        float height = span * 0.72;
        if (height < 380.0)
            height = 380.0;
        if (height > 920.0)
            height = 920.0;

        float uLin = 0.0;
        float dur = CurrentShotDuration();
        if (dur > 0.001)
            uLin = shotT / dur;
        float u = EaseOutCubic(uLin);

        vector shell = LiveShellPos(uLin);
        vector closeEye = LiftEye(
            shell + m_FlatRight * 48.0 - m_FlatFwd * 24.0 + Vector(0.0, 28.0, 0.0));
        vector closeLook = shell + m_FlatFwd * 80.0;

        float yaw = Math.Lerp(-0.18, 0.42, u);
        float c = Math.Cos(yaw);
        float s = Math.Sin(yaw);
        vector back = Vector(
            m_FlatFwd[0] * c + m_FlatRight[0] * s,
            0.0,
            m_FlatFwd[2] * c + m_FlatRight[2] * s);
        vector farEye = LiftEye(
            mid - back * (span * 0.08) + Vector(0.0, height, 0.0));
        vector farLook = Vector(mid[0], (mid[1] + apexY) * 0.5, mid[2]);

        eye = LerpVec(closeEye, farEye, u);
        lookAt = LerpVec(closeLook, farLook, u);
    }

    protected vector LiveShellPos(float u)
    {
        if (m_ActiveShell)
            return m_ActiveShell.GetOrigin();
        if (m_ShellPath)
        {
            int n = m_ShellPath.Count();
            if (n > 1)
            {
                float f = u * (n - 1);
                int i0 = f;
                if (i0 < 0)
                    i0 = 0;
                if (i0 > n - 2)
                    i0 = n - 2;
                float frac = f - i0;
                return LerpVec(m_ShellPath.Get(i0), m_ShellPath.Get(i0 + 1), frac);
            }
        }
        return m_LaunchPos + Vector(0.0, 8.0, 0.0);
    }

    protected vector ShellTangent()
    {
        if (m_ActiveShell)
        {
            ProjectileMoveComponent move = ProjectileMoveComponent.Cast(
                m_ActiveShell.FindComponent(ProjectileMoveComponent));
            if (move)
            {
                vector vel = move.GetVelocity();
                if (vel.LengthSq() > 25.0)
                {
                    vector tan = vel;
                    tan[1] = 0.0;
                    float len = tan.Length();
                    if (len > 0.001)
                        return tan / len;
                }
            }
        }
        return m_FlatFwd;
    }

    protected void DrawShellOverlay()
    {
        if (!m_ShellOverlay)
            m_ShellOverlay = new RDF_DebugShapeManager();
        m_ShellOverlay.Clear();
        UpdateLiveShellPrediction();

        ShapeFlags flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP;
        vector impact = m_ShellImpactPos;
        if (!m_ShellImpactValid)
            impact = StagePoint(LAUNCH_FWD_M + 900.0, 0.0, 0.5);

        m_ShellOverlay.AddLine(m_LaunchPos, impact, ARGBF(0.45, 0.85, 0.85, 0.85), flags);

        vector launchGround = m_LaunchPos;
        vector impactGround = impact;
        BaseWorld world = GetGame().GetWorld();
        if (world)
        {
            launchGround[1] = world.GetSurfaceY(m_LaunchPos[0], m_LaunchPos[2]) + 0.8;
            impactGround[1] = world.GetSurfaceY(impact[0], impact[2]) + 0.8;
        }
        m_ShellOverlay.AddCircleXZ(launchGround, 12.0, ARGBF(0.8, 1.0, 0.5, 0.12), 16, flags);
        m_ShellOverlay.AddCircleXZ(impactGround, 14.0, ARGBF(0.8, 0.2, 0.75, 1.0), 16, flags);

        if (m_ShellLiveTrail && m_ShellLiveTrail.Count() >= 2)
            m_ShellOverlay.AddPolyLine(m_ShellLiveTrail, ARGBF(1.0, 1.0, 0.2, 0.08), flags);
        if (m_ShellPath && m_ShellPath.Count() >= 2)
            m_ShellOverlay.AddPolyLine(m_ShellPath, ARGBF(0.85, 1.0, 0.45, 0.12), flags);

        if (!m_ActiveShell)
            return;

        vector shellPos = m_ActiveShell.GetOrigin();
        vector drop = shellPos;
        if (world)
            drop[1] = world.GetSurfaceY(shellPos[0], shellPos[2]) + 0.6;
        else
            drop[1] = m_RadarAnchor[1];

        m_ShellOverlay.AddSphere(shellPos, 1.35, ARGBF(1.0, 1.0, 0.25, 0.08), flags);
        m_ShellOverlay.AddSphere(shellPos, 0.55, ARGBF(1.0, 1.0, 0.85, 0.35), flags);
        m_ShellOverlay.AddLine(shellPos, drop, ARGBF(0.9, 1.0, 0.9, 0.35), flags);
        m_ShellOverlay.AddCircleXZ(drop, 3.0, ARGBF(0.85, 1.0, 0.85, 0.25), 12, flags);
    }

    protected void UpdateLiveShellPrediction()
    {
        if (!m_ActiveShell)
            return;

        vector shellPos = m_ActiveShell.GetOrigin();
        if (!m_ShellLiveTrail)
            m_ShellLiveTrail = new array<vector>();
        if (m_ShellLiveTrail.Count() == 0)
            m_ShellLiveTrail.Insert(shellPos);
        else
        {
            vector last = m_ShellLiveTrail.Get(m_ShellLiveTrail.Count() - 1);
            if (vector.DistanceSq(last, shellPos) > 36.0)
                m_ShellLiveTrail.Insert(shellPos);
            while (m_ShellLiveTrail.Count() > 40)
                m_ShellLiveTrail.RemoveOrdered(0);
        }

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;
        float groundY = world.GetSurfaceY(shellPos[0], shellPos[2]);
        float agl = shellPos[1] - groundY;
        if (agl <= 4.0)
        {
            m_ShellImpactPos = Vector(shellPos[0], groundY + 0.4, shellPos[2]);
            m_ShellImpactValid = true;
            RebuildShellPath(m_ShellMuzzleVel, m_ShellTofS);
            return;
        }

        if (m_DidLiveSolve)
            return;

        ProjectileMoveComponent move = ProjectileMoveComponent.Cast(
            m_ActiveShell.FindComponent(ProjectileMoveComponent));
        if (!move)
            return;
        vector vel = move.GetVelocity();
        if (vel.LengthSq() < 25.0)
            return;

        ApplyWorldImpactSolve(shellPos, vel);
        m_DidLiveSolve = true;
    }

    protected float Smooth01(float t)
    {
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
        return t * t * (3.0 - 2.0 * t);
    }

    protected float EaseInCubic(float t)
    {
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
        return t * t * t;
    }

    protected float EaseOutCubic(float t)
    {
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
        float inv = 1.0 - t;
        return 1.0 - inv * inv * inv;
    }

    protected vector LerpVec(vector a, vector b, float t)
    {
        return a + (b - a) * t;
    }

    protected vector Midpoint(vector a, vector b)
    {
        return (a + b) * 0.5;
    }

    protected bool IsLivePromoShell(IEntity ent)
    {
        if (!ent || !m_LiveShells)
            return false;
        if (m_LiveShells.Find(ent) >= 0)
            return true;
        return false;
    }

    protected void IgnoreEntityTree(IEntity ent)
    {
        if (!ent)
            return;
        if (IsLivePromoShell(ent))
            return;
        RDF_RadarScattererRegistry.Ignore(ent);
        IEntity child = ent.GetChildren();
        while (child)
        {
            IgnoreEntityTree(child);
            child = child.GetSibling();
        }
    }

    protected void IgnorePromoArtillery()
    {
        IgnoreEntityTree(m_Mortar);
        IgnoreEntityTree(m_MortarGunner);
        if (m_MortarHeldShell)
        {
            if (!IsLivePromoShell(m_MortarHeldShell))
                RDF_RadarScattererRegistry.Ignore(m_MortarHeldShell);
        }
    }

    protected void IgnoreRocketChildren()
    {
        if (!m_Rocket)
            return;
        IEntity child = m_Rocket.GetChildren();
        while (child)
        {
            IgnoreEntityTree(child);
            child = child.GetSibling();
        }
    }

    protected void DeleteSpawned()
    {
        ClearRocket();
        UnhookMortarFired();
        if (m_MortarGunner)
        {
            RDF_RadarScattererRegistry.Unregister(m_MortarGunner);
            SCR_EntityHelper.DeleteEntityAndChildren(m_MortarGunner);
            m_MortarGunner = null;
        }
        m_MortarHeldShell = null;
        if (m_Mortar)
        {
            RDF_RadarScattererRegistry.Unregister(m_Mortar);
            SCR_EntityHelper.DeleteEntityAndChildren(m_Mortar);
            m_Mortar = null;
        }
        if (m_JamHeli)
            RDF_RadarEmitterRegistry.SetEmitting(m_JamHeli, false);
        if (m_AirTarget)
            RDF_RadarEmitterRegistry.SetEmitting(m_AirTarget, false);
        m_EmitterArmed = false;
        ClearEntityArray(m_GroundTargets);
        ClearEntityArray(m_LiveShells);
        if (m_AirTarget)
        {
            RDF_RadarScattererRegistry.Unregister(m_AirTarget);
            SCR_EntityHelper.DeleteEntityAndChildren(m_AirTarget);
            m_AirTarget = null;
        }
        if (m_JamHeli)
        {
            RDF_RadarScattererRegistry.Unregister(m_JamHeli);
            SCR_EntityHelper.DeleteEntityAndChildren(m_JamHeli);
            m_JamHeli = null;
        }
        if (m_RadarVan)
        {
            RDF_RadarScattererRegistry.Unregister(m_RadarVan);
            SCR_EntityHelper.DeleteEntityAndChildren(m_RadarVan);
            m_RadarVan = null;
        }
    }

    protected void ClearEntityArray(array<IEntity> list)
    {
        if (!list)
            return;
        for (int i = 0; i < list.Count(); i++)
        {
            IEntity ent = list.Get(i);
            if (!ent)
                continue;
            RDF_RadarScattererRegistry.Unregister(ent);
            SCR_EntityHelper.DeleteEntityAndChildren(ent);
        }
        list.Clear();
    }
}
