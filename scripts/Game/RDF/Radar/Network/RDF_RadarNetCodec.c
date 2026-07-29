// Official-style packing for radar network sync.
// Live updates: array<int> + array<float> via RplRpc (stock-supported RPC types).
// Vectors are flattened as 3 floats. JIP uses ScriptBitWriter / ScriptBitReader.
class RDF_RadarNetCodec
{
    // Plot: ints type,flags,scattererId | floats dist,snr,cfar,az,el,radial,posxyz,velxyz
    static const int PLOT_INT_STRIDE = 3;
    static const int PLOT_FLOAT_STRIDE = 12;

    // Track: ints id,type,confirmed,hit | floats range,az,el,rr,snr,posxyz,velxyz
    static const int TRACK_INT_STRIDE = 4;
    static const int TRACK_FLOAT_STRIDE = 11;

    // WLR: ints trackId,launchValid,impactValid | floats launchxyz,impactxyz
    static const int WLR_INT_STRIDE = 3;
    static const int WLR_FLOAT_STRIDE = 6;

    // Datalink track: ints src,local,type,iff | floats range,az,el,rr,snr,time,pos,vel,origin
    static const int DL_TRACK_INT_STRIDE = 4;
    static const int DL_TRACK_FLOAT_STRIDE = 15;

    // Fused: ints id,type,iff,contrib,id0,id1,cross | floats snr,time,pos,vel
    static const int FUSED_INT_STRIDE = 7;
    static const int FUSED_FLOAT_STRIDE = 8;

    // Rpc arity is tight (~8 including method ref). Pack live payloads into 2 arrays.
    static const int SCAN_META_INT_COUNT = 6;
    static const int SCAN_META_FLOAT_COUNT = 10;
    static const int CONFIG_INT_COUNT = 9;
    static const int CONFIG_FLOAT_COUNT = 4;
    static const int DL_META_INT_COUNT = 2;

    static int PackBool4(bool a, bool b, bool c, bool d)
    {
        int flags = 0;
        if (a)
            flags = flags | 1;
        if (b)
            flags = flags | 2;
        if (c)
            flags = flags | 4;
        if (d)
            flags = flags | 8;
        return flags;
    }

    static bool FlagBit(int flags, int bit)
    {
        return (flags & bit) != 0;
    }

