# AGENTS.md — Synapse PDM firmware

This repository controls real automotive power outputs. Treat any actuator change as safety-critical.
This file coordinates independent coding-agent tasks; it does **not** authorize autonomous flashing, hardware actuation, or merging.

## Branch / integration policy
- Upstream compatibility baseline: joesbox/SynapsePDM. Our working integration branch is `fix/power-output-safety`.
- Work on a NEW agent-specific branch from the latest integration branch: `agent/fw-safety-*`, `agent/can-protocol-*`, or `agent/firmware-ci-*`.
- One issue per branch; submit a PR into `fix/power-output-safety` for review. Never push to `main`, force-push, or auto-merge.
- Preserve the original STM32F446ZET6 pinout, 14 outputs, 8 digital inputs, 8 analogue inputs, and binary EEPROM structures without an explicit migration plan.
- Preserve the current CAN IDs / message layouts unless both sender and receiver have compatible versioning. Keep `SynapsePDM/CAN DB/SynapsePDM.dbc` and `src/canwiki.txt` synchronized with firmware.

## Independent agent roles (avoid touching other roles' files)

### 1. Output protection and deterministic execution
Owned paths: `SynapsePDM/src/OutputHandler.*`, `SynapsePDM/src/OutputSafetyPolicy.h`, new `SynapsePDM/src/OutputProtection*`, tests `SynapsePDM/test/output_*`.
- Make PWM and digital outputs fail closed on overcurrent, invalid current sensor, invalid VBatt, and emergency shutdown.
- Design explicit OFF/STARTING/ON/FAULT_WAIT/LOCKOUT states, bounded retry cooldown, and edge-triggered reset policy.
- Do not rely on background UI, filesystem, or GSM tasks for overcurrent protection.
- No unvalidated high-side drivers, timer/DMA changes, or arbitrary threshold changes.
- Verify OFF actually drives output low across DMA, reset, boot, sleep and wake; request bench tests for physical behavior.

### 2. CAN safety and protocol hardening
Owned paths: `SynapsePDM/src/CANComms.*`, `SynapsePDM/src/CanFrameSafety.h`, protocol definitions and `SynapsePDM/test/can_*`.
- Validate 11-bit standard CAN identifiers, frame type, DLC, payload ranges, and all channels/input indices.
- Add configurable per-channel lost-CAN-command behavior with backward-compatible defaults and migration.
- Readback/ACK/transaction IDs should be versioned. Avoid introducing breaking changes.
- Verify no control or configuration writes happen on connect/discovery.
- Validate against a host-side fake CAN bus and the Cortex PDM test vectors.

### 3. Build/reliability agent
Owned paths: `.github/workflows/**`, `SynapsePDM/test/**` except another agent's active test files, `docs/testing/**`.
- Preserve report artifacts; never publish reports by committing back to source branch.
- Keep Cppcheck and host tests; add a genuine PlatformIO firmware build for both supported environments.
- Record build and memory usage, sanitizer and static-analysis results, test coverage; fix failures, not suppress them.
- Maintain deterministic mock/host tests, boundary cases and regression vectors.
- Separate static analysis, compilation, host tests and real-hardware testing in status reports.

## Mandatory safety invariants
- No nonfinite ADC input or divide-by-zero may be used to calculate output duty.
- A fault must override user/CAN/soft-start demand. No hidden automatic retry without explicit bounded policy.
- Unknown or malformed CAN/serial payloads never write configuration or actuate outputs.
- Loss of CAN communications must obey each output's explicitly configured safe state.
- Watchdog must not be perpetually fed if safety-critical tasks are stuck.
- Never claim 'automotive-ready' on CI alone: verify oscilloscope timing, actual copper and thermal behavior, harness, automotive electrical transients, EMC, and power-cycle recovery.

## Validation gates
1. Reproduce bug and add a failing test before implementing a fix where practical.
2. Run `python -m unittest` for cross-project protocol tests where available; run host C++ tests under `SynapsePDM/test`.
3. Run `pio run -d SynapsePDM -e STM32` and `pio run -d SynapsePDM -e STM32_no_bootloader` (or state why unavailable).
4. CI green, no unreviewed changes to wire protocol or persisted EEPROM layout.
5. PR describes expected behavior, risk, compatibility and the manual hardware test plan.

## Reference projects
- Original firmware: https://github.com/joesbox/SynapsePDM
- Paired GUI: https://github.com/rakyury/Cortex (feature branch `feature/r2-pdm-can-configurator`)
- R2 Racing Dash VM and CAN UI: https://github.com/rakyury/racing_dash (`configurator_qt/`, `shared/vm/`).
