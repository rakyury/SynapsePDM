# Synapse PDM — STM32Cube firmware (work-in-progress)

**EXPERIMENTAL. DO NOT FLASH ON A VEHICLE.**

This directory is a separate STM32Cube/HAL firmware project for the **existing
STM32F446ZET6 Synapse PDM hardware**. It does not change the legacy Arduino
firmware in `SynapsePDM/`.

## Architecture and provenance

- HAL / CMSIS, no Arduino dependencies
- STM32F446ZE, single Cortex-M4 @ 16 MHz HSI in the first-stage bring-up
- Adapted R2 Racing Dash `shared/` components (constants reduced for F446):
  `channel_engine`, `stateful_nodes`, `vm`, `filters`, `timers`, `tables`, `can_tx` (not enabled for transmission).
- Source snapshot: `rakyury/racing_dash@d3af311d61fb0e8848bea474fff7228d7e8eecc7`.
- Only allocation constants in the .h files differ; this prevents accidentally
  reserving the H747 dashboard's large arrays in the F446's 128 KB SRAM.

## Safety boundary

This first stage intentionally **does not energize any output**. It holds
14 high-side gate GPIOs low and fills a fixed-size channel engine with
diagnostic state and one bounded RAM-only VM result. An eventual output controller must be added with all of:
dedicated ADC current sampling and calibration, independent fault arbitration,
overcurrent trip deadlines, CAN timeout/failsafe, thermal derating, controlled
retry FSM, watchdog, brownout handling, and PWM timer/DMA safe update.

CAN1 now uses **NORMAL mode**, acknowledges bus traffic, and transmits diagnostic
telemetry at 500 kbit/s on PD0/PD1. This is no longer a silent/listen-only monitor.
The new standard-ID protocol 0x580..0x594 is documented in
[docs/hal-diagnostic-can.md](docs/hal-diagnostic-can.md) and the shared DBC. It
provides heartbeat, real diagnostic channel samples and a transactional upload of
one validated, RAM-only expression into result channel300. Legacy 0x700-series
configuration/control commands remain unimplemented in this HAL port. No received
message becomes output demand, a flash write or an EEPROM write.

No CAN command may bypass the output interlock. **No VM expression can control
hardware pins in this stage.** Display, modem, GPS, SD card, PWM, outputs and
the original Cortex USB protocol are not yet ported.

### Board pins retained from legacy firmware

- Output 1–7 GPIOG: PG10, PG9, PG6, PG5, PG4, PG3, PG2
- Output 8–14 GPIOF: PF15, PF14, PF13, PF12, PF2, PF1, PF0
- CAN1 RX=PD0, TX=PD1 (alternate AF9, legacy ALT_2)
- Current-sense ADC channels: PA0..PA7, PB1, PB0, PC3..PC0
- Ignition input: PE2

Final verification must compare these against the exact manufactured PCB and
power-driver datasheets. Default GPIO state during power-on reset is **not**
guaranteed by the firmware; pull-down hardware and rail sequencing must be
checked before connecting a load.

## Build

```bash
python -m pip install platformio
python -m platformio run -d pdm_cube -e stm32f446_cube
```

Host compile of shared code can use `gcc -std=c11` and the test sources
in `tests/`. The CI produces a build artifact, **not an approved firmware**.

### Important bootloader / vector-table restriction

The current `nucleo_f446ze` PlatformIO profile is only a compiler/linker
substitute for the same STM32F446ZET6 MCU. It links from **0x08000000**.
The original Synapse PDM bootloader image expects an application at
**0x08010000**, preserving the first 64 KiB of flash. The vector-table
offset, flash layout, bootloader handoff, and rollback have **not yet been
ported or verified**. **Do not flash this Cube build onto assembled PDM
hardware**, even for a bench test. Fix and validate the boot map first.

## Next migration steps

1. Verify cube build, baseline pin map and linker/RAM use.
2. Bench-validate the implemented diagnostic CAN v1 link, bus-off recovery,
   transaction timing and normal-mode bus behavior. Maintain 500 kbps and OFF outputs.
3. Implement ADC DMA for all current sense pins and calibration.
4. Implement output protection FSM and PWM timer/DMA with atomic safe buffers.
5. Add revisioned USB/CAN configuration protocol and PC simulator parity.
6. Port storage, thermal monitoring, ignition sleep/wake, optional features.
7. Hardware-in-the-loop tests before any driving.

Do not delete/replace old firmware or silently change EEPROM configuration.
