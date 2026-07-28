// Radar PPI HUD — layout-driven panel + CanvasWidget north-up PPI.
// Structure: UI/layouts/RDF/RadarPPI.layout (CreateWidgets); script binds Names and draws Canvas.
// Green phosphor theme to distinguish from the blue LiDAR HUD.
//
// Screen layout (bottom-right corner):
//
//  +================================+
//  | (o) RADAR        <preset>     |  header
//  +--------------------------------+
//  |         N                      |
//  |  CanvasWidget PPI  128 x 128   |
//  | W    (+) radar           E     |
//  |         S                      |
//  +--------------------------------+
//  | Det / Trk / Best ...           |
//  | legend                         |
//  +================================+

class RDF_RadarHUD
{
    //! Registered layout resource (open once in Workbench if GUID not resolved).
    static const ResourceName LAYOUT_PPI = "{A7C31E920F4B6D81}UI/layouts/RDF/RadarPPI.layout";

    static const int PX_RADAR_H = 128;
    static const int PX_RADAR_W = 128;
    //! Root panel size (must match RadarPPI.layout rootFrame).
    static const int PANEL_W = 144;
    static const int PANEL_H = 184;
    static const int PANEL_MARGIN = 16;

    // ---- PPI canvas internals (unit coords == pixel coords 1:1) ----
    static const float PPI_CX   = 64.0;
    static const float PPI_CY   = 64.0;
    static const float PPI_R    = 60.0;
    static const float PPI_RING = 30.0;

    // Real-world range corresponding to PPI_R pixels.
    float m_DisplayRange = 2000.0;
    // Counter-battery alert ring radius in world meters (maps into PPI).
    float m_WlrAlertRadiusM = 80.0;
    bool m_DrawWlrAlerts = true;

    // ---- ARGB colours (CRT phosphor green) ----
    static const int COL_TITLE    = ARGB(220,  90,  245, 140);
    static const int COL_MODE     = ARGB(165,  65,  180, 100);
    static const int COL_DATA     = ARGB(205, 100, 230, 140);
    static const int COL_MUTED    = ARGB(115,  60,  140,  90);

    static const int COL_PPI_BG     = ARGB(255,  2,   10,   6);
    static const int COL_PPI_RING   = ARGB( 90, 40,  200, 110);
    static const int COL_PPI_RING2  = ARGB( 45, 30,  140,  80);
    static const int COL_PPI_CROSS  = ARGB( 55, 35,  160,  95);
    static const int COL_PPI_SWEEP  = ARGB(200, 100, 255, 150);
    static const int COL_PPI_RADAR  = ARGB(255, 40,  255, 140);

    // Blip colours by target type.
    static const int COL_VEHICLE  = ARGB(255,  60, 255, 120);   // green
    static const int COL_PROJ     = ARGB(255, 255, 170,  40);   // orange
    static const int COL_EMITTER  = ARGB(255, 255,  90, 230);   // magenta
    static const int COL_MULTIPATH = ARGB(255, 80, 220, 255);  // cyan NLOS bounce
    static const int COL_ANON      = ARGB(255, 255, 230, 130); // pale yellow
    static const int COL_FALSEPLOT = ARGB(255, 245, 245, 245); // near white
    static const int COL_UNDET    = ARGB(120, 110, 130, 110);   // dim grey-green
    static const int COL_WLR_LAUNCH = ARGB(220, 255, 160, 40);  // orange launch
    static const int COL_WLR_IMPACT = ARGB(220, 80, 180, 255);  // blue impact
    static const int COL_WLR_LINK   = ARGB(140, 200, 200, 200);

    static const float UPDATE_INTERVAL = 0.15;
    static const int PPI_MAX_BLIPS = 256;

    protected static ref RDF_RadarHUD s_Instance;

    protected float m_LastUpdateTime = 0.0;

    protected Widget m_wRoot;
    protected CanvasWidget m_Canvas;
    protected TextWidget   m_wMode;
    protected TextWidget   m_wStats;
    protected TextWidget   m_wLegend;
    protected TextWidget   m_wRingLabel;

