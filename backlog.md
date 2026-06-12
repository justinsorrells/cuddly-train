# Backlog

Tasks execute in phase order. No production command-server behavior before
Phase 0 passes CI. Every task follows the framing and completion-report rules
in `AGENTS.md`. A task that discovers a contract contradiction stops and
reports it.

## Phase 0 — Repository bootstrap

- [x] Create agentic repository structure
- [x] Import and pin authoritative contracts (`UPSTREAM_SOURCES.md`, hash manifest)
- [x] Create `AGENTS.md`, `CLAUDE.md`, and the eight skills
- [x] Add invariant checker (`tools/check_invariants.py`)
- [x] Add contract-sync checker (`tools/check_contract_sync.py`)
- [x] Add host test harness (`tools/run_host_tests.sh` + smoke test)
- [x] Add Arduino CLI compile harness skeleton (`tools/build_teensy.sh`)
- [x] Add CI workflow
- [ ] Owner: ratify `Teensy_Command_Server_Contract.md` §31 judgment calls and
      update its Status header (then `check_contract_sync.py --update`)
- [ ] Owner: choose a LICENSE
- [x] Add agent orchestrator (ported from special-lamp:
      `tools/agent_orchestrator/` — Codex implement → checks → Claude review →
      Antigravity audit → auto-commit; tests in
      `tools/agent_orchestrator/test_orchestrate.py`)

## Phase 1 — Shared core contracts

- [ ] Define public API types and result types (`src/api/`)
- [ ] Define protocol vocabulary and constants (`src/core/Protocol.h`)
- [ ] Define fixed limits and integer widths (`src/core/Limits.h`)
- [ ] Define transport and clock interfaces + fakes (`tests/host/fakes/`)
- [ ] Define counters and session-state types

## Phase 2 — Registration and schema

- [ ] Implement command registry; reject duplicate commands (contract §5.2, §6)
- [ ] Generate schema from registry metadata — single source (§5.3, §14)
- [ ] Enforce supported argument types (§6.2)
- [ ] Validate schema size against the 8192-byte limit (§14)
- [ ] Registry/schema unit tests (§28.4)

## Phase 3 — Framing and parsing

- [ ] Bounded incremental line framer, 1024-byte limit incl. newline (§10)
- [ ] Discard-through-newline overflow behavior (§10.5)
- [ ] JSON/message validation with fixed-capacity documents (§10.6, §11)
- [ ] Fragmented/multiple-line/boundary-size/malformed tests (§28.3)

## Phase 4 — Session lifecycle

- [ ] Lifecycle state machine (§8)
- [ ] Schema-first transition within the 2 s registration window (§8.4, §9.3)
- [ ] Replacement-session supersession, exact ordering (§9.1)
- [ ] Parser/session cleanup; no commands across sessions (§21, §24)
- [ ] Controller-loss hook budget measurement + counter (§21)
- [ ] Lifecycle integration tests (§28.2)

## Phase 5 — Serialized transmit path

- [ ] One outbound serialization path (§13)
- [ ] Priority ordering (§13.4)
- [ ] Partial-write retries with network servicing between retries (§13.1)
- [ ] 100 ms configurable transmit deadline + failure outcomes (§13.6)
- [ ] Compact serialization, newline framing, flush (§10, §13.2)
- [ ] Blocked-peer and partial-write tests (§28.6)

## Phase 6 — Command dispatch

- [ ] Exact target validation before dispatch (§3.1, §23)
- [ ] Command/argument validation with contract error codes (§15, §6.3)
- [ ] Handler invocation; non-blocking expectations (§7)
- [ ] Success/error response building; seq + controller_ts echo (§16)
- [ ] Library-owned `board_proc_us` insertion/overwrite (§16.3)
- [ ] Dispatch and response tests incl. wrong-target (§28.5)

## Phase 7 — Telemetry

- [ ] 50 ms scheduler, non-blocking provider (§18.1, §18.2)
- [ ] Latest-snapshot coalescing; no stale bursts (§13.5)
- [ ] Board-local monotonic telemetry sequencing (§18.3)
- [ ] Telemetry continues through e-stop; starts within 250 ms of schema (§18.6)
- [ ] Consecutive deadline-failure teardown (§13.6)
- [ ] Timing/backpressure tests (§28.7)

## Phase 8 — Safety and heartbeat

- [ ] E-stop dispatch ahead of ordinary messages (§19, §23)
- [ ] E-stop hook duration measurement + 100 ms budget counter (§19.1)
- [ ] Truthful `estop_ack`; no ack on failure (§19.2)
- [ ] Board-originated `estop_triggered` event (§20)
- [ ] Controller-loss path (§21)
- [ ] Heartbeat acknowledgement, strict shape (§22)
- [ ] Wrong-target and malformed-message tests (§28.8, §28.9)

## Phase 9 — QNEthernet adapter

- [ ] Server startup, network configuration (§3.4)
- [ ] Connection acceptance + close-old supersession wiring (§9.1)
- [ ] Partial socket reads/writes mapped to core transport interface (§10.4)
- [ ] `flush()` and `setNoDelay(true)` (§13.2, §13.3)
- [ ] `ethernet_smoke` sketch; enable Teensy compile in CI
- [ ] Teensy compile passes for smoke + conformance sketches

## Phase 10 — Conformance and hardware validation

- [ ] Python conformance client (`tests/conformance/`) speaking the controller's
      protocol shapes
- [ ] Validate framing, schema-first, reconnect, supersession on hardware
- [ ] Validate e-stop, telemetry liveness, heartbeat on hardware
- [ ] Measure the four provisional numeric defaults (100 ms hook budgets,
      100 ms transmit deadline, 10-frame teardown); adjust values only, never
      architecture (contract §31)
- [ ] Record results in `tests/hardware/results/`
- [ ] Owner review → stamp the contract FROZEN
