# Board profiles and simulated start admission

This stage implements portable C board metadata and start admission, not output
actuation. The STM32 application remains a14-output Synapse firmware with all
gate GPIOs forced OFF. The24-output profile is simulation-only and has no PCB
pin mapping. No new CAN write command, EEPROM layout, or Cortex profile negotiation
is introduced here; existing HAL diagnostic v1 remains14-channel compatible.

## Board identity

Factory profile14 describes the existing Synapse gate/current pin mapping.
Factory profile24 has24 logical channels: ten with a target30A startup peak,
fourteen with a target17–20A continuous range. The targets are engineering
requirements, not commissioned current limits. The continuous current of the ten
startup channels and peak duration/repetition are still unspecified.

Unknown profiles, incompatible counts, invalid or colliding pins and unsafe
output defaults must fail validation. Missing24-channel pins are permitted only
in simulation; selecting profile24 for the STM32 application must fail compilation.
The existing14-channel firmware must not silently turn into a24-channel device.

## Start admission boundary

The scheduler is a single-threaded C model with bounded storage for24 channels.
It has no HAL, CAN, GPIO, timer/PWM output or persistent-storage API. Its states
describe virtual permission and reservation; they are not measured physical gate
states. The host simulator reports physical-output writes separately.

The configured simultaneous-start limit is2,3 or4. Every start also requires
valid per-channel startup/continuous envelopes and timing, a valid group, healthy
fresh feedback, and available total/group current budget. Fixture values used by
the simulator are named simulation inputs; factory target metadata does not
automatically authorize any load.

Reserve startup current before admission; retain reservations for active loads.
Transition to a continuous envelope only after validated completion. A failed or
timed-out start cannot free capacity merely because an OFF command was requested.
Unknown state or missing feedback must not be treated as confirmed OFF or0mA.
Duplicate requests cannot multiply reservations. Loss of authorization cancels
queued work; faults do not cause automatic restart. Reset discards old requests.

`PDM_Start_Init` copies explicit configuration. It is construction after known
shutdown, not a runtime reset; use `PDM_Start_Reset` to retain live reservations.
Request IDs use unsigned serial ordering. Duplicate requests do not refresh the
original queue deadline; expired or consumed requests cannot replay after reset.
Priorities0..3 are ordered high to low, with FIFO within each priority. A blocked
head is not bypassed; request expiry bounds queue waiting. This conservative
policy can delay a different group, and that tradeoff is intentional here.

`PDM_Start_Step` consumes timestamped global and per-channel feedback. Completion
must be sampled strictly after admission; OFF confirmation after a fault/cancel
must be strictly later than that event. `PDM_Start_ClearFault` is an explicit
recovery action, followed by a newer request and the configured cooldown.
All configured time horizons are bounded by INT32_MAX milliseconds; callers must
advance the model regularly within that half-range for wrap-safe arithmetic.

In this conservative simulator policy, confirmed OFF also requires measured
current0mA. Contradictory nonzero current retains the reservation and blocks
new admission. The independent total meter must stay within both the configured
input budget and the already reserved envelope; unknown extra current is a veto,
not spare capacity. These inputs are validated simulation/safety feedback, not
the nominal IS diagnostic ADC values. No automatic deadband or sensor offset is
invented for physical hardware.

The simulator supports both profiles and limits2/3/4. Its example5000mA steady
and30000mA startup values for the first ten channels, and17000/20000mA for the
others, are fictional fixtures. Likewise its group allocation and generous
dispatch budgets are not a proposed electrical rating or wiring plan.

## Validation

Run all C suites and the integrated host simulator:

```sh
python pdm_cube/tests/run_host_tests.py
python pdm_cube/tests/test_start_simulator.py --output host-test-results/start-simulator
```

Both default to ASan/UBSan. On hosts without sanitizer runtimes, `--no-sanitize`
is available but is not sanitizer evidence. `--cc` selects the host compiler.
CI preserves the simulator's report and logs alongside host JUnit.

Coverage includes simultaneous requests at limits2/3/4, existing-load and group
budgets, duplicate/stale requests, start timeout, fault reservation, confirmed
OFF, reset, invalid configuration and time wrap. Existing ADC/CAN/VM and
actual-main fake-HAL output-OFF tests must continue to pass.

Before any physical output implementation, determine actual load profiles,
startup duration/repetition, input/group budgets, cooling and protection
deadlines. Validate hardware pin maps, calibration, boot map and HIL separately.
This diagnostic ADC scan is not a short-circuit protection loop. No part of
profile selection or simulated admission authorizes output activation.
