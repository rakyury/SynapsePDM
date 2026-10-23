# Read-only scheduler snapshots over Classic CAN

Version 1 is an additive diagnostic stream, separate from heartbeat/ADC/VM v1.
There are **no scheduler CAN commands**, uploads, output bindings or physical
actuation. The real HAL build reports the existing Synapse14 board as
uncommissioned, inhibited and forced OFF. Its unknown limits are unavailable,
not configured zero-current limits. Only the host simulator supplies fictional
commissioned loads to the actual C start-admission model.

All frames are standard 11-bit Classic CAN DATA, DLC8. Integers are little-endian.

| ID | Bytes |
|---|---|
| 0x585 BEGIN | version:u8=1, reserved:u8=0, payload_length:u16, sequence:u32 |
| 0x586 DATA | sequence_low16:u16, chunk_index:u8, payload[5] |
| 0x587 END | sequence:u32, CRC32:u32 |

DATA indexes start at zero, in ascending order. Pad the final five-byte chunk
with zeros. CRC32 uses IEEE/zlib over the exact eight BEGIN bytes followed by
the exact payload length, excluding padding. A snapshot is captured once and
remains immutable throughout transmission; live model changes appear only in a
later snapshot. Length is exactly `24 + 8*group_count + 8*channel_count`, bounded
to 408 bytes. There are 14 or 24 channel records and at most 24 groups.

| Payload offset | Field |
|---|---|
| 0, 1 | profile ID (14/24), channel count (must match profile) |
| 2 | flags: bit0 available, bit1 simulation, bit2 inhibited, bit3 physical OFF, bit4 measurement unavailable; others zero |
| 3, 4 | start-slot limit (2..4 if available), reserved start slots |
| 5, 6, 7 | group count, queue count, reserved zero |
| 8 | capture uptime:u32 in milliseconds |
| 12, 16, 20 | total budget, reserved current, measured current:u32 in mA |
| 24 | group records, then channel records |

Group records are `budget_mA:u32, reserved_mA:u32`; their index is the group ID.
Channel order is logical outputs 1..count. Each record is
`state_flags:u8, reason:u8, queue_position:u8, group:u8, reserved_mA:u32`.
State occupies bits0..2; bit7 means a startup slot is still reserved, including
a fault awaiting OFF proof. Other state bits are zero. Queue position is 1-based,
zero when not queued. State values are OFF=0, QUEUED=1, STARTING=2, ON=3, FAULT=4.
Reason values 0..13 are, respectively: OK, BAD_CONFIG, BAD_CHANNEL, OLD_REQUEST,
EXPIRED, UNHEALTHY, UNKNOWN_OFF, SLOTS, TOTAL_BUDGET, GROUP_BUDGET, COOLDOWN,
DEADLINE, FAULT_LATCHED, CANCELLED.

Hardware unavailable snapshots use profile/count14, flags12, slots/groups/queue
and total fields zero. All channel records are OFF/BAD_CONFIG, queue0, group255,
reserve0. These zeros are transport placeholders, not current measurements or
commissioned budgets. A decoder exposes unavailable limits/reserves, not numeric
zero load estimates. Simulation captures set available+simulation+physicalOFF
(flags11), with inhibited bit added if the fixture inputs are inhibited/unhealthy.
Invalid, unhealthy, expired or future-dated total-current input sets both
measurement-unavailable and inhibited bits, and writes zero in the measured-current
field. That zero means unknown, not zero current. Queue and fault reservations
remain available. Freshness uses the configured feedback TTL at capture time.
The physical-OFF bit is mandatory for this experimental protocol.

The transmitter starts a new capture every1000ms when idle and sends at most one
snapshot frame per10ms tick. It abandons unfinished captures older than1500ms
and starts a fresh sequence. Heartbeat, rotating diagnostic/ADC sample and VM
replies retain priority. Main still sends at most three total frames per tick.
The largest snapshot is84 frames, a two-group24-channel snapshot49 frames,
and unavailable hardware30 frames. Without reply congestion their transfer
times are about830/480/290ms. Normal heartbeat and sample cadence is unchanged.
The additional stream is capped at100 frames/s; with heartbeat+samples the
nominal cap is210 frames/s before reply traffic. These are software bounds, not
a measured electrical CAN latency guarantee at500kbit/s.

Receivers require a compatible recent heartbeat and a newer32-bit sequence
(modular forward distance1..INT32_MAX) at BEGIN. Keep the high-water mark even
when a transfer fails. Reject missing/out-of-order/conflicting chunks, bad length,
reserved fields, CRC, impossible groups/queue/reservation totals or unknown
profile/version; identical retransmitted chunks may be ignored. Never expose a
partially assembled snapshot. Assembly timeout is1500ms. At completion, capture
age must be at most2000ms relative to heartbeat-derived uptime; up to200ms of
forward timestamp jitter is tolerated by Cortex. Published data expires once
capture age exceeds2500ms, even if fresh heartbeats continue.

Disconnect or a detected heartbeat reboot clears the sequence epoch. This
protocol has no authentication or boot-unique persistent nonce: its sequence/CRC
guards prevent accidental mixing and stale replay within an observed epoch,
not hostile replay of an entire prior boot or a forged CAN sender.

## Host fixture commands

`pdm_cube/tests/link_simulator.c` is a stdin/stdout test harness, not a CAN command
handler. `STARTCFG profile slots total_mA group_mA` configures fictional loads
(14/24, 2..4, baseline20A, two group baselines10A). The first10 profile24 channels
use start30A/steady10A; the remaining14 use start20A/steady17A. Profile14 uses
30A/10A throughout. Startup deadline5000ms, cooldown100ms, request TTL60000ms.

`STARTREQ channel`, `STARTALL`, `STARTCOMPLETE channel`, `STARTFAULT channel`,
`STARTOFF channel`, `STARTCANCEL channel`, `STARTCLEAR channel`, `STARTRESET`
operate only this model. Channels are1-based. Existing `TICK now_ms` updates the
model before capture. Completion/fault/OFF input changes take effect on the next
tick. Reset/cancel preserve reservations until fresh OFF proof; clear is explicit.
`STARTINVALID` and `STARTVALID` invalidate/restore the fixture's total-current
input validity; they preserve reservations and take effect on the next tick.
`STARTCFG` abandons an old partial capture but preserves the transport sequence.
Full `RESET` restores unavailable Synapse14 and resets the heartbeat epoch.
`TXLIMIT n` selects1..3 output frames per harness command (default3) for congestion
tests. Each command ends in `END`; no hardware adapter, GPIO or flash is accessed.
