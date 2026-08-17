// Absolute radar equation helpers + σ⁰ clutter model (matches Python tools).
// Pr = Pt * Gt * Gr * λ² * σ / ((4π)³ * R⁴ * L)
// Surface EM params come from RDF_RadarSurfaceTable (RadarData/SurfaceTable.json).
class RDF_RadarClutterModel
{
    static const float MIN_GRAZING_RAD = 0.00872664625; // 0.5 deg
    static const float MAX_SIGMA0 = 10.0;
    static const float C_LIGHT = 299792458.0;
    static const float FOUR_PI = 12.5663706144;

    //------------------------------------------------------------------------------------------------
    // σ⁰ (linear, m²/m²) at reference grazing for ERDF_DemSurfaceClass.
    static float GetSigma0Ref(int surfaceClass)
    {
        return RDF_RadarSurfaceTable.GetSigma0RefLin(surfaceClass);
    }

    //------------------------------------------------------------------------------------------------
    static float GetSigma0Exponent(int surfaceClass)
    {
        return RDF_RadarSurfaceTable.GetGammaK(surfaceClass);
    }

    //------------------------------------------------------------------------------------------------
    // Constant-gamma: σ⁰(θ) = σ⁰_ref * (sinθ / sinθ_ref)^k * clutter_scale
    static float GetSigma0(int surfaceClass, float grazingRad)
    {
        float theta = grazingRad;
        if (theta < MIN_GRAZING_RAD)
            theta = MIN_GRAZING_RAD;
        if (theta > (Math.PI * 0.5))
            theta = Math.PI * 0.5;

        float refValue = GetSigma0Ref(surfaceClass);
        float exponent = GetSigma0Exponent(surfaceClass);
        float thetaRef = RDF_RadarSurfaceTable.GetThetaRefRad();
        float ratio = Math.Sin(theta) / Math.Sin(thetaRef);
        if (ratio < 0.000001)
            ratio = 0.000001;

        float value = refValue * Math.Pow(ratio, exponent);
        value = value * RDF_RadarSurfaceTable.GetClutterScale(surfaceClass);
        if (value > MAX_SIGMA0)
            value = MAX_SIGMA0;
        return value;
    }

    //------------------------------------------------------------------------------------------------
    static float GetCanopySigma0(float grazingRad)
    {
        return GetSigma0(ERDF_DemSurfaceClass.RDF_DEM_SURF_VEGETATION, grazingRad);
    }

    //------------------------------------------------------------------------------------------------
    // One-way foliage/volume attenuation from SurfaceTable (dB/km).
    static float GetSurfaceAttenuationDbPerKm(int surfaceClass)
    {
        return RDF_RadarSurfaceTable.GetAttenuationDbPerKm(surfaceClass);
    }

    //------------------------------------------------------------------------------------------------
    // DbToLin / LinToDb helpers.
    static float DbToLin(float db)
    {
        return Math.Pow(10.0, db / 10.0);
    }

    //------------------------------------------------------------------------------------------------
    static float LinToDb(float value)
    {
        if (value <= 0.000000000000000000000000000001)
            return -300.0;
        return 10.0 * Math.Log10(value);
    }

    //------------------------------------------------------------------------------------------------
    // One-way Gaussian power pattern, 0.5 at half the full HPBW.
    static float GaussianBeamGain(float offsetDeg, float beamwidthDeg)
    {
        if (beamwidthDeg <= 0.000001)
            return 1.0;
        float halfWidth = beamwidthDeg * 0.5;
        float ratio = offsetDeg / halfWidth;
        // exp(-ln(2) * ratio^2) == 0.5^(ratio^2); Enforce has no Math.Exp.
        return Math.Pow(0.5, ratio * ratio);
    }

