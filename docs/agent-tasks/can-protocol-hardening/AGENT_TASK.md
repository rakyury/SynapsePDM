# Agent task: Firmware CAN: validated frames, stale-command safe state and readback

**Priority:** P0

**Branch:** `agent/can-protocol-hardening`

**Owned paths:** SynapsePDM/src/CANComms.*, SynapsePDM/src/CanFrameSafety.h, CAN DB/SynapsePDM.dbc, related tests

## Tasks
1. Validate all received DLC, standard/extended/RTR, IDs and parameters
2. Propose backward-compatible CAN timeout configuration and fail-state policy
3. Design versioned transactions with channel index, sequence and write acknowledgment
4. Create golden vectors compatible with original Cortex and Python PDM configurator

## Definition of done
Tests for malformed traffic, missing ECU, stale replies; updated DBC; PR targeting fix/power-output-safety.

## Rules
Read root `AGENTS.md` first. Work only within assigned paths, add regression tests, keep existing wire/EEPROM ABI unless versioned. No force push, no merge to main, and no hardware flash without validation. This file is a work item; it does not mean an autonomous coding agent is already running.
