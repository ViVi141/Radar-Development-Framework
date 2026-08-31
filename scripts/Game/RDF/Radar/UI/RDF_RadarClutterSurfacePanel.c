// CRT-look PPI for clutter surface RTTexture (288 H × 256 V).
// Readable MFD: intensity wedges under chrome; sector edges + status text.
// Perf: LineDraw chrome + polar az-bin range runs (not full-screen 1×1 raster).
class RDF_RadarClutterSurfacePanel
{
    static const int PX_W = 288;
    static const int PX_H = 256;
    static const float CX = 144.0;
    static const float CY = 118.0; // leave bottom strip for status
    static const float RADIUS = 108.0;
    static const float DEG_TO_RAD = 0.0174532925199;
    static const int LUT_REFRESH_FRAMES = 8;
    static const int RING_COUNT = 4;
    static const int RING_SEGS = 48;
    static const float BLINK_HZ = 2.0;
    // Drop weak cells so a strong sector does not paint a solid white pie.
    static const float NOISE_FLOOR_T = 0.38;

    static const int LVL_OFF = 0;
    static const int LVL_DIM = 1;
    static const int LVL_MID = 2;
    static const int LVL_BRT = 3;
    static const int LVL_PEAK = 4;

    static const int COL_BLACK = ARGB(255, 0, 0, 0);
    static const int COL_DIM = ARGB(255, 70, 70, 70);
    static const int COL_MID = ARGB(255, 130, 130, 130);
    static const int COL_BRT = ARGB(255, 200, 200, 200);
    static const int COL_WHITE = ARGB(255, 255, 255, 255);
    static const int COL_CHROME = ARGB(255, 180, 180, 180);

    protected CanvasWidget m_Canvas;
    protected TextWidget m_Status;
    protected ref array<float> m_Powers;
    protected ref array<int> m_Levels;
    protected int m_NAz;
    protected int m_NRng;
    protected float m_MaxRangeM;
    protected float m_SectorHalfDeg;
    protected float m_ScanAzDeg;
    protected int m_LutFrame;
    protected float m_LutDbLo;
    protected float m_LutDbHi;
    protected float m_VrmFrac;
    protected float m_VrmDir;
    protected bool m_BlinkOn;
    protected int m_LastRevision;
    protected bool m_HaveRevision;

    protected ref array<ref CanvasWidgetCommand> m_Cmds;
    protected ref PolygonDrawCommand m_BgCmd;
    protected ref array<float> m_BgVerts;
    protected ref array<ref CanvasWidgetCommand> m_OverlayCmds;
    protected ref array<ref PolygonDrawCommand> m_RunCmdPool;
    protected ref array<ref array<float>> m_RunVertPool;
    protected ref LineDrawCommand m_VrmCmd;
    protected ref array<float> m_VrmVerts;
    protected ref LineDrawCommand m_SweepCmd;
    protected ref array<float> m_SweepVerts;
    protected ref LineDrawCommand m_SectorLCmd;
    protected ref LineDrawCommand m_SectorRCmd;
    protected ref array<float> m_SectorLVerts;
    protected ref array<float> m_SectorRVerts;

    void RDF_RadarClutterSurfacePanel()
    {
        m_Powers = new array<float>();
        m_Levels = new array<int>();
        m_NAz = 0;
        m_NRng = 0;
        m_MaxRangeM = 2000.0;
        m_SectorHalfDeg = 45.0;
        m_ScanAzDeg = 0.0;
        m_LutFrame = 0;
        m_LutDbLo = -80.0;
        m_LutDbHi = -20.0;
        m_VrmFrac = 0.55;
        m_VrmDir = 1.0;
        m_BlinkOn = false;
        m_LastRevision = -1;
        m_HaveRevision = false;
        m_Cmds = new array<ref CanvasWidgetCommand>();
        m_OverlayCmds = new array<ref CanvasWidgetCommand>();
        m_RunCmdPool = new array<ref PolygonDrawCommand>();
        m_RunVertPool = new array<ref array<float>>();
    }

    void BindCanvas(CanvasWidget canvas)
    {
        m_Canvas = canvas;
        if (!m_Canvas)
            return;
        m_Canvas.SetVisible(true);
        m_Canvas.SetSizeInUnits(Vector(PX_W, PX_H, 0));
        FrameSlot.SetSizeX(m_Canvas, PX_W);
        FrameSlot.SetSizeY(m_Canvas, PX_H);
        BuildBackground();
        BuildOverlayChrome();
    }

