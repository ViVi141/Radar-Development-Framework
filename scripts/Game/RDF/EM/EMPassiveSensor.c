class EMPassiveSensor
{
    void EMPassiveSensor() {}

    // Return linear power (W) at position and fill direction/freq mask
    static float SamplePowerAtPosition(vector pos, out vector outMainDir, out int outFreqMask)
    {
        EMVoxelField ev = EMVoxelField.GetInstance();
        if (!ev)
        {
            outMainDir = Vector(0,0,0);
            outFreqMask = 0;
            return 0.0;
        }
        return ev.GetPowerAt(pos, outMainDir, outFreqMask);
    }

    // Sample signal descriptor metadata at position (PRF, pulse width, waveform type, carrier)
    // Returns true if a voxel cell with descriptors was present.
    static bool SampleSignalDescriptorAt(vector pos, out float outEstPRF, out float outLastPulseWidth, out int outWaveformType, out float outLastFrequency)
    {
        EMVoxelField ev = EMVoxelField.GetInstance();
        outEstPRF = 0.0;
        outLastPulseWidth = 0.0;
        outWaveformType = -1;
        outLastFrequency = 0.0;
        if (!ev)
            return false;
        return ev.GetSignalDescriptorAt(pos, out outEstPRF, out outLastPulseWidth, out outWaveformType, out outLastFrequency);
    }

    // Convenience: boolean detection against a linear-power sensitivity threshold (W)
    static bool IsDetected(vector pos, float sensitivityW, out float outPower, out vector outDir, out int outMask)
    {
        outPower = SamplePowerAtPosition(pos, outDir, outMask);
        return outPower > sensitivityW;
    }
}
