// Radar PPI HUD - header bar + CanvasWidget 2D top-down plan-position display (north-up)
// + data rows. All widgets created dynamically via WorkspaceWidget.CreateWidgetInWorkspace().
// No .layout file required. Green "phosphor" theme to distinguish from the blue LiDAR HUD.
//
// Screen layout (bottom-right corner, compact so it does not eat the view):
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
    // ---- panel geometry (reference px; origin computed at Build from workspace) ----
    static const int PX_W       = 136;
    static const int PX_HDR_H   = 22;

    static const int PX_RADAR_H = 128;
    static const int PX_RADAR_W = 128;

    static const int PX_ROW_H   = 18;
    static const int PX_PAD_X   = 5;
    static const int PX_PAD_Y   = 3;

    static const int PX_H       = PX_HDR_H + PX_RADAR_H + 2 * PX_ROW_H + 4;
    // Tuck into the corner; keep clear of screen edges / common UI chrome.
    static const int PX_MARGIN_RIGHT = 14;
    static const int PX_MARGIN_BOTTOM = 14;

    // ---- PPI canvas internals (unit coords == pixel coords 1:1) ----
    static const float PPI_CX   = 64.0;
    static const float PPI_CY   = 64.0;
    static const float PPI_R    = 60.0;
    static const float PPI_RING = 30.0;

    // Real-world range corresponding to PPI_R pixels.
    float m_DisplayRange = 2000.0;

    // Resolved at BuildWidgets() from WorkspaceWidget size (bottom-right).
    protected int m_PxLeft = 20;
    protected int m_PxTop = 700;
    protected int m_PxDataY = 937;

    // ---- ARGB colours (green phosphor theme) ----
    static const int COL_PANEL    = ARGB(165,  6,  16,  8);
    static const int COL_HDR      = ARGB(210, 12,  30,  16);
    static const int COL_TITLE    = ARGB(255, 120, 255, 140);
    static const int COL_MODE     = ARGB(255, 90,  220, 120);
    static const int COL_DATA     = ARGB(255, 120, 235, 150);
    static const int COL_COMPASS  = ARGB(180, 90,  220, 120);

    static const int COL_PPI_BG     = ARGB(240, 6,   20,  10);
    static const int COL_PPI_RING   = ARGB( 70, 60,  200, 100);
    static const int COL_PPI_SWEEP  = ARGB(160, 120, 255, 140);
    static const int COL_PPI_RADAR  = ARGB(255, 0,   255, 120);

    // Blip colours by target type.
    static const int COL_VEHICLE  = ARGB(255,  60, 255, 120);   // green
    static const int COL_PROJ     = ARGB(255, 255, 170,  40);   // orange
    static const int COL_EMITTER  = ARGB(255, 255,  90, 230);   // magenta
    static const int COL_MULTIPATH = ARGB(255, 80, 220, 255);  // cyan NLOS bounce
    static const int COL_ANON      = ARGB(255, 255, 230, 130); // pale yellow
    static const int COL_FALSEPLOT = ARGB(255, 245, 245, 245); // near white
    static const int COL_UNDET    = ARGB(120, 110, 130, 110);   // dim grey-green

    static const float UPDATE_INTERVAL = 0.15;
    static const int PPI_MAX_BLIPS = 256;

    protected static ref RDF_RadarHUD s_Instance;

    protected float m_LastUpdateTime = 0.0;

    protected ref array<ref Widget> m_AllWidgets;
    protected CanvasWidget m_Canvas;
    protected TextWidget   m_wMode;
    protected TextWidget   m_wStats;
    protected TextWidget   m_wLegend;

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
        // Always rebuild: stale m_AllWidgets after Hide/workspace swap would
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
        return inst && inst.m_AllWidgets != null;
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
        if (!inst || !inst.m_AllWidgets)
            return;
        inst.Update(targets, origin, forward, range, tracker);
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

        UpdatePPI(targets, origin, forward);
        UpdateDataRows(targets, tracker);
    }

    // ---- build all widgets once ----
    protected void BuildWidgets()
    {
        if (m_AllWidgets)
            return;

        WorkspaceWidget ws = GetGame().GetWorkspace();
        if (!ws)
        {
            Print("[RDF_RadarHUD] ERROR: GetWorkspace() returned null — close GM editor / return to character HUD", LogLevel.ERROR);
            return;
        }

        ResolveLayoutOrigin(ws);

        m_AllWidgets = new array<ref Widget>();

        MakeFrame(ws, m_PxLeft, m_PxTop, PX_W, PX_H, 90, COL_PANEL);
        MakeFrame(ws, m_PxLeft, m_PxTop, PX_W, PX_HDR_H, 91, COL_HDR);

        Widget wTitle = MakeText(ws, m_PxLeft + PX_PAD_X, m_PxTop + PX_PAD_Y,
                                 72, PX_HDR_H - PX_PAD_Y, 95, COL_TITLE);
        TextWidget.Cast(wTitle).SetText("(o) RADAR");
        TextWidget.Cast(wTitle).SetExactFontSize(14);

        Widget wMode = MakeText(ws, m_PxLeft + 74, m_PxTop + PX_PAD_Y,
                                PX_W - 78, PX_HDR_H - PX_PAD_Y, 95, COL_MODE);
        m_wMode = TextWidget.Cast(wMode);
        m_wMode.SetText("PPI");
        m_wMode.SetExactFontSize(12);

        int canvasTop = m_PxTop + PX_HDR_H;
        Widget wCanvas = ws.CreateWidgetInWorkspace(
            WidgetType.CanvasWidgetTypeID,
            m_PxLeft, canvasTop, PX_RADAR_W, PX_RADAR_H,
            0, null, 92);
        if (wCanvas)
        {
            m_Canvas = CanvasWidget.Cast(wCanvas);
            m_Canvas.SetVisible(true);
            m_Canvas.SetSizeInUnits(Vector(PX_RADAR_W, PX_RADAR_H, 0));
            m_AllWidgets.Insert(wCanvas);
            BuildStaticDrawCommands();
        }
        else
        {
            Print("[RDF_RadarHUD] WARN: CanvasWidget creation failed", LogLevel.WARNING);
        }

        // Compass labels (north-up plan display).
        MakeCompassLabel(ws, m_PxLeft + 58, canvasTop + 4,   "N");
        MakeCompassLabel(ws, m_PxLeft + 58, canvasTop + 112, "S");
        MakeCompassLabel(ws, m_PxLeft + 4,   canvasTop + 56, "W");
        MakeCompassLabel(ws, m_PxLeft + 114, canvasTop + 56, "E");

        string ringLabel = RangeLabel(m_DisplayRange * 0.5);
        Widget wRingLbl = MakeText(ws,
            m_PxLeft + (int)PPI_CX + 2,
            canvasTop + (int)(PPI_CY - PPI_RING) - 1,
            40, 12, 96, ARGB(130, 70, 200, 110));
        TextWidget.Cast(wRingLbl).SetText(ringLabel);
        TextWidget.Cast(wRingLbl).SetExactFontSize(10);

        m_wStats  = TextWidget.Cast(MakeDataRow(ws, 0, "Det --   Trk --"));
        m_wLegend = TextWidget.Cast(MakeDataRow(ws, 1, "veh/proj/emit/anon + white false"));
        TextWidget.Cast(m_wLegend).SetExactFontSize(11);

        Print(string.Format(
            "[RDF_RadarHUD] HUD built widgets=%1 at left=%2 top=%3 (workspace %4x%5) — look BOTTOM-RIGHT",
            m_AllWidgets.Count().ToString(),
            m_PxLeft.ToString(),
            m_PxTop.ToString(),
            ws.GetWidth().ToString(),
            ws.GetHeight().ToString()));
    }

    protected void ResolveLayoutOrigin(WorkspaceWidget ws)
    {
        int screenW = 1920;
        int screenH = 1080;
        if (ws)
        {
            int w = ws.GetWidth();
            int h = ws.GetHeight();
            if (w > 200)
                screenW = w;
            if (h > 200)
                screenH = h;
        }

        m_PxLeft = screenW - PX_W - PX_MARGIN_RIGHT;
        if (m_PxLeft < PX_MARGIN_RIGHT)
            m_PxLeft = PX_MARGIN_RIGHT;

        m_PxTop = screenH - PX_H - PX_MARGIN_BOTTOM;
        if (m_PxTop < PX_MARGIN_BOTTOM)
            m_PxTop = PX_MARGIN_BOTTOM;

        m_PxDataY = m_PxTop + PX_HDR_H + PX_RADAR_H + 2;
    }

    // ---- build PPI static draw commands ----
    protected void BuildStaticDrawCommands()
    {
        if (!m_Canvas)
            return;

        m_StaticCmds = new array<ref CanvasWidgetCommand>();
        m_AllCmds    = new array<ref CanvasWidgetCommand>();

        vector center = Vector(PPI_CX, PPI_CY, 0);

        array<float> bgVerts = new array<float>();
        m_Canvas.TessellateCircle(center, PPI_R, 48, bgVerts);
        PolygonDrawCommand bgDisc = new PolygonDrawCommand();
        bgDisc.m_iColor   = COL_PPI_BG;
        bgDisc.m_Vertices = bgVerts;
        m_StaticCmds.Insert(bgDisc);

        array<float> ring100Verts = new array<float>();
        m_Canvas.TessellateCircle(center, PPI_R - 1.0, 48, ring100Verts);
        LineDrawCommand ring100 = new LineDrawCommand();
        ring100.m_iColor         = COL_PPI_RING;
        ring100.m_fWidth         = 1.0;
        ring100.m_bShouldEnclose = true;
        ring100.m_Vertices       = ring100Verts;
        m_StaticCmds.Insert(ring100);

        array<float> ring50Verts = new array<float>();
        m_Canvas.TessellateCircle(center, PPI_RING, 48, ring50Verts);
        LineDrawCommand ring50 = new LineDrawCommand();
        ring50.m_iColor         = COL_PPI_RING;
        ring50.m_fWidth         = 1.0;
        ring50.m_bShouldEnclose = true;
        ring50.m_Vertices       = ring50Verts;
        m_StaticCmds.Insert(ring50);

        array<float> radarVerts = new array<float>();
        m_Canvas.TessellateCircle(center, 4.0, 12, radarVerts);
        PolygonDrawCommand radarDot = new PolygonDrawCommand();
        radarDot.m_iColor   = COL_PPI_RADAR;
        radarDot.m_Vertices = radarVerts;
        m_StaticCmds.Insert(radarDot);

        foreach (CanvasWidgetCommand cmd : m_StaticCmds)
            m_AllCmds.Insert(cmd);

        m_Canvas.SetDrawCommands(m_AllCmds);
    }

    // ---- update PPI with new scan data ----
    protected void UpdatePPI(
        array<ref RDF_RadarTarget> targets,
        vector origin,
        vector forward)
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

        m_Canvas.SetDrawCommands(m_AllCmds);
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
            string s = "Det " + detected.ToString() + "/" + total.ToString()
                + "   Trk " + confirmedTracks.ToString()
                + "   WLR " + wlrFixes.ToString();
            if (best)
            {
                s = s + "   " + TypeTag(best.m_Type) + " "
                    + RangeLabel(best.m_Distance) + " " + F0(best.m_SnrDb) + "dB";
            }
            m_wStats.SetText(s);
        }
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

    // ---- widget helpers ----
    protected Widget MakeFrame(WorkspaceWidget ws, int x, int y, int w, int h, int z, int color)
    {
        Widget fw = ws.CreateWidgetInWorkspace(WidgetType.FrameWidgetTypeID, x, y, w, h, 0, null, z);
        if (fw)
        {
            fw.SetColorInt(color);
            fw.SetVisible(true);
            m_AllWidgets.Insert(fw);
        }
        return fw;
    }

    protected Widget MakeText(WorkspaceWidget ws, int x, int y, int w, int h, int z, int color)
    {
        Widget tw = ws.CreateWidgetInWorkspace(WidgetType.TextWidgetTypeID, x, y, w, h, 0, null, z);
        if (tw)
        {
            tw.SetColorInt(color);
            tw.SetVisible(true);
            m_AllWidgets.Insert(tw);
        }
        return tw;
    }

    protected Widget MakeCompassLabel(WorkspaceWidget ws, int x, int y, string label)
    {
        Widget w = MakeText(ws, x, y, 14, 14, 96, COL_COMPASS);
        if (w)
        {
            TextWidget tw = TextWidget.Cast(w);
            tw.SetText(label);
            tw.SetExactFontSize(13);
        }
        return w;
    }

    protected Widget MakeDataRow(WorkspaceWidget ws, int rowIdx, string defaultText)
    {
        int y = m_PxDataY + rowIdx * PX_ROW_H + PX_PAD_Y;
        Widget w = MakeText(ws, m_PxLeft + PX_PAD_X, y, PX_W - PX_PAD_X * 2, PX_ROW_H - 2, 95, COL_DATA);
        if (w)
        {
            TextWidget tw = TextWidget.Cast(w);
            tw.SetText(defaultText);
            tw.SetExactFontSize(11);
        }
        return w;
    }

    protected void DestroyWidgets()
    {
        if (!m_AllWidgets)
            return;
        foreach (Widget w : m_AllWidgets)
        {
            if (w)
                w.RemoveFromHierarchy();
        }
        m_AllWidgets = null;
        m_Canvas     = null;
        m_wMode      = null;
        m_wStats     = null;
        m_wLegend    = null;
        m_StaticCmds = null;
        m_AllCmds    = null;
    }
}