    void BindStatus(TextWidget status)
    {
        m_Status = status;
        if (m_Status)
        {
            m_Status.SetVisible(true);
            m_Status.SetText("CLTR IDLE");
        }
    }

    void DrawIdleFrame()
    {
        if (!m_Canvas)
            return;
        UpdateBlinkPhase();
        BeginFrame();
        AppendSectorEdges();
        AppendDynamicChrome();
        AppendOverlay();
        m_Canvas.SetDrawCommands(m_Cmds);
        if (m_Status)
            m_Status.SetText("CLTR IDLE");
    }

    void Update(RDF_RadarSensor sensor)
    {
        if (!m_Canvas || !sensor)
            return;
        if (!sensor.IsClutterSurfaceEnabled())
            return;

        int nAz;
        int nRng;
        float maxRangeM;
        float sectorHalfDeg;
        float scanAzDeg;
        if (!sensor.TryGetClutterSurfaceMeta(nAz, nRng, maxRangeM, sectorHalfDeg, scanAzDeg))
            return;
        if (nAz < 1 || nRng < 1)
            return;

        m_NAz = nAz;
        m_NRng = nRng;
        m_MaxRangeM = maxRangeM;
        m_SectorHalfDeg = sectorHalfDeg;
        m_ScanAzDeg = scanAzDeg;

        int revision = sensor.GetClutterSurfaceRevision();
        bool dataChanged = true;
        if (m_HaveRevision && revision == m_LastRevision)
            dataChanged = false;
        m_LastRevision = revision;
        m_HaveRevision = true;

        // Chrome (VRM / blink sweep / status) still ticks every frame; only
        // re-quantize + rebuild clutter wedges when the surface revision moves.
        if (dataChanged)
        {
            sensor.CopyClutterSurfacePowers(m_Powers);
            EnsureLevelBuffer();
            m_LutFrame = m_LutFrame + 1;
            if (m_LutFrame >= LUT_REFRESH_FRAMES)
            {
                m_LutFrame = 0;
                RecomputeLut();
            }
            QuantizePowers();
        }

        m_VrmFrac = m_VrmFrac + m_VrmDir * 0.0015;
        if (m_VrmFrac > 0.92)
        {
            m_VrmFrac = 0.92;
            m_VrmDir = -1.0;
        }
        if (m_VrmFrac < 0.18)
        {
            m_VrmFrac = 0.18;
            m_VrmDir = 1.0;
        }

        UpdateBlinkPhase();
        BeginFrame();

        int runs = 0;
        int ia;
        for (ia = 0; ia < m_NAz; ia++)
            runs = DrawAzRowRuns(ia, runs);

        while (m_RunCmdPool.Count() > runs)
            m_RunCmdPool.Remove(m_RunCmdPool.Count() - 1);
        while (m_RunVertPool.Count() > runs)
            m_RunVertPool.Remove(m_RunVertPool.Count() - 1);

        AppendSectorEdges();
        AppendDynamicChrome();
        AppendOverlay();
        m_Canvas.SetDrawCommands(m_Cmds);
        UpdateStatusText(sensor);
    }

    protected void EnsureLevelBuffer()
    {
        int need = m_NAz * m_NRng;
        while (m_Levels.Count() < need)
            m_Levels.Insert(LVL_OFF);
        while (m_Levels.Count() > need)
            m_Levels.Remove(m_Levels.Count() - 1);
    }

    protected void QuantizePowers()
    {
        float span = m_LutDbHi - m_LutDbLo;
        if (span < 1.0)
            span = 1.0;

        int n = m_NAz * m_NRng;
        int i;
        for (i = 0; i < n; i++)
        {
            float pr = 0.0;
            if (i < m_Powers.Count())
                pr = m_Powers.Get(i);
            if (pr <= 0.0)
            {
                m_Levels.Set(i, LVL_OFF);
                continue;
            }

            int ir = i % m_NRng;
            float rM = ((ir + 0.5) / m_NRng) * m_MaxRangeM;
            if (rM < 50.0)
                rM = 50.0;
            float work = pr * rM * rM * rM * rM;
            float db = -300.0;
            if (work > 1.0e-30)
                db = 10.0 * Math.Log10(work);
            float t = (db - m_LutDbLo) / span;
            if (t < 0.0)
                t = 0.0;
            if (t > 1.0)
                t = 1.0;

            // Noise floor + stepped phosphor so structure stays visible.
            int lvl = LVL_OFF;
            if (t < NOISE_FLOOR_T)
                lvl = LVL_OFF;
            else if (t < 0.52)
                lvl = LVL_DIM;
            else if (t < 0.68)
                lvl = LVL_MID;
            else if (t < 0.84)
                lvl = LVL_BRT;
            else
                lvl = LVL_PEAK;
            m_Levels.Set(i, lvl);
        }
    }

