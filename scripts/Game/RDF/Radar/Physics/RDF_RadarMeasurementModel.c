// Extensible measurement / channel-error model on published observations.
// Called after CFAR and before Tracker / WLR. Does not mutate forward truth.
// Downstream mods should subclass and override SynthesizePlot (or SynthesizeAll).
class RDF_RadarMeasurementModel
{
    //------------------------------------------------------------------------------------------------
    // Batch entry used by the scanner. Default: detected plots only.
    void SynthesizeAll(
        array<ref RDF_RadarTarget> targets,
        vector radarOrigin,
        RDF_RadarHardware hardware,
        RDF_RadarSettings settings)
    {
        if (!targets || !hardware || !settings)
            return;

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t)
                continue;
            if (!t.m_Detected)
                continue;
            SynthesizePlot(t, radarOrigin, hardware, settings);
        }
    }

    //------------------------------------------------------------------------------------------------
    // Per-observation hook. Default delegates to built-in CRLB + quantization.
    // Override this in gameplay mods for σ floors, bias tables, weather, etc.
    void SynthesizePlot(
        RDF_RadarTarget target,
        vector radarOrigin,
        RDF_RadarHardware hardware,
        RDF_RadarSettings settings)
    {
        RDF_RadarMeasurement.Synthesize(target, radarOrigin, hardware, settings);
    }
}

//------------------------------------------------------------------------------------------------
// Explicit default type (same behaviour as the base). Useful when resetting
// Sensor.SetMeasurementModel after a custom model.
class RDF_RadarDefaultMeasurementModel : RDF_RadarMeasurementModel
{
}
