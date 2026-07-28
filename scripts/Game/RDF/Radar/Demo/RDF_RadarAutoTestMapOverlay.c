// Local map markers for AutoTests while the M-key map is open.
// Create once; update pose/text in place (no per-tick delete/recreate).
// Aircraft trail: sampled past positions + constant-velocity predicted path.
class RDF_RadarAutoTestMapOverlay
{
    protected static const int HISTORY_CAP = 48;
    protected static const float HISTORY_MIN_STEP_M = 25.0;
    protected static const int HISTORY_DRAW_STRIDE = 2;
    protected static const float PREDICT_HORIZON_S = 25.0;
    protected static const int PREDICT_SAMPLES = 10;
    // Official PLACED_CUSTOM icon indices from configs/Map/MapMarkerConfig.conf
    // Aircraft current pos uses PLACED_MILITARY AIR + ROTARY/FIXED_WING instead.
    protected static const int ICON_TRAIL = 0; // circle — past samples
    protected static const int ICON_PREDICT = 40; // waypoint — future samples
    protected static const int ICON_LAUNCH = 6; // drop-point — shell launch
    protected static const int ICON_IMPACT = 2; // cross — shell impact

    // Official color indices: white, orange, darkOrange, darkRed, red, green, ...
    protected static const int COLOR_TRAIL = 1; // orange
    protected static const int COLOR_PREDICT = 5; // green
    protected static const int COLOR_LAUNCH = 2; // darkOrange
    protected static const int COLOR_IMPACT = 4; // red

    protected static ref RDF_RadarAutoTestMapOverlay s_Instance;
    protected static bool s_HooksRegistered;
    protected static bool s_TickRegistered;

    protected bool m_Active;
    protected bool m_MapOpen;
    protected float m_LastRefreshWallS;

    protected IEntity m_Aircraft;
    protected string m_AircraftLabel;
    protected ref SCR_MapMarkerBase m_AircraftMarker;

    protected ref array<vector> m_HistoryPos;
    protected ref array<float> m_HistoryTimeS;
    protected ref array<vector> m_PredictPos;
    protected ref array<vector> m_DrawHistoryPos;
    protected ref array<string> m_HistoryLabels;
    protected ref array<string> m_PredictLabels;
    protected ref array<ref SCR_MapMarkerBase> m_HistoryMarkers;
    protected ref array<ref SCR_MapMarkerBase> m_PredictMarkers;

    protected ref array<vector> m_ShellLaunchPos;
    protected ref array<string> m_ShellLaunchLabels;
    protected ref array<vector> m_ShellImpactPos;
    protected ref array<string> m_ShellImpactLabels;
    protected ref array<ref SCR_MapMarkerBase> m_LaunchMarkers;
    protected ref array<ref SCR_MapMarkerBase> m_ImpactMarkers;

