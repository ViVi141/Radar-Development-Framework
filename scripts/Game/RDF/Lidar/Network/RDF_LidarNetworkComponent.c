[ComponentEditorProps(category: "GameScripted/RDF", description: "Network synchronization component for LiDAR framework")]
class RDF_LidarNetworkComponentClass : RDF_LidarNetworkAPIClass
{
}

class RDF_LidarNetworkComponent : RDF_LidarNetworkAPI
{
    protected RplComponent m_RplComponent;

	[RplProp(condition: RplCondition.NoOwner, onRplName: "OnDemoEnabledChanged")]
	protected bool m_DemoEnabled = false;

	// Replicated primitive config fields (avoid replicating complex objects)
	[RplProp(condition: RplCondition.NoOwner, onRplName: "OnDemoRayCountChanged")]
	protected int m_RayCount = 256;

	[RplProp(condition: RplCondition.NoOwner, onRplName: "OnDemoUpdateIntervalChanged")]
	protected float m_UpdateInterval = 5.0;

	[RplProp(condition: RplCondition.NoOwner, onRplName: "OnDemoRenderWorldChanged")]
	protected bool m_RenderWorld = true;

	[RplProp(condition: RplCondition.NoOwner, onRplName: "OnDemoDrawOriginAxisChanged")]
	protected bool m_DrawOriginAxis = false;

	[RplProp(condition: RplCondition.NoOwner, onRplName: "OnDemoVerboseChanged")]
	protected bool m_Verbose = false;

