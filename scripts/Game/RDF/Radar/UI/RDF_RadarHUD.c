// Radar terminal HUD — PPI + A-Scope + Range-Doppler + SNR waterfall.
// Structure: UI/layouts/RDF/RadarPPI.layout (CreateWidgets); script binds Names and draws Canvas.
// Green phosphor theme. Charts use plot measurements only (no entity truth).
//
// Screen layout (bottom-right corner, ~480 x 500):
//
//  +======================+============+
//  | RADAR     TERMINAL                |
//  | PPI 192x192          | A-SCOPE    |
//  +----------------------+------------+
//  | R-D MAP  456x120                  |
//  | WATERFALL 456x96                  |
//  | Det / Trk ...                     |
//  +===================================+

class RDF_RadarHUD
{
    //! Registered layout resource (open once in Workbench if GUID not resolved).
    static const ResourceName LAYOUT_PPI = "{A7C31E920F4B6D81}UI/layouts/RDF/RadarPPI.layout";

    static const int PX_RADAR_H = 192;
    static const int PX_RADAR_W = 192;
    //! Root panel size (must match RadarPPI.layout rootFrame).
    static const int PANEL_W = 480;
    static const int PANEL_H = 500;
    static const int PANEL_MARGIN = 16;

    static const int ASCOPE_W = 160;
    static const int ASCOPE_H = 178;
    static const int RD_W = 456;
    static const int RD_H = 120;
    static const int WF_W = 456;
    static const int WF_H = 96;

    // ---- PPI canvas internals (unit coords == pixel coords 1:1) ----
    static const float PPI_CX   = 96.0;
    static const float PPI_CY   = 96.0;
    static const float PPI_R    = 90.0;
    static const float PPI_RING = 45.0;

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

    static const int COL_CHART_BG   = ARGB(255,  2,   8,   5);
    static const int COL_CHART_GRID = ARGB( 70, 30, 120,  70);
    static const int COL_ASCOPE_LINE = ARGB(230, 80, 255, 140);

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
    protected CanvasWidget m_AscopeCanvas;
    protected CanvasWidget m_RdCanvas;
    protected CanvasWidget m_WaterfallCanvas;
    protected TextWidget   m_wMode;
    protected TextWidget   m_wStats;
    protected TextWidget   m_wLegend;
    protected TextWidget   m_wRingLabel;

    protected ref array<ref CanvasWidgetCommand> m_StaticCmds;
    protected ref array<ref CanvasWidgetCommand> m_AllCmds;
    protected ref array<ref CanvasWidgetCommand> m_AscopeCmds;
    protected ref array<ref CanvasWidgetCommand> m_RdCmds;
    protected ref array<ref CanvasWidgetCommand> m_WfCmds;

