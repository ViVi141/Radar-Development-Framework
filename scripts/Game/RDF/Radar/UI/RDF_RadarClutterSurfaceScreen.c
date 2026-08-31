// Cockpit shell: RTTextureWidget → screen-mesh $rendertarget (official pattern).
// Enable only while needed (local player / debug); always DisableScreen on leave.
// Content is RDF_RadarClutterSurfacePanel — not RDF_RadarHUD corner PPI.
class RDF_RadarClutterSurfaceScreen
{
    static const ResourceName LAYOUT =
        "{B8D42F01A1C37E90}UI/layouts/RDF/RadarClutterSurfaceScreen.layout";
    static const int MAX_FPS = 15;
    // RT pixel size: horizontal 288 × vertical 256 (landscape).
    static const int RT_W = 288;
    static const int RT_H = 256;

    protected Widget m_wRoot;
    protected RTTextureWidget m_wRTTexture;
    protected CanvasWidget m_wCanvas;
    protected ref RDF_RadarClutterSurfacePanel m_Panel;
    protected IEntity m_ScreenMesh;
    protected bool m_PreviewWorkspace;
    protected RDF_RadarSensor m_Sensor;

    bool IsEnabled()
    {
        if (m_wRoot)
            return true;
        return false;
    }

    // Product path: bind UI RT to a screen mesh whose material uses $rendertarget.
    bool EnableScreen(IEntity screenMesh, RDF_RadarSensor sensor)
    {
        DisableScreen();
        if (!screenMesh || !sensor)
            return false;

        WorkspaceWidget ws = GetGame().GetWorkspace();
        if (!ws)
        {
            Print("[RDF CAS] EnableScreen: workspace null", LogLevel.ERROR);
            return false;
        }

        m_wRoot = ws.CreateWidgets(LAYOUT, null);
        if (!m_wRoot)
        {
            Print("[RDF CAS] EnableScreen: CreateWidgets failed — open layout in Workbench once", LogLevel.ERROR);
            return false;
        }

        m_wRTTexture = RTTextureWidget.Cast(m_wRoot.FindAnyWidget("RTTexture"));
        Widget wCanvas = m_wRoot.FindAnyWidget("IntensityCanvas");
        if (wCanvas)
            m_wCanvas = CanvasWidget.Cast(wCanvas);
        TextWidget wStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("StatusText"));

        if (!m_wRTTexture || !m_wCanvas)
        {
            Print("[RDF CAS] EnableScreen: RTTexture/IntensityCanvas missing", LogLevel.ERROR);
            DisableScreen();
            return false;
        }

        m_ScreenMesh = screenMesh;
        m_PreviewWorkspace = false;
        m_Sensor = sensor;

        // Keep in hierarchy so RT updates; park off-screen (BallisticTable keeps
        // widget alive). SetEnabled(false) matches SCR_DataDisplayGadget.
        m_wRoot.SetVisible(true);
        FrameSlot.SetPos(m_wRoot, -4096, -4096);
        FrameSlot.SetSizeX(m_wRoot, RT_W);
        FrameSlot.SetSizeY(m_wRoot, RT_H);
        FrameSlot.SetPos(m_wRTTexture, 0, 0);
        FrameSlot.SetSizeX(m_wRTTexture, RT_W);
        FrameSlot.SetSizeY(m_wRTTexture, RT_H);
        if (m_wCanvas)
        {
            FrameSlot.SetPos(m_wCanvas, 0, 0);
            FrameSlot.SetSizeX(m_wCanvas, RT_W);
            FrameSlot.SetSizeY(m_wCanvas, 226);
        }
        m_wRTTexture.SetRenderTarget(screenMesh);
        m_wRTTexture.SetMaxFPS(MAX_FPS);
        m_wRTTexture.SetEnabled(false);

        m_Panel = new RDF_RadarClutterSurfacePanel();
        m_Panel.BindCanvas(m_wCanvas);
        m_Panel.BindStatus(wStatus);
        m_Panel.DrawIdleFrame();
        Print(string.Format("[RDF CAS] EnableScreen OK — RT %1x%2 (H×V)", RT_W, RT_H), LogLevel.NORMAL);
        return true;
    }

    // Dev path: show the same layout as a fixed workspace panel (no mesh).
    bool EnablePreview(RDF_RadarSensor sensor)
    {
        DisableScreen();
        if (!sensor)
            return false;

        WorkspaceWidget ws = GetGame().GetWorkspace();
        if (!ws)
            return false;

        m_wRoot = ws.CreateWidgets(LAYOUT, null);
        if (!m_wRoot)
            return false;

        m_wRTTexture = RTTextureWidget.Cast(m_wRoot.FindAnyWidget("RTTexture"));
        Widget wCanvas = m_wRoot.FindAnyWidget("IntensityCanvas");
        if (wCanvas)
            m_wCanvas = CanvasWidget.Cast(wCanvas);
        TextWidget wStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("StatusText"));
        if (!m_wCanvas)
        {
            DisableScreen();
            return false;
        }

        m_ScreenMesh = null;
        m_PreviewWorkspace = true;
        m_Sensor = sensor;
        m_wRoot.SetVisible(true);
        FrameSlot.SetPos(m_wRoot, 16, 16);
        FrameSlot.SetSizeX(m_wRoot, RT_W);
        FrameSlot.SetSizeY(m_wRoot, RT_H);

        m_Panel = new RDF_RadarClutterSurfacePanel();
        m_Panel.BindCanvas(m_wCanvas);
        m_Panel.BindStatus(wStatus);
        m_Panel.DrawIdleFrame();
        return true;
    }

    void DisableScreen()
    {
        if (m_wRTTexture && m_ScreenMesh)
            m_wRTTexture.RemoveRenderTarget(m_ScreenMesh);
        if (m_Panel)
        {
            m_Panel.Destroy();
            m_Panel = null;
        }
        if (m_wRoot)
        {
            m_wRoot.RemoveFromHierarchy();
            m_wRoot = null;
        }
        m_wRTTexture = null;
        m_wCanvas = null;
        m_ScreenMesh = null;
        m_Sensor = null;
        m_PreviewWorkspace = false;
    }

    void Update()
    {
        if (!m_Panel || !m_Sensor)
            return;
        m_Panel.Update(m_Sensor);
    }
}