    //------------------------------------------------------------------------------------------------
    // Two-way power floor from one-way sidelobe level (dB).
    static float TwoWaySidelobeFloor(float sidelobeLevelDb)
    {
        float oneWay = DbToLin(sidelobeLevelDb);
        if (oneWay < 0.0)
            oneWay = 0.0;
        return oneWay * oneWay;
    }

    //------------------------------------------------------------------------------------------------
    static float ApplyTwoWaySidelobeFloor(float twoWayGain, float sidelobeLevelDb)
    {
        float floorTwoWay = TwoWaySidelobeFloor(sidelobeLevelDb);
        if (twoWayGain < floorTwoWay)
            return floorTwoWay;
        return twoWayGain;
    }

    //------------------------------------------------------------------------------------------------
    static float GetStrongestBeamGain(
        RDF_RadarHardware hardware,
        float azimuthOffsetDeg,
        float elevationDeg,
        out string outBeamName)
    {
        outBeamName = "";
        if (!hardware || !hardware.m_ElevationBeams)
            return 0.0;

        float azimuthGain;
        if (RDF_RadarPatternLut.IsEnabled())
        {
            RDF_RadarPatternLut.EnsureReady(hardware);
            azimuthGain = RDF_RadarPatternLut.EvalOneWay(azimuthOffsetDeg);
        }
        else
        {
            azimuthGain = GaussianBeamGain(
                azimuthOffsetDeg,
                hardware.m_AzimuthBeamwidthDeg);
        }
        float strongest = 0.0;

        for (int i = 0; i < hardware.m_ElevationBeams.Count(); i++)
        {
            RDF_RadarElevationBeam beam = hardware.m_ElevationBeams.Get(i);
            if (!beam)
                continue;
            float elevationGain = GaussianBeamGain(
                elevationDeg - beam.m_BoresightDeg,
                beam.m_BeamwidthDeg);
            float relativeGain = DbToLin(beam.m_RelativeGainDb);
            float oneWay = azimuthGain * elevationGain * relativeGain;
            float twoWay = oneWay * oneWay;
            if (twoWay > strongest)
            {
                strongest = twoWay;
                outBeamName = beam.m_Name;
            }
        }
        return strongest;
    }

    //------------------------------------------------------------------------------------------------
    // Engineering polarization match (linear ≤1). Not a Mueller/Jones solver.
    // Vehicles depolarize more; projectiles keep match (corner-like); circular
    // trades some target match for rain-clutter rejection elsewhere.
    static float PolarizationMatchFactor(
        ERDF_RadarPolarization mode,
        ERDF_RadarTargetType targetType)
    {
        if (mode == ERDF_RadarPolarization.RDF_POL_CIRCULAR)
        {
            if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
                return 0.95;
            if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE)
                return 0.75;
            if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
                return 0.80;
            return 0.85;
        }

