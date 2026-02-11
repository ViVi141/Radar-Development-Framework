[ComponentEditorProps(category: "GameScripted/RDF", description: "Network synchronization component for LiDAR framework")]
class RDF_LidarNetworkComponentClass : RDF_LidarNetworkAPIClass
{
}

// Helper buffer used to assemble chunked CSV payloads received over unreliable RPCs.
class RDF_ScanPayloadBuffer
{
	int m_Serial;
	int m_ExpectedParts;
	ref array<string> m_Parts;
	float m_CreateTime;

	void RDF_ScanPayloadBuffer(int serial)
	{
		m_Serial = serial;
		m_ExpectedParts = 0;
		m_Parts = new array<string>();
		m_CreateTime = 0.0;
		if (GetGame().GetWorld())
			m_CreateTime = GetGame().GetWorld().GetWorldTime();
	} 
}

class RDF_LidarNetworkComponent : RDF_LidarNetworkAPI
{
    protected RplComponent m_RplComponent;

	// Chunking and assembly helpers
	const int RDF_MAX_CSV_CHUNK = 1000; // bytes per unreliable RPC chunk
	protected int m_ScanSerial = 0;
	protected ref array<ref RDF_ScanPayloadBuffer> m_PayloadBuffers;

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
	// Timestamp (world time) when last scan results were updated (local or received)
	protected float m_LastScanTime = 0.0; 

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
		if (!m_PayloadBuffers)
			m_PayloadBuffers = new array<ref RDF_ScanPayloadBuffer>();
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
		m_RayCount = Math.Max(config.m_RayCount, 1);
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
		rayCount = Math.Max(rayCount, 1);
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
		scanner.GetSettings().m_RayCount = Math.Max(m_RayCount, 1);
		scanner.GetSettings().m_UpdateInterval = Math.Max(0.01, m_UpdateInterval);

		scanner.Scan(subject, results);
		UpdateScanResults(results);

        // Serialize results to CSV and broadcast to clients (chunk if large)
        string csv = RDF_LidarExport.SamplesToCSV(results);
        if (!csv || csv == string.Empty)
        {
            // nothing to send
            return;
        }

        // If payload is large, try compressing before chunking to reduce bandwidth
        if (csv.Length() > (RDF_MAX_CSV_CHUNK * 3))
            csv = RDF_LidarExport.SamplesToCSV(results, true, 3); // compressed, 3 decimal places

        if (csv.Length() <= RDF_MAX_CSV_CHUNK)
        {
            Rpc(RpcDo_ScanCompleteWithPayload, csv);
        }
        else
        {
            m_ScanSerial++;
            int partCount = Math.Ceil(csv.Length() / (float)RDF_MAX_CSV_CHUNK);
            for (int i = 0; i < partCount; i++)
            {
                int start = i * RDF_MAX_CSV_CHUNK;
                int len = Math.Min(RDF_MAX_CSV_CHUNK, csv.Length() - start);
                string chunk = csv.Substring(start, len);
                Rpc(RpcDo_ScanCompleteChunk, m_ScanSerial, i, i == (partCount - 1), chunk);
            }
        }
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
			// mark time on server when results are produced
			if (GetGame().GetWorld())
				m_LastScanTime = GetGame().GetWorld().GetWorldTime();
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
		ApplyLocalScanResults(samples);
		// Clients may react to scan completion via other hooks
	}

	// Chunked arrival handler: assemble parts and apply when complete.
	[RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]
	protected void RpcDo_ScanCompleteChunk(int serial, int seq, bool isLast, string csvPart)
	{
		if (!csvPart || csvPart == string.Empty)
			return;

		if (!m_PayloadBuffers)
			m_PayloadBuffers = new array<ref RDF_ScanPayloadBuffer>();

		RDF_ScanPayloadBuffer buf = null;
		foreach (RDF_ScanPayloadBuffer b : m_PayloadBuffers)
		{
			if (b.m_Serial == serial)
			{
				buf = b;
				break;
			}
		}

		if (!buf)
		{
			buf = new RDF_ScanPayloadBuffer(serial);
			m_PayloadBuffers.Insert(buf);
		}

		// store as "seq|data" to allow unordered arrival
		buf.m_Parts.Insert(seq.ToString() + "|" + csvPart);
		if (isLast)
			buf.m_ExpectedParts = seq + 1;

		// Cleanup old buffers (e.g., >10s) to avoid leaked incomplete payloads
		float now = 0.0;
		if (GetGame().GetWorld())
			now = GetGame().GetWorld().GetWorldTime();
		for (int bi = m_PayloadBuffers.Count() - 1; bi >= 0; bi--)
		{
			if (now - m_PayloadBuffers.Get(bi).m_CreateTime > 10.0)
				m_PayloadBuffers.Remove(bi);
		} 

		// If we know expected part count and have all parts, attempt assembly
		if (buf.m_ExpectedParts > 0 && buf.m_Parts.Count() >= buf.m_ExpectedParts)
		{
			string assembled = "";
			for (int i = 0; i < buf.m_ExpectedParts; i++)
			{
				bool found = false;
				for (int j = 0; j < buf.m_Parts.Count(); j++)
				{
					string entry = buf.m_Parts.Get(j);
					array<string> kv = new array<string>();
					entry.Split("|", kv, false);
					if (kv.Count() < 2) continue;
					int s = kv.Get(0).ToInt();
					if (s == i)
					{
						assembled += kv.Get(1);
						found = true;
						break;
					}
				}
				if (!found)
				{
					// missing part; wait for further arrivals
					return;
				}
			}

			array<ref RDF_LidarSample> samples = RDF_LidarExport.ParseCSVToSamples(assembled);
			ApplyLocalScanResults(samples);
			// cleanup buffer
			m_PayloadBuffers.RemoveItem(buf);
		}
	}

	// Apply scan results locally on clients without triggering replication
	protected void ApplyLocalScanResults(array<ref RDF_LidarSample> results)
	{
		if (!m_LastScanResults)
			m_LastScanResults = new array<ref RDF_LidarSample>();

		m_LastScanResults.Clear();
		if (results)
		{
			m_LastScanResults.Reserve(results.Count());
			foreach (RDF_LidarSample s : results)
				m_LastScanResults.Insert(s);
			// mark time when client receives results
			if (GetGame().GetWorld())
				m_LastScanTime = GetGame().GetWorld().GetWorldTime();
			if (m_Verbose)
				Print("RDF_LidarNetworkComponent: Applied local scan results (" + m_LastScanResults.Count() + " samples)", LogLevel.NORMAL);
		}
		// notify local hooks
		OnScanResultsChanged();
	} 
	//------------------------------------------------------------------------------------------------
	override array<ref RDF_LidarSample> GetLastScanResults()
	{
		return m_LastScanResults;
	}

	//------------------------------------------------------------------------------------------------
	override bool HasSyncedSamples()
	{
		if (!m_LastScanResults || m_LastScanResults.Count() == 0)
			return false;

		float now = 0.0;
		if (GetGame().GetWorld())
			now = GetGame().GetWorld().GetWorldTime();

		// Treat data older than 60s as stale
		if (now - m_LastScanTime > 60.0)
			return false;

		return true;
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
