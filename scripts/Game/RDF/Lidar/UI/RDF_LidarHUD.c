// LiDAR HUD - header bar + CanvasWidget 2D top-down point cloud display (view-aligned) + data rows.
// All widgets created dynamically via WorkspaceWidget.CreateWidgetInWorkspace().
// No .layout file required.
//
// Screen layout (bottom-left, 1920x1080 reference px):
//
//  +==========================================+  Y=700
//  | [>] LiDAR              Point Cloud      |  header H=25
//  +------------------------------------------+  Y=725
//  |                   F (forward)             |
//  |    CanvasWidget PPI  210 x 210           |  Y=725
//  |  L      (+) player          R            |
//  |                   B (back)               |
//  +------------------------------------------+  Y=935
//  | Hits  123 / 512    Range  30 m to 1.2 km |
//  | Green=near  Yellow=mid  Red=far  (distance) |
//  +==========================================+  Y=957

class RDF_LidarHUD : RDF_LidarScanCompleteHandler
{
    // ---- panel geometry (reference px) ----
    static const int PX_LEFT    = 20;
    static const int PX_TOP     = 700;
    static const int PX_W       = 215;
    static const int PX_HDR_H   = 25;

    // Canvas (PPI-style display)
    static const int PX_RADAR_H = 210;
    static const int PX_RADAR_W = 210;

    // Data rows below canvas
    static const int PX_DATA_Y  = PX_TOP + PX_HDR_H + PX_RADAR_H + 2;
    static const int PX_ROW_H   = 21;
    static const int PX_PAD_X   = 7;
    static const int PX_PAD_Y   = 4;

    // Total panel height (2 rows: Hits/Range, Legend)
    static const int PX_H       = PX_HDR_H + PX_RADAR_H + 2 * PX_ROW_H + 6;

    // ---- PPI canvas internals (unit coords = pixel coords 1:1) ----
    static const float PPI_CX   = 105.0;
    static const float PPI_CY   = 105.0;
    static const float PPI_R    = 100.0;
    static const float PPI_RING = 50.0;

    // Real-world range corresponding to PPI_R pixels.
    float m_DisplayRange = 1000.0;   // default 1 km for LiDAR

    // ---- ARGB colours (blue/cyan theme to distinguish from radar green) ----
    static const int COL_PANEL    = ARGB(210,  8,  12,  20);
    static const int COL_HDR      = ARGB(255, 15,  25,  35);
    static const int COL_TITLE    = ARGB(255, 100, 200, 255);
    static const int COL_MODE     = ARGB(255, 80,  180, 220);
    static const int COL_DATA     = ARGB(255, 100, 210, 230);
    static const int COL_COMPASS  = ARGB(180, 80,  200, 220);

    // PPI canvas colours
    static const int COL_PPI_BG      = ARGB(240, 10,  20,  25);
    static const int COL_PPI_RING    = ARGB( 70, 60,  180, 200);
    static const int COL_PPI_AXIS    = ARGB( 50, 50,  160, 180);
    static const int COL_PPI_PLAYER  = ARGB(255, 0,   255, 200);
    // Distance gradient: near=green, mid=yellow, far=red (encodes 3D depth in 2D view)
    static const int COL_NEAR = ARGB(255,  0, 255, 100);   // green
    static const int COL_MID  = ARGB(255, 255, 220, 0);   // yellow
    static const int COL_FAR  = ARGB(255, 255, 80,  80);  // red

    // Min update interval (seconds) to prevent flickering
    static const float UPDATE_INTERVAL = 0.5;

    // ---- singleton ----
    protected static ref RDF_LidarHUD s_Instance;

    // ---- throttle ----
    protected float m_LastUpdateTime = 0.0;

    // ---- static widget references ----
    protected ref array<ref Widget> m_AllWidgets;
    protected CanvasWidget          m_Canvas;
    protected TextWidget            m_wMode;
    protected TextWidget            m_wHits;
    protected TextWidget            m_wLegend;

    // ---- CanvasWidget draw commands ----
    protected ref array<ref CanvasWidgetCommand> m_StaticCmds;
    protected ref array<ref CanvasWidgetCommand> m_AllCmds;

    // ---- color interpolation (distance -> depth cue) ----
    protected int InterpColor(int a, int b, float t)
    {
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        int aa = (a >> 24) & 0xFF;
        int ar = (a >> 16) & 0xFF;
        int ag = (a >> 8) & 0xFF;
        int ab = a & 0xFF;
        int ba = (b >> 24) & 0xFF;
        int br = (b >> 16) & 0xFF;
        int bg = (b >> 8) & 0xFF;
        int bb = b & 0xFF;
        int ra = aa + (int)((ba - aa) * t);
        int rr = ar + (int)((br - ar) * t);
        int rg = ag + (int)((bg - ag) * t);
        int rb = ab + (int)((bb - ab) * t);
        return (ra << 24) | (rr << 16) | (rg << 8) | rb;
    }

