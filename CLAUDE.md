# CLAUDE.md

Teensy 4.1 command-server library (board side of the Hyperloop networking
stack). This file is a pointer; the real rules live elsewhere — read them
before changing code.

## Sources of truth (in order)
1. `docs/contracts/V1_Networking_Decisions.md` — **frozen** controller
   contract. It wins on any conflict.
2. `docs/contracts/Teensy_Command_Server_Contract.md` — this repo's contract
   (pending owner ratification; numeric timing defaults provisional).
3. `docs/contracts/Board_Developer_Guide.md` — board-developer-facing rules.
4. `docs/companion/*` — developer guides (API, build/flash, conformance, timing).
5. `AGENTS.md` — boundaries, skills-by-task, commands, completion report.
   It holds the complete authority order; on any doubt, defer to it.
6. `.agents/skills/*/SKILL.md` — read the relevant skill(s) before working.

## Critical invariants (do not break without an explicit contract change)
- **Core/platform boundary:** `src/core` and `src/api` never include
  QNEthernet or Arduino networking types; all platform I/O lives in
  `src/platform/qnethernet`; time enters core via injected clocks.
- **Board statuses are exactly `{ok, error}`** — never `timeout`; new failure
  modes are contract §17 error codes, never new statuses or invented codes.
- **Schema-first, push-only:** schema is the first message on every session;
  telemetry is unsolicited 50 ms push and is the liveness signal — it must
  keep flowing, including during e-stop.
- **Honest e-stop:** `estop_ack` only after the hook confirms safe state
  (`details.state` exactly `"safe"`); no ack on failure; no board-side
  software e-stop latch.
- **Fixed capacity + bounded time:** no per-message dynamic allocation, no
  unbounded queues, every write bounded by the transmit deadline, hooks
  bounded at 100 ms.
- Contracts are hash-pinned; never edit `docs/contracts/` without explicit
  authorization.

## Verification (run from the repo root)
```bash
python3 tools/check_invariants.py     # static architecture guardrails
python3 tools/check_contract_sync.py  # contracts match pinned hashes
./tools/run_host_tests.sh             # host C++ tests
./tools/build_teensy.sh               # Teensy compile (needs arduino-cli)
```
Hardware tests are manual only — never claim them from a host run.
