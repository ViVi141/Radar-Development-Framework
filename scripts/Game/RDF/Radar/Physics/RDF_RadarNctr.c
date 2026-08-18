// Non-cooperative target recognition from micro-Doppler observables.
// Does not use entity type / prefab identity. Offline: rdf_radar_nctr.py.

enum ERDF_RadarNctrClass
{
    RDF_NCTR_UNKNOWN,
    RDF_NCTR_FIXED,
    RDF_NCTR_ROTOR,
    RDF_NCTR_FAN
}

class RDF_RadarNctr
{
    static const float ROTOR_FRAC_MIN = 0.15;
    static const float FAN_FRAC_MAX = 0.149;
    static const float TIP_CUE_MS = 40.0;

    //------------------------------------------------------------------------------------------------
    static ERDF_RadarNctrClass Classify(
        float rotorTipMs,
        float rotorFrac,
        float fanTipMs,
        float fanFrac,
        bool sidebandUsed,
        float snrDb)
    {
        if (snrDb < 0.0)
            return ERDF_RadarNctrClass.RDF_NCTR_UNKNOWN;

        bool rotorCue = false;
        if (rotorTipMs > TIP_CUE_MS)
        {
            if (rotorFrac >= ROTOR_FRAC_MIN)
                rotorCue = true;
        }
        bool fanCue = false;
        if (fanTipMs > TIP_CUE_MS)
        {
            if (fanFrac > 0.0)
            {
                if (fanFrac < FAN_FRAC_MAX)
                    fanCue = true;
            }
        }

        if (rotorCue)
        {
            if (sidebandUsed)
                return ERDF_RadarNctrClass.RDF_NCTR_ROTOR;
            if (snrDb >= 6.0)
                return ERDF_RadarNctrClass.RDF_NCTR_ROTOR;
        }
        if (fanCue)
        {
            if (sidebandUsed)
                return ERDF_RadarNctrClass.RDF_NCTR_FAN;
            if (snrDb >= 8.0)
                return ERDF_RadarNctrClass.RDF_NCTR_FAN;
        }
        if (snrDb >= 4.0)
            return ERDF_RadarNctrClass.RDF_NCTR_FIXED;
        return ERDF_RadarNctrClass.RDF_NCTR_UNKNOWN;
    }

    //------------------------------------------------------------------------------------------------
    static float ClassifyConfidence(
        ERDF_RadarNctrClass nctrClass,
        bool sidebandUsed,
        float snrDb)
    {
        if (nctrClass == ERDF_RadarNctrClass.RDF_NCTR_UNKNOWN)
            return 0.08;

        float snrTerm = snrDb / 20.0;
        if (snrTerm < 0.0)
            snrTerm = 0.0;
        if (snrTerm > 1.0)
            snrTerm = 1.0;

        float conf = 0.35 + 0.45 * snrTerm;
        if (sidebandUsed)
            conf = conf + 0.2;
        if (nctrClass == ERDF_RadarNctrClass.RDF_NCTR_FIXED)
            conf = conf * 0.85;
        if (conf < 0.05)
            conf = 0.05;
        if (conf > 1.0)
            conf = 1.0;
        return conf;
    }

    //------------------------------------------------------------------------------------------------
    static void ApplyToSample(RDF_RadarTruthSample sample, RDF_RadarSettings settings)
    {
        if (!sample)
            return;
        if (!settings || !settings.m_EnableNctr)
        {
            sample.m_NctrClass = ERDF_RadarNctrClass.RDF_NCTR_UNKNOWN;
            sample.m_NctrConfidence = 0.0;
            return;
        }
        if (!sample.m_Detected)
        {
            sample.m_NctrClass = ERDF_RadarNctrClass.RDF_NCTR_UNKNOWN;
            sample.m_NctrConfidence = 0.0;
            return;
        }

        ERDF_RadarNctrClass cls = Classify(
            sample.m_RotorTipSpeedMs,
            sample.m_RotorRcsFraction,
            sample.m_FanTipSpeedMs,
            sample.m_FanRcsFraction,
            sample.m_RotorSidebandUsed,
            sample.m_SnrDb);
        sample.m_NctrClass = cls;
        sample.m_NctrConfidence = ClassifyConfidence(
            cls,
            sample.m_RotorSidebandUsed,
            sample.m_SnrDb);
    }

    //------------------------------------------------------------------------------------------------
    static float TrackQuality(
        int hitCount,
        int confirmHits,
        float snrDb,
        float residualM,
        float gateM,
        bool coasting)
    {
        float hits = hitCount;
        float need = confirmHits;
        if (need < 1.0)
            need = 1.0;
        float age = hits / need;
        if (age > 1.0)
            age = 1.0;

        float snrTerm = snrDb / 20.0;
        if (snrTerm < 0.0)
            snrTerm = 0.0;
        if (snrTerm > 1.0)
            snrTerm = 1.0;

        float resTerm = 1.0;
        if (gateM > 1.0)
        {
            if (residualM > 0.0)
                resTerm = 1.0 - (residualM / gateM);
        }
        if (resTerm < 0.0)
            resTerm = 0.0;
        if (resTerm > 1.0)
            resTerm = 1.0;

        float conf = 0.45 * age + 0.35 * snrTerm + 0.20 * resTerm;
        if (coasting)
            conf = conf * 0.6;
        if (conf < 0.0)
            conf = 0.0;
        if (conf > 1.0)
            conf = 1.0;
        return conf;
    }

    //------------------------------------------------------------------------------------------------
    static string ClassToShort(ERDF_RadarNctrClass nctrClass)
    {
        if (nctrClass == ERDF_RadarNctrClass.RDF_NCTR_ROTOR)
            return "rotor";
        if (nctrClass == ERDF_RadarNctrClass.RDF_NCTR_FAN)
            return "fan";
        if (nctrClass == ERDF_RadarNctrClass.RDF_NCTR_FIXED)
            return "fixed";
        return "?";
    }

    //------------------------------------------------------------------------------------------------
    // Elevation bias toward the surface (deg). Low AGL + water is strongest.
    static float GlintElevationBiasDeg(
        float aglM,
        int surfaceClass,
        float worldTimeS,
        float rangeM)
    {
        if (aglM < 0.0)
            return 0.0;
        if (rangeM < 50.0)
            return 0.0;

        float floorAgl = aglM;
        if (floorAgl < 5.0)
            floorAgl = 5.0;
        float amp = 18.0 / floorAgl;
        if (amp > 2.5)
            amp = 2.5;

        float surf = 0.35;
        if (surfaceClass == ERDF_DemSurfaceClass.RDF_DEM_SURF_WATER)
            surf = 1.0;
        else if (surfaceClass == ERDF_DemSurfaceClass.RDF_DEM_SURF_VEGETATION)
            surf = 0.15;

        float phase = worldTimeS * 1.7;
        float swing = 1.0 + 0.35 * Math.Sin(phase);
        return -amp * surf * swing;
    }

    //------------------------------------------------------------------------------------------------
    // Rain damps water σ⁰ (intensity 0..1). Does not replace atmospheric rain loss.
    static float RainWaterClutterScale(float rainIntensity)
    {
        float r = rainIntensity;
        if (r < 0.0)
            r = 0.0;
        if (r > 1.0)
            r = 1.0;
        float scale = 1.0 - 0.55 * r;
        if (scale < 0.35)
            scale = 0.35;
        return scale;
    }
}
