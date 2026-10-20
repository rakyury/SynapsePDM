#include "CanFrameSafety.h"

#include <cassert>
#include <cstdint>

int main()
{
    using CanFrameSafety::IsValidInboundFrame;
    constexpr uint32_t status = 0x700, analogue = 0x712;
    constexpr uint32_t control = 0x740, system = 0x730;
    auto valid = [](uint32_t id, uint8_t dlc, bool extended = false, bool remote = false) {
        return IsValidInboundFrame(id, dlc, extended, remote, status, analogue, control, system);
    };
    assert(valid(status, 1));
    assert(!valid(status, 0));
    assert(valid(analogue, 1));
    assert(!valid(analogue, 0));
    assert(valid(control, 2));
    assert(!valid(control, 1));
    assert(valid(control + 1, 8));
    assert(!valid(control + 1, 7));
    assert(valid(control + 23, 8));
    assert(!valid(control + 23, 3));
    assert(valid(system, 8));
    assert(!valid(system, 7));
    assert(valid(system + 6, 7));
    assert(!valid(system + 6, 6));
    assert(!valid(control, 2, true));
    assert(!valid(control, 2, false, true));
    assert(!valid(control, 9));
    assert(valid(0x350, 0)); // unrelated CAN frames ignored by handlers
    return 0;
}
