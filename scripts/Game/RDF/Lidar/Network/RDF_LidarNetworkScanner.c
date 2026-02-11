// Network adapter for server-authoritative scans outside the demo.
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
}
