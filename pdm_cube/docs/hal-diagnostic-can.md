# HAL diagnostic CAN v1

CAN now runs in NORMAL mode at500kbps on PD0/PD1, acknowledging traffic and
transmitting only0x580..0x583. This intentionally replaces silent monitoring.
The general Racing Dash CanTx callback stays null. Legacy output/configuration
commands are not implemented; the HAL TX API rejects IDs outside the diagnostic
range. Physical output GPIOs are forced LOW before and after every foreground
tick and before configuring their output modes.

The shared DBC (`SynapsePDM/CAN DB/SynapsePDM.dbc`) and
`SynapsePDM/src/canwiki.txt` define all layouts, status codes, source IDs and
transaction behavior. The wire format is little-endian, classic standard DATA,
exactly eight bytes. Existing legacy message definitions are unchanged.

## Scheduling and state

`pdm_link.c` is portable C with no HAL, GPIO, storage or file IO. It uses the
actual vendored Racing Dash VM, channel engine and128-byte static code buffers.
Only the foreground RX poll calls Receive; VM execution cannot observe a partial
COMMIT. Validation and CRC complete before replacement; the active length/CRC and
buffer replace together between ticks. Tick executes the fixed result300 before
sampling. There is no VM_ExecuteAll duplication or writable output binding.

RX is capped at16 frames per10ms tick. HAL TX never waits for a mailbox and sends
at most3 frames per tick. Heartbeats have priority, then one rotating sample,
then upload replies. Thus ordinary samples complete a21-channel round in210ms;
with an available bus the round is bounded below500ms. During bus congestion the
pending sample is refreshed each tick, and its index/sequence advance only after
HAL accepts transmission. No finite telemetry deadline can be guaranteed on an
unavailable bus; clients expire heartbeat at500ms and samples at1000ms.

Replies use a32-frame ring. Required ACK+READBACK capacity is reserved before
mutating a command; when full, incoming commands are dropped without successful
mutation and the client times out. Telemetry uses separate slots and cannot be
starved by request floods. A physically lost COMMIT reply remains ambiguous;
the client must query the active CRC and must never automatically retry COMMIT.

Staging expires at1000ms of inactivity using unsigned elapsed time. Accepted
same-transaction BEGIN and identical DATA duplicates refresh activity but never
reset progress. Malformed payloads do not refresh it. Expiration, abort or reset
clears staging; only reset clears the active program. RAM-only means no persistence
over reboot, no EEPROM migration and no flash operation. The protocol is not
authenticated; explicit client arm/confirmation does not secure a hostile bus.

## Tests and actual-C simulator

Run `python pdm_cube/tests/run_host_tests.py` for all five host suites under
ASan/UBSan on Linux. Windows without sanitizer runtimes may pass `--no-sanitize`;
that is not sanitizer evidence. Tests cover valid upload/CRC query, invalid shapes,
wrong transactions, missing/conflicting/duplicate fragments, odd padding, timeout
rollover, old-active retention, queue saturation, current-value refresh after
congestion, telemetry sequences, strict bytecode rejection and10000 seeded fuzz
inputs. The fake-HAL main test compiles actual main.c and checks OFF software
writes before mode/RX/TX, a nonzero VM result isolated from gates and TX<=3/tick.
It does not measure electrical GPIO behavior.

Compile the actual C protocol+VM simulator:

```sh
cc -std=c11 -Wall -Wextra -Werror pdm_cube/tests/link_simulator.c \
  pdm_cube/src/pdm_link.c pdm_cube/src/r2_shared/vm/vm.c \
  pdm_cube/src/r2_shared/channel_engine/channel.c -o link_simulator
```

Commands on stdin (all response frames are `IDhex 16hexbytes`, followed by END):

- `RX now_ms_decimal id_hex 16hexbytes` with optional `dlc extended remote`.
- `TICK now_ms_decimal` runs the actual VM and schedules bounded telemetry.
- `SET channel_decimal value_decimal` injects only240,241,242,244 for tests.
- `RESET` clears all simulated state, including the RAM program.

No real CAN adapter is opened. The simulator drains queued frames immediately;
the firmware wrapper independently enforces its3-frame hardware mailbox budget.
An invalid simulator command prints ERROR then END. Test driver time is explicit.

ARM compilation, host tests and simulation do not authorize flashing. The existing
bootloader/vector-table restriction in README still applies. GPIO electrical
state, actual CAN timing, arbitration, error recovery, controller/harness behavior
and HIL remain unverified. All power outputs stay OFF until separate HIL approval.