    protected int LevelColor(int lvl)
    {
        if (lvl == LVL_DIM)
            return COL_DIM;
        if (lvl == LVL_MID)
            return COL_MID;
        if (lvl == LVL_BRT)
            return COL_BRT;
        if (lvl == LVL_PEAK)
        {
            if (m_BlinkOn)
                return COL_WHITE;
            return COL_BRT;
        }
        return COL_BLACK;
    }

    protected int DrawAzRowRuns(int ia, int poolStart)
    {
        if (m_NRng < 1 || m_NAz < 1)
            return poolStart;

        float half = m_SectorHalfDeg * DEG_TO_RAD;
        if (half < 0.01)
            half = 45.0 * DEG_TO_RAD;

        float az0 = -half + (ia / (m_NAz * 1.0)) * (2.0 * half);
        float az1 = -half + ((ia + 1) / (m_NAz * 1.0)) * (2.0 * half);

        int pool = poolStart;
        int ir = 0;
        while (ir < m_NRng)
        {
            int lvl = m_Levels.Get(ia * m_NRng + ir);
            int ir1 = ir;
            while (ir1 + 1 < m_NRng)
            {
                int next = m_Levels.Get(ia * m_NRng + (ir1 + 1));
                if (next != lvl)
                    break;
                ir1 = ir1 + 1;
            }

            if (lvl != LVL_OFF)
            {
                int color = LevelColor(lvl);
                if (color != COL_BLACK)
                {
                    float r0 = (ir / (m_NRng * 1.0)) * RADIUS;
                    float r1 = ((ir1 + 1) / (m_NRng * 1.0)) * RADIUS;
                    if (r0 < 1.0)
                        r0 = 1.0;
                    pool = EmitWedge(az0, az1, r0, r1, color, pool);
                }
            }

            ir = ir1 + 1;
        }
        return pool;
    }

    protected int EmitWedge(float az0, float az1, float r0, float r1, int color, int pool)
    {
        float s0 = Math.Sin(az0);
        float c0 = Math.Cos(az0);
        float s1 = Math.Sin(az1);
        float c1 = Math.Cos(az1);

        float x00 = CX + r0 * s0;
        float y00 = CY - r0 * c0;
        float x10 = CX + r0 * s1;
        float y10 = CY - r0 * c1;
        float x11 = CX + r1 * s1;
        float y11 = CY - r1 * c1;
        float x01 = CX + r1 * s0;
        float y01 = CY - r1 * c0;

        array<float> verts;
        if (pool < m_RunVertPool.Count())
            verts = m_RunVertPool.Get(pool);
        else
        {
            verts = new array<float>();
            m_RunVertPool.Insert(verts);
        }
        verts.Clear();
        verts.Insert(x00);
        verts.Insert(y00);
        verts.Insert(x10);
        verts.Insert(y10);
        verts.Insert(x11);
        verts.Insert(y11);
        verts.Insert(x01);
        verts.Insert(y01);

        PolygonDrawCommand cmd;
        if (pool < m_RunCmdPool.Count())
            cmd = m_RunCmdPool.Get(pool);
        else
        {
            cmd = new PolygonDrawCommand();
            m_RunCmdPool.Insert(cmd);
        }
        cmd.m_iColor = color;
        cmd.m_Vertices = verts;
        m_Cmds.Insert(cmd);
        return pool + 1;
    }

    protected void BuildBackground()
    {
        m_BgVerts = new array<float>();
        m_BgVerts.Insert(0.0);
        m_BgVerts.Insert(0.0);
        m_BgVerts.Insert(PX_W);
        m_BgVerts.Insert(0.0);
        m_BgVerts.Insert(PX_W);
        m_BgVerts.Insert(PX_H);
        m_BgVerts.Insert(0.0);
        m_BgVerts.Insert(PX_H);
        m_BgCmd = new PolygonDrawCommand();
        m_BgCmd.m_iColor = COL_BLACK;
        m_BgCmd.m_Vertices = m_BgVerts;
    }

