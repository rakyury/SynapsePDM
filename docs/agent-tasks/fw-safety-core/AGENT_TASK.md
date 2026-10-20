# Agent task: Firmware safety: deterministic output fault state machine

**Priority:** P0

**Branch:** `agent/fw-safety-core`

**Owned paths:** SynapsePDM/src/OutputHandler.*, SynapsePDM/src/OutputSafetyPolicy.h, new OutputProtection*, related host tests

## Tasks
1. Reproduce PWM overcurrent and sensor fault across soft-start and steady ON
2. Define OFF/STARTING/ON/FAULT_WAIT/LOCKOUT FSM and explicit retry cooldown
3. Guarantee override by hardware/sense fault in every output mode
4. Document physical bench test for DMA, sleep/wake and reset

## Definition of done
Host regression tests, PlatformIO firmware compile, deterministic behavior, PR targeting fix/power-output-safety.

## Rules
Read root `AGENTS.md` first. Work only within assigned paths, add regression tests, keep existing wire/EEPROM ABI unless versioned. No force push, no merge to main, and no hardware flash without validation. This file is a work item; it does not mean an autonomous coding agent is already running.
