---
name: repository-conventions
description: >
  Read before any change. Naming, include layout, public/private API
  boundaries, formatting, test placement, and the task completion report
  format for this repo.
---

# Repository Conventions

## Layout and boundaries

* `src/api/` — public board-application interfaces. Header-only where
  possible. Platform-free.
* `src/core/` — implementation: protocol, framing, registry, schema,
  sessions, schedulers, counters, limits. Platform-free; time and I/O enter
  through interfaces defined in `src/api`/`src/core` and injected by the
  platform layer.
* `src/platform/qnethernet/` — the only home for QNEthernet/Arduino
  networking includes.
* `src/TeensyCommandServer.h` — the facade; the one header sketches include.
* Public surface = facade + `src/api/*`. Everything else is private; do not
  document or expose it as API.

## Naming and style

* Files: `PascalCase.h/.cpp`, one class/concern per pair, matching names.
* Classes/structs `PascalCase`; methods/functions `camelCase`; constants and
  enum values `kPascalCase` or `UPPER_SNAKE` for contract vocabulary strings;
  member fields `trailing_underscore_`.
* `#pragma once`; include order: own header, C/C++ std, third-party, repo.
* No exceptions across the library boundary; no RTTI requirements.
* `constexpr` over macros; capacities live in `src/core/Limits.h` only.
* Comments state constraints the code can't show (contract section refs are
  good: `// §13.6: deadline-bounded`), never narrate the code.

## Tests

* `tests/host/unit/test_<component>.cpp`, `tests/host/integration/test_<flow>.cpp`.
* Each test file is self-contained with its own `main` (current harness);
  shared doubles live in `tests/host/fakes/` only.
* A behavior change without a test asserting it is incomplete.

## Task discipline

* Touch only the task's allowed files; report needed-but-forbidden changes
  instead of making them.
* Never edit `docs/contracts/` without explicit authorization (hash-pinned).
* Every task ends with the completion report from `AGENTS.md`, including real
  command output — `check_invariants.py`, `check_contract_sync.py`,
  `run_host_tests.sh` at minimum.
* Contradictions between documents are reported, not resolved ad hoc; real
  judgment calls get an entry in `docs/decisions/`.