    // Rings / axes drawn AFTER clutter so the face stays readable.
    protected void BuildOverlayChrome()
    {
        m_OverlayCmds.Clear();
        if (!m_Canvas)
            return;

        vector center = Vector(CX, CY, 0);
        int ri;
        for (ri = 1; ri <= RING_COUNT; ri++)
        {
            float rr = RADIUS * (ri / (RING_COUNT * 1.0));
            AddOverlayRing(center, rr, COL_CHROME, RING_SEGS);
        }
        AddOverlayRing(center, RADIUS, COL_WHITE, RING_SEGS);

        array<float> crossNS = new array<float>();
        crossNS.Insert(CX);
        crossNS.Insert(CY - RADIUS + 2.0);
        crossNS.Insert(CX);
        crossNS.Insert(CY + RADIUS - 2.0);
        LineDrawCommand axisNS = new LineDrawCommand();
        axisNS.m_iColor = COL_CHROME;
        axisNS.m_fWidth = 1.0;
        axisNS.m_Vertices = crossNS;
        m_OverlayCmds.Insert(axisNS);

        array<float> crossEW = new array<float>();
        crossEW.Insert(CX - RADIUS + 2.0);
        crossEW.Insert(CY);
        crossEW.Insert(CX + RADIUS - 2.0);
        crossEW.Insert(CY);
        LineDrawCommand axisEW = new LineDrawCommand();
        axisEW.m_iColor = COL_CHROME;
        axisEW.m_fWidth = 1.0;
        axisEW.m_Vertices = crossEW;
        m_OverlayCmds.Insert(axisEW);

        array<float> dotVerts = new array<float>();
        m_Canvas.TessellateCircle(center, 2.0, 10, dotVerts);
        PolygonDrawCommand dot = new PolygonDrawCommand();
        dot.m_iColor = COL_WHITE;
        dot.m_Vertices = dotVerts;
        m_OverlayCmds.Insert(dot);
    }

    protected void AddOverlayRing(vector center, float radius, int color, int segments)
    {
        if (!m_Canvas || radius < 2.0)
            return;
        array<float> verts = new array<float>();
        m_Canvas.TessellateCircle(center, radius, segments, verts);
        LineDrawCommand ring = new LineDrawCommand();
        ring.m_iColor = color;
        ring.m_fWidth = 1.0;
        ring.m_bShouldEnclose = true;
        ring.m_Vertices = verts;
        m_OverlayCmds.Insert(ring);
    }

    protected void BeginFrame()
    {
        m_Cmds.Clear();
        if (!m_BgCmd)
            BuildBackground();
        m_Cmds.Insert(m_BgCmd);
        if (m_OverlayCmds.Count() < 1)
            BuildOverlayChrome();
    }

    protected void AppendOverlay()
    {
        int i;
        for (i = 0; i < m_OverlayCmds.Count(); i++)
            m_Cmds.Insert(m_OverlayCmds.Get(i));
    }

    protected void AppendSectorEdges()
    {
        float half = m_SectorHalfDeg * DEG_TO_RAD;
        if (half < 0.01)
            half = 45.0 * DEG_TO_RAD;

        float sL = Math.Sin(-half);
        float cL = Math.Cos(-half);
        float sR = Math.Sin(half);
        float cR = Math.Cos(half);

        if (!m_SectorLVerts)
            m_SectorLVerts = new array<float>();
        m_SectorLVerts.Clear();
        m_SectorLVerts.Insert(CX);
        m_SectorLVerts.Insert(CY);
        m_SectorLVerts.Insert(CX + RADIUS * sL);
        m_SectorLVerts.Insert(CY - RADIUS * cL);
        if (!m_SectorLCmd)
            m_SectorLCmd = new LineDrawCommand();
        m_SectorLCmd.m_iColor = COL_WHITE;
        m_SectorLCmd.m_fWidth = 1.0;
        m_SectorLCmd.m_Vertices = m_SectorLVerts;
        m_Cmds.Insert(m_SectorLCmd);

        if (!m_SectorRVerts)
            m_SectorRVerts = new array<float>();
        m_SectorRVerts.Clear();
        m_SectorRVerts.Insert(CX);
        m_SectorRVerts.Insert(CY);
        m_SectorRVerts.Insert(CX + RADIUS * sR);
        m_SectorRVerts.Insert(CY - RADIUS * cR);
        if (!m_SectorRCmd)
            m_SectorRCmd = new LineDrawCommand();
        m_SectorRCmd.m_iColor = COL_WHITE;
        m_SectorRCmd.m_fWidth = 1.0;
        m_SectorRCmd.m_Vertices = m_SectorRVerts;
        m_Cmds.Insert(m_SectorRCmd);
    }

