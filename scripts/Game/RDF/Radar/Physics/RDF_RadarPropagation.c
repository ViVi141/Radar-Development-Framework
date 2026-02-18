// Electromagnetic wave propagation models for radar signal attenuation.
// All methods are static utility functions - no instantiation required.
class RDF_RadarPropagation
{
    void RDF_RadarPropagation() {}

    // Free-Space Path Loss (FSPL) - simplified Friis transmission equation.
    // Returns one-way path loss in dB.
    //   FSPL(dB) = 92.45 + 20*log10(d_km) + 20*log10(f_GHz)
    // Parameters:
    //   distance  - one-way range (m)
    //   frequency - carrier frequency (Hz)
    static float CalculateFSPL(float distance, float frequency)
    {
        if (distance <= 0 || frequency <= 0)
            return 0.0;

        float distanceKm   = distance  / 1000.0;
        float frequencyGHz = frequency / 1000000000.0;

        return 92.45 + (20.0 * Math.Log10(distanceKm)) + (20.0 * Math.Log10(frequencyGHz));
    }

    // Atmospheric gas attenuation - simplified ITU-R P.676 model.
    // Significant above 10 GHz; returns one-way attenuation in dB.
    // Parameters:
    //   distance    - one-way range (m)
    //   frequency   - carrier frequency (Hz)
    //   temperature - ambient temperature (deg C)
    //   humidity    - relative humidity (%)
    static float CalculateAtmosphericAttenuation(
        float distance,
        float frequency,
        float temperature,
        float humidity)
    {
        float frequencyGHz = frequency / 1000000000.0;

        // Negligible below 10 GHz.
        if (frequencyGHz < 10.0)
            return 0.0;

        // Oxygen absorption (simplified quadratic in GHz).
        float oxygenAbs = 0.01 * frequencyGHz * frequencyGHz;

        // Water-vapour absorption (linear in humidity).
        float waterAbs = (humidity / 100.0) * 0.05 * frequencyGHz;

        // Temperature correction: attenuation increases slightly in cold air.
        float tempFactor = 1.0 + (20.0 - temperature) * 0.002;
        tempFactor = Math.Clamp(tempFactor, 0.5, 2.0);

        float specificAtten = (oxygenAbs + waterAbs) * tempFactor; // dB/km
        return specificAtten * (distance / 1000.0);
    }

    // Rain attenuation - simplified ITU-R P.838 model.
    // Returns one-way attenuation in dB.
    // Parameters:
    //   distance  - one-way range (m)
    //   frequency - carrier frequency (Hz)
    //   rainRate  - precipitation rate (mm/h); 0 = dry
    static float CalculateRainAttenuation(float distance, float frequency, float rainRate)
    {
        if (rainRate <= 0)
            return 0.0;

        float frequencyGHz = frequency / 1000000000.0;

        // Empirical k and alpha coefficients (horizontal polarisation, simplified).
        float k     = 0.0001 * Math.Pow(frequencyGHz, 2.3);
        float alpha = 1.0 + 0.03 * Math.Log10(Math.Max(frequencyGHz, 0.01));

        // Specific attenuation: gamma = k * R^alpha  (dB/km).
        float specificAtten = k * Math.Pow(rainRate, alpha);

        return specificAtten * (distance / 1000.0);
    }

    // Total one-way propagation loss combining FSPL + atmospheric + rain (dB).
    static float CalculateTotalLoss(
        float distance,
        float frequency,
        float temperature,
        float humidity,
        float rainRate,
        bool enableAtmospheric)
    {
        float total = CalculateFSPL(distance, frequency);

        if (enableAtmospheric)
        {
            total += CalculateAtmosphericAttenuation(distance, frequency, temperature, humidity);
            total += CalculateRainAttenuation(distance, frequency, rainRate);
        }

        return total;
    }
}