    protected int ColorByDistance(float dist)
    {
        if (m_DisplayRange <= 0.0)
            return COL_MID;
        float t = dist / m_DisplayRange;
        if (t < 0.33)
            return InterpColor(COL_NEAR, COL_MID, t / 0.33);
        if (t < 0.66)
            return InterpColor(COL_MID, COL_FAR, (t - 0.33) / 0.33);
        return COL_FAR;
    }

    // ---- float helpers ----
    static string F0(float v)
    {
        if (v < 0.0) { int n = (int)(-v + 0.5); return "-" + n.ToString(); }
        int m = (int)(v + 0.5);
        return m.ToString();
    }

    // ---- singleton ----
    static RDF_LidarHUD GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_LidarHUD();
        return s_Instance;
    }

    static void Show()        { GetInstance().BuildWidgets(); }
    static void Hide()        { RDF_LidarHUD inst = GetInstance(); if (inst) inst.DestroyWidgets(); }
    static void SetMode(string name) { RDF_LidarHUD inst = GetInstance(); if (inst && inst.m_wMode) inst.m_wMode.SetText(name); }
    static void SetDisplayRange(float rangeM) { RDF_LidarHUD inst = GetInstance(); if (inst) inst.m_DisplayRange = rangeM; }

    // ---- scan callback ----
    override void OnScanComplete(array<ref RDF_LidarSample> samples)
    {
        if (!m_AllWidgets || !samples)
            return;

        m_DisplayRange = RDF_LidarAutoRunner.GetDemoScannerRange();

        float now = System.GetTickCount() * 0.001;
        if (now - m_LastUpdateTime < UPDATE_INTERVAL)
            return;
        m_LastUpdateTime = now;

        UpdateDataRows(samples);
        UpdatePPI(samples);
    }

    // ---- build all widgets once ----
    protected void BuildWidgets()
    {
        if (m_AllWidgets)
            return;

        WorkspaceWidget ws = GetGame().GetWorkspace();
        if (!ws)
        {
            Print("[RDF_LidarHUD] ERROR: GetWorkspace() returned null");
            return;
        }

        m_AllWidgets = new array<ref Widget>();

        // ---- outer panel background ----
        MakeFrame(ws, PX_LEFT, PX_TOP, PX_W, PX_H, 90, COL_PANEL);

        // ---- header bar ----
        MakeFrame(ws, PX_LEFT, PX_TOP, PX_W, PX_HDR_H, 91, COL_HDR);

        // Title
        Widget wTitle = MakeText(ws, PX_LEFT + PX_PAD_X, PX_TOP + PX_PAD_Y,
                                 110, PX_HDR_H - PX_PAD_Y, 95, COL_TITLE);
        TextWidget.Cast(wTitle).SetText("[>] LiDAR");
        TextWidget.Cast(wTitle).SetExactFontSize(17);

        // Mode label
        Widget wMode = MakeText(ws, PX_LEFT + 120, PX_TOP + PX_PAD_Y,
                                PX_W - 126, PX_HDR_H - PX_PAD_Y, 95, COL_MODE);
        m_wMode = TextWidget.Cast(wMode);
        m_wMode.SetText("Point Cloud");
        m_wMode.SetExactFontSize(15);

        // ---- CanvasWidget for PPI ----
        int canvasTop = PX_TOP + PX_HDR_H;
        Widget wCanvas = ws.CreateWidgetInWorkspace(
            WidgetType.CanvasWidgetTypeID,
            PX_LEFT, canvasTop, PX_RADAR_W, PX_RADAR_H,
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
            Print("[RDF_LidarHUD] WARN: CanvasWidget creation failed");
        }

        // ---- compass labels ----
        int cx     = PX_LEFT + PX_RADAR_W / 2 - 4;
        int cy     = canvasTop + PX_RADAR_H / 2 - 8;
        int cRight = PX_LEFT + PX_RADAR_W + 2;

        MakeCompassLabel(ws, cx,     canvasTop - 14,     "F");
        MakeCompassLabel(ws, cx,     canvasTop + PX_RADAR_H + 1, "B");
        MakeCompassLabel(ws, PX_LEFT - 10, cy,           "L");
        MakeCompassLabel(ws, cRight, cy,                  "R");

        // Range ring annotation
        string ringLabel = F0(m_DisplayRange * 0.5 / 1000.0) + "km";
        Widget wRingLbl = MakeText(ws,
            PX_LEFT + (int)PPI_CX + 3,
            canvasTop + (int)(PPI_CY - PPI_RING) - 1,
            40, 14, 96, ARGB(130, 60, 180, 200));
        TextWidget.Cast(wRingLbl).SetText(ringLabel);
        TextWidget.Cast(wRingLbl).SetExactFontSize(12);

        // ---- data rows ----
        m_wHits   = TextWidget.Cast(MakeDataRow(ws, 0, "Hits  --   Range --"));
        m_wLegend = TextWidget.Cast(MakeDataRow(ws, 1, "Green=near  Yellow=mid  Red=far"));
        TextWidget.Cast(m_wLegend).SetExactFontSize(11);

        Print("[RDF_LidarHUD] HUD built  widgets=" + m_AllWidgets.Count().ToString());
    }

    // ---- build PPI static draw commands ----
    protected void BuildStaticDrawCommands()
    {
        if (!m_Canvas)
            return;

        m_StaticCmds = new array<ref CanvasWidgetCommand>();
        m_AllCmds    = new array<ref CanvasWidgetCommand>();

        vector center = Vector(PPI_CX, PPI_CY, 0);

        // Background disc
        array<float> bgVerts = new array<float>();
        m_Canvas.TessellateCircle(center, PPI_R, 48, bgVerts);
        PolygonDrawCommand bgDisc = new PolygonDrawCommand();
        bgDisc.m_iColor   = COL_PPI_BG;
        bgDisc.m_Vertices = bgVerts;
        m_StaticCmds.Insert(bgDisc);

        // Outer range ring
        array<float> ring100Verts = new array<float>();
        m_Canvas.TessellateCircle(center, PPI_R - 1.0, 48, ring100Verts);
        LineDrawCommand ring100 = new LineDrawCommand();
        ring100.m_iColor       = COL_PPI_RING;
        ring100.m_fWidth       = 1.0;
        ring100.m_bShouldEnclose = true;
        ring100.m_Vertices     = ring100Verts;
        m_StaticCmds.Insert(ring100);

        // Inner range ring (50 %)
        array<float> ring50Verts = new array<float>();
        m_Canvas.TessellateCircle(center, PPI_RING, 48, ring50Verts);
        LineDrawCommand ring50 = new LineDrawCommand();
        ring50.m_iColor        = COL_PPI_RING;
        ring50.m_fWidth       = 1.0;
        ring50.m_bShouldEnclose = true;
        ring50.m_Vertices     = ring50Verts;
        m_StaticCmds.Insert(ring50);

        // Player dot at center
        array<float> playerVerts = new array<float>();
        m_Canvas.TessellateCircle(center, 4.0, 12, playerVerts);
        PolygonDrawCommand playerDot = new PolygonDrawCommand();
        playerDot.m_iColor   = COL_PPI_PLAYER;
        playerDot.m_Vertices = playerVerts;
        m_StaticCmds.Insert(playerDot);

        foreach (CanvasWidgetCommand cmd : m_StaticCmds)
            m_AllCmds.Insert(cmd);

        m_Canvas.SetDrawCommands(m_AllCmds);
    }

    // ---- update PPI with new sample data ----
    protected void UpdatePPI(array<ref RDF_LidarSample> samples)
    {
        if (!m_Canvas || !m_StaticCmds || !samples || samples.Count() == 0)
            return;

        m_AllCmds = new array<ref CanvasWidgetCommand>();
        foreach (CanvasWidgetCommand cmd : m_StaticCmds)
            m_AllCmds.Insert(cmd);

        vector origin = samples.Get(0).m_Start;

        vector camRight = Vector(1, 0, 0);
        vector camForward = Vector(0, 0, 1);
        PlayerController controller = GetGame().GetPlayerController();
        if (controller)
        {
            PlayerCamera playerCam = controller.GetPlayerCamera();
            if (playerCam)
            {
                vector camMat[4];
                playerCam.GetWorldCameraTransform(camMat);
                float fx = camMat[2][0];
                float fz = camMat[2][2];
                float flen = Math.Sqrt(fx * fx + fz * fz);
                if (flen > 0.0001)
                {
                    camForward = Vector(fx / flen, 0, fz / flen);
                    float rx = camMat[0][0];
                    float rz = camMat[0][2];
                    float rlen = Math.Sqrt(rx * rx + rz * rz);
                    if (rlen > 0.0001)
                        camRight = Vector(rx / rlen, 0, rz / rlen);
                }
            }
        }

        int hitCount = 0;
        foreach (RDF_LidarSample s : samples)
        {
            if (s && s.m_Hit)
                hitCount = hitCount + 1;
        }

        int maxBlips = 512;
        int step = 1;
        if (hitCount > maxBlips)
            step = hitCount / maxBlips;
        if (step < 1)
            step = 1;

        int hitIndex = 0;
        foreach (RDF_LidarSample s : samples)
        {
            if (!s || !s.m_Hit)
                continue;
            if (hitIndex % step != 0)
            {
                hitIndex = hitIndex + 1;
                continue;
            }
            hitIndex = hitIndex + 1;

            vector delta = s.m_HitPos - origin;
            float projRight = delta[0] * camRight[0] + delta[2] * camRight[2];
            float projForward = delta[0] * camForward[0] + delta[2] * camForward[2];
            float dist = s.m_Distance;

            if (m_DisplayRange <= 0.0)
                continue;

            float normX = projRight / m_DisplayRange;
            float normZ = projForward / m_DisplayRange;

            float d2 = normX * normX + normZ * normZ;
            if (d2 > 1.0)
            {
                float d = Math.Sqrt(d2);
                normX = normX / d;
                normZ = normZ / d;
            }

            float bx = PPI_CX + normX * PPI_R;
            float by = PPI_CY - normZ * PPI_R;

            float t = dist / m_DisplayRange;
            if (t > 1.0) t = 1.0;
            float blipR = 2.5 - t * 1.0;
            if (blipR < 0.8) blipR = 0.8;

            int blipCol = ColorByDistance(dist);

            array<float> blipVerts = new array<float>();
            m_Canvas.TessellateCircle(Vector(bx, by, 0), blipR, 6, blipVerts);
            PolygonDrawCommand blip = new PolygonDrawCommand();
            blip.m_iColor   = blipCol;
            blip.m_Vertices = blipVerts;
            m_AllCmds.Insert(blip);
        }

        m_Canvas.SetDrawCommands(m_AllCmds);
    }

    // ---- update data text rows ----
    protected void UpdateDataRows(array<ref RDF_LidarSample> samples)
    {
        int   totalRays  = samples.Count();
        int   totalHits  = 0;
        float minRange   = 9999999.0;
        float maxRange   = 0.0;

        foreach (RDF_LidarSample s : samples)
        {
            if (!s || !s.m_Hit)
                continue;

            totalHits = totalHits + 1;
            float tr = s.m_Distance;
            if (tr < minRange) minRange = tr;
            if (tr > maxRange) maxRange = tr;
        }

        if (m_wHits)
        {
            string hitsStr;
            if (totalHits > 0)
                hitsStr = "Hits " + totalHits.ToString() + "/" + totalRays.ToString() + "   Range " + F0(minRange) + " to " + F0(maxRange) + " m (max " + F0(m_DisplayRange) + " m)";
            else
                hitsStr = "Hits 0/" + totalRays.ToString() + "   Range -- (max " + F0(m_DisplayRange) + " m)";
            m_wHits.SetText(hitsStr);
        }
    }

    // ---- widget helpers ----
    protected Widget MakeFrame(WorkspaceWidget ws, int x, int y, int w, int h, int z, int color)
    {
        Widget fw = ws.CreateWidgetInWorkspace(WidgetType.FrameWidgetTypeID, x, y, w, h, 0, null, z);
        if (fw) { fw.SetColorInt(color); fw.SetVisible(true); m_AllWidgets.Insert(fw); }
        return fw;
    }

    protected Widget MakeText(WorkspaceWidget ws, int x, int y, int w, int h, int z, int color)
    {
        Widget tw = ws.CreateWidgetInWorkspace(WidgetType.TextWidgetTypeID, x, y, w, h, 0, null, z);
        if (tw) { tw.SetColorInt(color); tw.SetVisible(true); m_AllWidgets.Insert(tw); }
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
        int y = PX_DATA_Y + rowIdx * PX_ROW_H + PX_PAD_Y;
        Widget w = MakeText(ws, PX_LEFT + PX_PAD_X, y, PX_W - PX_PAD_X * 2, PX_ROW_H - 2, 95, COL_DATA);
        if (w)
        {
            TextWidget tw = TextWidget.Cast(w);
            tw.SetText(defaultText);
            tw.SetExactFontSize(15);
        }
        return w;
    }

    // ---- destroy all widgets ----
    protected void DestroyWidgets()
    {
        if (!m_AllWidgets) return;
        foreach (Widget w : m_AllWidgets) { if (w) w.RemoveFromHierarchy(); }
        m_AllWidgets  = null;
        m_Canvas      = null;
        m_wMode       = null;
        m_wHits       = null;
        m_wLegend     = null;
        m_StaticCmds  = null;
        m_AllCmds     = null;
    }
}
