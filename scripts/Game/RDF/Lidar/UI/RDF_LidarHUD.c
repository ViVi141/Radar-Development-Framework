// LiDAR HUD — layout-driven panel + CanvasWidget view-aligned PPI.
// Structure: UI/layouts/RDF/LidarPPI.layout (CreateWidgets); script binds Names and draws Canvas.
// Blue/cyan theme to distinguish from radar green.
// Phosphor afterglow + forward sweep line for Showcase animation.
//
// Screen layout (bottom-left):
//
//  +==========================================+
//  | [>] LiDAR              Point Cloud      |  header
//  +------------------------------------------+
//  |                   F (forward)             |
//  |    CanvasWidget PPI  210 x 210           |
//  |  L      (+) player          R            |
//  |                   B (back)               |
//  +------------------------------------------+
//  | Hits  123 / 512    Range  ...            |
//  | By density (g/cm³), alpha by distance      |
//  +==========================================+

class RDF_LidarHudPhosphorBlip
{
    float m_NormX;
    float m_NormZ;
    float m_Dist;
    float m_BirthS;
    float m_R;
    float m_G;
    float m_B;
    float m_BaseAlpha;
}

class RDF_LidarHUD : RDF_LidarScanCompleteHandler
{
    //! Registered layout resource (open once in Workbench if GUID not resolved).
    static const ResourceName LAYOUT_PPI = "{B8D42F031A5C7E92}UI/layouts/RDF/LidarPPI.layout";

    // Canvas (PPI-style display)
    static const int PX_RADAR_H = 210;
    static const int PX_RADAR_W = 210;

    // ---- PPI canvas internals (unit coords = pixel coords 1:1) ----
    static const float PPI_CX   = 105.0;
    static const float PPI_CY   = 105.0;
    static const float PPI_R    = 100.0;
    static const float PPI_RING = 50.0;

    // Real-world range corresponding to PPI_R pixels.
    float m_DisplayRange = 1000.0;   // default 1 km for LiDAR

    // ---- ARGB colours (ice CRT cyan) ----
    static const int COL_TITLE    = ARGB(220, 100, 210, 255);
    static const int COL_MODE     = ARGB(155,  80,  160, 200);
    static const int COL_DATA     = ARGB(205, 110, 215, 240);
    static const int COL_MUTED    = ARGB(100,  70,  130, 160);

    // PPI canvas colours
    static const int COL_PPI_BG      = ARGB(255,  2,   8,  14);
    static const int COL_PPI_RING    = ARGB( 95, 50,  170, 210);
    static const int COL_PPI_RING2   = ARGB( 45, 35,  110, 140);
    static const int COL_PPI_CROSS   = ARGB( 55, 40,  130, 165);
    static const int COL_PPI_AXIS    = ARGB( 50, 50,  160, 180);
    static const int COL_PPI_PLAYER  = ARGB(255, 80,  230, 255);
    static const int COL_PPI_SWEEP   = ARGB(220, 80,  230, 255);
    // Distance gradient: near=green, mid=yellow, far=red (encodes 3D depth in 2D view)
    static const int COL_NEAR = ARGB(255,  0, 255, 100);   // green
    static const int COL_MID  = ARGB(255, 255, 220, 0);   // yellow
    static const int COL_FAR  = ARGB(255, 255, 80,  80);  // red

    // Min update interval (seconds) for data text rows
    static const float UPDATE_INTERVAL = 0.5;
    // Max blips drawn / stored for phosphor afterglow
    static const int PPI_MAX_BLIPS = 512;
    static const float PHOSPHOR_LIFE_SEC = 2.2;

    // ---- singleton ----
    protected static ref RDF_LidarHUD s_Instance;

    // ---- throttle ----
    protected float m_LastUpdateTime = 0.0;

    // ---- layout-bound widget references ----
    protected Widget m_wRoot;
    protected CanvasWidget          m_Canvas;
    protected TextWidget            m_wMode;
    protected TextWidget            m_wHits;
    protected TextWidget            m_wLegend;
    protected TextWidget            m_wRingLabel;

    // ---- CanvasWidget draw commands ----
    protected ref array<ref CanvasWidgetCommand> m_StaticCmds;
    protected ref array<ref CanvasWidgetCommand> m_AllCmds;

