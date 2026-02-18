// Radar HUD - header bar + CanvasWidget PPI display + data rows.
// All widgets created dynamically via WorkspaceWidget.CreateWidgetInWorkspace().
// No .layout file required.
//
// Screen layout (bottom-left, 1920x1080 reference px):
//
//  +==========================================+  Y=700
//  | [>] RADAR              X-Band Pulse     |  header H=25
//  +------------------------------------------+  Y=725
//  |                   N                      |
//  |    CanvasWidget PPI  210 x 210           |  Y=725
//  |  W      (+) player          E            |
//  |                   S                      |
//  +------------------------------------------+  Y=935
//  | SNR  65.2 / 82.0 dB   Hits 23 / 512     |
//  | RCS  +18.0 dBsm       Vel   3.2 m/s     |
//  | Range   30 m  to  1447 m                 |
//  +==========================================+  Y=1000

class RDF_RadarHUD : RDF_LidarScanCompleteHandler
{
    // ---- panel geometry (reference px) ----
    static const int PX_LEFT    = 20;
    static const int PX_TOP     = 700;
    static const int PX_W       = 215;
    static const int PX_HDR_H   = 25;

    // Radar canvas (PPI display)
    static const int PX_RADAR_H = 210;
    static const int PX_RADAR_W = 210;

    // Data rows below canvas
    static const int PX_DATA_Y  = PX_TOP + PX_HDR_H + PX_RADAR_H + 2;
    static const int PX_ROW_H   = 21;
    static const int PX_PAD_X   = 7;
    static const int PX_PAD_Y   = 4;

    // Total panel height (4 rows: SNR, RCS, Range, Legend)
    static const int PX_H       = PX_HDR_H + PX_RADAR_H + 4 * PX_ROW_H + 6;

    // ---- PPI canvas internals (unit coords = pixel coords 1:1) ----
    // Canvas is 210x210; center at (105,105), display radius 100 units.
    static const float PPI_CX   = 105.0;   // canvas center X
    static const float PPI_CY   = 105.0;   // canvas center Y
    static const float PPI_R    = 100.0;   // outer radius (units)
    static const float PPI_RING = 50.0;    // inner range-ring radius

    // Real-world range corresponding to PPI_R pixels.
    // Set this from outside to match the active scanner's range.
    float m_DisplayRange = 8000.0;   // default for heli radar 8 km

    // ---- ARGB colours ----
    static const int COL_PANEL    = ARGB(210,  0,  12,  7);
    static const int COL_HDR      = ARGB(255,  0,  30, 15);
    static const int COL_TITLE    = ARGB(255,  0, 255, 110);
    // Highlight colours for classified targets (vehicles, buildings, aircraft)
    static const int COL_HL_VEHICLE_HEAVY = ARGB(255, 255, 255, 100);   // bright yellow-green
    static const int COL_HL_VEHICLE_LIGHT = ARGB(255,  80, 255, 255);    // cyan
    static const int COL_HL_STATIC        = ARGB(255, 255, 200,  0);    // orange (buildings)
    static const int COL_HL_AIRCRAFT     = ARGB(255, 255,  60, 200);    // magenta
    static const int COL_HL_NAVAL        = ARGB(255, 100, 200, 255);    // light blue
    static const int COL_HL_INFANTRY     = ARGB(220,  0, 255, 100);     // green
    static const int COL_HL_SMALL_UAV    = ARGB(255, 200, 200, 255);    // light purple
    static const int COL_MODE     = ARGB(255,  0, 160,  70);
    static const int COL_DATA     = ARGB(255,  0, 210,  95);
    static const int COL_DATA_DIM = ARGB(200,  0, 170,  75);
    static const int COL_COMPASS  = ARGB(180,  0, 200,  80);

    // PPI canvas colours
    static const int COL_PPI_BG      = ARGB(240,  0,  18,  9);
    static const int COL_PPI_RING    = ARGB( 70,  0, 180,  70);
    static const int COL_PPI_AXIS    = ARGB( 50,  0, 160,  60);
    static const int COL_PPI_PLAYER  = ARGB(255,  0, 255, 130);
    static const int COL_PPI_ENTITY  = ARGB(255,  0, 255,  60);
    static const int COL_PPI_TERRAIN = ARGB(255,190, 190,   0);

    // Min update interval (seconds) to prevent flickering
    static const float UPDATE_INTERVAL = 0.5;

    // ---- singleton ----
    protected static ref RDF_RadarHUD s_Instance;

    // ---- throttle ----
    protected float m_LastUpdateTime = 0.0;

    // ---- static widget references ----
    protected ref array<ref Widget> m_AllWidgets;
    protected CanvasWidget          m_Canvas;
    protected TextWidget            m_wMode;
    protected TextWidget            m_wSNR;
    protected TextWidget            m_wRCS;
    protected TextWidget            m_wRange;

