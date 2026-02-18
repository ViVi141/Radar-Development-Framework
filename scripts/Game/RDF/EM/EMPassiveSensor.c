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

    // Convenience: boolean detection against a linear-power sensitivity threshold (W)
    static bool IsDetected(vector pos, float sensitivityW, out float outPower, out vector outDir, out int outMask)
    {
        outPower = SamplePowerAtPosition(pos, outDir, outMask);
        return outPower > sensitivityW;
    }
}
