# Teensy 4.1 Command Server

A copy-and-extend template library for Teensy 4.1 boards on the Hyperloop
networking stack: board developers copy this firmware, register their
commands/telemetry/hooks, and the library handles TCP, newline-JSON framing,
schema-first sessions, telemetry push, e-stop handling, and heartbeat — per
`docs/contracts/Teensy_Command_Server_Contract.md`.

The controller side lives in the `special-lamp` repo. Its frozen contract
(`docs/contracts/V1_Networking_Decisions.md`, snapshotted here) wins on any
conflict.

**Agents:** start with `AGENTS.md`.

## Layout

```text
docs/contracts/      authoritative contracts (hash-pinned; see UPSTREAM_SOURCES.md)
docs/companion/      developer guides (API, build/flash, conformance, timing)
docs/decisions/      decision log for post-bootstrap judgment calls
src/api/             public board-application interfaces (platform-free)
src/core/            protocol, framing, registry, schema, sessions, scheduling (platform-free)
src/platform/        QNEthernet adapter — the only place Arduino networking types may appear
src/TeensyCommandServer.h   public facade
sketches/            .ino entry points (smoke + conformance images)
tests/host/          platform-free C++ unit/integration tests with fakes
tests/conformance/   Python conformance client run against a real board
tests/hardware/      manual hardware validation procedure + recorded results
tools/               invariant checker, contract-sync checker, build/test scripts
.agents/skills/      per-area execution rules for agents
```

Planned implementation files per directory are listed in each directory's
README; they are created by their backlog phase, not pre-created empty.

## Verification

```bash
python3 tools/check_invariants.py     # static architecture guardrails
python3 tools/check_contract_sync.py  # contracts match pinned hashes
./tools/run_host_tests.sh             # host C++ tests
./tools/build_teensy.sh               # Teensy compile (requires arduino-cli)
```

## Status

Bootstrap (Phase 0 of `backlog.md`). The contract awaits owner ratification;
its four numeric timing defaults are provisional pending hardware conformance
(contract §31). No command-server behavior is implemented yet.
