/* Compatibility bridge for joesbox/STM32SD with Arduino_Core_STM32 >= 3.
 *
 * The legacy STM32SD fork includes wiring_constants.h unconditionally.
 * STM32duino 3.0 uses the Arduino API header api/Common.h instead (as in
 * the current official stm32duino/STM32SD implementation).
 *
 * This is only a header-name redirect; no GPIO constants are redefined.
 * TODO: update/pin the STM32SD fork to use the upstream version guard.
 */
#ifndef SYNAPSEPDM_LEGACY_WIRING_CONSTANTS_BRIDGE_H
#define SYNAPSEPDM_LEGACY_WIRING_CONSTANTS_BRIDGE_H

#include <api/Common.h>

#endif