    static RDF_RadarAutoTestMapOverlay GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarAutoTestMapOverlay();
        return s_Instance;
    }

    static void Start()
    {
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        GetInstance().StopInternal();
    }

    // rolePrefix is a short test tag (e.g. "RDF Lock"); model name comes from
    // RDF_RadarSignatureLibrary.FormatDisplayName(aircraft).
    static void SetAircraft(IEntity aircraft, string rolePrefix)
    {
        RDF_RadarAutoTestMapOverlay inst = GetInstance();
        if (inst.m_Aircraft != aircraft)
            inst.ResetPathState();
        inst.m_Aircraft = aircraft;
        inst.m_AircraftLabel = rolePrefix;
        inst.RecordHistorySample();
        inst.RebuildPredictSamples();
        if (inst.m_MapOpen)
            inst.UpdateAircraftMarker();
    }

    static void ClearAircraft()
    {
        RDF_RadarAutoTestMapOverlay inst = GetInstance();
        inst.m_Aircraft = null;
        inst.m_AircraftLabel = "";
        inst.ResetPathState();
        inst.RemoveAircraftMarker();
        inst.ClearPathMarkers();
    }

    static void SetShellPoints(
        notnull array<vector> launches,
        notnull array<string> launchLabels,
        notnull array<vector> impacts,
        notnull array<string> impactLabels)
    {
        RDF_RadarAutoTestMapOverlay inst = GetInstance();
        inst.EnsureShellArrays();
        inst.m_ShellLaunchPos.Clear();
        inst.m_ShellLaunchLabels.Clear();
        inst.m_ShellImpactPos.Clear();
        inst.m_ShellImpactLabels.Clear();

        int nL = launches.Count();
        if (launchLabels.Count() < nL)
            nL = launchLabels.Count();
        for (int i = 0; i < nL; i++)
        {
            inst.m_ShellLaunchPos.Insert(launches.Get(i));
            inst.m_ShellLaunchLabels.Insert(launchLabels.Get(i));
        }

        int nI = impacts.Count();
        if (impactLabels.Count() < nI)
            nI = impactLabels.Count();
        for (int j = 0; j < nI; j++)
        {
            inst.m_ShellImpactPos.Insert(impacts.Get(j));
            inst.m_ShellImpactLabels.Insert(impactLabels.Get(j));
        }

        if (inst.m_MapOpen)
            inst.SyncShellMarkers();
    }

    static void ClearShellPoints()
    {
        RDF_RadarAutoTestMapOverlay inst = GetInstance();
        inst.EnsureShellArrays();
        inst.m_ShellLaunchPos.Clear();
        inst.m_ShellLaunchLabels.Clear();
        inst.m_ShellImpactPos.Clear();
        inst.m_ShellImpactLabels.Clear();
        inst.ClearShellMarkers();
    }

    protected void EnsureShellArrays()
    {
        if (!m_ShellLaunchPos)
            m_ShellLaunchPos = new array<vector>();
        if (!m_ShellLaunchLabels)
            m_ShellLaunchLabels = new array<string>();
        if (!m_ShellImpactPos)
            m_ShellImpactPos = new array<vector>();
        if (!m_ShellImpactLabels)
            m_ShellImpactLabels = new array<string>();
        if (!m_LaunchMarkers)
            m_LaunchMarkers = new array<ref SCR_MapMarkerBase>();
        if (!m_ImpactMarkers)
            m_ImpactMarkers = new array<ref SCR_MapMarkerBase>();
    }

    protected void EnsurePathArrays()
    {
        if (!m_HistoryPos)
            m_HistoryPos = new array<vector>();
        if (!m_HistoryTimeS)
            m_HistoryTimeS = new array<float>();
        if (!m_PredictPos)
            m_PredictPos = new array<vector>();
        if (!m_DrawHistoryPos)
            m_DrawHistoryPos = new array<vector>();
        if (!m_HistoryLabels)
            m_HistoryLabels = new array<string>();
        if (!m_PredictLabels)
            m_PredictLabels = new array<string>();
        if (!m_HistoryMarkers)
            m_HistoryMarkers = new array<ref SCR_MapMarkerBase>();
        if (!m_PredictMarkers)
            m_PredictMarkers = new array<ref SCR_MapMarkerBase>();
    }

    protected void ResetPathState()
    {
        EnsurePathArrays();
        m_HistoryPos.Clear();
        m_HistoryTimeS.Clear();
        m_PredictPos.Clear();
        m_DrawHistoryPos.Clear();
        m_HistoryLabels.Clear();
        m_PredictLabels.Clear();
    }

    protected void StartInternal()
    {
        m_Active = true;
        EnsureShellArrays();
        EnsurePathArrays();
        RegisterHooks();

        SCR_MapEntity mapEnt = SCR_MapEntity.GetMapInstance();
        if (mapEnt && mapEnt.IsOpen())
        {
            m_MapOpen = true;
            RebuildAllMarkers();
        }

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 400, true);
        }
    }

    protected void StopInternal()
    {
        m_Active = false;
        m_Aircraft = null;
        ResetPathState();
        ClearAllMarkers();
    }

    protected void RegisterHooks()
    {
        if (s_HooksRegistered)
            return;
        s_HooksRegistered = true;
        SCR_MapEntity.GetOnMapOpen().Insert(StaticOnMapOpen);
        SCR_MapEntity.GetOnMapClose().Insert(StaticOnMapClose);
    }

    protected static void StaticTick()
    {
        GetInstance().OnTick();
    }

    protected static void StaticOnMapOpen(MapConfiguration config)
    {
        GetInstance().OnMapOpen();
    }

    protected static void StaticOnMapClose(MapConfiguration config)
    {
        GetInstance().OnMapClose();
    }

    protected void OnMapOpen()
    {
        if (!m_Active)
            return;
        m_MapOpen = true;
        RebuildAllMarkers();
    }

    protected void OnMapClose()
    {
        m_MapOpen = false;
        // Map close destroys widgets; drop our refs without RemoveStaticMarker races.
        m_AircraftMarker = null;
        if (m_HistoryMarkers)
            m_HistoryMarkers.Clear();
        if (m_PredictMarkers)
            m_PredictMarkers.Clear();
        if (m_LaunchMarkers)
            m_LaunchMarkers.Clear();
        if (m_ImpactMarkers)
            m_ImpactMarkers.Clear();
    }

    protected void OnTick()
    {
        if (!m_Active)
            return;

        float nowS = System.GetTickCount() * 0.001;
        if (nowS - m_LastRefreshWallS < 0.45)
            return;
        m_LastRefreshWallS = nowS;

        RecordHistorySample();
        RebuildPredictSamples();

        if (!m_MapOpen)
            return;

        // Pose/text + path breadcrumbs — never delete/recreate unless widgets died.
        UpdateAircraftMarker();
    }

    protected void RebuildAllMarkers()
    {
        ClearAllMarkers();
        UpdateAircraftMarker();
        SyncShellMarkers();
    }

    protected void RecordHistorySample()
    {
        EnsurePathArrays();
        if (!m_Aircraft)
            return;

        vector pos = m_Aircraft.GetOrigin();
        if (m_HistoryPos.Count() > 0)
        {
            vector last = m_HistoryPos.Get(m_HistoryPos.Count() - 1);
            float dx = pos[0] - last[0];
            float dz = pos[2] - last[2];
            float minStep = HISTORY_MIN_STEP_M;
            if ((dx * dx + dz * dz) < (minStep * minStep))
                return;
        }

        m_HistoryPos.Insert(pos);
        m_HistoryTimeS.Insert(System.GetTickCount() * 0.001);
        while (m_HistoryPos.Count() > HISTORY_CAP)
        {
            m_HistoryPos.Remove(0);
            m_HistoryTimeS.Remove(0);
        }
    }

    protected void RebuildPredictSamples()
    {
        EnsurePathArrays();
        m_PredictPos.Clear();
        if (!m_Aircraft)
            return;

        vector pos = m_Aircraft.GetOrigin();
        vector vel = "0 0 0";
        Physics physics = m_Aircraft.GetPhysics();
        if (physics)
            vel = physics.GetVelocity();

        float speedSq = vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2];
        if (speedSq < 0.25 && m_HistoryPos.Count() >= 2)
        {
            int nH = m_HistoryPos.Count();
            vector a = m_HistoryPos.Get(nH - 2);
            vector b = m_HistoryPos.Get(nH - 1);
            float estDt = 0.45;
            if (m_HistoryTimeS.Count() >= 2)
            {
                float tA = m_HistoryTimeS.Get(nH - 2);
                float tB = m_HistoryTimeS.Get(nH - 1);
                estDt = tB - tA;
            }
            if (estDt < 0.05)
                estDt = 0.05;
            vel[0] = (b[0] - a[0]) / estDt;
            vel[1] = (b[1] - a[1]) / estDt;
            vel[2] = (b[2] - a[2]) / estDt;
            speedSq = vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2];
        }

        if (speedSq < 0.25)
            return;

        float stepS = PREDICT_HORIZON_S / PREDICT_SAMPLES;
        for (int i = 1; i <= PREDICT_SAMPLES; i++)
        {
            float t = stepS * i;
            vector p;
            p[0] = pos[0] + vel[0] * t;
            p[1] = pos[1] + vel[1] * t;
            p[2] = pos[2] + vel[2] * t;
            m_PredictPos.Insert(p);
        }
    }

    protected void BuildTrailDrawLists()
    {
        EnsurePathArrays();
        m_HistoryLabels.Clear();
        m_PredictLabels.Clear();
        m_DrawHistoryPos.Clear();

        int n = m_HistoryPos.Count();
        for (int i = 0; i < n; i++)
        {
            bool take = false;
            if ((i % HISTORY_DRAW_STRIDE) == 0)
                take = true;
            if (i == (n - 1))
                take = true;
            if (!take)
                continue;

            m_DrawHistoryPos.Insert(m_HistoryPos.Get(i));
            if (i == 0)
                m_HistoryLabels.Insert("past start");
            else
                m_HistoryLabels.Insert("past");
        }

        int np = m_PredictPos.Count();
        for (int j = 0; j < np; j++)
        {
            if (j == (np - 1))
                m_PredictLabels.Insert("pred +" + Math.Round(PREDICT_HORIZON_S).ToString() + "s");
            else
                m_PredictLabels.Insert("pred");
        }
    }

    protected void UpdateAircraftMarker()
    {
        if (!m_MapOpen)
            return;

        if (!m_Aircraft)
        {
            RemoveAircraftMarker();
            ClearPathMarkers();
            return;
        }

        SCR_MapMarkerManagerComponent mgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (!mgr)
            return;

        vector pos = m_Aircraft.GetOrigin();
        vector mat[4];
        m_Aircraft.GetWorldTransform(mat);
        vector fwd = mat[2];
        float headingDeg = Math.Atan2(fwd[2], fwd[0]) * 57.29577951;
        if (headingDeg < 0.0)
            headingDeg = headingDeg + 360.0;

        string modelName = RDF_RadarSignatureLibrary.FormatDisplayName(m_Aircraft);
        string prefix = m_AircraftLabel;
        if (prefix == "")
            prefix = "RDF AC";
        string title = prefix + " " + modelName;
        string text = string.Format(
            "AC %1  alt=%2m  hdg=%3",
            title,
            Math.Round(pos[1]).ToString(),
            Math.Round(headingDeg).ToString());

        int wx = Math.Round(pos[0]);
        int wz = Math.Round(pos[2]);
        int airIconFlags = ResolveAircraftSymbolIcon(m_Aircraft);

        if (!m_AircraftMarker || !m_AircraftMarker.GetRootWidget())
        {
            if (m_AircraftMarker)
                RemoveAircraftMarker();
            m_AircraftMarker = CreateAircraftMilitaryMarker(mgr, wx, wz, text, airIconFlags);
        }
        else
        {
            bool restyle = false;
            if (m_AircraftMarker.GetType() != SCR_EMapMarkerType.PLACED_MILITARY)
                restyle = true;
            if (m_AircraftMarker.GetFlags() != airIconFlags)
                restyle = true;
            if (restyle)
            {
                RemoveAircraftMarker();
                m_AircraftMarker = CreateAircraftMilitaryMarker(mgr, wx, wz, text, airIconFlags);
            }
            else
            {
                ApplyMarkerLive(m_AircraftMarker, wx, wz, 0, text);
            }
        }

        SyncPathMarkers(mgr);
    }

    protected int ResolveAircraftSymbolIcon(IEntity aircraft)
    {
        if (!aircraft)
            return EMilitarySymbolIcon.FIXED_WING;

        HelicopterControllerComponent heli = HelicopterControllerComponent.Cast(
            aircraft.FindComponent(HelicopterControllerComponent));
        if (heli)
            return EMilitarySymbolIcon.ROTARY_WING;

        return EMilitarySymbolIcon.FIXED_WING;
    }

    protected SCR_MapMarkerBase CreateAircraftMilitaryMarker(
        SCR_MapMarkerManagerComponent mgr,
        int worldX,
        int worldZ,
        string text,
        int iconFlags)
    {
        if (!mgr)
            return null;

        SCR_MapMarkerBase marker = mgr.PrepareMilitaryMarker(
            EMilitarySymbolIdentity.UNKNOWN,
            EMilitarySymbolDimension.AIR,
            iconFlags);
        if (!marker)
            return null;

        marker.SetWorldPos(worldX, worldZ);
        marker.SetCustomText(text);
        marker.SetRotation(0);
        mgr.InsertStaticMarker(marker, true);
        return marker;
    }

    protected void SyncPathMarkers(SCR_MapMarkerManagerComponent mgr)
    {
        if (!mgr)
            return;

        BuildTrailDrawLists();
        EnsurePathArrays();

        SyncPointList(
            mgr,
            m_DrawHistoryPos,
            m_HistoryLabels,
            m_HistoryMarkers,
            COLOR_TRAIL,
            ICON_TRAIL,
            "past",
            false);
        SyncPointList(
            mgr,
            m_PredictPos,
            m_PredictLabels,
            m_PredictMarkers,
            COLOR_PREDICT,
            ICON_PREDICT,
            "pred",
            false);
    }

    protected void SyncShellMarkers()
    {
        if (!m_MapOpen)
            return;

        EnsureShellArrays();
        SCR_MapMarkerManagerComponent mgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (!mgr)
            return;

        SyncPointList(
            mgr,
            m_ShellLaunchPos,
            m_ShellLaunchLabels,
            m_LaunchMarkers,
            COLOR_LAUNCH,
            ICON_LAUNCH,
            "launch",
            false);
        SyncPointList(
            mgr,
            m_ShellImpactPos,
            m_ShellImpactLabels,
            m_ImpactMarkers,
            COLOR_IMPACT,
            ICON_IMPACT,
            "impact",
            false);
    }

    protected void SyncPointList(
        SCR_MapMarkerManagerComponent mgr,
        array<vector> positions,
        array<string> labels,
        notnull array<ref SCR_MapMarkerBase> markers,
        int colorEntry,
        int iconEntry,
        string defaultLabel,
        bool orientAlongPath)
    {
        int want = 0;
        if (positions)
            want = positions.Count();

        // Shrink extras.
        while (markers.Count() > want)
        {
            int last = markers.Count() - 1;
            SCR_MapMarkerBase extra = markers.Get(last);
            if (extra)
                mgr.RemoveStaticMarker(extra);
            markers.Remove(last);
        }

        for (int i = 0; i < want; i++)
        {
            vector worldPos = positions.Get(i);
            string label = defaultLabel;
            if (labels && i < labels.Count())
                label = labels.Get(i);

            int wx = Math.Round(worldPos[0]);
            int wz = Math.Round(worldPos[2]);

            int rot = 0;
            if (orientAlongPath && want >= 2)
            {
                vector fromPos;
                vector toPos;
                if (i == 0)
                {
                    fromPos = positions.Get(0);
                    toPos = positions.Get(1);
                }
                else
                {
                    fromPos = positions.Get(i - 1);
                    toPos = positions.Get(i);
                }
                float dx = toPos[0] - fromPos[0];
                float dz = toPos[2] - fromPos[2];
                float headingDeg = Math.Atan2(dz, dx) * 57.29577951;
                if (headingDeg < 0.0)
                    headingDeg = headingDeg + 360.0;
                rot = Math.Round(headingDeg);
            }

            bool needCreate = false;
            if (i >= markers.Count())
            {
                needCreate = true;
            }
            else
            {
                SCR_MapMarkerBase existing = markers.Get(i);
                if (!existing || !existing.GetRootWidget())
                {
                    if (existing)
                        mgr.RemoveStaticMarker(existing);
                    needCreate = true;
                }
                else
                {
                    if (existing.GetIconEntry() != iconEntry)
                        needCreate = true;
                    if (existing.GetColorEntry() != colorEntry)
                        needCreate = true;
                    if (needCreate)
                        mgr.RemoveStaticMarker(existing);
                }
            }

            if (needCreate)
            {
                SCR_MapMarkerBase created = CreateMarker(
                    mgr, wx, wz, rot, label, colorEntry, iconEntry);
                if (i >= markers.Count())
                    markers.Insert(created);
                else
                    markers.Set(i, created);
                continue;
            }

            ApplyMarkerLive(markers.Get(i), wx, wz, rot, label);
        }
    }

    protected SCR_MapMarkerBase CreateMarker(
        SCR_MapMarkerManagerComponent mgr,
        int worldX,
        int worldZ,
        int rotationDeg,
        string text,
        int colorEntry,
        int iconEntry)
    {
        if (!mgr)
            return null;

        SCR_MapMarkerBase marker = new SCR_MapMarkerBase();
        marker.SetType(SCR_EMapMarkerType.PLACED_CUSTOM);
        marker.SetWorldPos(worldX, worldZ);
        marker.SetRotation(rotationDeg);
        marker.SetCustomText(text);
        marker.SetColorEntry(colorEntry);
        marker.SetIconEntry(iconEntry);
        mgr.InsertStaticMarker(marker, true);
        return marker;
    }

    protected void ApplyMarkerLive(
        SCR_MapMarkerBase marker,
        int worldX,
        int worldZ,
        int rotationDeg,
        string text)
    {
        if (!marker)
            return;

        marker.SetWorldPos(worldX, worldZ);
        marker.SetRotation(rotationDeg);
        marker.SetCustomText(text);

        Widget root = marker.GetRootWidget();
        if (!root)
            return;

        SCR_MapMarkerWidgetComponent comp = SCR_MapMarkerWidgetComponent.Cast(
            root.FindHandler(SCR_MapMarkerWidgetComponent));
        if (!comp)
            return;

        comp.SetText(text, true);
        comp.SetRotation(rotationDeg);
    }

    protected void RemoveAircraftMarker()
    {
        if (!m_AircraftMarker)
            return;
        SCR_MapMarkerManagerComponent mgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (mgr)
            mgr.RemoveStaticMarker(m_AircraftMarker);
        m_AircraftMarker = null;
    }

    protected void ClearPathMarkers()
    {
        EnsurePathArrays();
        SCR_MapMarkerManagerComponent mgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (mgr)
        {
            for (int i = m_HistoryMarkers.Count() - 1; i >= 0; i--)
            {
                SCR_MapMarkerBase m = m_HistoryMarkers.Get(i);
                if (m)
                    mgr.RemoveStaticMarker(m);
            }
            for (int j = m_PredictMarkers.Count() - 1; j >= 0; j--)
            {
                SCR_MapMarkerBase n = m_PredictMarkers.Get(j);
                if (n)
                    mgr.RemoveStaticMarker(n);
            }
        }
        m_HistoryMarkers.Clear();
        m_PredictMarkers.Clear();
    }

    protected void ClearShellMarkers()
    {
        EnsureShellArrays();
        SCR_MapMarkerManagerComponent mgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (mgr)
        {
            for (int i = m_LaunchMarkers.Count() - 1; i >= 0; i--)
            {
                SCR_MapMarkerBase m = m_LaunchMarkers.Get(i);
                if (m)
                    mgr.RemoveStaticMarker(m);
            }
            for (int j = m_ImpactMarkers.Count() - 1; j >= 0; j--)
            {
                SCR_MapMarkerBase n = m_ImpactMarkers.Get(j);
                if (n)
                    mgr.RemoveStaticMarker(n);
            }
        }
        m_LaunchMarkers.Clear();
        m_ImpactMarkers.Clear();
    }

    protected void ClearAllMarkers()
    {
        RemoveAircraftMarker();
        ClearPathMarkers();
        ClearShellMarkers();
    }
}
