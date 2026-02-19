// 
//  EMFieldNetworkAPI  base (no-op) interface for EM voxel field network layer.
// 
// Concrete implementation lives in EMFieldNetworkComponent.
//  Any code that wants to query EM detection results from remote clients
// should use this API so it is decoupled from the transport layer.
// 
//  Phase 5  Server/client responsibility contract:
//   Server  : maintains full EMVoxelField, runs TickDecay, collects detections,
//             serialises and broadcasts compact result packets.
//   Client  : receives packed detection lists, stores them locally,
//              provides read-only query interface.  Never touches EMVoxelField.
// 

// ------------------------------------------------------------------------
// Compact detection result  server  client sync payload (one target slot).
// Fields are chosen to fit per-target data under ~48 bytes when serialised.
// ------------------------------------------------------------------------
class EMDetectionResult
{
    vector m_vPosition;       // world-space position of the detected EM energy
    float  m_fPowerDbm;       // received power in dBm
    float  m_fBearingDeg;     // bearing from radar origin (YAW, degrees)
    float  m_fElevationDeg;   // elevation angle from radar origin (degrees)
    int    m_iFrequencyMask;  // frequency-band bitmask
    int    m_iWaveformType;   // 0=Pulse,1=CW,2=FMCW,3=LPI, -1=unknown
    float  m_fEstPRF;         // estimated pulse-repetition frequency (Hz), 0 if unknown

    void EMDetectionResult()
    {
        m_vPosition      = Vector(0, 0, 0);
        m_fPowerDbm      = -200.0;
        m_fBearingDeg    = 0.0;
        m_fElevationDeg  = 0.0;
        m_iFrequencyMask = 0;
        m_iWaveformType  = -1;
        m_fEstPRF        = 0.0;
    }

    // Convenience: return true if power exceeds a dBm threshold.
    bool IsDetected(float thresholdDbm)
    {
        return m_fPowerDbm >= thresholdDbm;
    }

    // Serialise to compact pipe-delimited string token (no whitespace).
    // Format: "X,Y,Z,PdBm,Bearing,Elev,FreqMask,WaveType,PRF"
    string Serialise()
    {
        return m_vPosition[0].ToString(1) + "," +
               m_vPosition[1].ToString(1) + "," +
               m_vPosition[2].ToString(1) + "," +
               m_fPowerDbm.ToString(2)    + "," +
               m_fBearingDeg.ToString(1)  + "," +
               m_fElevationDeg.ToString(1) + "," +
               m_iFrequencyMask.ToString() + "," +
               m_iWaveformType.ToString()  + "," +
               m_fEstPRF.ToString(1);
    }

    // Deserialise from a token produced by Serialise().
    static EMDetectionResult Deserialise(string token)
    {
        array<string> p = new array<string>();
        token.Split(",", p, false);
        EMDetectionResult r = new EMDetectionResult();
        if (p.Count() < 9)
            return r;
        r.m_vPosition      = Vector(p.Get(0).ToFloat(), p.Get(1).ToFloat(), p.Get(2).ToFloat());
        r.m_fPowerDbm      = p.Get(3).ToFloat();
        r.m_fBearingDeg    = p.Get(4).ToFloat();
        r.m_fElevationDeg  = p.Get(5).ToFloat();
        r.m_iFrequencyMask = p.Get(6).ToInt();
        r.m_iWaveformType  = p.Get(7).ToInt();
        r.m_fEstPRF        = p.Get(8).ToFloat();
        return r;
    }
}

// ------------------------------------------------------------------------
// Base API class  no-op implementation; provides the interface contract.
// ------------------------------------------------------------------------
[ComponentEditorProps(category: "GameScripted/RDF", description: "Base network API for EM voxel field  server/client sync interface")]
class EMFieldNetworkAPIClass : ScriptComponentClass {}

class EMFieldNetworkAPI : ScriptComponent
{
    // Returns true when the network layer (RplComponent) is ready.
    bool IsNetworkAvailable()
    {
        return false;
    }

    // -- Server-side configuration --

    // Set the broadcast interval (seconds between detection result packets).
    void SetSyncInterval(float intervalSec) {}

    // Set max number of top-power cells included per packet.
    void SetMaxDetectionsPerPacket(int count) {}

    // -- Client-side query --

    // Returns true if at least one synced detection result is available.
    bool HasReceivedDetections()
    {
        return false;
    }

    // Returns the last received detection list (null if none).
    array<ref EMDetectionResult> GetLastDetections()
    {
        return null;
    }

    // World-time stamp of the last received packet (0 if none).
    float GetLastDetectionTime()
    {
        return 0.0;
    }

    // -- Optional callbacks --

    // Override to react when a new detection packet is received on the client.
    void OnDetectionsReceived(array<ref EMDetectionResult> results, float timestamp) {}
}
