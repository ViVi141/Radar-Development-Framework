// Doppler effect processor for radar velocity estimation.
// Computes frequency shift, radial velocity, and MTI filter decisions.
// All methods are static utility functions.
class RDF_DopplerProcessor
{
    void RDF_DopplerProcessor() {}

    // Two-way Doppler frequency shift.
    //   fd = (2 * vr * f0) / c
    // Parameters:
    //   radialVelocity   - vr (m/s); positive = target approaching radar.
    //   carrierFrequency - f0 (Hz)
    // Returns: Doppler shift fd (Hz).
    static float CalculateDopplerShift(float radialVelocity, float carrierFrequency)
    {
        const float SPEED_OF_LIGHT = 299792458.0;

        return (2.0 * radialVelocity * carrierFrequency) / SPEED_OF_LIGHT;
    }

    // Radial (line-of-sight) velocity of a target relative to the radar.
    // Positive value = target approaching; negative = target receding.
    // Falls back to zero when either entity lacks a Physics component.
    static float CalculateRadialVelocity(IEntity radarEntity, IEntity targetEntity)
    {
        if (!radarEntity || !targetEntity)
            return 0.0;

        vector radarPos  = radarEntity.GetOrigin();
        vector targetPos = targetEntity.GetOrigin();

        // Line-of-sight unit vector from radar to target.
        vector los  = (targetPos - radarPos);
        float  dist = los.Length();
        if (dist < 0.001)
            return 0.0;
        los = los / dist;

        // Target velocity.
        vector targetVel = vector.Zero;
        Physics targetPhys = targetEntity.GetPhysics();
        if (targetPhys)
            targetVel = targetPhys.GetVelocity();

        // Radar platform velocity.
        vector radarVel = vector.Zero;
        Physics radarPhys = radarEntity.GetPhysics();
        if (radarPhys)
            radarVel = radarPhys.GetVelocity();

        // Relative velocity projected onto LOS - positive = closing.
        return vector.Dot(targetVel - radarVel, los);
    }

    // Compute radial velocity from two known world-space velocity vectors.
    // Useful when entity Physics components are unavailable.
    static float CalculateRadialVelocityFromVectors(
        vector radarPos,
        vector targetPos,
        vector radarVel,
        vector targetVel)
    {
        vector los  = (targetPos - radarPos);
        float  dist = los.Length();
        if (dist < 0.001)
            return 0.0;
        los = los / dist;

        return vector.Dot(targetVel - radarVel, los);
    }

    // Target velocity from Doppler frequency shift (inverse of CalculateDopplerShift).
    //   vr = fd * c / (2 * f0)
    static float VelocityFromDoppler(float dopplerFrequency, float carrierFrequency)
    {
        const float SPEED_OF_LIGHT = 299792458.0;

        if (carrierFrequency <= 0)
            return 0.0;

        return (dopplerFrequency * SPEED_OF_LIGHT) / (2.0 * carrierFrequency);
    }

    // Moving Target Indicator (MTI) decision.
    // Returns true when |fd| > minDopplerHz, suppressing stationary clutter.
    static bool IsMovingTarget(float dopplerFrequency, float minDopplerHz)
    {
        return Math.AbsFloat(dopplerFrequency) > minDopplerHz;
    }

    // Maximum unambiguous velocity interval +/- v_max.
    //   v_max = PRF * c / (4 * f0)
    // Velocities outside +/-v_max fold back (aliasing).
    static float MaxUnambiguousVelocity(float prf, float carrierFrequency)
    {
        const float SPEED_OF_LIGHT = 299792458.0;

        if (carrierFrequency <= 0 || prf <= 0)
            return 0.0;

        return (prf * SPEED_OF_LIGHT) / (4.0 * carrierFrequency);
    }
}
