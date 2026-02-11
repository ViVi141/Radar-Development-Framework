// Network adapter for server-authoritative scans outside the demo.

// Helper poller used by asynchronous scan requests. Implemented as a top-level class to avoid
// nested-class limitations in Enforce/Enfusion.
class RDF_LidarNetworkScannerPoller
{
	IEntity m_Subject;
	ref RDF_LidarScanner m_Scanner;
	ref array<ref RDF_LidarSample> m_OutSamples;
	RDF_LidarNetworkAPI m_Api;
	ref RDF_LidarScanCompleteHandler m_Handler;
	float m_Deadline;
	bool m_Finished;

	void RDF_LidarNetworkScannerPoller(IEntity subject, RDF_LidarScanner scanner, array<ref RDF_LidarSample> outSamples, RDF_LidarNetworkAPI api, float timeoutSeconds, RDF_LidarScanCompleteHandler handler)
	{
		m_Subject = subject;
		m_Scanner = scanner;
		m_OutSamples = outSamples;
		m_Api = api;
		m_Handler = handler;
		m_Deadline = 0.0;
		if (GetGame().GetWorld())
			m_Deadline = GetGame().GetWorld().GetWorldTime() + timeoutSeconds;
		m_Finished = false;
	}

	void Poll()
	{
		if (m_Finished) return;
		if (!m_Api || !m_Api.IsNetworkAvailable())
		{
			// fallback to local scan
			m_Scanner.Scan(m_Subject, m_OutSamples);
			if (m_Handler) m_Handler.OnScanComplete(m_OutSamples);
			m_Finished = true;
			return;
		}

		if (m_Api.HasSyncedSamples())
		{
			ref array<ref RDF_LidarSample> synced = m_Api.GetLastScanResults();
			if (synced)
			{
				m_OutSamples.Clear();
				m_OutSamples.Reserve(synced.Count());
				foreach (RDF_LidarSample s : synced)
					m_OutSamples.Insert(s);
				if (m_Handler) m_Handler.OnScanComplete(m_OutSamples);
			}
			m_Finished = true;
			return;
		}

		float now = 0.0;
		if (GetGame().GetWorld())
			now = GetGame().GetWorld().GetWorldTime();
		if (now >= m_Deadline)
		{
			// timeout: fallback to local scan
			m_Scanner.Scan(m_Subject, m_OutSamples);
			if (m_Handler) m_Handler.OnScanComplete(m_OutSamples);
			m_Finished = true;
			return;
		}
	}
};

class RDF_LidarNetworkScanner
{
	//------------------------------------------------------------------------------------------------
	//! Perform a scan using the network API when available. Returns true if output was updated.
	static bool Scan(IEntity subject, RDF_LidarScanner scanner, array<ref RDF_LidarSample> outSamples, RDF_LidarNetworkAPI api)
	{
		if (!subject || !scanner || !outSamples)
			return false;

		if (!api || !api.IsNetworkAvailable())
		{
			scanner.Scan(subject, outSamples);
			return true;
		}

		// Request server to perform scan (no subject parameter to avoid replication issues)
		api.RequestScan();
		if (!api.HasSyncedSamples())
			return false;

		array<ref RDF_LidarSample> synced = api.GetLastScanResults();
		if (!synced)
			return false;

		outSamples.Clear();
		outSamples.Reserve(synced.Count());
		foreach (RDF_LidarSample sample : synced)
		{
			outSamples.Insert(sample);
		}
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Convenience: use AutoRunner's network API.
	static bool ScanWithAutoRunnerAPI(IEntity subject, RDF_LidarScanner scanner, array<ref RDF_LidarSample> outSamples)
	{
		return Scan(subject, scanner, outSamples, RDF_LidarAutoRunner.GetNetworkAPI());
	}

	//------------------------------------------------------------------------------------------------
	// Asynchronous scan helper: request a server scan and poll for results until timeout (non-blocking).
	static ref array<ref RDF_LidarNetworkScannerPoller> s_Pollers;
	static bool s_PollersTickActive = false;



	static void StaticPollTick()
	{
		if (!s_Pollers || s_Pollers.Count() == 0)
			return;

		for (int i = s_Pollers.Count() - 1; i >= 0; i--)
		{
			RDF_LidarNetworkScannerPoller p = s_Pollers.Get(i);
			if (!p) { s_Pollers.Remove(i); continue; }
			p.Poll();
			if (p.m_Finished)
				s_Pollers.Remove(i);
		}
	}

	static void EnsurePollerTick()
	{
		if (!s_PollersTickActive)
		{
			s_PollersTickActive = true;
			if (!s_Pollers)
				s_Pollers = new array<ref RDF_LidarNetworkScannerPoller>();
			GetGame().GetCallqueue().CallLater(StaticPollTick, 0.1, true);
		}
	}

	// Non-blocking scan: requests server scan and calls handler when results available or timeout reached.
	static void ScanAsync(IEntity subject, RDF_LidarScanner scanner, array<ref RDF_LidarSample> outSamples, RDF_LidarNetworkAPI api, float timeoutSeconds, RDF_LidarScanCompleteHandler handler)
	{
		if (!subject || !scanner || !outSamples)
		{
			if (handler)
			{
				array<ref RDF_LidarSample> empty = new array<ref RDF_LidarSample>();
				handler.OnScanComplete(empty);
			}
			return;
		}

		if (!api || !api.IsNetworkAvailable())
		{
			scanner.Scan(subject, outSamples);
			if (handler) handler.OnScanComplete(outSamples);
			return;
		}

		// Request server scan once, then poll for results asynchronously
		api.RequestScan();
		RDF_LidarNetworkScannerPoller p = new RDF_LidarNetworkScannerPoller(subject, scanner, outSamples, api, timeoutSeconds, handler);
		s_Pollers.Insert(p);
		EnsurePollerTick();
	}

	// Convenience async variant using AutoRunner's network API
	static void ScanWithAutoRunnerAPIAsync(IEntity subject, RDF_LidarScanner scanner, array<ref RDF_LidarSample> outSamples, float timeoutSeconds, RDF_LidarScanCompleteHandler handler)
	{
		ScanAsync(subject, scanner, outSamples, RDF_LidarAutoRunner.GetNetworkAPI(), timeoutSeconds, handler);
	}
}