	// Local (non-replicated) storage for last scan results
	protected ref array<ref RDF_LidarSample> m_LastScanResults;

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		m_RplComponent = RplComponent.Cast(owner.FindComponent(RplComponent));
		if (!m_RplComponent)
		{
			Print("RDF_LidarNetworkComponent: No RplComponent found on owner.", LogLevel.ERROR);
			return;
		}
		if (!m_LastScanResults)
			m_LastScanResults = new array<ref RDF_LidarSample>();
	}

	//------------------------------------------------------------------------------------------------
	override bool IsNetworkAvailable()
	{
		return m_RplComponent != null;
	}

	//------------------------------------------------------------------------------------------------
	override void SetDemoEnabled(bool enabled)
	{
		if (!IsNetworkAvailable())
			return;

		if (m_RplComponent.IsProxy())
		{
			Rpc(RpcAsk_SetDemoEnabled, enabled);
			return;
		}

		m_DemoEnabled = enabled;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetDemoEnabled(bool enabled)
	{
		SetDemoEnabled(enabled);
	}

	//------------------------------------------------------------------------------------------------
	override void SetDemoConfig(RDF_LidarDemoConfig config)
	{
		if (!IsNetworkAvailable() || !config)
			return;

		// Client -> server request via RPC with primitives
		if (m_RplComponent.IsProxy())
		{
			// Extract primitive values to locals to avoid complex member expressions in RPC call
			int rc = config.m_RayCount;
			float ui = config.m_UpdateInterval;
			bool rw = config.m_RenderWorld;
			bool doa = config.m_DrawOriginAxis;
			bool vb = config.m_Verbose;
			Rpc(RpcAsk_SetDemoConfig, rc, ui, rw, doa, vb);
			return;
		}

		// Server applies primitives to RplProps
		m_RayCount = Math.Clamp(config.m_RayCount, 1, 4096);
		if (config.m_UpdateInterval > 0)
			m_UpdateInterval = Math.Max(0.01, config.m_UpdateInterval);
		m_RenderWorld = config.m_RenderWorld;
		m_DrawOriginAxis = config.m_DrawOriginAxis;
		m_Verbose = config.m_Verbose;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetDemoConfig(int rayCount, float updateInterval, bool renderWorld, bool drawOriginAxis, bool verbose)
	{
		// Server-side validation
		rayCount = Math.Clamp(rayCount, 1, 4096);
		if (updateInterval > 0 && updateInterval < 0.01)
			updateInterval = 0.01;

		m_RayCount = rayCount;
		if (updateInterval > 0)
			m_UpdateInterval = updateInterval;
		m_RenderWorld = renderWorld;
		m_DrawOriginAxis = drawOriginAxis;
		m_Verbose = verbose;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	override void RequestScan()
	{
		if (!IsNetworkAvailable())
			return;

		if (m_RplComponent.IsProxy())
		{
			Rpc(RpcAsk_PerformScan);
			return;
		}

		PerformScanInternal();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_PerformScan()
	{
		PerformScanInternal();
	}

	//------------------------------------------------------------------------------------------------
	protected void PerformScanInternal()
	{
		// Resolve subject from owner or default to local subject
		IEntity subject = GetOwner();
		if (!subject)
			return;

		array<ref RDF_LidarSample> results = new array<ref RDF_LidarSample>();
		RDF_LidarScanner scanner = new RDF_LidarScanner();

		// Apply replicated primitive config
		scanner.GetSettings().m_RayCount = Math.Clamp(m_RayCount, 1, 4096);
		scanner.GetSettings().m_UpdateInterval = Math.Max(0.01, m_UpdateInterval);

		scanner.Scan(subject, results);
		UpdateScanResults(results);

		// Serialize results to CSV and broadcast to clients
		string csv = RDF_LidarExport.SamplesToCSV(results);
		Rpc(RpcDo_ScanCompleteWithPayload, csv);
	}


	//------------------------------------------------------------------------------------------------
	protected void UpdateScanResults(array<ref RDF_LidarSample> results)
	{
		if (!m_LastScanResults)
			m_LastScanResults = new array<ref RDF_LidarSample>();

		m_LastScanResults.Clear();
		if (results)
		{
			m_LastScanResults.Reserve(results.Count());
			foreach (RDF_LidarSample sample : results)
			{
				m_LastScanResults.Insert(sample);
			}
		}
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]
	protected void RpcDo_ScanCompleteWithPayload(string csv)
	{
		// Deserialize CSV on clients and store locally
		if (!csv || csv == string.Empty)
			return;

		array<ref RDF_LidarSample> samples = RDF_LidarExport.ParseCSVToSamples(csv);
		if (!m_LastScanResults)
			m_LastScanResults = new array<ref RDF_LidarSample>();
		m_LastScanResults.Clear();
		foreach (RDF_LidarSample s : samples)
			m_LastScanResults.Insert(s);
		// Clients may react to scan completion via other hooks
	}

	//------------------------------------------------------------------------------------------------
	override bool HasSyncedSamples()
	{
		return m_LastScanResults && m_LastScanResults.Count() > 0;
	}

	//------------------------------------------------------------------------------------------------
	override array<ref RDF_LidarSample> GetLastScanResults()
	{
		return m_LastScanResults;
	}

	//------------------------------------------------------------------------------------------------
	void OnDemoEnabledChanged()
	{
		RDF_LidarAutoRunner.SetDemoEnabled(m_DemoEnabled, false);
	}

	// Apply replicated primitive config to AutoRunner (called by individual RplProp change handlers)
	protected void ApplyReplicatedConfigToRunner()
	{
		RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
		cfg.m_RayCount = m_RayCount;
		cfg.m_UpdateInterval = m_UpdateInterval;
		cfg.m_RenderWorld = m_RenderWorld;
		cfg.m_DrawOriginAxis = m_DrawOriginAxis;
		cfg.m_Verbose = m_Verbose;
		RDF_LidarAutoRunner.SetDemoConfig(cfg, false);
	}

	// Individual RplProp change callbacks
	void OnDemoRayCountChanged()
	{
		ApplyReplicatedConfigToRunner();
	}

	void OnDemoUpdateIntervalChanged()
	{
		ApplyReplicatedConfigToRunner();
	}

	void OnDemoRenderWorldChanged()
	{
		ApplyReplicatedConfigToRunner();
	}

	void OnDemoDrawOriginAxisChanged()
	{
		ApplyReplicatedConfigToRunner();
	}

	void OnDemoVerboseChanged()
	{
		ApplyReplicatedConfigToRunner();
	}

	// Legacy hook kept for compatibility
	void OnDemoConfigChanged()
	{
		ApplyReplicatedConfigToRunner();
	}

	//------------------------------------------------------------------------------------------------
	void OnScanResultsChanged()
	{
		// Sync hook; local visualizer uses HasSyncedSamples + GetLastScanResults.
	}
}
