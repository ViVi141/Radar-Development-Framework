// Radar-specific scan-complete handler.
// After each scan, prints hits / detected / SNR / RCS / Doppler / range stats.
//
// Enable via RDF_RadarAutoRunner.SetDemoVerbose(true) or supply directly:
//   RDF_RadarAutoRunner.SetScanCompleteHandler(new RDF_RadarDemoStatsHandler());
class RDF_RadarDemoStatsHandler : RDF_LidarScanCompleteHandler
{
    // When true, also classify all hits and print the target-type breakdown.
    bool m_PrintClassification = false;
    // When true, prints the ASCII top-down radar map after each scan.
    bool m_PrintRadarMap = true;

    protected ref RDF_RadarTextDisplay m_TextDisplay;

    // ---- float formatting helpers ----
    // Returns a float rounded to 1 decimal place as a string (e.g. "18.3").
    static string F1(float v)
    {
        int neg = 1;
        if (v < 0.0)
        {
            neg = -1;
            v   = -v;
        }
        int whole = (int)v;
        int frac  = (int)((v - (float)whole) * 10.0 + 0.5);
        if (frac >= 10)
        {
            whole = whole + 1;
            frac  = 0;
        }
        string s = whole.ToString() + "." + frac.ToString();
        if (neg < 0)
            s = "-" + s;
        return s;
    }

    // Returns a float rounded to the nearest integer as a string.
    static string F0(float v)
    {
        if (v < 0.0)
        {
            int n = (int)(-v + 0.5);
            return "-" + n.ToString();
        }
        int m = (int)(v + 0.5);
        return m.ToString();
    }

    override void OnScanComplete(array<ref RDF_LidarSample> samples)
    {
        if (!samples || samples.Count() == 0)
            return;

        int totalRays     = samples.Count();
        int totalHits     = 0;
        int detectedCount = 0;

        float sumSNR      = 0.0;
        float peakSNR     = -9999.0;
        float sumRCSdBsm  = 0.0;
        float sumVel      = 0.0;
        float maxDoppler  = 0.0;
        float minRange    = 9999999.0;
        float maxRange    = 0.0;

        foreach (RDF_LidarSample base : samples)
        {
            RDF_RadarSample s = RDF_RadarSample.Cast(base);
            if (!s || !s.m_Hit)
                continue;

            totalHits++;

            if (s.m_SignalToNoiseRatio > -900.0)
            {
                if (s.IsDetectable(0.0))
                {
                    detectedCount++;
                    sumSNR = sumSNR + s.m_SignalToNoiseRatio;

                    if (s.m_SignalToNoiseRatio > peakSNR)
                        peakSNR = s.m_SignalToNoiseRatio;

                    float vel = Math.AbsFloat(s.m_TargetVelocity);
                    sumVel = sumVel + vel;

                    float fd = Math.AbsFloat(s.m_DopplerFrequency);
                    if (fd > maxDoppler)
                        maxDoppler = fd;
                }

                float rcsdBsm = s.GetRCSdBsm();
                sumRCSdBsm = sumRCSdBsm + rcsdBsm;

                // Use true geometric distance rather than the mode-quantized m_Distance.
                float trueRange = (s.m_HitPos - s.m_Start).Length();
                if (trueRange < minRange)
                    minRange = trueRange;
                if (trueRange > maxRange)
                    maxRange = trueRange;
            }
        }

        // Line 1: counts
        string msg = "[RadarDemo] rays=" + totalRays.ToString()
            + "  hits=" + totalHits.ToString()
            + "  detected=" + detectedCount.ToString();
        Print(msg);

        // Line 2: SNR and velocity (only if there were detected targets)
        if (detectedCount > 0)
        {
            float avgSNR = sumSNR / (float)detectedCount;
            float avgVel = sumVel / (float)detectedCount;
            string snrLine = "           SNR avg=" + F1(avgSNR)
                + " dB  peak=" + F1(peakSNR)
                + " dB  |  vel avg=" + F1(avgVel)
                + " m/s  max_fd=" + F0(maxDoppler) + " Hz";
            Print(snrLine);
        }

        // Line 3: RCS and range (only if there were hits)
        if (totalHits > 0)
        {
            float avgRCS = sumRCSdBsm / (float)totalHits;
            string rcsLine = "           RCS avg=" + F1(avgRCS)
                + " dBsm  |  range min=" + F0(minRange)
                + " m  max=" + F0(maxRange) + " m";
            Print(rcsLine);
        }

        if (m_PrintClassification && totalHits > 0)
            RDF_TargetClassifier.PrintStats(samples);

        // ASCII radar map (only when there are detections, to keep the log readable).
        if (m_PrintRadarMap && totalHits > 0)
        {
            IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(false);
            if (subject)
            {
                if (!m_TextDisplay)
                    m_TextDisplay = new RDF_RadarTextDisplay();
                m_TextDisplay.PrintRadarMap(samples, subject);
            }
        }
    }
}