    protected ref array<float> m_AscopeBins;
    protected ref array<float> m_RdGrid;
    protected ref array<float> m_WfBuffer;
    protected ref array<float> m_WfRowScratch;
    protected ref array<float> m_HeatRowScratch;
    protected int m_WfWriteHead;

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
        UpdateSideCharts(targets);
        UpdateDataRows(targets, tracker);
        UpdateRingLabel();
    }

    protected void EnsureChartBuffers()
    {
        if (!m_AscopeBins)
            m_AscopeBins = new array<float>();
        if (!m_RdGrid)
            m_RdGrid = new array<float>();
        if (!m_WfBuffer)
            m_WfBuffer = new array<float>();
        if (!m_WfRowScratch)
            m_WfRowScratch = new array<float>();
        if (!m_HeatRowScratch)
            m_HeatRowScratch = new array<float>();
        if (!m_AscopeCmds)
            m_AscopeCmds = new array<ref CanvasWidgetCommand>();
        if (!m_RdCmds)
            m_RdCmds = new array<ref CanvasWidgetCommand>();
        if (!m_WfCmds)
            m_WfCmds = new array<ref CanvasWidgetCommand>();
    }

    protected void BindChartCanvas(string name, out CanvasWidget canvas, int unitW, int unitH)
    {
        canvas = null;
        if (!m_wRoot)
            return;
        Widget w = m_wRoot.FindAnyWidget(name);
        if (!w)
        {
            Print("[RDF_RadarHUD] WARN: " + name + " not found in layout", LogLevel.WARNING);
            return;
        }
        canvas = CanvasWidget.Cast(w);
        if (!canvas)
            return;
        canvas.SetVisible(true);
        canvas.SetSizeInUnits(Vector(unitW, unitH, 0));
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

        EnsureChartBuffers();
        BindChartCanvas("AscopeCanvas", m_AscopeCanvas, ASCOPE_W, ASCOPE_H);
        BindChartCanvas("RdCanvas", m_RdCanvas, RD_W, RD_H);
        BindChartCanvas("WaterfallCanvas", m_WaterfallCanvas, WF_W, WF_H);

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

        Print("[RDF_RadarHUD] terminal HUD built — PPI + A-Scope + R-D + waterfall (BOTTOM-RIGHT)");
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
            if (r > 6.5)
                r = 6.5;
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
            m_wLegend.SetText("plots only · no entity truth");
    }

    // ---- A-Scope / R-D / waterfall (plot measurements) ----
    protected void UpdateSideCharts(array<ref RDF_RadarTarget> targets)
    {
        EnsureChartBuffers();
        RDF_RadarHudCharts.BuildAscopeBins(targets, m_DisplayRange, m_AscopeBins);
        RDF_RadarHudCharts.BuildRangeDopplerGrid(targets, m_DisplayRange, m_RdGrid);

        RDF_RadarHudCharts.DownsampleAscopeToWaterfallRow(m_AscopeBins, m_WfRowScratch);
        m_WfWriteHead = RDF_RadarHudCharts.PushWaterfallRow(
            m_WfBuffer, m_WfWriteHead, m_WfRowScratch);

        DrawAscope();
        DrawRangeDoppler();
        DrawWaterfall();
    }

    protected void DrawChartBackground(
        notnull array<ref CanvasWidgetCommand> cmds,
        float width,
        float height)
    {
        array<float> bg = new array<float>();
        bg.Insert(0.0);
        bg.Insert(0.0);
        bg.Insert(width);
        bg.Insert(0.0);
        bg.Insert(width);
        bg.Insert(height);
        bg.Insert(0.0);
        bg.Insert(height);
        PolygonDrawCommand poly = new PolygonDrawCommand();
        poly.m_iColor = COL_CHART_BG;
        poly.m_Vertices = bg;
        cmds.Insert(poly);
    }

    protected void DrawAscope()
    {
        if (!m_AscopeCanvas)
            return;

        m_AscopeCmds.Clear();
        DrawChartBackground(m_AscopeCmds, ASCOPE_W, ASCOPE_H);

        // Horizontal grid.
        for (int g = 1; g < 4; g++)
        {
            float gy = (ASCOPE_H * g) / 4.0;
            array<float> gv = new array<float>();
            gv.Insert(0.0);
            gv.Insert(gy);
            gv.Insert(ASCOPE_W);
            gv.Insert(gy);
            LineDrawCommand grid = new LineDrawCommand();
            grid.m_iColor = COL_CHART_GRID;
            grid.m_fWidth = 1.0;
            grid.m_Vertices = gv;
            m_AscopeCmds.Insert(grid);
        }

        float peak = RDF_RadarHudCharts.FindPeak(m_AscopeBins);
        int n = m_AscopeBins.Count();
        if (n < 2)
        {
            m_AscopeCanvas.SetDrawCommands(m_AscopeCmds);
            return;
        }

        array<float> curve = new array<float>();
        float margin = 2.0;
        float plotH = ASCOPE_H - margin * 2.0;
        for (int i = 0; i < n; i++)
        {
            float x = (i * (ASCOPE_W - 1.0)) / (n - 1);
            float amp = m_AscopeBins.Get(i) / peak;
            if (amp < 0.0)
                amp = 0.0;
            if (amp > 1.0)
                amp = 1.0;
            float y = ASCOPE_H - margin - amp * plotH;
            curve.Insert(x);
            curve.Insert(y);
        }

        LineDrawCommand trace = new LineDrawCommand();
        trace.m_iColor = COL_ASCOPE_LINE;
        trace.m_fWidth = 1.5;
        trace.m_Vertices = curve;
        m_AscopeCmds.Insert(trace);

        m_AscopeCanvas.SetDrawCommands(m_AscopeCmds);
    }

    protected void DrawRangeDoppler()
    {
        if (!m_RdCanvas)
            return;

        m_RdCmds.Clear();
        DrawChartBackground(m_RdCmds, RD_W, RD_H);

        float peak = RDF_RadarHudCharts.FindPeak(m_RdGrid);
        float cellW = RD_W / RDF_RadarHudCharts.RD_RANGE_BINS;
        float cellH = RD_H / RDF_RadarHudCharts.RD_DOP_BINS;
        if (cellH < 1.0)
            cellH = 1.0;

        for (int d = 0; d < RDF_RadarHudCharts.RD_DOP_BINS; d++)
        {
            m_HeatRowScratch.Clear();
            int base = d * RDF_RadarHudCharts.RD_RANGE_BINS;
            for (int r = 0; r < RDF_RadarHudCharts.RD_RANGE_BINS; r++)
            {
                float v = 0.0;
                int idx = base + r;
                if (idx < m_RdGrid.Count())
                    v = m_RdGrid.Get(idx);
                m_HeatRowScratch.Insert(v);
            }
            // Doppler axis: top = +Hz, bottom = -Hz (d=0 at top).
            float y = (d + 0.5) * cellH;
            RDF_RadarHudCharts.AppendHeatRowRle(
                m_RdCmds,
                m_HeatRowScratch,
                y,
                0.0,
                cellW,
                cellH,
                peak);
        }

        m_RdCanvas.SetDrawCommands(m_RdCmds);
    }

    protected void DrawWaterfall()
    {
        if (!m_WaterfallCanvas)
            return;

        m_WfCmds.Clear();
        DrawChartBackground(m_WfCmds, WF_W, WF_H);

        float peak = RDF_RadarHudCharts.FindPeak(m_WfBuffer);
        float cellW = WF_W / RDF_RadarHudCharts.WF_COLS;
        float cellH = WF_H / RDF_RadarHudCharts.WF_ROWS;
        if (cellH < 1.0)
            cellH = 1.0;

        // Newest row just written is at writeHead-1; draw newest at top.
        for (int row = 0; row < RDF_RadarHudCharts.WF_ROWS; row++)
        {
            int srcRow = m_WfWriteHead - 1 - row;
            while (srcRow < 0)
                srcRow = srcRow + RDF_RadarHudCharts.WF_ROWS;

            m_HeatRowScratch.Clear();
            int base = srcRow * RDF_RadarHudCharts.WF_COLS;
            for (int c = 0; c < RDF_RadarHudCharts.WF_COLS; c++)
            {
                float v = 0.0;
                int idx = base + c;
                if (idx < m_WfBuffer.Count())
                    v = m_WfBuffer.Get(idx);
                m_HeatRowScratch.Insert(v);
            }

            float y = (row + 0.5) * cellH;
            RDF_RadarHudCharts.AppendHeatRowRle(
                m_WfCmds,
                m_HeatRowScratch,
                y,
                0.0,
                cellW,
                cellH,
                peak);
        }

        m_WaterfallCanvas.SetDrawCommands(m_WfCmds);
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
        m_wRoot = null;
        m_Canvas = null;
        m_AscopeCanvas = null;
        m_RdCanvas = null;
        m_WaterfallCanvas = null;
        m_wMode = null;
        m_wStats = null;
        m_wLegend = null;
        m_wRingLabel = null;
        m_StaticCmds = null;
        m_AllCmds = null;
        m_AscopeCmds = null;
        m_RdCmds = null;
        m_WfCmds = null;
    }
}
