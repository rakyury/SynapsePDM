# Fault fixtures before physical HIL

These controls are stdin commands in `tests/link_simulator.c`. They do not add
CAN commands, configure a board, or enable a physical output. The harness runs
the actual C scheduler, ADC conversion model, VM and telemetry serializer. Its
currents and commissioning limits are fictional, explicitly supplied fixtures.

Run `STARTCFG 14 2 300000 200000` (or profile24, slots2..4) before injecting a
scheduler fault. Advance the existing `TICK now_ms` command to apply inputs.
Each command produces bounded CAN output and then `END`; malformed controls
produce `ERROR` and do not partially apply a fault. Arguments are unsigned decimal.

| Command | Effect and bounds |
| --- | --- |
| `STARTLINK 0` / `STARTLINK 1` | Lose/restore scheduler link health. This does not suppress the harness telemetry transport. |
| `STARTFREEZE 1` / `STARTFREEZE 0` | Freeze/resume global and every channel's feedback timestamps. TTL is1000ms; values remain available for stale-data tests. |
| `STARTCURRENT channel mA` | Set channel1..profile-count current,0..1000000mA; leave its OFF/completion/health flags unchanged. |
| `STARTTOTAL mA` | Override the independent total-current measurement,0..25000000mA. |
| `STARTAUTO` | Restore total current to fixture baseline plus all channel measurements. |
| `STARTGROUP group healthy` | Set health0/1 for group1..2. Channel groups alternate1,2. |
| `STARTHEALTH healthy` | Set global health0/1. |
| `STARTINHIBIT inhibited` | Set global inhibit0/1. |
| `STARTSENSOR channel valid` | Set per-channel sensor validity0/1. |

Existing `STARTINVALID`/`STARTVALID` affect global measurement validity.
`STARTOFF channel` explicitly supplies healthy, confirmed-OFF, zero-current
feedback, but does not repair an invalid sensor. To inject contradictory OFF
proof, issue `STARTOFF 1` then `STARTCURRENT 1 1` before the next tick. Fault
reserves cannot be released by that contradictory sample. `STARTCFG` and full
`RESET` clear injections; scheduler `STARTRESET` does not repair sensors or link.

Link loss cancels queued requests and faults active channels. Restoring link or
sensor health does not clear latched faults or replay canceled requests. Fresh,
consistent OFF proof releases a reservation, and explicit clear plus a newer
request is needed to start again. For other transient health/inhibit faults,
unexpired queued requests remain queued according to existing scheduler policy.
Measured current above the reserved envelope vetoes admission even when below
the total budget. Frozen/invalid total current is unknown in CAN telemetry,
with inhibited and measurement-unavailable flags; fault reservations remain visible.

## Evidence and limits

- `test_link_faults.c`: exact harness parser plus actual scheduler across
  profiles14/24 and slots2/3/4; link loss/recovery, TTL boundary, frozen samples,
  overcurrent, contradictory OFF, group/global/sensor faults, inhibit, independent
  total current, reset behavior, malformed/bounded commands and diagnostic gates0.
- `test_can_monitor.c`: injected HAL bus-off/FIFO0-overrun error bitmasks count
  transitions, explicit initialization clears the diagnostic epoch;1000 mailbox
  starvation calls leave RX bounded to16 frames per poll. Existing tests cover
  receive/transmit HAL errors and initialization/cleanup failures.
- `test_main_outputs.c`:201 failed-TX ticks still execute ADC and VM while GPIO
  OFF writes fence peripheral work. Recovery keeps the3-frame-per-tick ceiling.
- Existing `test_adc_hal.c`: conversion and scan deadlines, HAL initialization,
  configuration/start/poll failures, overrun before/during polling and mux faults.
  Existing `test_adc.c`: saturation, invalid Vref/calibration, stale data and
  deterministic arbitrary raw/calibration batches. These are reused, not duplicated.

The HAL CAN mock supplies `HAL_CAN_GetError` values; it does not simulate bxCAN
registers, interrupt delivery, transceiver behavior or physical bus-off recovery.
The current polling CAN adapter has no installed CAN error IRQ handler in this
target. Hardware error detection/recovery therefore remains an explicit HIL
verification item. Mock GPIO writes do not prove electrical OFF, power-up timing,
thermal margins or safe operation of connected loads. No hardware HIL was run.

Run all C suites with `python pdm_cube/tests/run_host_tests.py --cc gcc` on a
sanitizer-capable host. Windows GCC without sanitizer runtime can use
`--no-sanitize`; this is weaker evidence and CI must still run ASan/UBSan.
