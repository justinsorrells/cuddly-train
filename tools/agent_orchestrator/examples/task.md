# Task: Implement the command registry with duplicate rejection

> **Example / dry-run demo only.** Real runs follow `backlog.md` phase order —
> do not execute this for real before Phase 1 (shared core contracts) is
> complete. The Limits.h fallback below exists only so the example is
> self-contained.

Read AGENTS.md, docs/contracts/Teensy_Command_Server_Contract.md (§5, §6, §14),
and the skills: command-server-contract, fixed-capacity-cpp,
repository-conventions, host-conformance-testing.

Implement the fixed-capacity command registry only (backlog Phase 2, first two
items).

Scope:
- Create `src/core/CommandRegistry.h/.cpp` per the layout in `src/core/README.md`.
- Fixed-capacity registration table sized from a `constexpr` in `src/core/Limits.h`
  (create Limits.h if Phase 1 has not, with only the constants this task needs).
- Each entry registers: name, argument schema (name -> type from the V1 set:
  int/float/bool/string), `blocked_by_estop` (explicit, no default), handler
  reference.
- Duplicate command names fail registration (contract §5.2) — no silent
  replacement.
- Registration is immutable after the server starts (§5.1): expose a freeze/seal
  operation; registration after seal fails.
- No dynamic allocation in the registry; no exceptions across the boundary.

Do not implement:
- schema serialization (next task)
- dispatch, framing, networking, telemetry
- any file under src/platform/ or sketches/

Tests (tests/host/unit/test_command_registry.cpp, self-contained main):
- register + lookup round trip
- duplicate name rejected
- registration after seal rejected
- capacity exhaustion rejected (no overflow, no allocation)
- blocked_by_estop stored per command

Do not modify docs/contracts. If you find a contract contradiction, report it
instead of editing the contract.

Run `python3 tools/check_invariants.py`, `python3 tools/check_contract_sync.py`,
and `./tools/run_host_tests.sh`, and report results.
