// Binning + phosphor pseudo-color helpers for RDF_RadarHUD side charts.
// Data comes from plot measurements only (range / Doppler / SNR) — not entity truth.
class RDF_RadarHudCharts
{
    static const int ASCOPE_BINS = 128;
    static const int RD_RANGE_BINS = 64;
    static const int RD_DOP_BINS = 48;
    static const int WF_ROWS = 72;
    static const int WF_COLS = 64;

    static const float DOPPLER_HALF_HZ = 500.0;
    static const float SNR_DB_FLOOR = -5.0;
    static const float SNR_DB_CEIL = 40.0;

    //------------------------------------------------------------------------------------------------
    // Map 0..1 intensity to CRT phosphor ARGB (dark green -> lime -> yellow -> white).
    static int PseudoColor(float t)
    {
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;

        int r;
        int g;
        int b;
        if (t < 0.33)
        {
            float u = t / 0.33;
            r = 2;
            g = 10 + (int)(u * 90.0);
            b = 6;
        }
        else
        {
            if (t < 0.66)
            {
                float u = (t - 0.33) / 0.33;
                r = 20 + (int)(u * 200.0);
                g = 100 + (int)(u * 140.0);
                b = 20;
            }
            else
            {
                float u = (t - 0.66) / 0.34;
                r = 220 + (int)(u * 35.0);
                g = 240 + (int)(u * 15.0);
                b = 40 + (int)(u * 200.0);
            }
        }

        if (r > 255)
            r = 255;
        if (g > 255)
            g = 255;
        if (b > 255)
            b = 255;
        return ARGB(255, r, g, b);
    }

    //------------------------------------------------------------------------------------------------
    static float SnrDbToUnit(float snrDb)
    {
        float t = (snrDb - SNR_DB_FLOOR) / (SNR_DB_CEIL - SNR_DB_FLOOR);
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
        return t;
    }

    //------------------------------------------------------------------------------------------------
    static float PlotPowerLin(RDF_RadarTarget t)
    {
        if (!t)
            return 0.0;
        if (t.m_ProcessedPowerW > 0.0)
            return t.m_ProcessedPowerW;
        return RDF_RadarClutterModel.DbToLin(t.m_SnrDb);
    }

    //------------------------------------------------------------------------------------------------
    static void EnsureFloatArray(notnull array<float> arr, int count, float fill)
    {
        while (arr.Count() < count)
            arr.Insert(fill);
        while (arr.Count() > count)
            arr.Remove(arr.Count() - 1);
        for (int i = 0; i < count; i++)
            arr.Set(i, fill);
    }

    //------------------------------------------------------------------------------------------------
    static void AddNoiseFloor(notnull array<float> bins, float amp)
    {
        int n = bins.Count();
        for (int i = 0; i < n; i++)
        {
            float n0 = Math.RandomFloat(0.0, 1.0);
            float n1 = Math.RandomFloat(0.0, 1.0);
            float noise = amp * (0.35 + 0.65 * n0 * n1);
            bins.Set(i, bins.Get(i) + noise);
        }
    }

    //------------------------------------------------------------------------------------------------
    static int RangeBin(float rangeM, float displayRangeM, int binCount)
    {
        if (displayRangeM <= 0.0 || binCount <= 0)
            return 0;
        float u = rangeM / displayRangeM;
        if (u < 0.0)
            u = 0.0;
        if (u > 0.999)
            u = 0.999;
        return (int)(u * binCount);
    }

    //------------------------------------------------------------------------------------------------
    static int DopplerBin(float dopplerHz, int binCount)
    {
        if (binCount <= 0)
            return 0;
        float half = DOPPLER_HALF_HZ;
        float u = (dopplerHz + half) / (2.0 * half);
        if (u < 0.0)
            u = 0.0;
        if (u > 0.999)
            u = 0.999;
        return (int)(u * binCount);
    }

    //------------------------------------------------------------------------------------------------
    // Build A-Scope amplitude bins from plots (linear power, noise floor included).
    static void BuildAscopeBins(
        array<ref RDF_RadarTarget> targets,
        float displayRangeM,
        notnull array<float> outBins)
    {
        EnsureFloatArray(outBins, ASCOPE_BINS, 0.0);
        AddNoiseFloor(outBins, 0.04);

        if (!targets || displayRangeM <= 0.0)
            return;

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t)
                continue;
            if (t.m_Distance <= 0.0)
                continue;

