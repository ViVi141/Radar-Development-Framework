// Export LiDAR scan results to CSV-style text. Use Print to console or copy for external tools.
class RDF_LidarExport
{
    // CSV header line.
    static string GetCSVHeader()
    {
        return "index,hit,startX,startY,startZ,endX,endY,endZ,dirX,dirY,dirZ,hitPosX,hitPosY,hitPosZ,distance,colliderName";
    }

    // Format one sample as CSV row.
    static string SampleToCSVRow(RDF_LidarSample sample)
    {
        if (!sample) return "";
        string hitStr = "0";
        if (sample.m_Hit)
            hitStr = "1";
        vector s = sample.m_Start;
        vector e = sample.m_End;
        vector d = sample.m_Dir;
        vector h = sample.m_HitPos;
        string coll = sample.m_ColliderName;
        if (!coll)
            coll = "";
        string row = sample.m_Index.ToString() + "," + hitStr + ",";
        row = row + s[0].ToString() + "," + s[1].ToString() + "," + s[2].ToString() + ",";
        row = row + e[0].ToString() + "," + e[1].ToString() + "," + e[2].ToString() + ",";
        row = row + d[0].ToString() + "," + d[1].ToString() + "," + d[2].ToString() + ",";
        row = row + h[0].ToString() + "," + h[1].ToString() + "," + h[2].ToString() + ",";
        row = row + sample.m_Distance.ToString() + ",\"" + coll + "\"";
        return row;
    }

    // Print full CSV (header + all rows) to console. Copy from log for external files.
    static void PrintCSVToConsole(array<ref RDF_LidarSample> samples)
    {
        if (!samples) return;
        Print(RDF_LidarExport.GetCSVHeader());
        for (int i = 0; i < samples.Count(); i++)
        {
            RDF_LidarSample s = samples.Get(i);
            if (s)
                Print(RDF_LidarExport.SampleToCSVRow(s));
        }
    }

    // Export last scan from a visualizer to console (CSV).
    static void ExportLastScanToConsole(RDF_LidarVisualizer visualizer)
    {
        if (!visualizer) return;
        ref array<ref RDF_LidarSample> samples = visualizer.GetLastSamples();
        RDF_LidarExport.PrintCSVToConsole(samples);
    }
}
