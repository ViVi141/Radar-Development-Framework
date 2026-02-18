// Target class enumeration used by RDF_TargetClassifier.
enum ERadarTargetClass
{
    UNKNOWN       = 0,
    INFANTRY      = 1, // Small RCS, low speed.
    VEHICLE_LIGHT = 2, // Medium RCS, medium speed.
    VEHICLE_HEAVY = 3, // Large RCS, medium speed.
    AIRCRAFT      = 4, // Any RCS, high speed / altitude.
    STATIC_OBJECT = 5, // Any RCS, near-zero velocity.
    SMALL_UAV     = 6, // Small RCS, medium speed, elevated.
    NAVAL         = 7  // Very large RCS, low speed.
}

// Rule-based target classifier using RCS magnitude and Doppler velocity.
// Designed as a baseline; users can extend with additional features.
class RDF_TargetClassifier
{
    void RDF_TargetClassifier() {}

    // Classify a single radar sample.
    // Returns ERadarTargetClass.UNKNOWN for non-hits or invalid samples.
    static ERadarTargetClass ClassifyTarget(RDF_RadarSample sample)
    {
        if (!sample || !sample.m_Hit)
            return ERadarTargetClass.UNKNOWN;

        float rcs   = sample.m_RadarCrossSection; // m^2
        float speed = Math.AbsFloat(sample.m_TargetVelocity); // m/s

        // Stationary or very slow -> static object or infrastructure.
        if (speed < 0.5)
            return ERadarTargetClass.STATIC_OBJECT;

        // High-speed targets are aircraft regardless of RCS.
        if (speed > 50.0)
            return ERadarTargetClass.AIRCRAFT;

        // Small UAV: tiny RCS + medium speed.
        if (rcs < 0.05 && speed > 5.0)
            return ERadarTargetClass.SMALL_UAV;

        // Infantry: small RCS, slow walking pace.
        if (rcs < 1.0 && speed < 5.0)
            return ERadarTargetClass.INFANTRY;

        // Naval: huge RCS, low speed.
        if (rcs > 1000.0 && speed < 15.0)
            return ERadarTargetClass.NAVAL;

        // Heavy vehicle: large RCS, moderate speed.
        if (rcs >= 10.0 && speed < 40.0)
            return ERadarTargetClass.VEHICLE_HEAVY;

        // Light vehicle: medium RCS, moderate speed.
        if (rcs >= 1.0 && speed < 40.0)
            return ERadarTargetClass.VEHICLE_LIGHT;

        return ERadarTargetClass.UNKNOWN;
    }

    // Classify all samples in a scan result, returning a parallel array of classes.
    static void ClassifyAll(
        array<ref RDF_LidarSample> samples,
        out array<ERadarTargetClass> outClasses)
    {
        if (!outClasses)
            outClasses = new array<ERadarTargetClass>();
        outClasses.Clear();

        foreach (RDF_LidarSample base : samples)
        {
            RDF_RadarSample rs = RDF_RadarSample.Cast(base);
            if (rs)
                outClasses.Insert(ClassifyTarget(rs));
            else
                outClasses.Insert(ERadarTargetClass.UNKNOWN);
        }
    }

    // Human-readable label for a target class.
    static string GetClassName(ERadarTargetClass cls)
    {
        switch (cls)
        {
            case ERadarTargetClass.INFANTRY:      return "Infantry";
            case ERadarTargetClass.VEHICLE_LIGHT: return "Light Vehicle";
            case ERadarTargetClass.VEHICLE_HEAVY: return "Heavy Vehicle";
            case ERadarTargetClass.AIRCRAFT:      return "Aircraft";
            case ERadarTargetClass.STATIC_OBJECT: return "Static";
            case ERadarTargetClass.SMALL_UAV:     return "Small UAV";
            case ERadarTargetClass.NAVAL:         return "Naval";
            default:                              return "Unknown";
        }
        return "Unknown";
    }

    // Debug-print classification statistics for a scan.
    static void PrintStats(array<ref RDF_LidarSample> samples)
    {
        int counts[8];
        for (int i = 0; i < 8; i++) counts[i] = 0;

        foreach (RDF_LidarSample base : samples)
        {
            RDF_RadarSample rs = RDF_RadarSample.Cast(base);
            if (!rs)
                continue;
            ERadarTargetClass cls = ClassifyTarget(rs);
            int clsIdx = cls;
            if (clsIdx >= 0 && clsIdx < 8)
                counts[clsIdx] = counts[clsIdx] + 1;
        }

        // Build a fixed enum array to avoid int->enum cast which is unsupported.
        ERadarTargetClass enumValues[8];
        enumValues[0] = ERadarTargetClass.UNKNOWN;
        enumValues[1] = ERadarTargetClass.INFANTRY;
        enumValues[2] = ERadarTargetClass.VEHICLE_LIGHT;
        enumValues[3] = ERadarTargetClass.VEHICLE_HEAVY;
        enumValues[4] = ERadarTargetClass.AIRCRAFT;
        enumValues[5] = ERadarTargetClass.STATIC_OBJECT;
        enumValues[6] = ERadarTargetClass.SMALL_UAV;
        enumValues[7] = ERadarTargetClass.NAVAL;

        Print("[Classifier] Target breakdown:");
        for (int c = 0; c < 8; c++)
        {
            if (counts[c] > 0)
                Print(string.Format("  %s : %d", GetClassName(enumValues[c]), counts[c]));
        }
    }
}
