# Agent task: Reproducible STM32 PlatformIO build and validation CI

**Priority:** P0

**Branch:** `agent/firmware-build-ci`

**Owned paths:** .github/workflows/**, test harnesses, build docs

## Tasks
1. Build both STM32 and STM32_no_bootloader profiles in CI
2. Keep Cppcheck and host regressions separate from MCU compilation
3. Store binaries, artifacts, memory usage and reports
4. Pin toolchain/dependencies and investigate actual compiler errors

## Definition of done
CI green with real firmware compilation; no bypassing failing checks; PR targeting fix/power-output-safety.

## Rules
Read root `AGENTS.md` first. Work only within assigned paths, add regression tests, keep existing wire/EEPROM ABI unless versioned. No force push, no merge to main, and no hardware flash without validation. This file is a work item; it does not mean an autonomous coding agent is already running.
