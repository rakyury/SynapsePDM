// Pure, hardware-independent guards shared by the PDM output handler and host tests.
#pragma once

#include <cmath>

namespace OutputSafetyPolicy
{
inline bool IsValidPwmSupply(float volts)
{
    // A missing ADC sample, NaN, or an implausible measurement must never
    // produce a non-finite or overflowing compensated PWM duty.
    return std::isfinite(volts) && volts >= 3.0f && volts <= 40.0f;
}

inline bool IsSenseFault(float senseVolts, float faultThreshold)
{
    return !std::isfinite(senseVolts) ||
           !std::isfinite(faultThreshold) ||
           senseVolts > faultThreshold;
}

inline bool IsOverCurrent(float currentAmps, float thresholdAmps)
{
    return !std::isfinite(currentAmps) ||
           !std::isfinite(thresholdAmps) ||
           thresholdAmps < 0.0f ||
           currentAmps > thresholdAmps;
}
} // namespace OutputSafetyPolicy
