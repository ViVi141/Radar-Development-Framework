// CA-CFAR gate helper for coarse azimuth/range bins in game-time radar scans.
class RDF_RadarCfarGate
{
    static bool CellDetected(
        notnull array<float> rowPowers,
        int binIdx,
        float noiseFloorW,
        int guardCells,
        int trainingCells,
        float pfa)
    {
        int nbin = rowPowers.Count();
        if (nbin <= 0)
            return false;
        if (binIdx < 0 || binIdx >= nbin)
            return false;

        int nTrain = trainingCells;
        if (nTrain < 2)
            nTrain = 2;
        float alpha = nTrain * (Math.Pow(pfa, -1.0 / nTrain) - 1.0);
        if (alpha < 1.0)
            alpha = 1.0;

        int half = nTrain / 2;
        float sum = 0.0;
        int count = 0;

        int left0 = binIdx - guardCells - half;
        int left1 = binIdx - guardCells - 1;
        for (int i = left0; i <= left1; i++)
        {
            if (i < 0)
                continue;
            if (i >= nbin)
                continue;
            sum = sum + rowPowers.Get(i);
            count = count + 1;
        }

        int right0 = binIdx + guardCells + 1;
        int right1 = binIdx + guardCells + half;
        for (int j = right0; j <= right1; j++)
        {
            if (j < 0)
                continue;
            if (j >= nbin)
                continue;
            sum = sum + rowPowers.Get(j);
            count = count + 1;
        }

        float localNoise = noiseFloorW;
        if (count > 0)
            localNoise = sum / count;
        if (localNoise < noiseFloorW)
            localNoise = noiseFloorW;

        float threshold = localNoise * alpha;
        return rowPowers.Get(binIdx) > threshold;
    }
}
