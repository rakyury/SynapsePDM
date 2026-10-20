#include "OutputSafetyPolicy.h"

#include <cassert>
#include <limits>

int main()
{
    using namespace OutputSafetyPolicy;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    assert(!IsValidPwmSupply(0.0f));
    assert(!IsValidPwmSupply(-12.0f));
    assert(!IsValidPwmSupply(2.9f));
    assert(IsValidPwmSupply(3.0f));
    assert(IsValidPwmSupply(6.0f)); // cranking
    assert(IsValidPwmSupply(13.8f));
    assert(IsValidPwmSupply(40.0f));
    assert(!IsValidPwmSupply(40.1f));
    assert(!IsValidPwmSupply(nan));
    assert(!IsValidPwmSupply(inf));
    assert(!IsValidPwmSupply(-inf));

    assert(!IsSenseFault(0.0f, 2.0f));
    assert(!IsSenseFault(2.0f, 2.0f));
    assert(IsSenseFault(2.0001f, 2.0f));
    assert(IsSenseFault(nan, 2.0f));
    assert(IsSenseFault(inf, 2.0f));
    assert(IsSenseFault(1.0f, nan));

    assert(!IsOverCurrent(0.0f, 10.0f));
    assert(!IsOverCurrent(10.0f, 10.0f));
    assert(IsOverCurrent(10.01f, 10.0f));
    assert(IsOverCurrent(nan, 10.0f));
    assert(IsOverCurrent(inf, 10.0f));
    assert(IsOverCurrent(1.0f, nan));
    assert(IsOverCurrent(0.0f, -1.0f));
    return 0;
}
