---
name: command-server-contract
description: >
  Read before any change in this repo. Authority order, frozen message shapes,
  line limits, sequence rules, e-stop and liveness rules, prohibited behavior,
  and required validation commands for the Teensy 4.1 command server.
---

# Command Server Contract

> **Authority.** `docs/contracts/V1_Networking_Decisions.md` (frozen) >
> `docs/contracts/Teensy_Command_Server_Contract.md` (this repo's contract) >
> `Board_Developer_Guide.md` > companion docs > AGENTS.md > skills > task text.
> On contradiction: stop and report. Parenthetical refs (§n) point at the
> Teensy contract unless marked V1.

## Wire basics (§10)

* UTF-8 newline-delimited JSON, one object per line, exactly one trailing `\n`,
  compact serialization (no inter-token whitespace).
* Receive limit (controller→board): **1024 bytes including the newline**.
* Transmit limit (board→controller): **8192 bytes including the newline**.
  Oversized board output is session-fatal controller-side — never send it.
* TCP read boundaries are never message boundaries.

## Sequence and timestamp rules (§15, §16, §18.3)

* Command `seq` is the controller-owned `board_seq` (uint64). Echo it
  unchanged in the response; never substitute.
* `controller_ts` is an opaque controller-monotonic token. Echo untouched;
  never convert, compare, interpret, or persist.
* Schema/telemetry/event `seq` is board-local and informational; telemetry
  seq is monotonic per boot.
* `timestamp` fields are board-local uptime, informational only.

## Closed vocabularies (§16.4, §17)

* Board statuses: `ok`, `error` — never `timeout`.
* Board error codes: `MISSING_FIELD`, `INVALID_TYPE`, `UNKNOWN_COMMAND`,
  `INVALID_ARGUMENT`, `INTERNAL_ERROR`, plus condition-based-only
  `ESTOP_ACTIVE`. Everything else is controller-owned. An invented code makes
  the whole response malformed → dropped → controller times the command out.
* No new message types or event names. `board_proc_us` lives inside `result`
  and is library-owned. `estop_ack.details.state` is exactly `"safe"`.

## Session rules (§8, §9)

* Schema is the first message of every session, sent immediately on accept
  (controller registration timeout: 2 s). No board-level `get_schema`.
* Exactly one active session; a replacement connection supersedes the old one
  (close-old, exact six-step ordering in §9.1).
* Wrong `target` ⇒ drop + count (`invalid_targets`), no response, no hooks —
  target validation precedes type dispatch (§3.1, §23).

## Liveness and timing (§18.6, §13.6, §19.1)

* Telemetry is push-only at 50 ms and is the liveness signal: the controller
  FAULTs after ~250 ms of silence. Telemetry starts within 250 ms of schema
  and keeps flowing during e-stop.
* Every write has a finite transmit deadline (default 100 ms). Critical-message
  deadline expiry closes the session; 10 consecutive telemetry failures close
  the session.
* E-stop and controller-loss hooks: 100 ms budget, measured, counted.
* All four numbers are provisional pending hardware conformance (§31):
  hardware results may change values, never architecture.

## Prohibited (§27 — non-exhaustive, read the section)

No Redis, no GUI, no board-to-board routing, no commands outside the schema,
no network I/O in ISRs, no unbounded allocation or queues, no false or
qualified `estop_ack`, no board-side software e-stop latch, no blocking on
physical completion, no unbounded write retries.

## Validate every change

```bash
python3 tools/check_invariants.py
python3 tools/check_contract_sync.py
./tools/run_host_tests.sh
```