    protected void AppendDynamicChrome()
    {
        if (!m_Canvas)
            return;

        vector center = Vector(CX, CY, 0);
        float rr = RADIUS * m_VrmFrac;
        if (!m_VrmVerts)
            m_VrmVerts = new array<float>();
        m_VrmVerts.Clear();
        m_Canvas.TessellateCircle(center, rr, RING_SEGS, m_VrmVerts);
        if (!m_VrmCmd)
            m_VrmCmd = new LineDrawCommand();
        m_VrmCmd.m_iColor = COL_WHITE;
        m_VrmCmd.m_fWidth = 1.0;
        m_VrmCmd.m_bShouldEnclose = true;
        m_VrmCmd.m_Vertices = m_VrmVerts;
        m_Cmds.Insert(m_VrmCmd);

        if (m_BlinkOn)
        {
            if (!m_SweepVerts)
                m_SweepVerts = new array<float>();
            m_SweepVerts.Clear();
            m_SweepVerts.Insert(CX);
            m_SweepVerts.Insert(CY);
            m_SweepVerts.Insert(CX);
            m_SweepVerts.Insert(CY - RADIUS);
            if (!m_SweepCmd)
                m_SweepCmd = new LineDrawCommand();
            m_SweepCmd.m_iColor = COL_WHITE;
            m_SweepCmd.m_fWidth = 1.5;
            m_SweepCmd.m_Vertices = m_SweepVerts;
            m_Cmds.Insert(m_SweepCmd);
        }
    }

    protected void UpdateStatusText(RDF_RadarSensor sensor)
    {
        if (!m_Status)
            return;
        int rM = m_MaxRangeM;
        int halfDeg = m_SectorHalfDeg;
        int ringM = m_MaxRangeM / RING_COUNT;
        int azDeg = m_ScanAzDeg;
        int rev = 0;
        int rays = 0;
        int samp = 0;
        int cur = 0;
        if (sensor)
        {
            rev = sensor.GetClutterSurfaceRevision();
            sensor.TryGetClutterSurfaceFill(rays, samp, cur);
        }
        string s = "CLTR  R=" + rM.ToString() + "m  ±" + halfDeg.ToString()
            + "  ring=" + ringM.ToString() + "m  az=" + azDeg.ToString()
            + "  r" + rev.ToString() + "  fill=" + rays.ToString();
        m_Status.SetText(s);
    }

    protected void UpdateBlinkPhase()
    {
        float tMs = 0.0;
        BaseWorld world = GetGame().GetWorld();
        if (world)
            tMs = world.GetWorldTime();
        float tSec = tMs * 0.001;
        float phase = Math.Mod(tSec * BLINK_HZ, 1.0);
        if (phase < 0.5)
            m_BlinkOn = true;
        else
            m_BlinkOn = false;
    }

    protected void RecomputeLut()
    {
        int n = m_Powers.Count();
        if (n < 1 || m_NRng < 1)
            return;

        float sum = 0.0;
        int count = 0;
        float pMin = 1.0e30;
        float pMax = -1.0e30;
        int i;
        for (i = 0; i < n; i++)
        {
            float pr = m_Powers.Get(i);
            if (pr <= 0.0)
                continue;
            int ir = i % m_NRng;
            float r = ((ir + 0.5) / m_NRng) * m_MaxRangeM;
            if (r < 50.0)
                r = 50.0;
            float work = pr * r * r * r * r;
            float db = -300.0;
            if (work > 1.0e-30)
                db = 10.0 * Math.Log10(work);
            if (db < pMin)
                pMin = db;
            if (db > pMax)
                pMax = db;
            sum = sum + db;
            count = count + 1;
        }
        if (count < 1)
        {
            m_LutDbLo = -80.0;
            m_LutDbHi = -20.0;
            return;
        }
        float mean = sum / count;
        // Stretch around mean so relative hotspots read; avoid painting all white.
        m_LutDbLo = mean - 12.0;
        m_LutDbHi = mean + 18.0;
        if (pMax > m_LutDbHi)
            m_LutDbHi = pMax;
        if (pMin < m_LutDbLo)
            m_LutDbLo = pMin;
        if (m_LutDbHi < m_LutDbLo + 8.0)
            m_LutDbHi = m_LutDbLo + 8.0;
    }

    void Destroy()
    {
        m_Canvas = null;
        m_Status = null;
        m_Cmds = null;
        m_BgCmd = null;
        m_BgVerts = null;
        m_OverlayCmds = null;
        m_RunCmdPool = null;
        m_RunVertPool = null;
        m_VrmCmd = null;
        m_VrmVerts = null;
        m_SweepCmd = null;
        m_SweepVerts = null;
        m_SectorLCmd = null;
        m_SectorRCmd = null;
        m_SectorLVerts = null;
        m_SectorRVerts = null;
        m_Powers = null;
        m_Levels = null;
    }
}
