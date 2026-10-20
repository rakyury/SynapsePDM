// CAN DLC and identifier validation for inbound PDM control messages.
// Keep this independent of the hardware driver so it can be host tested.
#pragma once

#include <stdint.h>

namespace CanFrameSafety
{
inline bool IsValidInboundFrame(uint32_t id, uint8_t dlc, bool extended, bool remote,
                                uint32_t channelStatusId, uint32_t analogueStatusId,
                                uint32_t channelConfigId, uint32_t systemConfigId)
{
    if (extended || remote || dlc > 8)
        return false;

    if (id == channelStatusId || id == analogueStatusId)
        return dlc >= 1;

    // Basic CAN channel control carries channel number and enable/PWM byte.
    if (id == channelConfigId)
        return dlc >= 2;

    // Extended channel configuration uses the parameter-mask at byte 7.
    if (id > channelConfigId && id <= channelConfigId + 23U)
        return dlc == 8;

    // System configuration frame +6 only needs 7 payload bytes.
    if (id == systemConfigId + 6U)
        return dlc >= 7;

    if (id >= systemConfigId && id <= systemConfigId + 5U)
        return dlc == 8;

    return true;
}
} // namespace CanFrameSafety