    // ---- phosphor afterglow ----
    protected ref array<ref RDF_LidarHudPhosphorBlip> m_Phosphor;
    protected int m_PhosphorWrite;
    protected vector m_CamForward;
    protected vector m_CamRight;
    protected bool m_HaveCamBasis;

    // ---- color interpolation (distance -> depth cue) ----
    protected int InterpColor(int a, int b, float t)
    {
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        int aa;
        int ar;
        int ag;
        int ab;
        int ba;
        int br;
        int bg;
        int bb;
        Color.UnpackInt(a, aa, ar, ag, ab);
        Color.UnpackInt(b, ba, br, bg, bb);
        int ra = aa + (int)((ba - aa) * t);
        int rr = ar + (int)((br - ar) * t);
        int rg = ag + (int)((bg - ag) * t);
        int rb = ab + (int)((bb - ab) * t);
        return ARGB(ra, rr, rg, rb);
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

    // HUD point color by surface density (g/cm³), alpha by distance (same logic as RDF_LidarMaterialColorStrategy).
    protected int ColorByMaterial(RDF_LidarSample sample)
    {
        if (!sample || !sample.m_Hit)
            return ARGB(128, 128, 128, 128);

        float density = -1.0;
        if (sample.m_Surface)
        {
            BallisticInfo bi = sample.m_Surface.GetBallisticInfo();
            if (bi)
                density = bi.GetDensity();
        }

        float t = 0.5;
        float densityMax = 8.0;
        if (density >= 0.0 && densityMax > 0.0)
        {
            t = density / densityMax;
            if (t > 1.0)
                t = 1.0;
        }

        float r = Math.Lerp(0.2, 1.0, t);
        float g = Math.Lerp(0.5, 0.4, t);
        float b = Math.Lerp(1.0, 0.3, t);
        if (r > 1.0) r = 1.0;
        if (g > 1.0) g = 1.0;
        if (b > 1.0) b = 1.0;

        float range = Math.Max(0.1, m_DisplayRange);
        float distNorm = sample.m_Distance / range;
        if (distNorm > 1.0)
            distNorm = 1.0;
        if (distNorm < 0.0)
            distNorm = 0.0;
        float alpha = 1.0 - 0.7 * distNorm;
        if (alpha < 0.2)
            alpha = 0.2;
        if (alpha > 1.0)
            alpha = 1.0;

        int ar = (int)(alpha * 255.0);
        int rr = (int)(r * 255.0);
        int gg = (int)(g * 255.0);
        int bb = (int)(b * 255.0);
        if (ar > 255) ar = 255;
        if (rr > 255) rr = 255;
        if (gg > 255) gg = 255;
        if (bb > 255) bb = 255;
        return ARGB(ar, rr, gg, bb);
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

    // Returns true if the HUD panel is currently built and visible.
    static bool IsVisible()
    {
        RDF_LidarHUD inst = GetInstance();
        return inst && inst.m_wRoot != null;
    }

    // Push a sample array into the HUD directly — works independently of AutoRunner.
    // The HUD must be shown first via Show(); SetDisplayRange() should be set to match
    // the scanner's range so the PPI scale is correct.
    static void FeedSamples(array<ref RDF_LidarSample> samples)
    {
        if (!samples)
            return;
        RDF_LidarHUD inst = GetInstance();
        if (inst)
            inst.OnScanComplete(samples);
    }

    // Fade phosphor blips between scans (called by AutoRunner CallLater ~100 ms).
    static void TickAfterglow()
    {
        RDF_LidarHUD inst = GetInstance();
        if (inst)
            inst.RedrawPhosphor();
    }

    // Demo convenience: wire HUD as AutoRunner scan-complete handler.
    // Prefer FeedSamples() for non-demo consumers; equivalent to
    // RDF_LidarAutoRunner.SetScanCompleteHandler(RDF_LidarHUD.GetInstance()).
    static void AttachToAutoRunner()
    {
        RDF_LidarAutoRunner.SetScanCompleteHandler(GetInstance());
    }

    // Clear AutoRunner scan-complete handler (Demo convenience).
    static void DetachFromAutoRunner()
    {
        RDF_LidarAutoRunner.SetScanCompleteHandler(null);
    }

    // ---- scan callback ----
    override void OnScanComplete(array<ref RDF_LidarSample> samples)
    {
        if (!m_wRoot || !samples)
            return;

        // Only pull the range from AutoRunner when the demo is actually driving scans;
        // if FeedSamples() is used standalone, m_DisplayRange keeps the value set by
        // SetDisplayRange() so the PPI scale is controlled by the caller.
        if (RDF_LidarAutoRunner.IsRunning())
            m_DisplayRange = RDF_LidarAutoRunner.GetDemoScannerRange();

        float now = System.GetTickCount() * 0.001;
        UpdateCamBasis();
        IngestPhosphor(samples, now);
        RedrawPhosphor();

        if (now - m_LastUpdateTime >= UPDATE_INTERVAL)
        {
            m_LastUpdateTime = now;
            UpdateDataRows(samples);
            UpdateRingLabel();
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Instantiate LidarPPI.layout and bind named widgets.
    protected void BuildWidgets()
    {
        if (m_wRoot)
            return;

        WorkspaceWidget ws = GetGame().GetWorkspace();
        if (!ws)
        {
            Print("[RDF_LidarHUD] ERROR: GetWorkspace() returned null");
            return;
        }

        m_wRoot = ws.CreateWidgets(LAYOUT_PPI, null);
        if (!m_wRoot)
        {
            Print("[RDF_LidarHUD] ERROR: CreateWidgets failed for LidarPPI.layout — register UI/layouts/RDF in Workbench");
            return;
        }

        m_wRoot.SetVisible(true);

        if (!m_Phosphor)
            m_Phosphor = new array<ref RDF_LidarHudPhosphorBlip>();
        m_PhosphorWrite = 0;
        m_CamForward = Vector(0, 0, 1);
        m_CamRight = Vector(1, 0, 0);
        m_HaveCamBasis = false;

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
            Print("[RDF_LidarHUD] WARN: PpiCanvas not found in layout");
        }

        m_wMode = TextWidget.Cast(m_wRoot.FindAnyWidget("ModeText"));
        m_wHits = TextWidget.Cast(m_wRoot.FindAnyWidget("StatsText"));
        m_wLegend = TextWidget.Cast(m_wRoot.FindAnyWidget("LegendText"));
        m_wRingLabel = TextWidget.Cast(m_wRoot.FindAnyWidget("RingLabel"));

        if (m_wMode)
            m_wMode.SetColorInt(COL_MODE);
        if (m_wHits)
            m_wHits.SetColorInt(COL_DATA);
        if (m_wLegend)
            m_wLegend.SetColorInt(COL_MUTED);

        Widget wTitle = m_wRoot.FindAnyWidget("TitleText");
        if (wTitle)
            wTitle.SetColorInt(COL_TITLE);
        if (m_wMode)
            m_wMode.SetColorInt(COL_MODE);

        UpdateRingLabel();

        Print("[RDF_LidarHUD] HUD built from LidarPPI.layout — look BOTTOM-LEFT");
    }

    protected void UpdateRingLabel()
    {
        if (!m_wRingLabel)
            return;
        m_wRingLabel.SetText(F0(m_DisplayRange * 0.5 / 1000.0) + "km");
    }

    // ---- CRT face: black disc, range rings, view crosshair, origin pip ----
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
        crossNS.Insert(PPI_CY - PPI_R + 3.0);
        crossNS.Insert(PPI_CX);
        crossNS.Insert(PPI_CY + PPI_R - 3.0);
        LineDrawCommand axisNS = new LineDrawCommand();
        axisNS.m_iColor   = COL_PPI_CROSS;
        axisNS.m_fWidth   = 1.0;
        axisNS.m_Vertices = crossNS;
        m_StaticCmds.Insert(axisNS);

        array<float> crossEW = new array<float>();
        crossEW.Insert(PPI_CX - PPI_R + 3.0);
        crossEW.Insert(PPI_CY);
        crossEW.Insert(PPI_CX + PPI_R - 3.0);
        crossEW.Insert(PPI_CY);
        LineDrawCommand axisEW = new LineDrawCommand();
        axisEW.m_iColor   = COL_PPI_CROSS;
        axisEW.m_fWidth   = 1.0;
        axisEW.m_Vertices = crossEW;
        m_StaticCmds.Insert(axisEW);

        array<float> playerVerts = new array<float>();
        m_Canvas.TessellateCircle(center, 3.0, 12, playerVerts);
        PolygonDrawCommand playerDot = new PolygonDrawCommand();
        playerDot.m_iColor   = COL_PPI_PLAYER;
        playerDot.m_Vertices = playerVerts;
        m_StaticCmds.Insert(playerDot);

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

    protected void UpdateCamBasis()
    {
        m_CamRight = Vector(1, 0, 0);
        m_CamForward = Vector(0, 0, 1);
        m_HaveCamBasis = false;

        // Prefer subject body forward (north-up PPI like radar), fall back to camera.
        IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (subject)
        {
            vector worldMat[4];
            subject.GetWorldTransform(worldMat);
            float fx = worldMat[2][0];
            float fz = worldMat[2][2];
            float flen = Math.Sqrt(fx * fx + fz * fz);
            if (flen > 0.0001)
            {
                m_CamForward = Vector(fx / flen, 0, fz / flen);
                float rx = worldMat[0][0];
                float rz = worldMat[0][2];
                float rlen = Math.Sqrt(rx * rx + rz * rz);
                if (rlen > 0.0001)
                    m_CamRight = Vector(rx / rlen, 0, rz / rlen);
                else
                    m_CamRight = Vector(m_CamForward[2], 0, -m_CamForward[0]);
                m_HaveCamBasis = true;
                return;
            }
        }

        PlayerController controller = GetGame().GetPlayerController();
        if (!controller)
            return;
        PlayerCamera playerCam = controller.GetPlayerCamera();
        if (!playerCam)
            return;

        vector camMat[4];
        playerCam.GetWorldCameraTransform(camMat);
        float cfx = camMat[2][0];
        float cfz = camMat[2][2];
        float cflen = Math.Sqrt(cfx * cfx + cfz * cfz);
        if (cflen > 0.0001)
        {
            m_CamForward = Vector(cfx / cflen, 0, cfz / cflen);
            float crx = camMat[0][0];
            float crz = camMat[0][2];
            float crlen = Math.Sqrt(crx * crx + crz * crz);
            if (crlen > 0.0001)
                m_CamRight = Vector(crx / crlen, 0, crz / crlen);
            m_HaveCamBasis = true;
        }
    }

    protected void IngestPhosphor(array<ref RDF_LidarSample> samples, float nowS)
    {
        if (!samples || samples.Count() == 0)
            return;
        if (!m_Phosphor)
            m_Phosphor = new array<ref RDF_LidarHudPhosphorBlip>();

        vector origin = samples.Get(0).m_Start;
        if (m_DisplayRange <= 0.0)
            return;

        int hitCount = 0;
        foreach (RDF_LidarSample sCheck : samples)
        {
            if (sCheck && sCheck.m_Hit)
                hitCount = hitCount + 1;
        }

        int step = 1;
        if (hitCount > PPI_MAX_BLIPS)
            step = hitCount / PPI_MAX_BLIPS;
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
            float projRight = delta[0] * m_CamRight[0] + delta[2] * m_CamRight[2];
            float projForward = delta[0] * m_CamForward[0] + delta[2] * m_CamForward[2];

            float normX = projRight / m_DisplayRange;
            float normZ = projForward / m_DisplayRange;
            float d2 = normX * normX + normZ * normZ;
            if (d2 > 1.0)
            {
                float d = Math.Sqrt(d2);
                normX = normX / d;
                normZ = normZ / d;
            }

            int col = ColorByMaterial(s);
            int ca;
            int cr;
            int cg;
            int cb;
            Color.UnpackInt(col, ca, cr, cg, cb);
            float baseA = ca / 255.0;
            float r = cr / 255.0;
            float g = cg / 255.0;
            float b = cb / 255.0;

            RDF_LidarHudPhosphorBlip blip;
            if (m_Phosphor.Count() < PPI_MAX_BLIPS)
            {
                blip = new RDF_LidarHudPhosphorBlip();
                m_Phosphor.Insert(blip);
            }
            else
            {
                if (m_PhosphorWrite >= m_Phosphor.Count())
                    m_PhosphorWrite = 0;
                blip = m_Phosphor.Get(m_PhosphorWrite);
                if (!blip)
                {
                    blip = new RDF_LidarHudPhosphorBlip();
                    m_Phosphor.Set(m_PhosphorWrite, blip);
                }
                m_PhosphorWrite = m_PhosphorWrite + 1;
            }

            blip.m_NormX = normX;
            blip.m_NormZ = normZ;
            blip.m_Dist = s.m_Distance;
            blip.m_BirthS = nowS;
            blip.m_R = r;
            blip.m_G = g;
            blip.m_B = b;
            blip.m_BaseAlpha = baseA;
        }
    }

    protected void RedrawPhosphor()
    {
        if (!m_Canvas || !m_StaticCmds)
            return;

        if (m_AllCmds)
            m_AllCmds.Clear();
        else
            m_AllCmds = new array<ref CanvasWidgetCommand>();
        foreach (CanvasWidgetCommand cmd : m_StaticCmds)
            m_AllCmds.Insert(cmd);

        float nowS = System.GetTickCount() * 0.001;

        if (m_Phosphor)
        {
            for (int i = 0; i < m_Phosphor.Count(); i++)
            {
                RDF_LidarHudPhosphorBlip blip = m_Phosphor.Get(i);
                if (!blip)
                    continue;
                float age = nowS - blip.m_BirthS;
                if (age < 0.0)
                    age = 0.0;
                if (age > PHOSPHOR_LIFE_SEC)
                    continue;

                float lifeT = 1.0 - (age / PHOSPHOR_LIFE_SEC);
                float alpha = blip.m_BaseAlpha * (0.2 + 0.8 * lifeT);
                if (alpha < 0.05)
                    continue;

                float bx = PPI_CX + blip.m_NormX * PPI_R;
                float by = PPI_CY - blip.m_NormZ * PPI_R;

                float t = 0.0;
                if (m_DisplayRange > 0.0)
                    t = blip.m_Dist / m_DisplayRange;
                if (t > 1.0)
                    t = 1.0;
                float blipR = (2.5 - t * 1.0) * (0.55 + 0.45 * lifeT);
                if (blipR < 0.6)
                    blipR = 0.6;

                int ar = (int)(alpha * 255.0);
                int rr = (int)(blip.m_R * 255.0);
                int gg = (int)(blip.m_G * 255.0);
                int bb = (int)(blip.m_B * 255.0);
                if (ar > 255) ar = 255;
                if (rr > 255) rr = 255;
                if (gg > 255) gg = 255;
                if (bb > 255) bb = 255;
                int blipCol = ARGB(ar, rr, gg, bb);

                array<float> blipVerts = new array<float>();
                m_Canvas.TessellateCircle(Vector(bx, by, 0), blipR, 6, blipVerts);
                PolygonDrawCommand poly = new PolygonDrawCommand();
                poly.m_iColor = blipCol;
                poly.m_Vertices = blipVerts;
                m_AllCmds.Insert(poly);
            }
        }

        // Cyan sweep line: view-aligned PPI puts subject forward at screen-up.
        array<float> sweepVerts = new array<float>();
        sweepVerts.Insert(PPI_CX);
        sweepVerts.Insert(PPI_CY);
        sweepVerts.Insert(PPI_CX);
        sweepVerts.Insert(PPI_CY - PPI_R);
        LineDrawCommand sweep = new LineDrawCommand();
        sweep.m_iColor = COL_PPI_SWEEP;
        sweep.m_fWidth = 1.5;
        sweep.m_Vertices = sweepVerts;
        m_AllCmds.Insert(sweep);

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
            {
                hitsStr = "HITS " + totalHits.ToString() + "/" + totalRays.ToString()
                    + "  ·  " + F0(minRange) + "-" + F0(maxRange) + "m";
            }
            else
            {
                hitsStr = "HITS 0/" + totalRays.ToString() + "  ·  MAX " + F0(m_DisplayRange) + "m";
            }
            m_wHits.SetText(hitsStr);
        }
        if (m_wLegend)
            m_wLegend.SetText("");
    }

    // ---- destroy layout root ----
    protected void DestroyWidgets()
    {
        if (m_wRoot)
            m_wRoot.RemoveFromHierarchy();
        m_wRoot      = null;
        m_Canvas     = null;
        m_wMode      = null;
        m_wHits      = null;
        m_wLegend    = null;
        m_wRingLabel = null;
        m_StaticCmds = null;
        m_AllCmds    = null;
        if (m_Phosphor)
            m_Phosphor.Clear();
        m_PhosphorWrite = 0;
        m_HaveCamBasis = false;
    }
}
