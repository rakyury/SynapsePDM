# Parallel firmware workboard

**Status:** Tasks and agent branches are prepared; no autonomous agent is running solely because files/branches exist. GitHub Issues are currently disabled in this fork. All work must land via reviewed PRs into `fix/power-output-safety`.

| Priority | Lane | Prepared branch | Instructions |
|---|---|---|---|
| P0 | Output safety state machine | `agent/fw-safety-core` | [AGENT_TASK](https://github.com/rakyury/SynapsePDM/blob/agent/fw-safety-core/AGENT_TASK.md) |
| P0 | CAN safety / protocol | `agent/can-protocol-hardening` | [AGENT_TASK](https://github.com/rakyury/SynapsePDM/blob/agent/can-protocol-hardening/AGENT_TASK.md) |
| P0 | Firmware compilation / CI | `agent/firmware-build-ci` | [AGENT_TASK](https://github.com/rakyury/SynapsePDM/blob/agent/firmware-build-ci/AGENT_TASK.md) |

**Source of truth:** root `AGENTS.md`, real GitHub Actions, upstream firmware and DBC. Preserve stock `main` until safety and hardware tests pass.
