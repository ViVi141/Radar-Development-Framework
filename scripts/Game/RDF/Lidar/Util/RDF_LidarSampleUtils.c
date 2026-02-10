// Static helpers for filtering and statistics on RDF_LidarSample arrays.
class RDF_LidarSampleUtils
{
    // Return the closest hit sample (by distance), or null if none.
    static RDF_LidarSample GetClosestHit(array<ref RDF_LidarSample> samples)
    {
        if (!samples || samples.Count() == 0) return null;
        RDF_LidarSample closest = null;
        float minDist = 1e10;
        for (int i = 0; i < samples.Count(); i++)
        {
            RDF_LidarSample s = samples.Get(i);
            if (s && s.m_Hit && s.m_Distance < minDist)
            {
                minDist = s.m_Distance;
                closest = s;
            }
        }
        return closest;
    }

    // Return the furthest hit sample (by distance), or null if none.
    static RDF_LidarSample GetFurthestHit(array<ref RDF_LidarSample> samples)
    {
        if (!samples || samples.Count() == 0) return null;
        RDF_LidarSample furthest = null;
        float maxDist = -1.0;
        for (int i = 0; i < samples.Count(); i++)
        {
            RDF_LidarSample s = samples.Get(i);
            if (s && s.m_Hit && s.m_Distance > maxDist)
            {
                maxDist = s.m_Distance;
                furthest = s;
            }
        }
        return furthest;
    }

    // Number of samples that hit something.
    static int GetHitCount(array<ref RDF_LidarSample> samples)
    {
        if (!samples) return 0;
        int n = 0;
        for (int i = 0; i < samples.Count(); i++)
        {
            RDF_LidarSample s = samples.Get(i);
            if (s && s.m_Hit) n++;
        }
        return n;
    }

    // Fill outSamples with hits whose distance is in [minDist, maxDist]. outSamples must be non-null.
    static void GetHitsInRange(array<ref RDF_LidarSample> samples, float minDist, float maxDist, array<ref RDF_LidarSample> outSamples)
    {
        if (!samples || !outSamples) return;
        outSamples.Clear();
        for (int i = 0; i < samples.Count(); i++)
        {
            RDF_LidarSample s = samples.Get(i);
            if (s && s.m_Hit && s.m_Distance >= minDist && s.m_Distance <= maxDist)
                outSamples.Insert(s);
        }
    }

    // Average distance. If hitsOnly true, only over hits; otherwise over all samples.
    static float GetAverageDistance(array<ref RDF_LidarSample> samples, bool hitsOnly = true)
    {
        if (!samples || samples.Count() == 0) return 0.0;
        float sum = 0.0;
        int n = 0;
        for (int i = 0; i < samples.Count(); i++)
        {
            RDF_LidarSample s = samples.Get(i);
            if (!s) continue;
            if (hitsOnly && !s.m_Hit) continue;
            sum += s.m_Distance;
            n++;
        }
        if (n == 0) return 0.0;
        return sum / n;
    }
}
