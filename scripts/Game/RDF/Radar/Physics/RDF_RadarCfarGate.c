// CA / GO / SO CFAR gate helper for coarse azimuth/range bins.
class RDF_RadarCfarGate
{
    static bool CellDetected(
        notnull array<float> rowPowers,
        int binIdx,
        float noiseFloorW,
        int guardCells,
        int trainingCells,
        float pfa,
        ERDF_CfarMode mode)
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
        float sumLeft = 0.0;
        int countLeft = 0;
        float sumRight = 0.0;
        int countRight = 0;

        int left0 = binIdx - guardCells - half;
        int left1 = binIdx - guardCells - 1;
        for (int i = left0; i <= left1; i++)
        {
            if (i < 0)
                continue;
            if (i >= nbin)
                continue;
            sumLeft = sumLeft + rowPowers.Get(i);
            countLeft = countLeft + 1;
        }

        int right0 = binIdx + guardCells + 1;
        int right1 = binIdx + guardCells + half;
        for (int j = right0; j <= right1; j++)
        {
            if (j < 0)
                continue;
            if (j >= nbin)
                continue;
            sumRight = sumRight + rowPowers.Get(j);
            countRight = countRight + 1;
        }

        float meanLeft = noiseFloorW;
        if (countLeft > 0)
            meanLeft = sumLeft / countLeft;
        float meanRight = noiseFloorW;
        if (countRight > 0)
            meanRight = sumRight / countRight;

        float localNoise = noiseFloorW;
        if (mode == ERDF_CfarMode.RDF_CFAR_GO)
        {
            // Greater-of: raise threshold at clutter edges (fewer false alarms).
            if (meanLeft > meanRight)
                localNoise = meanLeft;
            else
                localNoise = meanRight;
        }
        else if (mode == ERDF_CfarMode.RDF_CFAR_SO)
        {
            // Smaller-of: lower threshold near edges (better weak-target capture).
            if (countLeft <= 0 && countRight <= 0)
            {
                localNoise = noiseFloorW;
            }
            else if (countLeft <= 0)
            {
                localNoise = meanRight;
            }
            else if (countRight <= 0)
            {
                localNoise = meanLeft;
            }
            else if (meanLeft < meanRight)
            {
                localNoise = meanLeft;
            }
            else
            {
                localNoise = meanRight;
            }
        }
        else
        {
            // Cell-averaging across both sides.
            int count = countLeft + countRight;
            float sum = sumLeft + sumRight;
            if (count > 0)
                localNoise = sum / count;
        }

        if (localNoise < noiseFloorW)
            localNoise = noiseFloorW;

        float threshold = localNoise * alpha;
        return rowPowers.Get(binIdx) > threshold;
    }
}
