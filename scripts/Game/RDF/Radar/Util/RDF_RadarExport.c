// Radar-specific CSV export utilities.
// Extends the LiDAR export concept with additional EM-wave columns.
// Usage:
//   string header = RDF_RadarExport.GetCSVHeader();
//   foreach (RDF_LidarSample s : samples)
//   {
//       RDF_RadarSample rs = RDF_RadarSample.Cast(s);
//       if (rs) lines += RDF_RadarExport.SampleToCSVRow(rs) + "\n";
//   }
class RDF_RadarExport
{
    void RDF_RadarExport() {}

    // Column header for radar CSV files.
    static string GetCSVHeader()
    {
        return "Index,Hit,Distance_m,"
            +  "HitPos_X,HitPos_Y,HitPos_Z,"
            +  "Dir_X,Dir_Y,Dir_Z,"
            +  "TransmitPower_W,ReceivedPower_W,"
            +  "SNR_dB,RCS_m2,RCS_dBsm,"
            +  "PathLoss_dB,AtmLoss_dB,RainAtten_dB,"
            +  "DopplerFreq_Hz,TargetVel_ms,"
            +  "IncidenceAngle_deg,ReflectionCoeff,"
            +  "TimeOfFlight_s,"
            +  "MaterialType,EntityClass";
    }

    // Serialise one RDF_RadarSample to a CSV data row.
    static string SampleToCSVRow(RDF_RadarSample sample)
    {
        if (!sample)
            return "";

        int hitInt = 0;
        if (sample.m_Hit)
            hitInt = 1;

        // Geometry columns.
        string row = string.Format("%d,%d,%.4f,%.4f,%.4f,%.4f,%.6f,%.6f,%.6f",
            sample.m_Index,
            hitInt,
            sample.m_Distance,
            sample.m_HitPos[0], sample.m_HitPos[1], sample.m_HitPos[2],
            sample.m_Dir[0],    sample.m_Dir[1],    sample.m_Dir[2]);

        // EM power columns.
        row += string.Format(",%.6e,%.6e",
            sample.m_TransmitPower,
            sample.m_ReceivedPower);

        // SNR & RCS.
        row += string.Format(",%.3f,%.6f,%.3f",
            sample.m_SignalToNoiseRatio,
            sample.m_RadarCrossSection,
            sample.GetRCSdBsm());

        // Propagation losses.
        row += string.Format(",%.3f,%.3f,%.3f",
            sample.m_PathLoss,
            sample.m_AtmosphericLoss,
            sample.m_RainAttenuation);

        // Doppler.
        row += string.Format(",%.3f,%.4f",
            sample.m_DopplerFrequency,
            sample.m_TargetVelocity);

        // Reflection geometry.
        row += string.Format(",%.3f,%.4f",
            sample.m_IncidenceAngle,
            sample.m_ReflectionCoefficient);

        // Timing.
        row += string.Format(",%.9f", sample.m_TimeOfFlight);

        // Material / entity class.
        string entityClass = "";

        row += "," + sample.m_MaterialType;
        row += "," + entityClass;

        return row;
    }

    // Convert an entire scan result to a complete CSV string (header + rows).
    static string ScanToCSV(array<ref RDF_LidarSample> samples)
    {
        string csv = GetCSVHeader() + "\n";

        if (!samples)
            return csv;

        foreach (RDF_LidarSample base : samples)
        {
            RDF_RadarSample rs = RDF_RadarSample.Cast(base);
            if (rs)
                csv += SampleToCSVRow(rs) + "\n";
        }

        return csv;
    }

    // Filter scan results to hits-only before converting to CSV.
    static string HitsToCSV(array<ref RDF_LidarSample> samples)
    {
        string csv = GetCSVHeader() + "\n";

        if (!samples)
            return csv;

        foreach (RDF_LidarSample base : samples)
        {
            RDF_RadarSample rs = RDF_RadarSample.Cast(base);
            if (rs && rs.m_Hit)
                csv += SampleToCSVRow(rs) + "\n";
        }

        return csv;
    }

    // Print a quick debug summary of a scan to the Reforger log.
    static void PrintScanSummary(array<ref RDF_LidarSample> samples)
    {
        if (!samples)
        {
            Print("[RadarExport] No samples.");
            return;
        }

        int   total  = samples.Count();
        int   hits   = 0;
        float maxSNR = -999.0;
        float maxRCS = 0.0;
        float maxVel = 0.0;

        foreach (RDF_LidarSample base : samples)
        {
            RDF_RadarSample rs = RDF_RadarSample.Cast(base);
            if (!rs)
                continue;

            if (rs.m_Hit)
            {
                hits++;
                if (rs.m_SignalToNoiseRatio > maxSNR)
                    maxSNR = rs.m_SignalToNoiseRatio;
                if (rs.m_RadarCrossSection > maxRCS)
                    maxRCS = rs.m_RadarCrossSection;
                float absVel = Math.AbsFloat(rs.m_TargetVelocity);
                if (absVel > maxVel)
                    maxVel = absVel;
            }
        }

        float hitPct = (float)hits / (float)Math.Max(total, 1) * 100.0;
        Print(string.Format("[RadarExport] Scan: %d rays, %d hits (%.1f%%)",
            total, hits, hitPct));

        if (hits > 0)
        {
            float rcsDBsm = 10.0 * Math.Log10(Math.Max(maxRCS, 0.000001));
            Print(string.Format("  Max SNR: %.1f dB   Max RCS: %.2f m^2 (%.1f dBsm)   Max |Vel|: %.1f m/s",
                maxSNR, maxRCS, rcsDBsm, maxVel));
        }
    }
}