    static int TargetTypeToInt(ERDF_RadarTargetType type)
    {
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return 1;
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return 2;
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS)
            return 3;
        return 0;
    }

    static ERDF_RadarTargetType IntToTargetType(int typeValue)
    {
        if (typeValue == 1)
            return ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE;
        if (typeValue == 2)
            return ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER;
        if (typeValue == 3)
            return ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
        return ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE;
    }

    static int IffToInt(ERDF_RadarIff iff)
    {
        if (iff == ERDF_RadarIff.RDF_IFF_FRIEND)
            return 1;
        if (iff == ERDF_RadarIff.RDF_IFF_FOE)
            return 2;
        if (iff == ERDF_RadarIff.RDF_IFF_NEUTRAL)
            return 3;
        return 0;
    }

    static ERDF_RadarIff IntToIff(int v)
    {
        if (v == 1)
            return ERDF_RadarIff.RDF_IFF_FRIEND;
        if (v == 2)
            return ERDF_RadarIff.RDF_IFF_FOE;
        if (v == 3)
            return ERDF_RadarIff.RDF_IFF_NEUTRAL;
        return ERDF_RadarIff.RDF_IFF_UNKNOWN;
    }

    static int BoolToInt(bool value)
    {
        if (value)
            return 1;
        return 0;
    }

    static void PushVector(notnull array<float> floats, vector v)
    {
        floats.Insert(v[0]);
        floats.Insert(v[1]);
        floats.Insert(v[2]);
    }

    static vector ReadVectorAt(array<float> floats, int index)
    {
        return Vector(floats.Get(index), floats.Get(index + 1), floats.Get(index + 2));
    }

    static void PackScan(
        array<ref RDF_RadarTarget> plots,
        array<ref RDF_RadarTrack> tracks,
        notnull array<int> plotInts,
        notnull array<float> plotFloats,
        notnull array<int> trackInts,
        notnull array<float> trackFloats,
        notnull array<int> wlrInts,
        notnull array<float> wlrFloats)
    {
        plotInts.Clear();
        plotFloats.Clear();
        trackInts.Clear();
        trackFloats.Clear();
        wlrInts.Clear();
        wlrFloats.Clear();

        if (plots)
        {
            for (int i = 0; i < plots.Count(); i++)
            {
                RDF_RadarTarget t = plots.Get(i);
                if (!t)
                    continue;
                plotInts.Insert(TargetTypeToInt(t.m_Type));
                plotInts.Insert(PackBool4(t.m_Detected, t.m_IsAnonymous, t.m_IsFalsePlot, t.m_LosBlocked));
                plotInts.Insert(t.m_ScattererId);
                plotFloats.Insert(t.m_Distance);
                plotFloats.Insert(t.m_SnrDb);
                plotFloats.Insert(t.m_CfarPowerW);
                plotFloats.Insert(t.m_AzimuthDeg);
                plotFloats.Insert(t.m_ElevationDeg);
                plotFloats.Insert(t.m_RadialSpeedMs);
                PushVector(plotFloats, t.m_Position);
                PushVector(plotFloats, t.m_Velocity);
            }
        }

        if (tracks)
        {
            for (int k = 0; k < tracks.Count(); k++)
            {
                RDF_RadarTrack tr = tracks.Get(k);
                if (!tr)
                    continue;
                trackInts.Insert(tr.m_TrackId);
                trackInts.Insert(TargetTypeToInt(tr.m_Type));
                trackInts.Insert(BoolToInt(tr.m_Confirmed));
                trackInts.Insert(tr.m_HitCount);
                trackFloats.Insert(tr.m_FilteredRangeM);
                trackFloats.Insert(tr.m_FilteredAzimuthDeg);
                trackFloats.Insert(tr.m_FilteredElevationDeg);
                trackFloats.Insert(tr.m_FilteredRangeRateMs);
                trackFloats.Insert(tr.m_LastSnrDb);
                PushVector(trackFloats, tr.m_FilteredPosition);
                PushVector(trackFloats, tr.m_FilteredVelocity);

                RDF_RadarWlrFix fix = tr.m_LastWlrFix;
                if (fix && (fix.m_LaunchValid || fix.m_ImpactValid))
                {
                    wlrInts.Insert(tr.m_TrackId);
                    wlrInts.Insert(BoolToInt(fix.m_LaunchValid));
                    wlrInts.Insert(BoolToInt(fix.m_ImpactValid));
                    PushVector(wlrFloats, fix.m_LaunchPos);
                    PushVector(wlrFloats, fix.m_ImpactPos);
                }
            }
        }
    }

    static void UnpackScan(
        array<int> plotInts,
        array<float> plotFloats,
        array<int> trackInts,
        array<float> trackFloats,
        array<int> wlrInts,
        array<float> wlrFloats,
        notnull array<ref RDF_RadarTarget> outPlots,
        notnull array<ref RDF_RadarTrack> outTracks)
    {
        outPlots.Clear();
        outTracks.Clear();

        int plotCount = 0;
        if (plotInts && plotFloats)
        {
            int byInt = plotInts.Count() / PLOT_INT_STRIDE;
            int byFloat = plotFloats.Count() / PLOT_FLOAT_STRIDE;
            plotCount = byInt;
            if (byFloat < plotCount)
                plotCount = byFloat;
        }

        for (int i = 0; i < plotCount; i++)
        {
            int ii = i * PLOT_INT_STRIDE;
            int fi = i * PLOT_FLOAT_STRIDE;
            RDF_RadarTarget t = new RDF_RadarTarget();
            t.m_Type = IntToTargetType(plotInts.Get(ii));
            int flags = plotInts.Get(ii + 1);
            t.m_Detected = FlagBit(flags, 1);
            t.m_IsAnonymous = FlagBit(flags, 2);
            t.m_IsFalsePlot = FlagBit(flags, 4);
            t.m_LosBlocked = FlagBit(flags, 8);
            t.m_ScattererId = plotInts.Get(ii + 2);
            t.m_Distance = plotFloats.Get(fi);
            t.m_SnrDb = plotFloats.Get(fi + 1);
            t.m_CfarPowerW = plotFloats.Get(fi + 2);
            t.m_AzimuthDeg = plotFloats.Get(fi + 3);
            t.m_ElevationDeg = plotFloats.Get(fi + 4);
            t.m_RadialSpeedMs = plotFloats.Get(fi + 5);
            t.m_Position = ReadVectorAt(plotFloats, fi + 6);
            t.m_Velocity = ReadVectorAt(plotFloats, fi + 9);
            t.m_Entity = null;
            t.m_LosHitFraction = 1.0;
            t.m_MultipathFactor = 1.0;
            outPlots.Insert(t);
        }

        int trackCount = 0;
        if (trackInts && trackFloats)
        {
            int byInt = trackInts.Count() / TRACK_INT_STRIDE;
            int byFloat = trackFloats.Count() / TRACK_FLOAT_STRIDE;
            trackCount = byInt;
            if (byFloat < trackCount)
                trackCount = byFloat;
        }

        for (int k = 0; k < trackCount; k++)
        {
            int ii = k * TRACK_INT_STRIDE;
            int fi = k * TRACK_FLOAT_STRIDE;
            RDF_RadarTrack tr = new RDF_RadarTrack();
            tr.m_TrackId = trackInts.Get(ii);
            tr.m_Type = IntToTargetType(trackInts.Get(ii + 1));
            tr.m_Confirmed = trackInts.Get(ii + 2) != 0;
            tr.m_HitCount = trackInts.Get(ii + 3);
            tr.m_FilteredRangeM = trackFloats.Get(fi);
            tr.m_FilteredAzimuthDeg = trackFloats.Get(fi + 1);
            tr.m_FilteredElevationDeg = trackFloats.Get(fi + 2);
            tr.m_FilteredRangeRateMs = trackFloats.Get(fi + 3);
            tr.m_LastSnrDb = trackFloats.Get(fi + 4);
            tr.m_FilteredPosition = ReadVectorAt(trackFloats, fi + 5);
            tr.m_FilteredVelocity = ReadVectorAt(trackFloats, fi + 8);
            tr.m_MissCount = 0;
            tr.m_Entity = null;
            tr.m_LastUpdateTime = 0.0;
            tr.Push(tr.m_FilteredPosition, tr.m_FilteredVelocity, 0.0);
            outTracks.Insert(tr);
        }

        int wlrCount = 0;
        if (wlrInts && wlrFloats)
        {
            int byInt = wlrInts.Count() / WLR_INT_STRIDE;
            int byFloat = wlrFloats.Count() / WLR_FLOAT_STRIDE;
            wlrCount = byInt;
            if (byFloat < wlrCount)
                wlrCount = byFloat;
        }

        for (int w = 0; w < wlrCount; w++)
        {
            int ii = w * WLR_INT_STRIDE;
            int fi = w * WLR_FLOAT_STRIDE;
            int trackId = wlrInts.Get(ii);
            RDF_RadarTrack tr = FindTrackById(outTracks, trackId);
            if (!tr)
                continue;
            RDF_RadarWlrFix fix = new RDF_RadarWlrFix();
            fix.m_LaunchValid = wlrInts.Get(ii + 1) != 0;
            fix.m_ImpactValid = wlrInts.Get(ii + 2) != 0;
            fix.m_LaunchPos = ReadVectorAt(wlrFloats, fi);
            fix.m_ImpactPos = ReadVectorAt(wlrFloats, fi + 3);
            tr.m_LastWlrFix = fix;
        }
    }

    static void PackScanRpc(
        vector origin,
        vector forward,
        float rangeM,
        bool hasLock,
        int lockState,
        int lockTrackId,
        vector lockAim,
        array<ref RDF_RadarTarget> plots,
        array<ref RDF_RadarTrack> tracks,
        notnull array<int> outInts,
        notnull array<float> outFloats)
    {
        array<int> plotInts = new array<int>();
        array<float> plotFloats = new array<float>();
        array<int> trackInts = new array<int>();
        array<float> trackFloats = new array<float>();
        array<int> wlrInts = new array<int>();
        array<float> wlrFloats = new array<float>();
        PackScan(plots, tracks, plotInts, plotFloats, trackInts, trackFloats, wlrInts, wlrFloats);

        int plotCount = plotInts.Count() / PLOT_INT_STRIDE;
        int trackCount = trackInts.Count() / TRACK_INT_STRIDE;
        int wlrCount = wlrInts.Count() / WLR_INT_STRIDE;

        outInts.Clear();
        outInts.Insert(BoolToInt(hasLock));
        outInts.Insert(lockState);
        outInts.Insert(lockTrackId);
        outInts.Insert(plotCount);
        outInts.Insert(trackCount);
        outInts.Insert(wlrCount);
        AppendInts(outInts, plotInts);
        AppendInts(outInts, trackInts);
        AppendInts(outInts, wlrInts);

        outFloats.Clear();
        PushVector(outFloats, origin);
        PushVector(outFloats, forward);
        outFloats.Insert(rangeM);
        PushVector(outFloats, lockAim);
        AppendFloats(outFloats, plotFloats);
        AppendFloats(outFloats, trackFloats);
        AppendFloats(outFloats, wlrFloats);
    }

    static bool UnpackScanRpc(
        array<int> ints,
        array<float> floats,
        out vector origin,
        out vector forward,
        out float rangeM,
        out bool hasLock,
        out int lockState,
        out int lockTrackId,
        out vector lockAim,
        notnull array<ref RDF_RadarTarget> outPlots,
        notnull array<ref RDF_RadarTrack> outTracks)
    {
        origin = "0 0 0";
        forward = "1 0 0";
        rangeM = 0.0;
        hasLock = false;
        lockState = 0;
        lockTrackId = -1;
        lockAim = "0 0 0";
        outPlots.Clear();
        outTracks.Clear();
        if (!ints || !floats)
            return false;
        if (ints.Count() < SCAN_META_INT_COUNT)
            return false;
        if (floats.Count() < SCAN_META_FLOAT_COUNT)
            return false;

        hasLock = ints.Get(0) != 0;
        lockState = ints.Get(1);
        lockTrackId = ints.Get(2);
        int plotCount = ints.Get(3);
        int trackCount = ints.Get(4);
        int wlrCount = ints.Get(5);
        if (plotCount < 0)
            plotCount = 0;
        if (trackCount < 0)
            trackCount = 0;
        if (wlrCount < 0)
            wlrCount = 0;

        origin = ReadVectorAt(floats, 0);
        forward = ReadVectorAt(floats, 3);
        rangeM = floats.Get(6);
        lockAim = ReadVectorAt(floats, 7);

        int plotIntLen = plotCount * PLOT_INT_STRIDE;
        int trackIntLen = trackCount * TRACK_INT_STRIDE;
        int wlrIntLen = wlrCount * WLR_INT_STRIDE;
        int plotFloatLen = plotCount * PLOT_FLOAT_STRIDE;
        int trackFloatLen = trackCount * TRACK_FLOAT_STRIDE;
        int wlrFloatLen = wlrCount * WLR_FLOAT_STRIDE;

        int needInts = SCAN_META_INT_COUNT + plotIntLen + trackIntLen + wlrIntLen;
        int needFloats = SCAN_META_FLOAT_COUNT + plotFloatLen + trackFloatLen + wlrFloatLen;
        if (ints.Count() < needInts)
            return false;
        if (floats.Count() < needFloats)
            return false;

        array<int> plotInts = SliceInts(ints, SCAN_META_INT_COUNT, plotIntLen);
        array<int> trackInts = SliceInts(ints, SCAN_META_INT_COUNT + plotIntLen, trackIntLen);
        array<int> wlrInts = SliceInts(ints, SCAN_META_INT_COUNT + plotIntLen + trackIntLen, wlrIntLen);
        array<float> plotFloats = SliceFloats(floats, SCAN_META_FLOAT_COUNT, plotFloatLen);
        array<float> trackFloats = SliceFloats(floats, SCAN_META_FLOAT_COUNT + plotFloatLen, trackFloatLen);
        array<float> wlrFloats = SliceFloats(floats, SCAN_META_FLOAT_COUNT + plotFloatLen + trackFloatLen, wlrFloatLen);

        UnpackScan(plotInts, plotFloats, trackInts, trackFloats, wlrInts, wlrFloats, outPlots, outTracks);
        return true;
    }

    static void PackConfigRpc(
        float rangeM,
        float updateInterval,
        bool enabled,
        bool verbose,
        float sectorHalfAngleDeg,
        int maxTargets,
        bool enableWeaponLocate,
        bool enableWlrHudAlerts,
        int cfarMode,
        float detectionSnrDb,
        bool includeVehicles,
        bool includeProjectiles,
        bool includeRadarEmitters,
        notnull array<int> outInts,
        notnull array<float> outFloats)
    {
        outInts.Clear();
        outInts.Insert(BoolToInt(enabled));
        outInts.Insert(BoolToInt(verbose));
        outInts.Insert(maxTargets);
        outInts.Insert(BoolToInt(enableWeaponLocate));
        outInts.Insert(BoolToInt(enableWlrHudAlerts));
        outInts.Insert(cfarMode);
        outInts.Insert(BoolToInt(includeVehicles));
        outInts.Insert(BoolToInt(includeProjectiles));
        outInts.Insert(BoolToInt(includeRadarEmitters));

        outFloats.Clear();
        outFloats.Insert(rangeM);
        outFloats.Insert(updateInterval);
        outFloats.Insert(sectorHalfAngleDeg);
        outFloats.Insert(detectionSnrDb);
    }

    static bool UnpackConfigRpc(
        array<int> ints,
        array<float> floats,
        out float rangeM,
        out float updateInterval,
        out bool enabled,
        out bool verbose,
        out float sectorHalfAngleDeg,
        out int maxTargets,
        out bool enableWeaponLocate,
        out bool enableWlrHudAlerts,
        out int cfarMode,
        out float detectionSnrDb,
        out bool includeVehicles,
        out bool includeProjectiles,
        out bool includeRadarEmitters)
    {
        rangeM = 2000.0;
        updateInterval = 0.2;
        enabled = true;
        verbose = false;
        sectorHalfAngleDeg = 45.0;
        maxTargets = 64;
        enableWeaponLocate = true;
        enableWlrHudAlerts = true;
        cfarMode = 0;
        detectionSnrDb = 8.0;
        includeVehicles = true;
        includeProjectiles = true;
        includeRadarEmitters = true;
        if (!ints || !floats)
            return false;
        if (ints.Count() < CONFIG_INT_COUNT)
            return false;
        if (floats.Count() < CONFIG_FLOAT_COUNT)
            return false;

        enabled = ints.Get(0) != 0;
        verbose = ints.Get(1) != 0;
        maxTargets = ints.Get(2);
        enableWeaponLocate = ints.Get(3) != 0;
        enableWlrHudAlerts = ints.Get(4) != 0;
        cfarMode = ints.Get(5);
        includeVehicles = ints.Get(6) != 0;
        includeProjectiles = ints.Get(7) != 0;
        includeRadarEmitters = ints.Get(8) != 0;
        rangeM = floats.Get(0);
        updateInterval = floats.Get(1);
        sectorHalfAngleDeg = floats.Get(2);
        detectionSnrDb = floats.Get(3);
        return true;
    }

    static void PackDatalinkRpc(
        array<ref RDF_RadarDatalinkTrack> tracks,
        array<ref RDF_RadarFusedTrack> fused,
        notnull array<int> outInts,
        notnull array<float> outFloats)
    {
        array<int> trackInts = new array<int>();
        array<float> trackFloats = new array<float>();
        array<int> fusedInts = new array<int>();
        array<float> fusedFloats = new array<float>();
        PackDatalink(tracks, fused, trackInts, trackFloats, fusedInts, fusedFloats);

        int trackCount = trackInts.Count() / DL_TRACK_INT_STRIDE;
        int fusedCount = fusedInts.Count() / FUSED_INT_STRIDE;
        outInts.Clear();
        outInts.Insert(trackCount);
        outInts.Insert(fusedCount);
        AppendInts(outInts, trackInts);
        AppendInts(outInts, fusedInts);

        outFloats.Clear();
        AppendFloats(outFloats, trackFloats);
        AppendFloats(outFloats, fusedFloats);
    }

    static bool UnpackDatalinkRpc(
        array<int> ints,
        array<float> floats,
        notnull array<ref RDF_RadarDatalinkTrack> outTracks,
        notnull array<ref RDF_RadarFusedTrack> outFused)
    {
        outTracks.Clear();
        outFused.Clear();
        if (!ints || !floats)
            return false;
        if (ints.Count() < DL_META_INT_COUNT)
            return false;

        int trackCount = ints.Get(0);
        int fusedCount = ints.Get(1);
        if (trackCount < 0)
            trackCount = 0;
        if (fusedCount < 0)
            fusedCount = 0;

        int trackIntLen = trackCount * DL_TRACK_INT_STRIDE;
        int fusedIntLen = fusedCount * FUSED_INT_STRIDE;
        int trackFloatLen = trackCount * DL_TRACK_FLOAT_STRIDE;
        int fusedFloatLen = fusedCount * FUSED_FLOAT_STRIDE;
        if (ints.Count() < DL_META_INT_COUNT + trackIntLen + fusedIntLen)
            return false;
        if (floats.Count() < trackFloatLen + fusedFloatLen)
            return false;

        array<int> trackInts = SliceInts(ints, DL_META_INT_COUNT, trackIntLen);
        array<int> fusedInts = SliceInts(ints, DL_META_INT_COUNT + trackIntLen, fusedIntLen);
        array<float> trackFloats = SliceFloats(floats, 0, trackFloatLen);
        array<float> fusedFloats = SliceFloats(floats, trackFloatLen, fusedFloatLen);
        UnpackDatalink(trackInts, trackFloats, fusedInts, fusedFloats, outTracks, outFused);
        return true;
    }

    static void AppendInts(notnull array<int> dst, array<int> src)
    {
        if (!src)
            return;
        for (int i = 0; i < src.Count(); i++)
            dst.Insert(src.Get(i));
    }

    static void AppendFloats(notnull array<float> dst, array<float> src)
    {
        if (!src)
            return;
        for (int i = 0; i < src.Count(); i++)
            dst.Insert(src.Get(i));
    }

    static array<int> SliceInts(array<int> src, int start, int length)
    {
        array<int> outValues = new array<int>();
        if (!src || length <= 0)
            return outValues;
        for (int i = 0; i < length; i++)
            outValues.Insert(src.Get(start + i));
        return outValues;
    }

    static array<float> SliceFloats(array<float> src, int start, int length)
    {
        array<float> outValues = new array<float>();
        if (!src || length <= 0)
            return outValues;
        for (int i = 0; i < length; i++)
            outValues.Insert(src.Get(start + i));
        return outValues;
    }

    static RDF_RadarTrack FindTrackById(array<ref RDF_RadarTrack> tracks, int trackId)
    {
        if (!tracks)
            return null;
        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (tr && tr.m_TrackId == trackId)
                return tr;
        }
        return null;
    }

    static void WriteScanBits(
        ScriptBitWriter writer,
        vector origin,
        vector forward,
        float rangeM,
        bool hasLock,
        int lockState,
        int lockTrackId,
        vector lockAim,
        array<ref RDF_RadarTarget> plots,
        array<ref RDF_RadarTrack> tracks)
    {
        writer.WriteVector(origin);
        writer.WriteVector(forward);
        writer.WriteFloat(rangeM);
        writer.WriteBool(hasLock);
        writer.WriteInt(lockState);
        writer.WriteInt(lockTrackId);
        writer.WriteVector(lockAim);

        array<int> plotInts = new array<int>();
        array<float> plotFloats = new array<float>();
        array<int> trackInts = new array<int>();
        array<float> trackFloats = new array<float>();
        array<int> wlrInts = new array<int>();
        array<float> wlrFloats = new array<float>();
        PackScan(plots, tracks, plotInts, plotFloats, trackInts, trackFloats, wlrInts, wlrFloats);
        WriteIntArray(writer, plotInts);
        WriteFloatArray(writer, plotFloats);
        WriteIntArray(writer, trackInts);
        WriteFloatArray(writer, trackFloats);
        WriteIntArray(writer, wlrInts);
        WriteFloatArray(writer, wlrFloats);
    }

    static bool ReadScanBits(
        ScriptBitReader reader,
        out vector origin,
        out vector forward,
        out float rangeM,
        out bool hasLock,
        out int lockState,
        out int lockTrackId,
        out vector lockAim,
        notnull array<ref RDF_RadarTarget> outPlots,
        notnull array<ref RDF_RadarTrack> outTracks)
    {
        reader.ReadVector(origin);
        reader.ReadVector(forward);
        reader.ReadFloat(rangeM);
        reader.ReadBool(hasLock);
        reader.ReadInt(lockState);
        reader.ReadInt(lockTrackId);
        reader.ReadVector(lockAim);

        array<int> plotInts = new array<int>();
        array<float> plotFloats = new array<float>();
        array<int> trackInts = new array<int>();
        array<float> trackFloats = new array<float>();
        array<int> wlrInts = new array<int>();
        array<float> wlrFloats = new array<float>();
        ReadIntArray(reader, plotInts);
        ReadFloatArray(reader, plotFloats);
        ReadIntArray(reader, trackInts);
        ReadFloatArray(reader, trackFloats);
        ReadIntArray(reader, wlrInts);
        ReadFloatArray(reader, wlrFloats);
        UnpackScan(plotInts, plotFloats, trackInts, trackFloats, wlrInts, wlrFloats, outPlots, outTracks);
        return true;
    }

    static void PackDatalink(
        array<ref RDF_RadarDatalinkTrack> tracks,
        array<ref RDF_RadarFusedTrack> fused,
        notnull array<int> trackInts,
        notnull array<float> trackFloats,
        notnull array<int> fusedInts,
        notnull array<float> fusedFloats)
    {
        trackInts.Clear();
        trackFloats.Clear();
        fusedInts.Clear();
        fusedFloats.Clear();

        if (tracks)
        {
            for (int i = 0; i < tracks.Count(); i++)
            {
                RDF_RadarDatalinkTrack t = tracks.Get(i);
                if (!t)
                    continue;
                trackInts.Insert(t.m_SourceRadarId);
                trackInts.Insert(t.m_LocalTrackId);
                trackInts.Insert(TargetTypeToInt(t.m_Type));
                trackInts.Insert(IffToInt(t.m_Iff));
                trackFloats.Insert(t.m_RangeM);
                trackFloats.Insert(t.m_AzimuthDeg);
                trackFloats.Insert(t.m_ElevationDeg);
                trackFloats.Insert(t.m_RangeRateMs);
                trackFloats.Insert(t.m_SnrDb);
                trackFloats.Insert(t.m_TimeS);
                PushVector(trackFloats, t.m_WorldPos);
                PushVector(trackFloats, t.m_Velocity);
                PushVector(trackFloats, t.m_RadarOrigin);
            }
        }

        if (fused)
        {
            for (int f = 0; f < fused.Count(); f++)
            {
                RDF_RadarFusedTrack ft = fused.Get(f);
                if (!ft)
                    continue;
                fusedInts.Insert(ft.m_FusedId);
                fusedInts.Insert(TargetTypeToInt(ft.m_Type));
                fusedInts.Insert(IffToInt(ft.m_Iff));
                fusedInts.Insert(ft.m_ContributorCount);
                fusedInts.Insert(ft.m_ContributorRadarId0);
                fusedInts.Insert(ft.m_ContributorRadarId1);
                fusedInts.Insert(BoolToInt(ft.m_CrossFixUsed));
                fusedFloats.Insert(ft.m_SnrDb);
                fusedFloats.Insert(ft.m_TimeS);
                PushVector(fusedFloats, ft.m_WorldPos);
                PushVector(fusedFloats, ft.m_Velocity);
            }
        }
    }

    static void UnpackDatalink(
        array<int> trackInts,
        array<float> trackFloats,
        array<int> fusedInts,
        array<float> fusedFloats,
        notnull array<ref RDF_RadarDatalinkTrack> outTracks,
        notnull array<ref RDF_RadarFusedTrack> outFused)
    {
        outTracks.Clear();
        outFused.Clear();

        int trackCount = 0;
        if (trackInts && trackFloats)
        {
            int byInt = trackInts.Count() / DL_TRACK_INT_STRIDE;
            int byFloat = trackFloats.Count() / DL_TRACK_FLOAT_STRIDE;
            trackCount = byInt;
            if (byFloat < trackCount)
                trackCount = byFloat;
        }

        for (int i = 0; i < trackCount; i++)
        {
            int ii = i * DL_TRACK_INT_STRIDE;
            int fi = i * DL_TRACK_FLOAT_STRIDE;
            RDF_RadarDatalinkTrack t = new RDF_RadarDatalinkTrack();
            t.m_SourceRadarId = trackInts.Get(ii);
            t.m_LocalTrackId = trackInts.Get(ii + 1);
            t.m_Type = IntToTargetType(trackInts.Get(ii + 2));
            t.m_Iff = IntToIff(trackInts.Get(ii + 3));
            t.m_RangeM = trackFloats.Get(fi);
            t.m_AzimuthDeg = trackFloats.Get(fi + 1);
            t.m_ElevationDeg = trackFloats.Get(fi + 2);
            t.m_RangeRateMs = trackFloats.Get(fi + 3);
            t.m_SnrDb = trackFloats.Get(fi + 4);
            t.m_TimeS = trackFloats.Get(fi + 5);
            t.m_WorldPos = ReadVectorAt(trackFloats, fi + 6);
            t.m_Velocity = ReadVectorAt(trackFloats, fi + 9);
            t.m_RadarOrigin = ReadVectorAt(trackFloats, fi + 12);
            outTracks.Insert(t);
        }

        int fusedCount = 0;
        if (fusedInts && fusedFloats)
        {
            int byInt = fusedInts.Count() / FUSED_INT_STRIDE;
            int byFloat = fusedFloats.Count() / FUSED_FLOAT_STRIDE;
            fusedCount = byInt;
            if (byFloat < fusedCount)
                fusedCount = byFloat;
        }

        for (int f = 0; f < fusedCount; f++)
        {
            int ii = f * FUSED_INT_STRIDE;
            int fi = f * FUSED_FLOAT_STRIDE;
            RDF_RadarFusedTrack ft = new RDF_RadarFusedTrack();
            ft.m_FusedId = fusedInts.Get(ii);
            ft.m_Type = IntToTargetType(fusedInts.Get(ii + 1));
            ft.m_Iff = IntToIff(fusedInts.Get(ii + 2));
            ft.m_ContributorCount = fusedInts.Get(ii + 3);
            ft.m_ContributorRadarId0 = fusedInts.Get(ii + 4);
            ft.m_ContributorRadarId1 = fusedInts.Get(ii + 5);
            ft.m_CrossFixUsed = fusedInts.Get(ii + 6) != 0;
            ft.m_SnrDb = fusedFloats.Get(fi);
            ft.m_TimeS = fusedFloats.Get(fi + 1);
            ft.m_WorldPos = ReadVectorAt(fusedFloats, fi + 2);
            ft.m_Velocity = ReadVectorAt(fusedFloats, fi + 5);
            outFused.Insert(ft);
        }
    }

    static void WriteDatalinkBits(
        ScriptBitWriter writer,
        array<ref RDF_RadarDatalinkTrack> tracks,
        array<ref RDF_RadarFusedTrack> fused)
    {
        array<int> trackInts = new array<int>();
        array<float> trackFloats = new array<float>();
        array<int> fusedInts = new array<int>();
        array<float> fusedFloats = new array<float>();
        PackDatalink(tracks, fused, trackInts, trackFloats, fusedInts, fusedFloats);
        WriteIntArray(writer, trackInts);
        WriteFloatArray(writer, trackFloats);
        WriteIntArray(writer, fusedInts);
        WriteFloatArray(writer, fusedFloats);
    }

    static bool ReadDatalinkBits(
        ScriptBitReader reader,
        notnull array<ref RDF_RadarDatalinkTrack> outTracks,
        notnull array<ref RDF_RadarFusedTrack> outFused)
    {
        array<int> trackInts = new array<int>();
        array<float> trackFloats = new array<float>();
        array<int> fusedInts = new array<int>();
        array<float> fusedFloats = new array<float>();
        ReadIntArray(reader, trackInts);
        ReadFloatArray(reader, trackFloats);
        ReadIntArray(reader, fusedInts);
        ReadFloatArray(reader, fusedFloats);
        UnpackDatalink(trackInts, trackFloats, fusedInts, fusedFloats, outTracks, outFused);
        return true;
    }

    static void WriteIntArray(ScriptBitWriter writer, array<int> values)
    {
        int count = 0;
        if (values)
            count = values.Count();
        writer.WriteInt(count);
        for (int i = 0; i < count; i++)
            writer.WriteInt(values.Get(i));
    }

    static void ReadIntArray(ScriptBitReader reader, notnull array<int> outValues)
    {
        outValues.Clear();
        int count;
        reader.ReadInt(count);
        if (count < 0)
            count = 0;
        for (int i = 0; i < count; i++)
        {
            int v;
            reader.ReadInt(v);
            outValues.Insert(v);
        }
    }

    static void WriteFloatArray(ScriptBitWriter writer, array<float> values)
    {
        int count = 0;
        if (values)
            count = values.Count();
        writer.WriteInt(count);
        for (int i = 0; i < count; i++)
            writer.WriteFloat(values.Get(i));
    }

    static void ReadFloatArray(ScriptBitReader reader, notnull array<float> outValues)
    {
        outValues.Clear();
        int count;
        reader.ReadInt(count);
        if (count < 0)
            count = 0;
        for (int i = 0; i < count; i++)
        {
            float v;
            reader.ReadFloat(v);
            outValues.Insert(v);
        }
    }
}
