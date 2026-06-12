# Teensy Command Server Backlog

Authoritative ledger of agent tasks, completed strictly in order. Run with:

```bash
python3 tools/agent_orchestrator/orchestrate.py --backlog backlog.md
```

General rules for every task:

* Read `AGENTS.md`.
* Read `docs/contracts/Teensy_Command_Server_Contract.md` (this repo's contract).
* Read `docs/contracts/V1_Networking_Decisions.md` (frozen; wins on any conflict).
* Read `docs/contracts/Board_Developer_Guide.md` if board-developer-facing behavior is affected.
* Read the relevant `.agents/skills/*/SKILL.md` files.
* Preserve the firmware invariants:

  * `src/core` and `src/api` never include QNEthernet/Arduino networking types; all platform I/O lives in `src/platform/qnethernet`; time enters core through injected clocks
  * board terminal statuses are only `ok` and `error` — never `timeout`, never new values; failures use contract §17 error codes
  * command responses echo the controller's `seq` and `controller_ts` untouched; `board_proc_us` is library-owned inside `result`
  * fixed capacity everywhere: no per-message dynamic allocation, no `DynamicJsonDocument`, no unbounded queues; writes bounded by the transmit deadline; hooks bounded at 100 ms
  * schema-first on every session, generated from registration metadata (single source); telemetry is push-only at 50 ms and is the liveness signal — it keeps flowing during e-stop
  * honest e-stop: `estop_ack` only after the hook confirms safe state (`details.state` exactly `"safe"`); no ack on failure; no board-side software e-stop latch
* Do not modify `docs/contracts/`, `AGENTS.md`, or `.agents/skills/` unless the task explicitly grants permission (contracts are hash-pinned).
* If a direct contradiction with the contracts is found, stop and report it instead of editing the contracts.
* Host tests are platform-free and deterministic: fake clock, no wall-clock sleeps.
* Run the validation commands and report results.

Operator gates (not agent tasks; tracked here for visibility):

* Contract ratified 2026-06-12 (first ledger entry below is the record).
  FROZEN still waits on Phase-10 hardware validation.
* Choose a LICENSE.
* Review `docs/companion/Library_API.md` when its task completes — that review gates all `src/api/` work.
* Phase-10 hardware validation of the four provisional timing numerics (values may change, architecture may not — contract §31).

---

* [x] Task: Bootstrap agentic repository (Phase 0)

  Completed outside the ledger, 2026-06-11/12, before the initial commit:
  structure, hash-pinned contracts (`contracts.sha256`, `UPSTREAM_SOURCES.md`),
  `AGENTS.md` + `CLAUDE.md` + eight skills, invariant checker, contract-sync
  checker, host test harness, Teensy build harness, CI, and the agent
  orchestrator ported from special-lamp. Hardened through a six-round codex
  audit cycle (17 findings, all fixed; closing statement issued) plus a
  backlog-extraction bug fix (blank-line-before-checkbox). Recorded here so
  the ledger is complete from the repo's first commit.

---

* [x] Task: OPERATOR GATE — ratify the Teensy command-server contract

  RATIFIED 2026-06-12 by the operator (Justin), conveyed in session; the §31
  judgment calls are accepted. Status header updated to RATIFIED FOR
  IMPLEMENTATION and the hash manifest regenerated in the same change. The
  contract is not yet FROZEN — that waits on Phase-10 hardware validation of
  the four provisional timing numerics.

  ## Goal

  This is an operator action recorded as a blocking ledger item because the
  contract's own status line requires it: "Ratify via backlog before
  firmware work begins." No implementation task below may run until the
  operator checks this entry off.

  Operator steps:

  * Review and ratify `docs/contracts/Teensy_Command_Server_Contract.md`,
    in particular the five resolved judgment calls in §31.
  * Update the contract's Status header (e.g. "RATIFIED FOR IMPLEMENTATION —
    numeric timing defaults provisional pending hardware conformance, §31").
  * Run `python3 tools/check_contract_sync.py --update` in the same commit.
  * Check this task off (`* [x]`) — the checkmark is the ratification record.

  ## Agent instructions

  If you are an agent executing this task: it is not implementable by you.
  Make NO file changes. Report that the backlog is blocked on operator
  ratification and stop. (Making no changes halts the orchestrator run
  without committing and without checking this task off.)

  ## Validation

  None — operator action. The contract-sync check enforces that the Status
  edit and the manifest update land together.

---

* [ ] Task: Define fixed limits and integer widths (Limits.h)

  ## Goal

  Create `src/core/Limits.h` as the single home for every compile-time
  capacity and timing default in the library. Everything downstream sizes
  from this header; no constant it owns may be redefined elsewhere.

  ## Scope

  Implement, all `constexpr`, no macros, each with a contract-section
  comment:

  * `kBoardRxMaxLineBytes = 1024` and `kBoardTxMaxLineBytes = 8192` — both
    counted **including the terminating newline** (§10.1–10.3); say so in
    the comment
  * `kTelemetryPeriodMs = 50` (§18)
  * `kLivenessWindowMs = 250` (§18.6 — controller-side fact, documented for
    tests)
  * `kTransmitDeadlineMs = 100` and
    `kTelemetryDeadlineFailuresToTeardown = 10` (§13.6)
  * `kHookBudgetMs = 100` — e-stop and controller-loss hooks (§19.1, §21)
  * registration window 2000 ms, informational (§8.4)
  * parser/serializer buffer capacities sized against the line limits (§11)
  * registry capacities: max commands, max args per command — conservative
    values, commented that the full schema must fit 8192 bytes (§14)
  * integer-width aliases: `uint64_t` for seq values, `int32_t` for contract
    `int` arguments (§6.2)

  Mark the four provisional timing numerics (transmit deadline, both hook
  budgets, teardown threshold) with a comment referencing contract §31:
  hardware validation may change values, never architecture.

  ## Do not implement

  * any behavior, parsing, or I/O — constants and aliases only
  * Arduino/QNEthernet includes
  * dynamic allocation of any kind

  ## Tests should cover

  * `tests/host/unit/test_limits.cpp` (self-contained main) statically
    asserts the contract-pinned values: 1024, 8192, 50, 250, 100, 10, 100
  * buffer capacities are >= their corresponding line limits
  * header compiles standalone under `c++ -std=c++17 -I src`

  ## Files

  * `src/core/Limits.h`
  * `tests/host/unit/test_limits.cpp`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [ ] Task: Define the protocol vocabulary (Protocol.h)

  ## Goal

  Create `src/core/Protocol.h` holding the closed wire vocabulary as enums
  and constants with to-string mappings. Types only — parsing, validation,
  and serialization are later framing tasks.

  ## Scope

  Implement:

  * message type names: schema, command, response, telemetry, event, estop,
    heartbeat (§14–§23)
  * board terminal statuses: `ok`, `error` — exactly two; `timeout` must not
    appear anywhere in the board vocabulary (§16.4, controller-owned)
  * board error codes: MISSING_FIELD, INVALID_TYPE, UNKNOWN_COMMAND,
    INVALID_ARGUMENT, INTERNAL_ERROR, ESTOP_ACTIVE — with a comment that
    ESTOP_ACTIVE is condition-based only, never latched (§17, §6.5)
  * event names: `estop_ack`, `estop_triggered`; the literal `estop_ack`
    details state `"safe"` (§19.2, §20)
  * protocol version `"1"` (§3.2)
  * argument type names: int, float, bool, string (§6.2)
  * field-name constants — every key used by the contract message shapes:
    type, seq, timestamp, controller_ts, source, target, command, args,
    status, result, error, code, message, telemetry, schema, event, details,
    reason, protocol_version, firmware_version, blocked_by_estop,
    board_proc_us, commands, state (where `commands`/`state` are the schema
    body keys §14 and `reason` is the `estop_triggered` details key §20)

  Enums are closed: every vocabulary item carries its contract-section
  reference; header-only; platform-free; no allocation.

  ## Do not implement

  * parsing, validation, serialization, framing (Phase 3)
  * controller-owned error codes (BOARD_UNAVAILABLE, BOARD_BUSY,
    COMMAND_TIMEOUT, CONTROLLER_SHUTDOWN, UNKNOWN_TARGET,
    PROTOCOL_VERSION_MISMATCH) — they must not exist board-side
  * new message types, statuses, codes, or event names

  ## Tests should cover

  * exact string values of every vocabulary item
  * the status enum has exactly two members
  * the error-code set matches §17 exactly — no controller-owned codes
  * round-trip of every to-string mapping

  ## Files

  * `src/core/Protocol.h`
  * `tests/host/unit/test_protocol_vocab.cpp`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [ ] Task: Define bounded counters (Counters.h)

  ## Goal

  Create `src/core/Counters.h`: the contract §25 counter set as a plain
  struct with fixed unsigned fields, an increment helper, and a const
  snapshot accessor.

  ## Scope

  One field per counter — this list is authoritative (§25 plus the
  audit-cycle additions):

  sessions_accepted, sessions_rejected, sessions_superseded, schemas_sent,
  commands_received, commands_ok, commands_error, unknown_commands,
  invalid_arguments, invalid_json, invalid_targets, oversized_lines,
  telemetry_sent, telemetry_coalesced, telemetry_dropped, estop_received,
  estop_ack_sent, estop_apply_failed, estop_hook_over_budget,
  controller_loss_hook_over_budget, heartbeat_received, heartbeat_ack_sent,
  tx_failures, controller_disconnects

  Unsigned wraparound is acceptable and documented. No atomics — the service
  loop is single-threaded; comment why. No logging, no allocation.

  ## Do not implement

  * counter consumers or wiring (each later task increments its own)
  * telemetry/diagnostic exposure of counters (later task, §25)

  ## Tests should cover

  * zero-initialization of every field
  * increments observed through the snapshot accessor
  * compile-time use of every listed field (a removed counter fails the build)

  ## Files

  * `src/core/Counters.h`
  * `tests/host/unit/test_counters.cpp`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [ ] Task: Define core time and transport seams with host fakes

  ## Goal

  Create the two injection seams that make the whole library host-testable —
  `Clock` and `Transport` in `src/core` — plus the shared deterministic
  fakes in `tests/host/fakes/`.

  ## Scope

  Implement in `src/core` (abstract, header-only, no Arduino types):

  * `Clock`: monotonic milliseconds and microseconds
  * `Transport`: non-blocking read of available bytes into a caller buffer;
    write-some returning bytes accepted (may be partial or zero); flush; and
    a connection-state query distinguishing the four §24 conditions — open,
    bytes-available, closed, link-down

  Implement in `tests/host/fakes/`:

  * `FakeClock`: manual advance only, never wall-clock
  * `FakeTransport`: scripted inbound byte chunks delivered across reads at
    arbitrary split points; configurable per-write byte acceptance to
    simulate partial writes and a peer that stops reading; scriptable
    closure and link loss; records all written bytes for assertions

  Fakes are the shared test doubles for every later phase — no per-test
  variants (host-conformance-testing skill).

  ## Do not implement

  * the QNEthernet adapter (Phase 9)
  * framing, buffering, or retry logic on top of the seams (Phases 3/5)
  * allocation requirements in the interfaces

  ## Tests should cover

  * split-delivery of a scripted message across multiple reads
  * partial-write acceptance counting and a peer that stops accepting bytes
  * scripted closure mid-script and link-loss reporting
  * clock advance observed through the interface

  ## Files

  * `src/core/Clock.h`, `src/core/Transport.h`
  * `tests/host/fakes/FakeClock.h`, `tests/host/fakes/FakeTransport.h`
  * `tests/host/unit/test_fakes.cpp`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [ ] Task: Define session lifecycle states and the transition table

  ## Goal

  Create `src/core/SessionState.h`: the six contract §8 lifecycle states as
  a closed enum, a to-string mapping, and a pure total
  `isValidTransition(from, to)` predicate. No state-machine driver — that is
  Phase 4.

  ## Scope

  * exactly six states: BOOT_SAFE, NETWORK_STARTING, LISTENING,
    SESSION_CONNECTED, SESSION_ACTIVE, SESSION_CLOSING (§8)
  * legal edges: the §8 forward chain
    (BOOT_SAFE → NETWORK_STARTING → LISTENING → SESSION_CONNECTED →
    SESSION_ACTIVE → SESSION_CLOSING → LISTENING), plus
    SESSION_CONNECTED → SESSION_CLOSING (schema-send failure, §9.3), plus
    any active-session state → SESSION_CLOSING (controller loss and
    supersession, §21, §9.1)
  * the predicate is total: every (from, to) pair returns a defined answer
  * the enum must not encode e-stop — connection lifecycle and safety state
    are orthogonal axes; comment referencing V1 contract section 2

  ## Do not implement

  * the state machine driver, timers, or transitions-with-side-effects
    (Phase 4)
  * any e-stop or safety state representation

  ## Tests should cover

  * the full 6x6 transition matrix, written out from the contract text —
    not generated from the implementation

  ## Files

  * `src/core/SessionState.h`
  * `tests/host/unit/test_session_state.cpp`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [ ] Task: Draft the Library API companion document

  ## Goal

  Replace the `docs/companion/Library_API.md` stub with a complete proposal
  of the public C++ surface — exact signatures for every contract §5
  operation. This is a documentation task: it produces the proposal the
  operator reviews; no source files.

  PENDING OPERATOR REVIEW: this document gates Phase 2 — no `src/api/`
  header may be written until the operator has reviewed it. The document
  must say so in its own status line.

  ## Scope

  Propose:

  * exact signatures for the §5 operations: set identity, register command,
    register telemetry schema, register state schema, set telemetry
    provider, set e-stop hook, set controller-loss hook, start, service
  * the command-handler signature and result types: ok-with-object /
    error-with-code+message, no exceptions across the boundary (§7.3, §7.4)
  * hook signatures with the 100 ms budget and a boolean safe-state return
    (§19.1, §21; Board_Developer_Guide section 10)
  * the telemetry provider signature — fast, non-blocking, latest snapshot
    (§18.2)
  * registration-table behavior: fail on duplicate (§5.2), fail after seal
    (§5.1), capacities from Limits.h
  * a one-to-one mapping table: every public operation → its contract §5
    line
  * explicit non-goals: no dynamic registration, no exceptions, no Arduino
    types in the API, no networking exposed to board applications

  ## Do not implement

  * any source file — `src/api/` headers are written in Phase 2, after the
    operator review
  * changes to `docs/contracts/` (this is a companion doc; the contract wins
    on any conflict)

  ## Tests should cover

  * none (documentation task); validation commands still run and must pass

  ## Files

  * `docs/companion/Library_API.md`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

<!-- Planned future tasks, appended as ledger entries when their
     predecessors complete (phase roadmap from the bootstrap plan):

     Phase 2 — registration and schema: src/api headers per the reviewed
       Library_API.md; command registry with duplicate rejection (§5.2, §6);
       schema generation from registry metadata, single source (§5.3, §14);
       argument-type enforcement (§6.2); schema size validation vs 8192.
     Phase 3 — framing and parsing: bounded incremental line framer with
       discard-through-newline (§10); fixed-capacity JSON parse + message
       validation (§10.6, §11).
     Phase 4 — session lifecycle: state machine driver; schema-first within
       the 2 s window (§8.4, §9.3); close-old supersession ordering (§9.1);
       controller-loss path with hook budget measurement (§21).
     Phase 5 — serialized transmit path: single outbound path with priority
       order (§13.4); deadline-bounded partial-write retries (§13.1, §13.6);
       compact serialization + flush (§13.2).
     Phase 6 — command dispatch: target validation before dispatch (§3.1,
       §23); argument validation with §17 codes; response building with
       seq/controller_ts echo and library-owned board_proc_us (§16).
     Phase 7 — telemetry: 50 ms coalescing scheduler (§18, §13.5); monotonic
       sequencing; telemetry-through-estop and liveness guarantees (§18.6);
       consecutive-deadline-failure teardown (§13.6).
     Phase 8 — safety and heartbeat: estop dispatch ahead of ordinary
       messages (§19, §23); hook budget measurement + truthful estop_ack
       (§19.1, §19.2); estop_triggered emission (§20); heartbeat ack (§22).
     Phase 9 — QNEthernet adapter: transport/server/clock implementations
       (§3.4, §9.1, §10.4, §13.2, §13.3); ethernet_smoke sketch; enable the
       Teensy compile CI steps.
     Phase 10 — conformance and hardware: Python conformance client;
       hardware validation of the four §31 numerics; record results in
       tests/hardware/results/; operator review → contract FROZEN. -->
