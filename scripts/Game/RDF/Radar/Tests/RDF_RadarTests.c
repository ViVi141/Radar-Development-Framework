// Unit-level physics tests for the radar simulation modules.
// Run via RDF_RadarTests.RunAllTests() from a game script entry point.
// Results are printed to the Reforger log.
class RDF_RadarTests
{
    void RDF_RadarTests() {}

    // Radar equation - verify Pr at a known range matches the analytical answer.
    // Reference: Pt=1kW, Gt=30dBi, lambda=3cm, sigma=1m^2, R=10km -> Pr ~= 3.6e-13 W
    static void TestRadarEquation()
    {
        Print("[Test] Radar Equation");

        float Pt         = 1000.0;
        float Gt         = RDF_RadarEquation.DBiToLinear(30.0); // ~= 1000
        float wavelength = 0.03;
        float sigma      = 1.0;
        float range      = 10000.0;

        float Pr    = RDF_RadarEquation.CalculateReceivedPower(Pt, Gt, wavelength, sigma, range, 1.0);
        float PrDBm = RDF_RadarEquation.WattsToDBm(Pr);

        Print(string.Format("  Pt=1kW, Gt=30dBi, lambda=3cm, sigma=1m^2, R=10km"));
        Print(string.Format("  Pr = %.3e W  (%.1f dBm)  - expect ~-94 dBm", Pr, PrDBm));

        // Maximum detection range for a 10m^2 target at -100 dBm sensitivity.
        float sensitivity = RDF_RadarEquation.DBmToWatts(-100.0);
        float Rmax = RDF_RadarEquation.CalculateMaxDetectionRange(Pt, Gt, wavelength, 10.0, sensitivity, 1.0);
        Print(string.Format("  Rmax (10m^2 target, -100dBm) = %.1f km", Rmax / 1000.0));
        Print("");
    }

    // SNR calculation - verify thermal noise floor and SNR at threshold.
    static void TestSNR()
    {
        Print("[Test] SNR & Noise Power");

        // Noise power: 1 MHz BW, 5 dB NF, 290 K.
        float noise    = RDF_RadarEquation.CalculateNoisePower(1000000.0, 5.0, 290.0);
        float noiseDBm = RDF_RadarEquation.WattsToDBm(noise);
        Print(string.Format("  Noise power (1MHz BW, 5dB NF, 290K) = %.2e W  (%.1f dBm)", noise, noiseDBm));
        Print("  - expect roughly -107 dBm");

        float snr = RDF_RadarEquation.CalculateSNR(0.000000001, noise, 1.0);
        Print(string.Format("  SNR for Pr=1nW: %.1f dB", snr));
        Print("");
    }

    // Doppler - verify shift magnitude for a 30 m/s closing target at 10 GHz.
    // Expected: fd ~= 2000 Hz.
    static void TestDoppler()
    {
        Print("[Test] Doppler Effect");

        float velocity  = 30.0;
        float frequency = 10000000000.0;

        float fd    = RDF_DopplerProcessor.CalculateDopplerShift(velocity, frequency);
        Print(string.Format("  v=30 m/s, f=10GHz -> fd = %.1f Hz  (expect ~2000 Hz)", fd));

        float vBack = RDF_DopplerProcessor.VelocityFromDoppler(fd, frequency);
        Print(string.Format("  Inverse: fd -> v = %.2f m/s  (expect 30.0 m/s)", vBack));

        float vMax = RDF_DopplerProcessor.MaxUnambiguousVelocity(1000.0, frequency);
        Print(string.Format("  Max unambiguous velocity (PRF=1kHz): %.2f m/s", vMax));
        Print("");
    }

