# AGENTS.md — Teensy 4.1 Command Server

Implementation repo for the command-server library defined by
`docs/contracts/Teensy_Command_Server_Contract.md`. Agents implement the
contract; they do not reinterpret it.

## Authority order (higher wins on any conflict)

```text
1. docs/contracts/V1_Networking_Decisions.md        (frozen controller contract)
2. docs/contracts/Teensy_Command_Server_Contract.md (this repo's contract)
3. docs/contracts/Board_Developer_Guide.md
4. docs/companion/*
5. AGENTS.md (this file)
6. .agents/skills/*
7. backlog task text
```

If two documents conflict, **stop and report the contradiction**. Never
silently choose an interpretation.

Contract status: the Teensy contract is awaiting owner ratification, and its
four numeric timing defaults (100 ms hook budgets, 100 ms transmit deadline,
10-frame telemetry teardown) are provisional pending hardware conformance
(contract §31). Hardware testing may change the numbers, never the
architecture.

## Repository boundaries (hard invariants)

* **`src/core` and `src/api` must not depend on QNEthernet or Arduino
  networking types.** All platform I/O lives in `src/platform/qnethernet`.
  Timing enters core only through injected clock interfaces.
* Public surface is `src/TeensyCommandServer.h` and `src/api/*` only.
  `src/core` and `src/platform` are implementation details.
* Contracts in `docs/contracts/` are hash-pinned. Editing one without an
  explicitly authorized task fails `check_contract_sync.py`.
* Board-side vocabulary is closed: statuses `ok`/`error` only, error codes
  per contract §17, no new message types or event names, `board_proc_us`
  inside `result`, `estop_ack.details.state` exactly `"safe"`.
* No Redis, no GUI communication, no board-to-board routing, no board-level
  `get_schema`, no UDP transport, no runtime command registration.
* Committed source is portable: no generated `#line` directives, no absolute
  paths, no build artifacts.

## Required skills by task type

Read the relevant `SKILL.md` files in `.agents/skills/` **before** writing code:

| Task touches | Read |
|---|---|
| anything | `command-server-contract`, `repository-conventions` |
| buffers, JSON documents, parsing storage | `fixed-capacity-cpp` |
| line framing, message parse/serialize | `ndjson-framing` |
| transport, sessions, sockets, sketches | `qnethernet-transport` |
| e-stop, controller-loss, hooks, ack | `teensy-safety-hooks` |
| anything under `tests/` | `host-conformance-testing` |
| `.ino` files, build scripts, CI compile | `arduino-cli-builds` |

## Verification (run from the repo root; all must pass)

```bash
python3 tools/check_invariants.py     # static architecture guardrails
python3 tools/check_contract_sync.py  # contract files match pinned hashes
./tools/run_host_tests.sh             # host C++ unit + integration tests
./tools/build_teensy.sh               # Arduino CLI compile (no-op until sketches exist)
```

Hardware/conformance runs (`./tools/run_conformance.sh`, `tests/hardware/`)
require a physical Teensy and are **never** simulated or claimed in hosted CI.
Record real hardware results in `tests/hardware/results/`.

## Task framing

Every agent task must identify, before work starts:

```text
contract sections
required skills
allowed files
forbidden files
acceptance criteria
tests
validation commands
```

No opportunistic unrelated edits. If a needed change falls outside the
allowed files, report it instead of making it.

## Completion report (required from every implementation task)

```text
Task:
Contract sections implemented:
Files changed:
Behavior added:
Tests added:
Validation run:            (paste actual command output summaries)
Known limitations:
Contract questions discovered:
```

Claims that tests pass without shown output are not acceptable.
