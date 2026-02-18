// Radar range equation and SNR calculations.
// Implements the monostatic radar equation in both linear and dB forms.
// All methods are static utility functions.
class RDF_RadarEquation
{
    void RDF_RadarEquation() {}

    // Monostatic radar equation (linear form).
    //   Pr = (Pt * Gt * Gr * lambda^2 * sigma) / ((4*PI)^3 * R^4 * L)
    // Parameters (all linear, not dB):
    //   transmitPower    - Pt (W)
    //   antennaGain      - Gt = Gr (dimensionless)
    //   wavelength       - lambda (m)
    //   rcs              - sigma (m^2)
    //   range            - R (m)
    //   systemLoss       - L (dimensionless >= 1)
    // Returns: received power Pr (W).
    static float CalculateReceivedPower(
        float transmitPower,
        float antennaGain,
        float wavelength,
        float rcs,
        float range,
        float systemLoss)
    {
        if (range <= 0 || rcs <= 0 || systemLoss <= 0)
            return 0.0;

        float fourPiCubed = Math.Pow(4.0 * Math.PI, 3.0);
        float rFourth     = Math.Pow(range, 4.0);

        float numerator   = transmitPower * antennaGain * antennaGain
                            * wavelength * wavelength * rcs;
        float denominator = fourPiCubed * rFourth * systemLoss;

        return numerator / denominator;
    }

    // Monostatic radar equation (dB form).
    //   Pr(dBm) = Pt(dBm) + 2*Gt(dBi) + 20*log10(lambda)
    //             + sigma(dBsm) - 30*log10(4*PI) - 40*log10(R) - L(dB)
    // Parameters:
    //   transmitPowerDBm - Pt (dBm)
    //   antennaGainDB    - Gt (dBi)
    //   wavelengthM      - lambda (m)
    //   rcsDBsm          - sigma (dBsm)
    //   rangeM           - R (m)
    //   systemLossDB     - L (dB)
    // Returns: received power (dBm).
    static float CalculateReceivedPowerDB(
        float transmitPowerDBm,
        float antennaGainDB,
        float wavelengthM,
        float rcsDBsm,
        float rangeM,
        float systemLossDB)
    {
        if (rangeM <= 0 || wavelengthM <= 0)
            return -999.0;

        float pr = transmitPowerDBm;
        pr += 2.0 * antennaGainDB;
        pr += 20.0 * Math.Log10(wavelengthM);
        pr += rcsDBsm;
        pr -= 30.0 * Math.Log10(4.0 * Math.PI);
        pr -= 40.0 * Math.Log10(rangeM);
        pr -= systemLossDB;

        return pr;
    }

    // Maximum detection range for a given minimum RCS.
    //   R_max = ( (Pt * Gt^2 * lambda^2 * sigma) / ((4*PI)^3 * Pr_min * L) )^(1/4)
    // Parameters (linear):
    //   transmitPower       - Pt (W)
    //   antennaGain         - Gt (dimensionless)
    //   wavelength          - lambda (m)
    //   minRCS              - sigma_min (m^2)
    //   receiverSensitivity - Pr_min (W)
    //   systemLoss          - L (dimensionless >= 1)
    // Returns: maximum detection range (m).
    static float CalculateMaxDetectionRange(
        float transmitPower,
        float antennaGain,
        float wavelength,
        float minRCS,
        float receiverSensitivity,
        float systemLoss)
    {
        if (receiverSensitivity <= 0 || systemLoss <= 0 || minRCS <= 0)
            return 0.0;

        float fourPiCubed = Math.Pow(4.0 * Math.PI, 3.0);

        float numerator   = transmitPower * antennaGain * antennaGain
                            * wavelength * wavelength * minRCS;
        float denominator = fourPiCubed * receiverSensitivity * systemLoss;

        if (denominator <= 0)
            return 0.0;

        return Math.Pow(numerator / denominator, 0.25);
    }

    // Signal-to-noise ratio (dB).
    //   SNR = 10 * log10( (Pr * N_i) / Pn )
    // Parameters:
    //   receivedPower    - Pr (W)
    //   noisePower       - Pn (W)
    //   integrationGain  - N_i (dimensionless >= 1; pulse integration benefit)
    // Returns: SNR (dB).
    static float CalculateSNR(
        float receivedPower,
        float noisePower,
        float integrationGain)
    {
        if (noisePower <= 0)
            return 100.0;

        float snrLinear = (receivedPower * integrationGain) / noisePower;

        if (snrLinear <= 0)
            return -100.0;

        return 10.0 * Math.Log10(snrLinear);
    }

    // Thermal noise power at the receiver input.
    //   Pn = k * T * B * F
    // Parameters:
    //   bandwidth      - B (Hz)
    //   noiseFigureDB  - F (dB)
    //   temperature    - T (K); standard reference = 290 K
    // Returns: noise power (W).
    static float CalculateNoisePower(
        float bandwidth,
        float noiseFigureDB,
        float temperature)
    {
        // Boltzmann constant k = 1.38064852e-23 J/K
        // Written as decimal to avoid scientific notation parsing issues.
        const float BOLTZMANN = 0.0000000000000000000000138064852;

        float thermalNoise   = BOLTZMANN * temperature * bandwidth;
        float noiseFigureLin = Math.Pow(10.0, noiseFigureDB / 10.0);

        return thermalNoise * noiseFigureLin;
    }

    // Convert transmit power from dBm to Watts.
    static float DBmToWatts(float dBm)
    {
        return Math.Pow(10.0, (dBm - 30.0) / 10.0);
    }

    // Convert power in Watts to dBm.
    static float WattsToDBm(float watts)
    {
        if (watts <= 0)
            return -999.0;
        return 10.0 * Math.Log10(watts) + 30.0;
    }

    // Convert linear gain to dBi.
    static float LinearToDBi(float linearGain)
    {
        if (linearGain <= 0)
            return -999.0;
        return 10.0 * Math.Log10(linearGain);
    }

    // Convert antenna gain from dBi to linear.
    static float DBiToLinear(float gainDB)
    {
        return Math.Pow(10.0, gainDB / 10.0);
    }
}