    // ---- CanvasWidget draw commands (kept alive as members) ----
    // Static commands (bg disc, rings, axes, player dot) never change.
    // Dynamic commands (blips) are rebuilt each scan.
    protected ref array<ref CanvasWidgetCommand> m_StaticCmds;
    protected ref array<ref CanvasWidgetCommand> m_AllCmds;

    // ---- float helpers ----
    static string F1(float v)
    {
        int neg = 1;
        if (v < 0.0) { neg = -1; v = -v; }
        int whole = (int)v;
        int frac  = (int)((v - (float)whole) * 10.0 + 0.5);
        if (frac >= 10) { whole = whole + 1; frac = 0; }
        string s = whole.ToString() + "." + frac.ToString();
        if (neg < 0) s = "-" + s;
        return s;
    }

    static string F0(float v)
    {
        if (v < 0.0) { int n = (int)(-v + 0.5); return "-" + n.ToString(); }
        int m = (int)(v + 0.5);
        return m.ToString();
    }

    // ---- singleton ----
    static RDF_RadarHUD GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarHUD();
        return s_Instance;
    }

    static void Show()        { GetInstance().BuildWidgets(); }
    static void Hide()        { RDF_RadarHUD inst = GetInstance(); if (inst) inst.DestroyWidgets(); }
    static void SetMode(string name) { RDF_RadarHUD inst = GetInstance(); if (inst && inst.m_wMode) inst.m_wMode.SetText(name); }
    static void SetDisplayRange(float rangeM) { RDF_RadarHUD inst = GetInstance(); if (inst) inst.m_DisplayRange = rangeM; }

    // ---- scan callback ----
    override void OnScanComplete(array<ref RDF_LidarSample> samples)
    {
        if (!m_AllWidgets || !samples)
            return;

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
            Print("[RDF_RadarHUD] ERROR: GetWorkspace() returned null");
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
        TextWidget.Cast(wTitle).SetText("[>] RADAR");
        TextWidget.Cast(wTitle).SetExactFontSize(17);

        // Mode label
        Widget wMode = MakeText(ws, PX_LEFT + 120, PX_TOP + PX_PAD_Y,
                                PX_W - 126, PX_HDR_H - PX_PAD_Y, 95, COL_MODE);
        m_wMode = TextWidget.Cast(wMode);
        m_wMode.SetText("Heli Radar");
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
            // Make unit space 1:1 with pixel space
            m_Canvas.SetSizeInUnits(Vector(PX_RADAR_W, PX_RADAR_H, 0));
            m_AllWidgets.Insert(wCanvas);
            BuildStaticDrawCommands();
        }
        else
        {
            Print("[RDF_RadarHUD] WARN: CanvasWidget creation failed");
        }

        // ---- compass labels at canvas edges ----
        int cx     = PX_LEFT + PX_RADAR_W / 2 - 4;
        int cy     = canvasTop + PX_RADAR_H / 2 - 8;
        int cRight = PX_LEFT + PX_RADAR_W + 2;

        MakeCompassLabel(ws, cx,     canvasTop - 14,     "N");
        MakeCompassLabel(ws, cx,     canvasTop + PX_RADAR_H + 1, "S");
        MakeCompassLabel(ws, PX_LEFT - 10, cy,           "W");
        MakeCompassLabel(ws, cRight, cy,                  "E");

        // Range ring annotation (50 % range)
        string ringLabel = F0(m_DisplayRange * 0.5 / 1000.0) + "km";
        Widget wRingLbl = MakeText(ws,
            PX_LEFT + (int)PPI_CX + 3,
            canvasTop + (int)(PPI_CY - PPI_RING) - 1,
            40, 14, 96, ARGB(130, 0, 180, 60));
        TextWidget.Cast(wRingLbl).SetText(ringLabel);
        TextWidget.Cast(wRingLbl).SetExactFontSize(12);

        // ---- data rows ----
        m_wSNR = TextWidget.Cast(MakeDataRow(ws, 0, "SNR   --   Hits --"));
        m_wRCS = TextWidget.Cast(MakeDataRow(ws, 1, "RCS   --   Vel  --"));
        m_wRange = TextWidget.Cast(MakeDataRow(ws, 2, "Range  --"));
        Widget wLegend = MakeDataRow(ws, 3, "Grey=terrain  Cyan=vehicle  Orange=static");
        TextWidget.Cast(wLegend).SetExactFontSize(11);

        Print("[RDF_RadarHUD] HUD built  widgets=" + m_AllWidgets.Count().ToString());
    }

    // ---- build PPI static draw commands ----
    protected void BuildStaticDrawCommands()
    {
        if (!m_Canvas)
            return;

        m_StaticCmds = new array<ref CanvasWidgetCommand>();
        m_AllCmds    = new array<ref CanvasWidgetCommand>();

        vector center = Vector(PPI_CX, PPI_CY, 0);

        // Background disc (filled circle)
        array<float> bgVerts = new array<float>();
        m_Canvas.TessellateCircle(center, PPI_R, 48, bgVerts);
        PolygonDrawCommand bgDisc = new PolygonDrawCommand();
        bgDisc.m_iColor   = COL_PPI_BG;
        bgDisc.m_Vertices = bgVerts;
        m_StaticCmds.Insert(bgDisc);

        // Outer range ring (100 %)
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
        ring50.m_fWidth        = 1.0;
        ring50.m_bShouldEnclose = true;
        ring50.m_Vertices      = ring50Verts;
        m_StaticCmds.Insert(ring50);

        // North-South axis
        LineDrawCommand axisNS = new LineDrawCommand();
        axisNS.m_iColor  = COL_PPI_AXIS;
        axisNS.m_fWidth  = 1.0;
        array<float> nsVerts = new array<float>();
        nsVerts.Insert(PPI_CX);  nsVerts.Insert(PPI_CY - PPI_R);
        nsVerts.Insert(PPI_CX);  nsVerts.Insert(PPI_CY + PPI_R);
        axisNS.m_Vertices = nsVerts;
        m_StaticCmds.Insert(axisNS);

        // East-West axis
        LineDrawCommand axisEW = new LineDrawCommand();
        axisEW.m_iColor  = COL_PPI_AXIS;
        axisEW.m_fWidth  = 1.0;
        array<float> ewVerts = new array<float>();
        ewVerts.Insert(PPI_CX - PPI_R);  ewVerts.Insert(PPI_CY);
        ewVerts.Insert(PPI_CX + PPI_R);  ewVerts.Insert(PPI_CY);
        axisEW.m_Vertices = ewVerts;
        m_StaticCmds.Insert(axisEW);

        // Player dot (small filled circle at center)
        array<float> playerVerts = new array<float>();
        m_Canvas.TessellateCircle(center, 4.0, 12, playerVerts);
        PolygonDrawCommand playerDot = new PolygonDrawCommand();
        playerDot.m_iColor   = COL_PPI_PLAYER;
        playerDot.m_Vertices = playerVerts;
        m_StaticCmds.Insert(playerDot);

        // Push static commands into combined array and apply
        foreach (CanvasWidgetCommand cmd : m_StaticCmds)
            m_AllCmds.Insert(cmd);

        m_Canvas.SetDrawCommands(m_AllCmds);
    }

    // ---- update PPI with new sample data ----
    protected void UpdatePPI(array<ref RDF_LidarSample> samples)
    {
        if (!m_Canvas || !m_StaticCmds)
            return;

        // Rebuild combined command list: static first, then blips.
        m_AllCmds = new array<ref CanvasWidgetCommand>();
        foreach (CanvasWidgetCommand cmd : m_StaticCmds)
            m_AllCmds.Insert(cmd);

        // Determine player origin from first hit sample.
        vector playerPos = Vector(0, 0, 0);
        bool gotOrigin = false;
        foreach (RDF_LidarSample base : samples)
        {
            if (base && base.m_Hit)
            {
                playerPos = base.m_Start;
                gotOrigin = true;
                break;
            }
        }
        if (!gotOrigin)
        {
            m_Canvas.SetDrawCommands(m_AllCmds);
            return;
        }

        int blipCount = 0;
        int maxBlips  = 128;

        foreach (RDF_LidarSample base : samples)
        {
            RDF_RadarSample s = RDF_RadarSample.Cast(base);
            if (!s || !s.m_Hit)
                continue;
            if (!s.IsDetectable(0.0))
                continue;

            ERadarTargetClass tc = RDF_TargetClassifier.ClassifyTarget(s);
            if (tc == ERadarTargetClass.UNKNOWN && s.m_Entity == null)
                continue;

            if (blipCount >= maxBlips)
                break;

            vector delta = s.m_HitPos - playerPos;
            float dx = delta[0];   // East  = right
            float dz = delta[2];   // North = up (row 0 = north edge)

            float normX = dx / m_DisplayRange;
            float normZ = dz / m_DisplayRange;

            // Clamp to display disc
            float d2 = normX * normX + normZ * normZ;
            if (d2 > 1.0)
            {
                float d = Math.Sqrt(d2);
                normX = normX / d;
                normZ = normZ / d;
            }

            float bx = PPI_CX + normX * PPI_R;
            float by = PPI_CY - normZ * PPI_R;   // north = up = lower Y

            float blipR;
            int   blipCol;
            if (tc == ERadarTargetClass.VEHICLE_HEAVY)
            {
                blipR   = 6.0;
                blipCol = COL_HL_VEHICLE_HEAVY;
            }
            else if (tc == ERadarTargetClass.VEHICLE_LIGHT)
            {
                blipR   = 5.0;
                blipCol = COL_HL_VEHICLE_LIGHT;
            }
            else if (tc == ERadarTargetClass.STATIC_OBJECT)
            {
                blipR   = 5.0;
                blipCol = COL_HL_STATIC;
            }
            else if (tc == ERadarTargetClass.AIRCRAFT)
            {
                blipR   = 6.0;
                blipCol = COL_HL_AIRCRAFT;
            }
            else if (tc == ERadarTargetClass.NAVAL)
            {
                blipR   = 7.0;
                blipCol = COL_HL_NAVAL;
            }
            else if (tc == ERadarTargetClass.INFANTRY)
            {
                blipR   = 3.0;
                blipCol = COL_HL_INFANTRY;
            }
            else if (tc == ERadarTargetClass.SMALL_UAV)
            {
                blipR   = 3.5;
                blipCol = COL_HL_SMALL_UAV;
            }
            else
            {
                // UNKNOWN or terrain: keep original size/colour by entity and RCS.
                if (s.m_Entity != null)
                {
                    float rcs = s.GetRCSdBsm();
                    if (rcs > 10.0)
                    {
                        blipR   = 5.0;
                        blipCol = ARGB(255,  0, 255,  60);
                    }
                    else
                    {
                        blipR   = 3.5;
                        blipCol = ARGB(220,  0, 200, 180);
                    }
                }
                else
                {
                    // Terrain / ground: dim grey-green (not yellow) so it does not dominate.
                    blipR   = 2.0;
                    blipCol = ARGB(120, 80, 100, 80);
                }
            }

            array<float> blipVerts = new array<float>();
            m_Canvas.TessellateCircle(Vector(bx, by, 0), blipR, 8, blipVerts);
            PolygonDrawCommand blip = new PolygonDrawCommand();
            blip.m_iColor   = blipCol;
            blip.m_Vertices = blipVerts;
            m_AllCmds.Insert(blip);
            blipCount = blipCount + 1;
        }

        m_Canvas.SetDrawCommands(m_AllCmds);
    }

    // ---- update data text rows ----
    protected void UpdateDataRows(array<ref RDF_LidarSample> samples)
    {
        int   totalRays  = samples.Count();
        int   totalHits  = 0;
        int   detected   = 0;
        float sumSNR     = 0.0;
        float peakSNR    = -9999.0;
        float sumRCS     = 0.0;
        float sumVel     = 0.0;
        float maxDoppler = 0.0;
        float minRange   = 9999999.0;
        float maxRange   = 0.0;

        foreach (RDF_LidarSample base : samples)
        {
            RDF_RadarSample s = RDF_RadarSample.Cast(base);
            if (!s || !s.m_Hit)
                continue;

            totalHits = totalHits + 1;
            sumRCS    = sumRCS + s.GetRCSdBsm();

            float tr = (s.m_HitPos - s.m_Start).Length();
            if (tr < minRange) minRange = tr;
            if (tr > maxRange) maxRange = tr;

            if (s.IsDetectable(0.0))
            {
                detected = detected + 1;
                sumSNR   = sumSNR + s.m_SignalToNoiseRatio;
                if (s.m_SignalToNoiseRatio > peakSNR) peakSNR = s.m_SignalToNoiseRatio;
                float vel = Math.AbsFloat(s.m_TargetVelocity);
                sumVel = sumVel + vel;
                float fd = Math.AbsFloat(s.m_DopplerFrequency);
                if (fd > maxDoppler) maxDoppler = fd;
            }
        }

        if (m_wSNR)
        {
            string snrStr;
            if (detected > 0)
                snrStr = "SNR " + F1(sumSNR / (float)detected) + " dB   Hits " + detected.ToString() + "/" + totalRays.ToString();
            else
                snrStr = "SNR --   Hits 0/" + totalRays.ToString();
            m_wSNR.SetText(snrStr);
        }

        if (m_wRCS)
        {
            string rcsStr;
            if (totalHits > 0)
                rcsStr = "RCS " + F1(sumRCS / (float)totalHits) + " dBsm";
            else
                rcsStr = "RCS --";
            if (detected > 0)
                rcsStr = rcsStr + "   Vel " + F1(sumVel / (float)detected) + " m/s";
            m_wRCS.SetText(rcsStr);
        }

        if (m_wRange)
        {
            string rStr;
            if (totalHits > 0)
                rStr = "Range " + F0(minRange) + " m  to  " + F0(maxRange) + " m";
            else
                rStr = "Range --";
            m_wRange.SetText(rStr);
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
        m_wSNR        = null;
        m_wRCS        = null;
        m_wRange      = null;
        m_StaticCmds  = null;
        m_AllCmds     = null;
    }
}
