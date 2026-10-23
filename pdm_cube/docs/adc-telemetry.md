# Diagnostic ADC telemetry

ADC1 samples the existing14-channel Synapse PCB without enabling any gate.
This is diagnostic acquisition, not a protection loop. GPIOF/G output masks,
the unconditional OFF interlock and the RAM-only diagnostic VM are unchanged.
ADC channels are deliberately excluded from VM inputs until validity semantics
for sensor-dependent expressions are designed.

## Physical inputs and conversion

The current-sense pin order comes from legacy Globals.h:
PA0,PA1,PA2,PA3,PA4,PA5,PA6,PB1,PB0,PA7,PC3,PC2,PC1,PC0.
Battery usesPC4. The17-word scan adds internal VREFINT and MCU die temperature.
Only A0..7/B0..1/C0..4 are configured analog; gate pins are on F/G.

The pure C model uses bounded int64 arithmetic and nearest rounding. VDDA is
derived from actual factory VREFINT_CAL at0x1FFF7A2A with3300mV reference.
Temperature uses TS_CAL1 at0x1FFF7A2C (30C) and TS_CAL2 at0x1FFF7A2E (110C),
corrected for VDDA. These addresses and ADC limits follow ST
[DS10693](https://www.st.com/resource/en/datasheet/stm32f446ze.pdf) and
[RM0390](https://www.st.com/resource/en/reference_manual/dm00135183.pdf).

Battery retains legacy System.cpp gain3.9787mV/count at nominal VDDA3294mV:
`battery_mV = round(raw * 39787 * VDDA_mV / (10000 * 3294))`.
Current retains OutputHandler.h R_IS1000ohm and ChannelConfig.h nominal
KILIS18407.72: `current_mA = round(raw * VDDA_mV * 1840772 / (4095 * 100000))`.
These are nominal board estimates, not measured per-board calibration. IS values
remain observable when gates are OFF; they are not independently verified load
currents. Total is valid only when all14 current readings are valid.

Zero current raw is a valid nominal zero. Saturation4095, sense voltage>=2000mV,
invalid factory calibration, implausible VDDA or temperature cause invalid
quality. VDDA bounds1800..3600mV and temperature bounds-40..125C are validation
guards, not an assertion of board operating ratings. No fallback calibration is
invented for hardware; missing samples are never reported as measured zero.

## Bounded acquisition

Each10ms tick performs at most one HAL poll with timeout0 and one subsequent
configure/start. ADC stays enabled to avoid the HAL start stabilization loop.
Internal temperature/VREF is enabled once, VBAT mux disabled, and conversion
starts after at least1ms without spinning. A480-cycle sampling time is used.
The actual APB2 clock selects a divider keeping ADC between0.6 and18MHz.

The STM32 HAL temperature configuration path includes an unconditional delay.
After configuring VREF through HAL, the already stabilized temperature channel
is selected with masked rank1/SMP18 writes; no other sequence or sample bits
are changed. The wrapper verifies ADC enable and internal mux state each tick.

The model publishes only a complete17-conversion batch. Nominal scan time is
170ms; conversion deadline30ms and whole-batch deadline250ms reject stalled
acquisition. The timestamp is the first conversion start, conservatively
including scan age. OVR, HAL errors, bad raw words, unexpected disable/mux or
deadline failure invalidate readings and latch acquisition stopped until
explicit reinitialization. There is no automatic restart or partial publication.

## Wire extension and display

Standard Classic DATA ID0x584, DLC8, little endian:
`channel:u16, quality:u8, per-channel sequence:u8, value:i32`.
Channels100/101/102 carry battery mV / sum mA / MCU die milli-Celsius;
110..123 carry individual current mA. Quality0=factory-calibrated,
1=unavailable,2=nominal board estimate,3=invalid/fault,4=stale acquisition.
Only0/2 are numeric. Age>500ms becomes stale; queued payloads are refreshed
before transmission. Cortex also expires samples after1000ms and requires a
fresh compatible heartbeat. Invalid values create graph gaps.

Old heartbeat and0x581 diagnostics remain byte compatible. Old clients ignore
0x584; new clients show unavailable ADC rows when paired with old firmware.
The shared DBC and canwiki document the identical additive format.

## Verification and remaining hardware work

Host tests exercise conversion/ranges/calibration,10000 seeded batches, pin
order, mux, clocks, deadlines, OVR/errors and actual main.c OFF fencing.
The paired Cortex simulator runs actual C conversion/link/VM with injected raw
words through Python CAN and Qt; it does not simulate analog electronics.
Linux CI runs ASan/UBSan. ARM build verifies compilation, not electrical behavior.

Board pin mapping, analog settling, current calibration, sensor accuracy,
thermal behavior and physical OFF during reset/boot require bench/HIL evidence.
The README bootloader/vector restriction still prohibits flashing this build.
Outputs remain strictly OFF pending separately reviewed HIL work.