            int bin = RangeBin(t.m_Distance, displayRangeM, ASCOPE_BINS);
            float power = PlotPowerLin(t);
            if (!t.m_Detected)
                power = power * 0.25;
            float cur = outBins.Get(bin);
            if (power > cur)
                outBins.Set(bin, power);
        }
    }

    //------------------------------------------------------------------------------------------------
    // Flattened RD_RANGE_BINS * RD_DOP_BINS grid (row-major: doppler major? use range + dop*range).
    // Index = dopBin * RD_RANGE_BINS + rangeBin so each Doppler row is contiguous for RLE draw.
    static void BuildRangeDopplerGrid(
        array<ref RDF_RadarTarget> targets,
        float displayRangeM,
        notnull array<float> outGrid)
    {
        int cells = RD_RANGE_BINS * RD_DOP_BINS;
        EnsureFloatArray(outGrid, cells, 0.0);
        AddNoiseFloor(outGrid, 0.03);

        if (!targets || displayRangeM <= 0.0)
            return;

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t)
                continue;
            if (t.m_Distance <= 0.0)
                continue;

            int rBin = RangeBin(t.m_Distance, displayRangeM, RD_RANGE_BINS);
            int dBin = DopplerBin(t.m_DopplerHz, RD_DOP_BINS);
            int idx = dBin * RD_RANGE_BINS + rBin;
            float add = SnrDbToUnit(t.m_SnrDb);
            if (!t.m_Detected)
                add = add * 0.35;
            if (add < 0.08)
                add = 0.08;
            float cur = outGrid.Get(idx);
            outGrid.Set(idx, cur + add);
        }
    }

    //------------------------------------------------------------------------------------------------
    // Downsample A-Scope bins into WF_COLS columns for one waterfall row.
    static void DownsampleAscopeToWaterfallRow(
        notnull array<float> ascopeBins,
        notnull array<float> outRow)
    {
        EnsureFloatArray(outRow, WF_COLS, 0.0);
        int srcN = ascopeBins.Count();
        if (srcN <= 0)
            return;

        for (int c = 0; c < WF_COLS; c++)
        {
            int i0 = (c * srcN) / WF_COLS;
            int i1 = ((c + 1) * srcN) / WF_COLS;
            if (i1 <= i0)
                i1 = i0 + 1;
            if (i1 > srcN)
                i1 = srcN;
            float peak = 0.0;
            for (int i = i0; i < i1; i++)
            {
                float v = ascopeBins.Get(i);
                if (v > peak)
                    peak = v;
            }
            outRow.Set(c, peak);
        }
    }

    //------------------------------------------------------------------------------------------------
    // Push one row into a circular waterfall buffer (newest at writeHead).
    // Buffer layout: row-major WF_ROWS * WF_COLS, writeHead is next row to overwrite.
    static int PushWaterfallRow(
        notnull array<float> buffer,
        int writeHead,
        notnull array<float> row)
    {
        int cells = WF_ROWS * WF_COLS;
        EnsureFloatArray(buffer, cells, 0.0);
        if (writeHead < 0 || writeHead >= WF_ROWS)
            writeHead = 0;

        int base = writeHead * WF_COLS;
        for (int c = 0; c < WF_COLS; c++)
        {
            float v = 0.0;
            if (c < row.Count())
                v = row.Get(c);
            buffer.Set(base + c, v);
        }

        writeHead = writeHead + 1;
        if (writeHead >= WF_ROWS)
            writeHead = 0;
        return writeHead;
    }

    //------------------------------------------------------------------------------------------------
    static float FindPeak(notnull array<float> values)
    {
        float peak = 0.001;
        for (int i = 0; i < values.Count(); i++)
        {
            float v = values.Get(i);
            if (v > peak)
                peak = v;
        }
        return peak;
    }

    //------------------------------------------------------------------------------------------------
    // Draw a heat row as RLE horizontal strips (one LineDrawCommand per color run).
    static void AppendHeatRowRle(
        notnull array<ref CanvasWidgetCommand> cmds,
        notnull array<float> rowValues,
        float y,
        float x0,
        float cellW,
        float lineW,
        float peak)
    {
        if (peak < 0.001)
            peak = 0.001;

        int n = rowValues.Count();
        if (n <= 0)
            return;

        int runStart = 0;
        float runT0 = rowValues.Get(0) / peak;
        int runBand0 = (int)(runT0 * 15.0 + 0.5);
        if (runBand0 < 0)
            runBand0 = 0;
        if (runBand0 > 15)
            runBand0 = 15;
        int runColor = PseudoColor(runBand0 / 15.0);

        for (int c = 1; c <= n; c++)
        {
            bool flush = false;
            int nextColor = runColor;
            if (c >= n)
            {
                flush = true;
            }
            else
            {
                float t = rowValues.Get(c) / peak;
                // 16-level banding so neighbouring noise cells RLE together.
                int band = (int)(t * 15.0 + 0.5);
                if (band < 0)
                    band = 0;
                if (band > 15)
                    band = 15;
                nextColor = PseudoColor(band / 15.0);
                if (nextColor != runColor)
                    flush = true;
            }

            if (!flush)
                continue;

            float xa = x0 + runStart * cellW;
            float xb = x0 + c * cellW;
            if (xb - xa < 0.5)
                xb = xa + 0.5;
            array<float> verts = new array<float>();
            verts.Insert(xa);
            verts.Insert(y);
            verts.Insert(xb);
            verts.Insert(y);
            LineDrawCommand seg = new LineDrawCommand();
            seg.m_iColor = runColor;
            seg.m_fWidth = lineW;
            seg.m_Vertices = verts;
            cmds.Insert(seg);

            if (c < n)
            {
                runStart = c;
                runColor = nextColor;
            }
        }
    }
}