        // H or V linear (same table; cross-pol trim is m_PolarizationFactor).
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return 1.0;
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE)
            return 0.65;
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return 0.85;
        return 0.90;
    }

    //------------------------------------------------------------------------------------------------
    // Circular polarization rain-clutter rejection (linear scale on DEM clutter).
    static float PolarizationClutterFactor(
        ERDF_RadarPolarization mode,
        float rainDbPerKmOneWay)
    {
        if (mode != ERDF_RadarPolarization.RDF_POL_CIRCULAR)
            return 1.0;
        if (rainDbPerKmOneWay < 0.05)
            return 1.0;
        // Strong rain → keep ~0.2 of wet-ground clutter power.
        return 0.20;
    }

    //------------------------------------------------------------------------------------------------
    static float ReceivedPowerW(
        float peakPowerW,
        float antennaGainDbi,
        float frequencyHz,
        float systemLossDb,
        float sigmaM2,
        float rangeM,
        float patternTwoWay)
    {
        if (rangeM < 1.0)
            rangeM = 1.0;
        if (sigmaM2 <= 0.0)
            return 0.0;
        if (patternTwoWay <= 0.0)
            return 0.0;
        if (frequencyHz <= 0.0)
            return 0.0;

        float wavelength = C_LIGHT / frequencyHz;
        float gainLin = DbToLin(antennaGainDbi);
        float lossLin = DbToLin(systemLossDb);
        float denom = FOUR_PI * FOUR_PI * FOUR_PI * lossLin;
        float radarConst = peakPowerW * gainLin * gainLin * wavelength * wavelength / denom;
        return radarConst * patternTwoWay * sigmaM2 / (rangeM * rangeM * rangeM * rangeM);
    }

    //------------------------------------------------------------------------------------------------
    // One-way ESM / RWR receive (Friis): Pr = Pt Gt Gr λ² / ((4π)² R² L) * strength * pattern.
    // patternOneWay is the receiver antenna power pattern (not two-way radar).
    static float ReceivedPowerEsmW(
        float emitPeakPowerW,
        float emitAntennaGainDbi,
        float emitFrequencyHz,
        float receiveAntennaGainDbi,
        float systemLossDb,
        float rangeM,
        float emitStrength,
        float patternOneWay)
    {
        if (rangeM < 1.0)
            rangeM = 1.0;
        if (emitPeakPowerW <= 0.0)
            return 0.0;
        if (emitFrequencyHz <= 0.0)
            return 0.0;
        if (patternOneWay <= 0.0)
            return 0.0;
        if (emitStrength <= 0.0)
            return 0.0;

        float wavelength = C_LIGHT / emitFrequencyHz;
        float gt = DbToLin(emitAntennaGainDbi);
        float gr = DbToLin(receiveAntennaGainDbi);
        float lossLin = DbToLin(systemLossDb);
        float denom = FOUR_PI * FOUR_PI * lossLin;
        float friis =
            emitPeakPowerW * gt * gr * wavelength * wavelength / denom;
        return friis * emitStrength * patternOneWay
            / (rangeM * rangeM);
    }

    //------------------------------------------------------------------------------------------------
    // Clear-air one-way loss near sea level (engineering fit, dB/km).
    static float AtmosphericOneWayDbPerKm(float frequencyHz)
    {
        float fGhz = frequencyHz / 1000000000.0;
        if (fGhz < 1.0)
            return 0.003;
        if (fGhz < 4.0)
            return 0.007;
        if (fGhz < 8.0)
            return 0.012;
        if (fGhz < 12.0)
            return 0.02;
        return 0.04;
    }

    //------------------------------------------------------------------------------------------------
    // Two-way atmospheric+rain loss as a linear factor (>=1) to divide received power by.
    static float AtmosphericLossLinear(
        float rangeM,
        float atmDbPerKmOneWay,
        float rainDbPerKmOneWay)
    {
        if (rangeM < 0.0)
            rangeM = 0.0;
        float alpha = atmDbPerKmOneWay + rainDbPerKmOneWay;
        if (alpha <= 0.0)
            return 1.0;
        float lossDb = 2.0 * alpha * (rangeM / 1000.0);
        float factor = DbToLin(lossDb);
        if (factor < 1.0)
            return 1.0;
        return factor;
    }

    //------------------------------------------------------------------------------------------------
    // Two-pulse MTI power transfer, peak-normalized to 1 at fd = PRF/2.
    static float MtiTwoPulseGain(float dopplerHz, float prfHz)
    {
        if (prfHz <= 0.0)
            return 1.0;
        float phase = Math.PI * dopplerHz / prfHz;
        float s = Math.Sin(phase);
        return s * s;
    }

    //------------------------------------------------------------------------------------------------
    // Three-pulse binomial canceller, peak-normalized: sin^4(π fd/PRF).
    static float MtiThreePulseGain(float dopplerHz, float prfHz)
    {
        float g2 = MtiTwoPulseGain(dopplerHz, prfHz);
        return g2 * g2;
    }

    //------------------------------------------------------------------------------------------------
    // Classic canceller gain for TwoPulse / ThreePulse (MTD returns 1 — use bank path).
    static float MtiCancellerGain(ERDF_MtiMode mode, float dopplerHz, float prfHz)
    {
        if (mode == ERDF_MtiMode.RDF_MTI_THREE_PULSE)
            return MtiThreePulseGain(dopplerHz, prfHz);
        return MtiTwoPulseGain(dopplerHz, prfHz);
    }

    //------------------------------------------------------------------------------------------------
    // Power-weighted max canceller gain over Doppler spectrum lines and PRF list.
    // outPeakFd is the fd of the winning line (first PRF that achieves the max).
    static float MaxMtiCancellerSpectrumGain(
        array<float> dopplerHzLines,
        array<float> powers,
        array<float> prfHzList,
        ERDF_MtiMode mode,
        out float outPeakFd)
    {
        outPeakFd = 0.0;
        if (!dopplerHzLines || dopplerHzLines.Count() == 0)
            return 1.0;
        if (!prfHzList || prfHzList.Count() == 0)
            return 1.0;

        float powerSum = 0.0;
        int nLines = dopplerHzLines.Count();
        for (int i = 0; i < nLines; i++)
        {
            float w = 1.0;
            if (powers && i < powers.Count())
                w = powers.Get(i);
            if (w < 0.0)
                w = 0.0;
            powerSum = powerSum + w;
        }
        if (powerSum <= 0.0)
            powerSum = 1.0;

        float best = -1.0;
        float bestFd = dopplerHzLines.Get(0);
        for (int p = 0; p < prfHzList.Count(); p++)
        {
            float prf = prfHzList.Get(p);
            if (prf < 1.0)
                continue;
            float channel = 0.0;
            float fdAcc = 0.0;
            float fdW = 0.0;
            for (int j = 0; j < nLines; j++)
            {
                float fd = dopplerHzLines.Get(j);
                float wj = 1.0;
                if (powers && j < powers.Count())
                    wj = powers.Get(j);
                if (wj < 0.0)
                    wj = 0.0;
                float g = MtiCancellerGain(mode, fd, prf);
                float contrib = wj * g;
                channel = channel + contrib;
                fdAcc = fdAcc + fd * contrib;
                fdW = fdW + contrib;
            }
            float normalized = channel / powerSum;
            if (normalized > best)
            {
                best = normalized;
                if (fdW > 0.0)
                    bestFd = fdAcc / fdW;
                else
                    bestFd = dopplerHzLines.Get(0);
            }
        }
        outPeakFd = bestFd;
        if (best < 0.0)
            best = 0.0;
        return best;
    }

    //------------------------------------------------------------------------------------------------
    // Wrap normalized Doppler (fd/PRF) into [-0.5, 0.5).
    static float WrapNormalizedDoppler(float normFd)
    {
        float x = normFd;
        while (x >= 0.5)
            x = x - 1.0;
        while (x < -0.5)
            x = x + 1.0;
        return x;
    }

    //------------------------------------------------------------------------------------------------
    // DFT / MTD bin power response |H_k(fd)|², peak-normalized to 1 at bin centre.
    // binCount pulses, rectangular window (Dirichlet kernel).
    static float MtdBinPowerGain(
        float dopplerHz,
        float prfHz,
        int binIndex,
        int binCount)
    {
        if (prfHz <= 0.0)
            return 1.0;
        if (binCount < 2)
            return 1.0;
        if (binIndex < 0)
            return 0.0;
        if (binIndex >= binCount)
            return 0.0;

        float normFd = WrapNormalizedDoppler(dopplerHz / prfHz);
        // binIndex / binCount is int/int (truncating) in Enforce — every bin
        // centre would collapse to 0. Force float division.
        float binCenter = binIndex / (float)binCount;
        if (binCenter >= 0.5)
            binCenter = binCenter - 1.0;
        float delta = WrapNormalizedDoppler(normFd - binCenter);
        float absDelta = delta;
        if (absDelta < 0.0)
            absDelta = -absDelta;
        if (absDelta < 0.0000001)
            return 1.0;

        float n = binCount;
        float num = Math.Sin(Math.PI * n * delta);
        float den = n * Math.Sin(Math.PI * delta);
        if (den < 0.0)
            den = -den;
        if (den < 0.0000001)
            return 0.0;
        if (num < 0.0)
            num = -num;
        float h = num / den;
        return h * h;
    }

    //------------------------------------------------------------------------------------------------
    // max_k |H_k(fd)|² for a single Doppler line. outBin is the winning index.
    static float MaxMtdBinGain(
        float dopplerHz,
        float prfHz,
        int binCount,
        out int outBin)
    {
        outBin = 0;
        if (prfHz <= 0.0 || binCount < 2)
            return 1.0;

        float best = -1.0;
        int bestBin = 0;
        for (int k = 0; k < binCount; k++)
        {
            float g = MtdBinPowerGain(dopplerHz, prfHz, k, binCount);
            if (g > best)
            {
                best = g;
                bestBin = k;
            }
        }
        outBin = bestBin;
        if (best < 0.0)
            best = 0.0;
        return best;
    }

    //------------------------------------------------------------------------------------------------
    // Peak over a short Doppler spectrum (body line + optional micro-Doppler lines).
    // Picks the bin with best signal / clutter-gain score so weak rotor sidebands in
    // non-zero bins beat a strong body line trapped in the clutter map cell.
    // Returns the fraction of total spectral power that lands in the winning bin.
    static float MaxMtdSpectrumGain(
        array<float> dopplerHzLines,
        array<float> powers,
        float prfHz,
        int binCount,
        float mtiClutterFloor,
        float mtdClutterLeakage,
        out int outBin,
        out float outPeakDopplerHz)
    {
        outBin = 0;
        outPeakDopplerHz = 0.0;
        if (!dopplerHzLines || dopplerHzLines.Count() == 0)
            return 1.0;
        if (prfHz <= 0.0 || binCount < 2)
            return 1.0;

        float bestScore = -1.0;
        float bestNormalized = 0.0;
        int bestBin = 0;
        float bestFd = dopplerHzLines.Get(0);
        float powerSum = 0.0;
        int n = dopplerHzLines.Count();
        for (int i = 0; i < n; i++)
        {
            float p = 1.0;
            if (powers && i < powers.Count())
                p = powers.Get(i);
            if (p < 0.0)
                p = 0.0;
            powerSum = powerSum + p;
        }
        if (powerSum <= 0.0)
            powerSum = 1.0;

        for (int k = 0; k < binCount; k++)
        {
            float channelPower = 0.0;
            float channelFd = 0.0;
            float channelFdWeight = 0.0;
            for (int i = 0; i < n; i++)
            {
                float p = 1.0;
                if (powers && i < powers.Count())
                    p = powers.Get(i);
                if (p <= 0.0)
                    continue;
                float fd = dopplerHzLines.Get(i);
                float g = MtdBinPowerGain(fd, prfHz, k, binCount);
                float contrib = p * g;
                channelPower = channelPower + contrib;
                channelFd = channelFd + fd * contrib;
                channelFdWeight = channelFdWeight + contrib;
            }
            float normalized = channelPower / powerSum;
            float clutterGain = MtdClutterBinGain(
                k, binCount, mtiClutterFloor, mtdClutterLeakage);
            // Thermal pedestal so empty bins don't win on tiny leakage alone.
            float scoreDenom = clutterGain + 0.0000001;
            float score = normalized / scoreDenom;
            if (score > bestScore)
            {
                bestScore = score;
                bestNormalized = normalized;
                bestBin = k;
                if (channelFdWeight > 0.0)
                    bestFd = channelFd / channelFdWeight;
                else
                    bestFd = dopplerHzLines.Get(0);
            }
        }

        outBin = bestBin;
        outPeakDopplerHz = bestFd;
        if (bestNormalized < 0.0)
            bestNormalized = 0.0;
        return bestNormalized;
    }

    //------------------------------------------------------------------------------------------------
    // Clutter attenuation into a Doppler bin. Zero-speed bin uses mtiClutterFloor;
    // others use leakage only (thermal+leakage path in PhysicalDetect).
    static float MtdClutterBinGain(
        int binIndex,
        int binCount,
        float mtiClutterFloor,
        float mtdClutterLeakage)
    {
        if (binCount < 2)
            return mtiClutterFloor;

        // Near-zero bins: index 0 and (for even N) the Nyquist wrap neighbour is
        // still "moving"; only bin 0 is treated as the clutter map cell.
        if (binIndex == 0)
        {
            float floorGain = mtiClutterFloor;
            if (floorGain < 0.000001)
                floorGain = 0.000001;
            return floorGain;
        }

        float leak = mtdClutterLeakage;
        if (leak < 0.000000001)
            leak = 0.000000001;
        return leak;
    }

    //------------------------------------------------------------------------------------------------
    static float DopplerHz(float radialSpeedMs, float wavelengthM)
    {
        if (wavelengthM <= 0.0)
            return 0.0;
        return 2.0 * radialSpeedMs / wavelengthM;
    }

    //------------------------------------------------------------------------------------------------
    // Horizontal-pol Fresnel reflection coefficient at grazing angle (radians).
    // epsR = relative permittivity from SurfaceTable (≈15 land, ≈80 sea).
    static float FresnelReflectionCoeffH(float epsR, float grazingRad)
    {
        float eps = epsR;
        if (eps < 1.0)
            eps = 1.0;
        float theta = grazingRad;
        if (theta < 0.001)
            theta = 0.001;
        if (theta > (Math.PI * 0.5))
            theta = Math.PI * 0.5;

        float s = Math.Sin(theta);
        float c = Math.Cos(theta);
        float inside = eps - c * c;
        if (inside < 0.000001)
            inside = 0.000001;
        float root = Math.Sqrt(inside);
        float denom = s + root;
        if (denom < 0.000001)
            denom = 0.000001;
        return (s - root) / denom;
    }

    //------------------------------------------------------------------------------------------------
    // Roughness damping on |Γ| (no Math.Exp: exp(-x) = 0.5^(x/ln2)).
    static float RoughnessDampReflectionAbs(float gammaAbs, float roughness, float grazingRad)
    {
        float g = gammaAbs;
        if (g < 0.0)
            g = -g;
        float r = roughness;
        if (r < 0.0)
            r = 0.0;
        float s = Math.Sin(grazingRad);
        if (s < 0.0)
            s = 0.0;
        float x = 8.0 * r * s;
        if (x <= 0.0)
            return g;
        // ln(2) ≈ 0.693147
        float damp = Math.Pow(0.5, x / 0.693147);
        if (damp < 0.05)
            damp = 0.05;
        if (damp > 1.0)
            damp = 1.0;
        return g * damp;
    }

    //------------------------------------------------------------------------------------------------
    // Clear-LOS two-ray multipath power factor (direct + specular ground bounce).
    // Aligns with tools/dem/rdf_radar_channel.MultipathModel — not a waveform sim.
    static float TwoRayMultipathFactor(
        float wavelengthM,
        float rangeM,
        float radarHeightAglM,
        float targetHeightAglM,
        float reflectionCoeff,
        float maxHeightM,
        float minFactor,
        float maxFactor)
    {
        if (wavelengthM <= 0.0)
            return 1.0;
        if (rangeM < 1.0)
            return 1.0;
        if (radarHeightAglM < 1.0)
            return 1.0;
        if (targetHeightAglM < 1.0)
            return 1.0;
        if (maxHeightM > 0.0)
        {
            if (targetHeightAglM > maxHeightM)
                return 1.0;
        }

        float delta = 2.0 * radarHeightAglM * targetHeightAglM / rangeM;
        float phase = 2.0 * Math.PI * delta / wavelengthM;
        float real = 1.0 + reflectionCoeff * Math.Cos(phase);
        float imag = reflectionCoeff * Math.Sin(phase);
        float factor = real * real + imag * imag;

        float lo = minFactor;
        if (lo < 0.01)
            lo = 0.01;
        float hi = maxFactor;
        if (hi < lo)
            hi = lo;
        if (factor < lo)
            factor = lo;
        if (factor > hi)
            factor = hi;
        return factor;
    }

    //------------------------------------------------------------------------------------------------
    // Effective earth radius (m): k * 6371000. k=4/3 ≈ standard refraction.
    static float EffectiveEarthRadiusM(float earthRadiusFactor)
    {
        float k = earthRadiusFactor;
        if (k < 0.5)
            k = 0.5;
        if (k > 4.0)
            k = 4.0;
        return k * 6371000.0;
    }

    //------------------------------------------------------------------------------------------------
    // Geometric radio horizon for two terminals over smooth k-Earth (m).
    static float RadioHorizonRangeM(
        float radarHeightAglM,
        float targetHeightAglM,
        float earthRadiusFactor)
    {
        float re = EffectiveEarthRadiusM(earthRadiusFactor);
        float hr = radarHeightAglM;
        if (hr < 0.0)
            hr = 0.0;
        float ht = targetHeightAglM;
        if (ht < 0.0)
            ht = 0.0;
        return Math.Sqrt(2.0 * re * hr) + Math.Sqrt(2.0 * re * ht);
    }

    //------------------------------------------------------------------------------------------------
    // Soft power factor beyond radio horizon (1 inside, rolls off outside).
    static float HorizonSoftFactor(float rangeM, float horizonM)
    {
        if (horizonM <= 1.0)
            return 1.0;
        if (rangeM <= horizonM)
            return 1.0;
        float over = (rangeM - horizonM) / horizonM;
        if (over < 0.0)
            over = 0.0;
        // 1 / (1 + 4*over^2) — smooth, no hard cliff.
        float denom = 1.0 + 4.0 * over * over;
        float f = 1.0 / denom;
        if (f < 0.02)
            f = 0.02;
        return f;
    }

    //------------------------------------------------------------------------------------------------
    // Apparent elevation bias (deg) from k-Earth ray vs flat geometry.
    // Positive = target appears higher than geometric head-up angle.
    static float RefractionElevationBiasDeg(float rangeM, float earthRadiusFactor)
    {
        if (rangeM < 1.0)
            return 0.0;
        float re = EffectiveEarthRadiusM(earthRadiusFactor);
        if (re < 1.0)
            return 0.0;
        // δ ≈ R / (2 Re) rad for standard curved-earth correction remnant.
        float biasRad = rangeM / (2.0 * re);
        return biasRad * 57.2957795;
    }

    //------------------------------------------------------------------------------------------------
    // Fold true range into [0, R_unamb).
    static float FoldRangeAmbiguous(float rangeM, float unambiguousRangeM)
    {
        if (unambiguousRangeM <= 1.0)
            return rangeM;
        if (rangeM < 0.0)
            return 0.0;
        float folded = rangeM - Math.Floor(rangeM / unambiguousRangeM) * unambiguousRangeM;
        if (folded < 0.0)
            folded = folded + unambiguousRangeM;
        return folded;
    }

    //------------------------------------------------------------------------------------------------
    // Fold Doppler into [-PRF/2, PRF/2).
    static float FoldDopplerAmbiguous(float dopplerHz, float prfHz)
    {
        if (prfHz <= 0.0)
            return dopplerHz;
        float half = prfHz * 0.5;
        float x = dopplerHz;
        while (x >= half)
            x = x - prfHz;
        while (x < -half)
            x = x + prfHz;
        return x;
    }
}
