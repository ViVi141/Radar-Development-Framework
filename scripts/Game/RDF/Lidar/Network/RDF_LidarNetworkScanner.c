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
        // A network round-trip timeout must elapse in REAL time. The world
        // clock (GetWorldTime) freezes on pause / time-scale 0 / pre-mission,
        // which would make the deadline unreachable and the poller poll
        // forever. GetTickCount is wall-clock milliseconds, always advancing.
        m_Deadline = System.GetTickCount() + (int)(timeoutSeconds * 1000.0);
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

        // Wall-clock ms (matches m_Deadline; world time can freeze under pause).
        float now = System.GetTickCount();
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
        {
            // No pollers: stop scheduling further ticks.
            s_PollersTickActive = false;
            return;
        }

        for (int i = s_Pollers.Count() - 1; i >= 0; i--)
        {
            RDF_LidarNetworkScannerPoller p = s_Pollers.Get(i);
            if (!p) { s_Pollers.Remove(i); continue; }
            // Drop pollers whose subject entity was deleted mid-flight so we
            // do not hold a stale IEntity ref (and do not call Scan with a
            // null subject). The scanner ref is owned by the poller; clearing
            // it here lets the GC reclaim the scanner once the poller is
            // dropped.
            if (!p.m_Subject)
            {
                p.m_Scanner = null;
                p.m_OutSamples = null;
                p.m_Handler = null;
                p.m_Finished = true;
                s_Pollers.Remove(i);
                continue;
            }
            p.Poll();
            if (p.m_Finished)
                s_Pollers.Remove(i);
        }

        // If there are still pollers remaining, schedule next tick; otherwise stop.
        if (s_Pollers && s_Pollers.Count() > 0)
            GetGame().GetCallqueue().CallLater(StaticPollTick, 100, false);
        else
            s_PollersTickActive = false;
    }

    // Release every pending poller and drop the static ref array. Call on
    // scene change / shutdown so pollers do not keep strong refs to deleted
    // entities / scanners across loads. Idempotent.
    static void Cleanup()
    {
        if (s_Pollers)
        {
            foreach (RDF_LidarNetworkScannerPoller p : s_Pollers)
            {
                if (p)
                {
                    p.m_Scanner = null;
                    p.m_OutSamples = null;
                    p.m_Handler = null;
                    p.m_Finished = true;
                }
            }
            s_Pollers.Clear();
            s_Pollers = null;
        }
        s_PollersTickActive = false;
    }

    static void EnsurePollerTick()
    {
        if (!s_PollersTickActive)
        {
            s_PollersTickActive = true;
            if (!s_Pollers)
                s_Pollers = new array<ref RDF_LidarNetworkScannerPoller>();
            // Schedule a single-shot tick; StaticPollTick will re-schedule while work exists.
            GetGame().GetCallqueue().CallLater(StaticPollTick, 100, false);
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

        // Cap poller count to prevent unbounded growth if ScanAsync is called repeatedly
        const int RDF_MAX_POLLERS = 8;
        if (s_Pollers && s_Pollers.Count() >= RDF_MAX_POLLERS)
        {
            scanner.Scan(subject, outSamples);
            if (handler)
                handler.OnScanComplete(outSamples);
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