    static void TestBlindSpeed()
    {
        Print("[Test] Doppler Blind-Speed Detection");
        float freq = 10000000000.0; // 10 GHz
        float prf  = 1000.0;        // 1 kHz

        // blind speed (n=1): v = PRF * c / (2*f0)
        float expected = (prf * 299792458.0) / (2.0 * freq);
        Print(string.Format("  Expected blind-speed (n=1) = %.3f m/s", expected));

        bool isBlind = RDF_DopplerProcessor.IsBlindSpeed(expected, prf, freq, 1.0);
        Print(string.Format("  IsBlindSpeed(%.3f m/s) = %1", expected, isBlind));
        if (isBlind) Print("  [OK]"); else Print("  [FAIL]");

        Print("");
    }

    static void TestCFAR()
    {
        Print("[Test] CA-CFAR (PoC)");

        int N = 36;
        array<ref RDF_LidarSample> samples = new array<ref RDF_LidarSample>();
        for (int i = 0; i < N; ++i)
        {
            RDF_RadarSample s = new RDF_RadarSample();
            float ang = (2.0 * Math.PI) * (float)i / (float)N;
            s.m_Dir = Vector(Math.Cos(ang), 0.0, Math.Sin(ang));
            s.m_Hit = true;
            s.m_ReceivedPower = 1e-9; // background
            samples.Insert(s);
        }

        // Insert a strong cell at index 0
        RDF_RadarSample strong = RDF_RadarSample.Cast(samples.Get(0));
        strong.m_ReceivedPower = 1e-7; // 100x background

        // Apply CFAR: window=8, guard=5deg, multiplier=10
        RDF_CFar.ApplyCA_CFAR(samples, 8, 5.0, 10.0);

        bool strongOk = RDF_RadarSample.Cast(samples.Get(0)).m_Hit;
        bool neighborSuppressed = !RDF_RadarSample.Cast(samples.Get(5)).m_Hit;

        Print(string.Format("  strong cell detected = %1, neighbour suppressed = %1", strongOk, neighborSuppressed));
        if (strongOk && neighborSuppressed) Print("  [OK]"); else Print("  [FAIL]");

        Print("");
    }

    static void TestOSCFAR()
    {
        Print("[Test] OS-CFAR (PoC)");

        int N = 36;
        array<ref RDF_LidarSample> samples = new array<ref RDF_LidarSample>();
        for (int i = 0; i < N; ++i)
        {
            RDF_RadarSample s = new RDF_RadarSample();
            float ang = (2.0 * Math.PI) * (float)i / (float)N;
            s.m_Dir = Vector(Math.Cos(ang), 0.0, Math.Sin(ang));
            s.m_Hit = true;
            s.m_ReceivedPower = 1e-9; // background
            samples.Insert(s);
        }

        // Insert a strong cell at index 0
        RDF_RadarSample strong = RDF_RadarSample.Cast(samples.Get(0));
        strong.m_ReceivedPower = 1e-7; // 100x background

        // Apply OS-CFAR: window=8, guard=5deg, multiplier=10, rank=4
        RDF_CFar.ApplyOS_CFAR(samples, 8, 5.0, 10.0, 4);

        bool strongOk = RDF_RadarSample.Cast(samples.Get(0)).m_Hit;
        bool neighborSuppressed = !RDF_RadarSample.Cast(samples.Get(5)).m_Hit;

        Print(string.Format("  strong cell detected = %1, neighbour suppressed = %1", strongOk, neighborSuppressed));
        if (strongOk && neighborSuppressed) Print("  [OK]"); else Print("  [FAIL]");

        Print("");
    }

    static void TestCFARAutoScale()
    {
        Print("[Test] CA-CFAR Auto-scale Multiplier");
        int N = 16;
        float pfa = 1e-6;
        float alpha = RDF_CFar.CalculateCAThresholdMultiplier(N, pfa);
        Print(string.Format("  N=%1, Pfa=%.1e -> alpha=%.3f", N, pfa, alpha));
        if (alpha > 0.0) Print("  [OK]"); else Print("  [FAIL]");
        Print("");
    }

