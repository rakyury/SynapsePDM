# Experimental HAL output safety policy

The C policy is independent of HAL and the Racing Dash VM. It does not write
GPIO, generate PWM, transmit CAN or authorize physical actuation. main.c supplies
OFF demand and invalid sensing/command flags to all 14 policy instances. Thus
they lock out while the existing GPIO interlock keeps every gate LOW. The
Racing Dash shared modules, CAN wire layout and persisted legacy data are unchanged.

## Deterministic behavior

Healthy demand enters STARTING, then ON after startup_ms (zero enters ON
immediately). Demand OFF returns either active state to OFF. Overcurrent, invalid
current sensing, invalid battery sensing or lost command overrides every state
before demand and startup processing. Emergency immediately enters LOCKOUT.

Automatic retries require an explicit nonzero max_retries and nonzero cooldown.
The cooldown starts at the first fault; persistent faults never restart an output.
After the cooldown, healthy demand may consume one retry. The next fault after the
budget is spent locks out. Turning demand OFF never replenishes this budget.
Fault history remains latched after a successful retry. The bring-up policy has
zero retries; no current/voltage thresholds have been invented or commissioned.

Reset requires a new rising edge while demand is OFF and all inputs are healthy.
An edge during a fault or demand ON is consumed, so holding reset never clears a
later lockout. Reset clears history and the retry budget. Invalid policy or state
locks out non-resettable until initialization. Time intervals are limited to
INT32_MAX milliseconds and calls must be less than 2^31 milliseconds apart;
unsigned subtraction handles tick rollover. Watchdog/stall enforcement is not
implemented by this pure policy and remains a separate hardware gate.

## Host checks

`cc -std=c11 -Wall -Wextra -Werror pdm_cube/tests/test_output_safety.c pdm_cube/src/pdm_output_safety.c -o output_safety_test`

Run the executable. CI should add address/undefined sanitizers. Vectors cover all
five external faults in OFF/STARTING/ON, cooldown boundary and rollover, finite
retry budget, persistent faults, demand release, held and unsafe reset edges,
zero startup, disabled retry, invalid policy and null inputs.

## Required bench gates before any actuation change

Review ADC validity, threshold calibration, sample age and timing independently.
Verify every mapped output with an oscilloscope and representative driver/load,
including boot, reset, brownout, emergency, sensor disconnect, stalled scheduler,
CAN loss, DMA/PWM disable, sleep/wake and power cycles. Verify actual LOW voltage
and driver turn-off, not only GPIO register values. Scope fault latency and retry
timing under CPU/CAN load. No DMA/PWM path is currently enabled by this port.
Board pull-downs and failure behavior before software starts require inspection.
CI and host simulation cannot establish hardware safety or readiness.