    protected ref array<ref CanvasWidgetCommand> m_StaticCmds;
    protected ref array<ref CanvasWidgetCommand> m_AllCmds;

    // ---- float helpers ----
    static string F0(float v)
    {
        if (v < 0.0)
        {
            int n = (int)(-v + 0.5);
            return "-" + n.ToString();
        }
        int m = (int)(v + 0.5);
        return m.ToString();
    }

    // Range in metres -> compact label ("850m" / "1.2km").
    static string RangeLabel(float rangeM)
    {
        if (rangeM < 1000.0)
            return F0(rangeM) + "m";
        int km10 = (int)(rangeM / 100.0 + 0.5);
        int whole = km10 / 10;
        int frac = km10 % 10;
        return whole.ToString() + "." + frac.ToString() + "km";
    }

    // ---- singleton ----
    static RDF_RadarHUD GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarHUD();
        return s_Instance;
    }

    static void Show()
    {
        // Always rebuild: stale root after Hide/workspace swap would
        // otherwise leave the PPI invisible with no console error.
        RDF_RadarHUD inst = GetInstance();
        inst.DestroyWidgets();
        inst.BuildWidgets();
    }

    static void Hide()
    {
        RDF_RadarHUD inst = GetInstance();
        if (inst)
            inst.DestroyWidgets();
    }

    static void SetMode(string name)
    {
        RDF_RadarHUD inst = GetInstance();
        if (inst && inst.m_wMode)
            inst.m_wMode.SetText(name);
    }

    static void SetDisplayRange(float rangeM)
    {
        RDF_RadarHUD inst = GetInstance();
        if (inst && rangeM > 0.0)
            inst.m_DisplayRange = rangeM;
    }

    static bool IsVisible()
    {
        RDF_RadarHUD inst = GetInstance();
        return inst && inst.m_wRoot != null;
    }

    // Push one scan into the HUD. origin/forward are world-space; range sets the
    // PPI scale. tracker (optional) drives the confirmed-track count.
    static void FeedScan(
        array<ref RDF_RadarTarget> targets,
        vector origin,
        vector forward,
        float range,
        RDF_RadarProjectileTracker tracker)
    {
        RDF_RadarHUD inst = GetInstance();
        if (!inst || !inst.m_wRoot)
            return;
        inst.Update(targets, origin, forward, range, tracker);
    }

    static void SetWlrAlertRadiusM(float radiusM)
    {
        RDF_RadarHUD inst = GetInstance();
        if (!inst)
            return;
        if (radiusM < 5.0)
            radiusM = 5.0;
        if (radiusM > 2000.0)
            radiusM = 2000.0;
        inst.m_WlrAlertRadiusM = radiusM;
    }

    static void SetWlrAlertsEnabled(bool enabled)
    {
        RDF_RadarHUD inst = GetInstance();
        if (!inst)
            return;
        inst.m_DrawWlrAlerts = enabled;
    }

    // ---- update ----
    protected void Update(
        array<ref RDF_RadarTarget> targets,
        vector origin,
        vector forward,
        float range,
        RDF_RadarProjectileTracker tracker)
    {
        if (range > 0.0)
            m_DisplayRange = range;

        float now = System.GetTickCount() * 0.001;
        if (now - m_LastUpdateTime < UPDATE_INTERVAL)
            return;
        m_LastUpdateTime = now;

        UpdatePPI(targets, origin, forward, tracker);
        UpdateDataRows(targets, tracker);
        UpdateRingLabel();
    }

    //------------------------------------------------------------------------------------------------
    //! Instantiate RadarPPI.layout and bind named widgets.
    protected void BuildWidgets()
    {
        if (m_wRoot)
            return;

        WorkspaceWidget ws = GetGame().GetWorkspace();
        if (!ws)
        {
            Print("[RDF_RadarHUD] ERROR: GetWorkspace() returned null — close GM editor / return to character HUD", LogLevel.ERROR);
            return;
        }

        m_wRoot = ws.CreateWidgets(LAYOUT_PPI, null);
        if (!m_wRoot)
        {
            Print("[RDF_RadarHUD] ERROR: CreateWidgets failed for RadarPPI.layout — register UI/layouts/RDF in Workbench", LogLevel.ERROR);
            return;
        }

        m_wRoot.SetVisible(true);
        PinRootBottomRight(ws);

        Widget wCanvas = m_wRoot.FindAnyWidget("PpiCanvas");
        if (wCanvas)
        {
            m_Canvas = CanvasWidget.Cast(wCanvas);
            if (m_Canvas)
            {
                m_Canvas.SetVisible(true);
                m_Canvas.SetSizeInUnits(Vector(PX_RADAR_W, PX_RADAR_H, 0));
                BuildStaticDrawCommands();
            }
        }
        else
        {
            Print("[RDF_RadarHUD] WARN: PpiCanvas not found in layout", LogLevel.WARNING);
        }

        m_wMode = TextWidget.Cast(m_wRoot.FindAnyWidget("ModeText"));
        m_wStats = TextWidget.Cast(m_wRoot.FindAnyWidget("StatsText"));
        m_wLegend = TextWidget.Cast(m_wRoot.FindAnyWidget("LegendText"));
        m_wRingLabel = TextWidget.Cast(m_wRoot.FindAnyWidget("RingLabel"));

        if (m_wMode)
            m_wMode.SetColorInt(COL_MODE);
        if (m_wStats)
            m_wStats.SetColorInt(COL_DATA);
        if (m_wLegend)
            m_wLegend.SetColorInt(COL_MUTED);

        Widget wTitle = m_wRoot.FindAnyWidget("TitleText");
        if (wTitle)
            wTitle.SetColorInt(COL_TITLE);
        if (m_wMode)
            m_wMode.SetColorInt(COL_MODE);

        UpdateRingLabel();

        Print("[RDF_RadarHUD] HUD built from RadarPPI.layout — look BOTTOM-RIGHT");
    }

    //------------------------------------------------------------------------------------------------
    //! Force root into workspace bottom-right (layout Anchor alone is unreliable under CreateWidgets).
    protected void PinRootBottomRight(WorkspaceWidget ws)
    {
        if (!m_wRoot || !ws)
            return;

        int screenW = ws.GetWidth();
        int screenH = ws.GetHeight();
        if (screenW < 200)
            screenW = 1920;
        if (screenH < 200)
            screenH = 1080;

        int left = screenW - PANEL_W - PANEL_MARGIN;
        int top = screenH - PANEL_H - PANEL_MARGIN;
        if (left < PANEL_MARGIN)
            left = PANEL_MARGIN;
        if (top < PANEL_MARGIN)
            top = PANEL_MARGIN;

        FrameSlot.SetAnchor(m_wRoot, 0.0, 0.0);
        FrameSlot.SetSize(m_wRoot, PANEL_W, PANEL_H);
        FrameSlot.SetPos(m_wRoot, left, top);
    }

    protected void UpdateRingLabel()
    {
        if (!m_wRingLabel)
            return;
        m_wRingLabel.SetText(RangeLabel(m_DisplayRange * 0.5));
    }

    // ---- CRT face: black disc, range rings, crosshair, origin pip ----
    protected void BuildStaticDrawCommands()
    {
        if (!m_Canvas)
            return;

        m_StaticCmds = new array<ref CanvasWidgetCommand>();
        m_AllCmds    = new array<ref CanvasWidgetCommand>();

        vector center = Vector(PPI_CX, PPI_CY, 0);

        array<float> bgVerts = new array<float>();
        m_Canvas.TessellateCircle(center, PPI_R, 56, bgVerts);
        PolygonDrawCommand bgDisc = new PolygonDrawCommand();
        bgDisc.m_iColor   = COL_PPI_BG;
        bgDisc.m_Vertices = bgVerts;
        m_StaticCmds.Insert(bgDisc);

        AddPpiRing(center, PPI_R * 0.25, COL_PPI_RING2, 40);
        AddPpiRing(center, PPI_RING, COL_PPI_RING2, 44);
        AddPpiRing(center, PPI_R * 0.75, COL_PPI_RING2, 48);
        AddPpiRing(center, PPI_R - 1.0, COL_PPI_RING, 56);

        array<float> crossNS = new array<float>();
        crossNS.Insert(PPI_CX);
        crossNS.Insert(PPI_CY - PPI_R + 2.0);
        crossNS.Insert(PPI_CX);
        crossNS.Insert(PPI_CY + PPI_R - 2.0);
        LineDrawCommand axisNS = new LineDrawCommand();
        axisNS.m_iColor   = COL_PPI_CROSS;
        axisNS.m_fWidth   = 1.0;
        axisNS.m_Vertices = crossNS;
        m_StaticCmds.Insert(axisNS);

        array<float> crossEW = new array<float>();
        crossEW.Insert(PPI_CX - PPI_R + 2.0);
        crossEW.Insert(PPI_CY);
        crossEW.Insert(PPI_CX + PPI_R - 2.0);
        crossEW.Insert(PPI_CY);
        LineDrawCommand axisEW = new LineDrawCommand();
        axisEW.m_iColor   = COL_PPI_CROSS;
        axisEW.m_fWidth   = 1.0;
        axisEW.m_Vertices = crossEW;
        m_StaticCmds.Insert(axisEW);

        array<float> radarVerts = new array<float>();
        m_Canvas.TessellateCircle(center, 2.5, 12, radarVerts);
        PolygonDrawCommand radarDot = new PolygonDrawCommand();
        radarDot.m_iColor   = COL_PPI_RADAR;
        radarDot.m_Vertices = radarVerts;
        m_StaticCmds.Insert(radarDot);

        foreach (CanvasWidgetCommand cmd : m_StaticCmds)
            m_AllCmds.Insert(cmd);

        m_Canvas.SetDrawCommands(m_AllCmds);
    }

    protected void AddPpiRing(vector center, float radius, int color, int segments)
    {
        if (!m_Canvas || radius < 2.0)
            return;
        array<float> verts = new array<float>();
        m_Canvas.TessellateCircle(center, radius, segments, verts);
        LineDrawCommand ring = new LineDrawCommand();
        ring.m_iColor         = color;
        ring.m_fWidth         = 1.0;
        ring.m_bShouldEnclose = true;
        ring.m_Vertices       = verts;
        m_StaticCmds.Insert(ring);
    }

    // ---- update PPI with new scan data ----
    protected void UpdatePPI(
        array<ref RDF_RadarTarget> targets,
        vector origin,
        vector forward,
        RDF_RadarProjectileTracker tracker)
    {
        if (!m_Canvas || !m_StaticCmds)
            return;

        if (m_AllCmds)
            m_AllCmds.Clear();
        else
            m_AllCmds = new array<ref CanvasWidgetCommand>();
        foreach (CanvasWidgetCommand cmd : m_StaticCmds)
            m_AllCmds.Insert(cmd);

        // Sweep line: radar boresight on a north-up plan (screen up = world +Z).
        // This is entity facing (transform[2]), not free-look camera direction.
        float fx = forward[0];
        float fz = forward[2];
        float flen = Math.Sqrt(fx * fx + fz * fz);
        if (flen > 0.0001)
        {
            float sweepX = PPI_CX + (fx / flen) * PPI_R;
            float sweepY = PPI_CY - (fz / flen) * PPI_R;
            array<float> sweepVerts = new array<float>();
            sweepVerts.Insert(PPI_CX);
            sweepVerts.Insert(PPI_CY);
            sweepVerts.Insert(sweepX);
            sweepVerts.Insert(sweepY);
            LineDrawCommand sweep = new LineDrawCommand();
            sweep.m_iColor   = COL_PPI_SWEEP;
            sweep.m_fWidth   = 1.5;
            sweep.m_Vertices = sweepVerts;
            m_AllCmds.Insert(sweep);
        }

        if (targets && m_DisplayRange > 0.0)
        {
            int blipsAdded = 0;
            foreach (RDF_RadarTarget t : targets)
            {
                if (blipsAdded >= PPI_MAX_BLIPS)
                    break;
                if (!t)
                    continue;

                vector delta = t.m_Position - origin;
                float normX = delta[0] / m_DisplayRange;
                float normZ = delta[2] / m_DisplayRange;

                float d2 = normX * normX + normZ * normZ;
                if (d2 > 1.0)
                {
                    float d = Math.Sqrt(d2);
                    normX = normX / d;
                    normZ = normZ / d;
                }

                float bx = PPI_CX + normX * PPI_R;
                float by = PPI_CY - normZ * PPI_R;

                int blipCol = BlipColor(t);
                float blipR = BlipRadius(t);

                array<float> blipVerts = new array<float>();
                m_Canvas.TessellateCircle(Vector(bx, by, 0), blipR, 8, blipVerts);
                PolygonDrawCommand blip = new PolygonDrawCommand();
                blip.m_iColor   = blipCol;
                blip.m_Vertices = blipVerts;
                m_AllCmds.Insert(blip);
                blipsAdded = blipsAdded + 1;
            }
        }

        DrawWlrAlerts(origin, tracker);
        m_Canvas.SetDrawCommands(m_AllCmds);
    }

    // Counter-battery alert rings: launch (orange) and impact (blue) on the PPI.
    protected void DrawWlrAlerts(vector origin, RDF_RadarProjectileTracker tracker)
    {
        if (!tracker || !m_Canvas || m_DisplayRange <= 0.0)
            return;
        if (!m_DrawWlrAlerts)
            return;

        array<ref RDF_RadarTrack> tracks = tracker.GetAllTracks();
        if (!tracks)
            return;

        float alertNorm = m_WlrAlertRadiusM / m_DisplayRange;
        float alertPx = alertNorm * PPI_R;
        if (alertPx < 4.0)
            alertPx = 4.0;
        if (alertPx > 18.0)
            alertPx = 18.0;

        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (!tr || !tr.m_Confirmed)
                continue;
            RDF_RadarWlrFix fix = tr.m_LastWlrFix;
            if (!fix)
                continue;

            if (fix.m_LaunchValid && fix.m_ImpactValid)
            {
                float lx0;
                float ly0;
                float lx1;
                float ly1;
                if (WorldToPpi(origin, fix.m_LaunchPos, lx0, ly0)
                    && WorldToPpi(origin, fix.m_ImpactPos, lx1, ly1))
                {
                    array<float> linkVerts = new array<float>();
                    linkVerts.Insert(lx0);
                    linkVerts.Insert(ly0);
                    linkVerts.Insert(lx1);
                    linkVerts.Insert(ly1);
                    LineDrawCommand link = new LineDrawCommand();
                    link.m_iColor = COL_WLR_LINK;
                    link.m_fWidth = 1.0;
                    link.m_Vertices = linkVerts;
                    m_AllCmds.Insert(link);
                }
            }

            if (fix.m_LaunchValid)
                DrawPpiAlertRing(origin, fix.m_LaunchPos, alertPx, COL_WLR_LAUNCH);
            if (fix.m_ImpactValid)
                DrawPpiAlertRing(origin, fix.m_ImpactPos, alertPx, COL_WLR_IMPACT);
        }
    }

    protected bool WorldToPpi(vector origin, vector worldPos, out float outX, out float outY)
    {
        outX = PPI_CX;
        outY = PPI_CY;
        vector delta = worldPos - origin;
        float normX = delta[0] / m_DisplayRange;
        float normZ = delta[2] / m_DisplayRange;
        float d2 = normX * normX + normZ * normZ;
        if (d2 > 1.0)
        {
            float d = Math.Sqrt(d2);
            normX = normX / d;
            normZ = normZ / d;
        }
        outX = PPI_CX + normX * PPI_R;
        outY = PPI_CY - normZ * PPI_R;
        return true;
    }

    protected void DrawPpiAlertRing(vector origin, vector worldPos, float radiusPx, int color)
    {
        float bx;
        float by;
        if (!WorldToPpi(origin, worldPos, bx, by))
            return;

        array<float> ringVerts = new array<float>();
        m_Canvas.TessellateCircle(Vector(bx, by, 0), radiusPx, 20, ringVerts);
        LineDrawCommand ring = new LineDrawCommand();
        ring.m_iColor = color;
        ring.m_fWidth = 1.5;
        ring.m_bShouldEnclose = true;
        ring.m_Vertices = ringVerts;
        m_AllCmds.Insert(ring);

        array<float> coreVerts = new array<float>();
        m_Canvas.TessellateCircle(Vector(bx, by, 0), 2.5, 8, coreVerts);
        PolygonDrawCommand core = new PolygonDrawCommand();
        core.m_iColor = color;
        core.m_Vertices = coreVerts;
        m_AllCmds.Insert(core);
    }

    protected int BlipColor(RDF_RadarTarget t)
    {
        if (!t.m_Detected)
            return COL_UNDET;
        if (t.m_IsFalsePlot)
            return COL_FALSEPLOT;
        if (t.m_IsAnonymous)
            return COL_ANON;
        if (t.m_LosBlocked)
            return COL_MULTIPATH;
        if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return COL_PROJ;
        if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return COL_EMITTER;
        return COL_VEHICLE;
    }

    // Blip size scales with SNR (stronger returns draw larger).
    protected float BlipRadius(RDF_RadarTarget t)
    {
        float r = 2.0;
        if (t.m_Detected)
        {
            r = 2.0 + t.m_SnrDb * 0.08;
            if (r < 2.0)
                r = 2.0;
            if (r > 5.0)
                r = 5.0;
        }
        else
        {
            r = 1.5;
        }
        return r;
    }

    // ---- update data text rows ----
    protected void UpdateDataRows(
        array<ref RDF_RadarTarget> targets,
        RDF_RadarProjectileTracker tracker)
    {
        int total = 0;
        int detected = 0;
        RDF_RadarTarget best;
        float bestSnr = -100000.0;

        if (targets)
        {
            total = targets.Count();
            foreach (RDF_RadarTarget t : targets)
            {
                if (!t)
                    continue;
                if (t.m_Detected)
                {
                    detected = detected + 1;
                    if (t.m_SnrDb > bestSnr)
                    {
                        bestSnr = t.m_SnrDb;
                        best = t;
                    }
                }
            }
        }

        int confirmedTracks = 0;
        int wlrFixes = 0;
        if (tracker)
        {
            array<ref RDF_RadarTrack> tracks = tracker.GetAllTracks();
            if (tracks)
            {
                foreach (RDF_RadarTrack tr : tracks)
                {
                    if (!tr || !tr.m_Confirmed)
                        continue;
                    confirmedTracks = confirmedTracks + 1;
                    if (tr.m_LastWlrFix && tr.m_LastWlrFix.m_LaunchValid)
                        wlrFixes = wlrFixes + 1;
                }
            }
        }

        if (m_wStats)
        {
            string s = "DET " + detected.ToString() + "/" + total.ToString()
                + "  TRK " + confirmedTracks.ToString()
                + "  WLR " + wlrFixes.ToString();
            if (best)
            {
                s = s + "  ·  " + TypeTag(best.m_Type) + " "
                    + RangeLabel(best.m_Distance);
            }
            m_wStats.SetText(s);
        }
        if (m_wLegend)
            m_wLegend.SetText("");
    }

    protected string TypeTag(ERDF_RadarTargetType type)
    {
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return "PROJ";
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return "EMIT";
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS)
            return "ANON";
        return "VEH";
    }

    // ---- destroy layout root ----
    protected void DestroyWidgets()
    {
        if (m_wRoot)
            m_wRoot.RemoveFromHierarchy();
        m_wRoot      = null;
        m_Canvas     = null;
        m_wMode      = null;
        m_wStats     = null;
        m_wLegend    = null;
        m_wRingLabel = null;
        m_StaticCmds = null;
        m_AllCmds    = null;
    }
}