    static void TestOSCFARAutoScale()
    {
        Print("[Test] OS-CFAR Auto-scale Estimation (Monte‑Carlo PoC)");
        int N = 16;
        int rank = 4;
        float pfa = 1e-6;
        float alpha = RDF_CFar.EstimateOSMultiplier(N, rank, pfa, 1024);
        Print(string.Format("  N=%1, rank=%1, Pfa=%.1e -> alpha≈%.3f", N, rank, pfa, alpha));
        if (alpha > 0.0) Print("  [OK]"); else Print("  [FAIL]");
        Print("");
    }

    static void TestOSCFARCache()
    {
        Print("[Test] OS-CFAR Cache / Lookup Table (Precompute)");
        array<int> windows = {8, 16};
        array<int> ranks = {2, 4};
        array<float> pfas = {1e-3, 1e-6};

        RDF_CFar.PrecomputeOSMultiplierTable(windows, ranks, pfas, 512);

        float v = RDF_CFar.GetCachedOSMultiplier(16, 4, 1e-6);
        Print(string.Format("  Cached multiplier (16,4,1e-6) = %.6f", v));
        if (v > 0.0) Print("  [OK]"); else Print("  [FAIL]");
        Print("");
    }

    static void TestOSCFARFileLookup()
    {
        Print("[Test] OS-CFAR File Lookup (offline CSV)");
        string path = "scripts/Game/RDF/Radar/Data/os_cfar_multipliers.csv";

        // Explicitly load file (function should be idempotent)
        RDF_CFar.LoadOSMultiplierTableFromFile(path);

        float v = RDF_CFar.GetCachedOSMultiplier(16, 4, 1e-6);
        Print(string.Format("  File lookup (16,4,1e-6) = %.6f", v));
        if (v > 0.0) Print("  [OK]"); else Print("  [FAIL]");
        Print("");
    }

    static void TestOSCFARScannerAutoLoad()
    {
        Print("[Test] OS-CFAR Scanner Auto-Load from Settings");

        // Build settings that explicitly point to the offline CSV and enable auto-load
        RDF_RadarSettings s = new RDF_RadarSettings();
        s.m_CfarUseOfflineTable = true;
        s.m_CfarOfflineTablePath = "scripts/Game/RDF/Radar/Data/os_cfar_multipliers.csv";

        // Construct a scanner using these settings — ctor should load the table
        RDF_RadarScanner sc = new RDF_RadarScanner(s);

        float v = RDF_CFar.GetCachedOSMultiplier(16, 4, 1e-6);
        Print(string.Format("  Scanner-driven file lookup (16,4,1e-6) = %.6f", v));
        if (v > 0.0) Print("  [OK]"); else Print("  [FAIL]");
        Print("");
    }
    // RCS models - sphere, plate, cylinder at X-band.
    static void TestRCS()
    {
        Print("[Test] RCS Models (lambda=3cm, X-band)");

        float wavelength = 0.03;

        float sphereRCS  = RDF_RCSModel.CalculateSphereRCS(1.0, wavelength);
        float sphereDBsm = 10.0 * Math.Log10(sphereRCS);
        Print(string.Format("  Sphere  r=1m  : %.3f m^2  (%.1f dBsm)  - expect ~3.14 m^2", sphereRCS, sphereDBsm));

        float plateRCS   = RDF_RCSModel.CalculatePlateRCS(1.0, wavelength);
        float plateDBsm  = 10.0 * Math.Log10(plateRCS);
        Print(string.Format("  Plate   A=1m^2: %.0f m^2  (%.1f dBsm)", plateRCS, plateDBsm));

        float cylRCS     = RDF_RCSModel.CalculateCylinderRCS(2.0, 0.1, wavelength);
        float cylDBsm    = 10.0 * Math.Log10(cylRCS);
        Print(string.Format("  Cyl   L=2m r=0.1m: %.2f m^2  (%.1f dBsm)", cylRCS, cylDBsm));
        Print("");
    }

    // Propagation - verify FSPL at 10 km and 10 GHz.
    // Expected: FSPL ~= 152 dB.
    static void TestPropagation()
    {
        Print("[Test] Propagation Losses");

        float dist = 10000.0;
        float freq = 10000000000.0;

        float fspl = RDF_RadarPropagation.CalculateFSPL(dist, freq);
        Print(string.Format("  FSPL at 10km, 10GHz = %.1f dB  (expect ~152 dB)", fspl));

        float atm  = RDF_RadarPropagation.CalculateAtmosphericAttenuation(dist, freq, 20.0, 60.0);
        Print(string.Format("  Atmospheric atten (10km, 10GHz) = %.2f dB", atm));

        float rain = RDF_RadarPropagation.CalculateRainAttenuation(dist, freq, 10.0);
        Print(string.Format("  Rain atten (10km, 10GHz, 10mm/h) = %.2f dB", rain));
        Print("");
    }

    // EM wave parameters - band detection and wavelength calculation.
    static void TestEMWaveParameters()
    {
        Print("[Test] EM Wave Parameters");

        RDF_EMWaveParameters em = new RDF_EMWaveParameters();
        em.m_CarrierFrequency = 10000000000.0;
        em.CalculateWavelength();
        em.DetermineBand();
        Print(string.Format("  10 GHz -> lambda=%.4f m, band=%s  (expect 0.0300 m, X)", em.m_Wavelength, em.m_BandName));

        em.m_CarrierFrequency = 77000000000.0;
        em.CalculateWavelength();
        em.DetermineBand();
        Print(string.Format("  77 GHz -> lambda=%.5f m, band=%s  (expect ~0.00389 m, Ka)", em.m_Wavelength, em.m_BandName));
        Print("");
    }

    static void TestAntennaPattern()
    {
        Print("[Test] Antenna Pattern Approximation");
        RDF_EMWaveParameters em = new RDF_EMWaveParameters();
        em.m_AntennaGain = 30.0;
        em.m_BeamwidthAzimuth = 6.0;
        em.m_BeamwidthElevation = 6.0;

        float g0 = em.GetAntennaGainLinear(0.0, 0.0);
        float gHalf = em.GetAntennaGainLinear(3.0, 0.0); // at half-beam (3 deg)
        float gOff = em.GetAntennaGainLinear(30.0, 0.0);

        Print(string.Format("  Peak gain (linear) = %.3f", g0));
        Print(string.Format("  Gain at half-beam (3deg) = %.3f  (expect ~0.5*peak)", gHalf));
        Print(string.Format("  Gain at 30deg off-axis = %.6f (sidelobe floor applied)", gOff));
        Print("");
    }

    // Verify that CalculateMaxDetectionRange produces physically plausible results
    // and that detection falls off correctly with range.
    //
    // Reference geometry (X-band search radar):
    //   Pt = 1 kW, Gt = 30 dBi (~1000 linear), lambda = 3 cm,
    //   NF = 5 dB, BW = 1 MHz, SNR_min = 10 dB, L = 6 dB.
    //
    // Analytical cross-check:
    //   Pn  = k*T*B*F = 1.38e-23 * 290 * 1e6 * 3.16 ~= 1.26e-14 W
    //   Pr_min = Pn * 10^(10/10) ~= 1.26e-13 W
    //   Rmax = (Pt * Gt^2 * lambda^2 * sigma / ((4*pi)^3 * Pr_min * L))^0.25
    //        ~= 39 km for sigma = 1 m^2
    static void TestDetectionRange()
    {
        Print("[Test] Maximum Detection Range");

        float Pt       = 1000.0;
        float Gt       = RDF_RadarEquation.DBiToLinear(30.0);
        float lambda   = 0.03;
        float NF_dB    = 5.0;
        float BW       = 1000000.0;
        float Temp     = 290.0;
        float SNR_min  = 10.0;
        float L_dB     = 6.0;
        float L_lin    = RDF_RadarEquation.DBiToLinear(L_dB);

        float Pn      = RDF_RadarEquation.CalculateNoisePower(BW, NF_dB, Temp);
        float Pr_min  = Pn * RDF_RadarEquation.DBiToLinear(SNR_min);

        // Test three representative target sizes.
        float sigma1  = 0.01;   // small UAV / bird (~-20 dBsm)
        float sigma2  = 1.0;    // infantry / light vehicle (~0 dBsm)
        float sigma3  = 100.0;  // heavy vehicle / aircraft (~+20 dBsm)

        float Rmax1   = RDF_RadarEquation.CalculateMaxDetectionRange(Pt, Gt, lambda, sigma1, Pr_min, L_lin);
        float Rmax2   = RDF_RadarEquation.CalculateMaxDetectionRange(Pt, Gt, lambda, sigma2, Pr_min, L_lin);
        float Rmax3   = RDF_RadarEquation.CalculateMaxDetectionRange(Pt, Gt, lambda, sigma3, Pr_min, L_lin);

        Print(string.Format("  sigma=0.01 m^2 (-20 dBsm): Rmax = %.1f km", Rmax1 / 1000.0));
        Print(string.Format("  sigma=1    m^2 (  0 dBsm): Rmax = %.1f km  (expect ~39 km)", Rmax2 / 1000.0));
        Print(string.Format("  sigma=100  m^2 (+20 dBsm): Rmax = %.1f km", Rmax3 / 1000.0));

        // Print all three ranges; verify manually that Rmax1 < Rmax2 < Rmax3.

        // 6 dB loss effect: Rmax shrinks by ~1.41x vs no-loss (R^4 law, L factor 4).
        float RmaxNoLoss = RDF_RadarEquation.CalculateMaxDetectionRange(Pt, Gt, lambda, sigma2, Pr_min, 1.0);
        float ratio      = RmaxNoLoss / Rmax2;
        Print(string.Format("  Rmax ratio (no_loss / with_loss=6dB) = %.3f  (expect ~1.41)", ratio));

        // SNR at exactly Rmax2 should sit near the detection threshold.
        float Pr_at_Rmax  = RDF_RadarEquation.CalculateReceivedPower(Pt, Gt, lambda, sigma2, Rmax2, L_lin);
        float SNR_at_Rmax = RDF_RadarEquation.CalculateSNR(Pr_at_Rmax, Pn, 1.0);
        Print(string.Format("  SNR at Rmax2     = %.2f dB  (expect ~%.1f dB)", SNR_at_Rmax, SNR_min));

        // SNR at half range should be ~12 dB higher (2^4=16, 10*log10(16)~12 dB).
        float halfRange = Rmax2 * 0.5;
        float Pr_half   = RDF_RadarEquation.CalculateReceivedPower(Pt, Gt, lambda, sigma2, halfRange, L_lin);
        float SNR_half  = RDF_RadarEquation.CalculateSNR(Pr_half, Pn, 1.0);
        float SNR_gain  = SNR_half - SNR_at_Rmax;
        Print(string.Format("  SNR at Rmax2/2   = %.2f dB  (gain = %.1f dB, expect ~12 dB)", SNR_half, SNR_gain));
        Print("");
    }

    // Run the complete test suite.
    static void RunAllTests()
    {
        Print("========================================");
        Print("  RDF Radar Physics Tests");
        Print("========================================");
        Print("");

        TestEMWaveParameters();
        TestAntennaPattern();
        TestPropagation();
        TestRCS();
        TestRadarEquation();
        TestSNR();
        TestDoppler();
        TestBlindSpeed();
        TestCFAR();
        TestOSCFAR();
        TestCFARAutoScale();
        TestOSCFARAutoScale();
        TestOSCFARCache();
        TestOSCFARFileLookup();
        TestDetectionRange();

        // Run EM voxel field PoC tests (Phase‑1/2)
        if (EMVoxelField) EMVoxelFieldTests.RunAllTests();

        Print("========================================");
        Print("  Tests complete.");
        Print("========================================");
    }
}
