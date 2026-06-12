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
  * layered include direction, enforced by `check_invariants.py`: `src/support` → std only; `src/api` → support; `src/core` → api + support; `src/platform` → all (`Limits.h` and `BoundedJsonWriter.h` live in `src/support`)
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
* Review `docs/companion/Library_API.md` — ledgered below as an unchecked OPERATOR GATE entry (the checkmark is the acceptance record); it gates all `src/api/` work.
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

* [x] Task: Define fixed limits and integer widths (Limits.h)

  RELOCATED 2026-06-12: originally created under `src/core`, then moved
  during dependency-layer hardening to `src/support/Limits.h` in namespace
  `teensy_command_server::support` (tests and includers updated in the same
  change). The support-layer location is authoritative; `src/core/Limits.h`
  no longer exists.

  ## Goal

  Create `Limits.h` (now `src/support/Limits.h` — see the relocation note
  above) as the single home for every compile-time capacity and timing
  default in the library. Everything downstream sizes from this header; no
  constant it owns may be redefined elsewhere.

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

  * `tests/host/unit/test_limits.cpp` (self-contained main, includes
    `support/Limits.h`) statically asserts the contract-pinned values:
    1024, 8192, 50, 250, 100, 10, 100
  * buffer capacities are >= their corresponding line limits
  * header compiles standalone under `c++ -std=c++17 -I src`
    (include path unchanged by the relocation; the header is reached as
    `support/Limits.h`)

  ## Files

  * `src/support/Limits.h` (originally `src/core/Limits.h`; see the
    relocation note above)
  * `tests/host/unit/test_limits.cpp`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [x] Task: Define the protocol vocabulary (Protocol.h)

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

* [x] Task: Define bounded counters (Counters.h)

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

* [x] Task: Define core time and transport seams with host fakes

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

* [x] Task: Define session lifecycle states and the transition table

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

* [x] Task: Draft the Library API companion document

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

* [ ] Task: OPERATOR GATE — review and accept the Library API proposal

  ## Goal

  Operator action recorded as a blocking ledger item: review
  `docs/companion/Library_API.md` (committed 2026-06-12) and accept or amend
  the proposed public C++ surface. Every Phase-2 task below writes `src/api/`
  headers from that document, so nothing below may run until this entry is
  checked off.

  Operator steps:

  * Review the proposal end to end: core value types, command metadata,
    `CommandArgs`/`ObjectWriter`, `CommandResult` and the handler signature,
    telemetry provider, safety hooks, facade, registration behavior, the
    ownership/lifetime and dependency-direction rules, the one-to-one §5
    mapping table, and the non-goals.
  * Request changes by editing the document directly (it is a companion doc,
    not hash-pinned), or accept it as written.
  * Change its status line from PENDING OPERATOR REVIEW to
    ACCEPTED <date> in the same change.
  * Check this task off (`* [x]`) — the checkmark is the acceptance record.

  ## Agent instructions

  If you are an agent executing this task: it is not implementable by you.
  Make NO file changes. Report that the backlog is blocked on the operator's
  Library API review and stop. (Making no changes halts the orchestrator run
  without committing and without checking this task off.)

  ## Validation

  None — operator action.

---

* [ ] Task: Define the public API value and metadata types

  ## Goal

  Create the data-only `src/api/` headers exactly as specified by the
  accepted `docs/companion/Library_API.md`: board identity and network
  configuration, registration/startup status reporting, and command
  metadata. Pure types — no registry, no engine, no callback machinery.

  ## Scope

  * `ValueType` (the four §6.2 argument types), `ErrorCode` (exactly the six
    board-emittable §17 codes — controller-owned codes like `BOARD_BUSY` are
    not API values), `StatusCode` and `Status` (local registration/startup
    outcomes, never wire statuses) per the proposal's Core Value Types
  * `BoardIdentity`, `NetworkConfig` (plain bytes and scalars — no Arduino
    networking types), `ServerConfig` (§3)
  * `ArgumentSpec`, `CommandSpec` (explicit `blocked_by_estop`, §6.4),
    `FieldSpec` per the proposal's Command Metadata section
  * the canonical API-to-wire error mapping is production code, pinned to
    `src/core/ErrorCodeMapping.h` (core may include the api header that
    declares `ErrorCode`, plus `core/Protocol.h`; `src/api` still never
    includes core — enforced by `check_invariants.py`):
    `constexpr` conversion from `api::ErrorCode` to `core::BoardErrorCode`
    and a `constexpr` reverse conversion, both total and one-to-one across
    all six board-emittable codes, with no default fallthrough that
    silently maps an unknown value — future dispatch code uses this one
    conversion, never its own
  * header layout follows the proposal's Public Surface list

  ## Do not implement

  * `CommandArgs`, `ObjectWriter`, `CommandResult`, hook/provider typedefs
    (next task)
  * the registry, schema generation, the facade, any engine logic

  ## Tests should cover

  * the `ErrorCodeMapping.h` conversion, both directions, across the
    complete six-code set — written out from the contract §17 list, not
    generated; round-trip (`api → core → api`) is identity for every code
  * `Status::ok()` truth table across all `StatusCode` values
  * specs reference `src/support/Limits.h` capacities (compile-time checks)

  ## Files

  * `src/api/BoardIdentity.h`, `src/api/ServerStatus.h`,
    `src/api/CommandTypes.h` (exact split per the accepted proposal)
  * `src/core/ErrorCodeMapping.h`
  * `tests/host/unit/test_api_types.cpp`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [ ] Task: Define the handler boundary types (results, writers, hooks)

  ## Goal

  Create the callback-boundary surface from the accepted proposal:
  `CommandResult` with its factory helpers and the `CommandHandler` typedef,
  the bounded `ObjectWriter`, the `TelemetryProvider` and `SafetyHook`
  typedefs, and the `CommandArgs`/`CommandContext` declarations.

  ## Scope

  * `src/support/BoundedJsonWriter.h` first (namespace
    `teensy_command_server::support` — the neutral dependency layer, so api
    may use it without depending on core; include direction is enforced by
    `check_invariants.py`): the single bounded compact-JSON writer for the
    whole library — escaping, compact formatting, overflow behavior, and
    byte accounting implemented exactly once. The schema builder and all
    later response/telemetry/event/heartbeat builders MUST reuse it; no
    later phase may introduce a second serializer (proposal, Object Writers
    section)
  * BoundedJsonWriter serializes finite double values using one pinned
    round-trip-safe policy: `DBL_DECIMAL_DIG` (17) significant digits
    (chosen over shortest-round-trip formatting because `std::to_chars`
    floating-point support is not guaranteed by the Teensy toolchain).
    Serializing a finite double and parsing it again recovers the
    identical double value. NaN and positive/negative infinity are
    rejected (JSON has no representation for them; the write fails, it
    does not emit `null`)
  * `api::ObjectWriter` wraps or delegates to `support::BoundedJsonWriter`;
    every `add*` returns false on overflow instead of truncating or
    reallocating; rejects the library-owned `board_proc_us` key (§16)
  * ObjectWriter capacity is the response budget minus reserved envelope
    overhead (response keys, `status`, `result`/`error` structure,
    `board_proc_us`, escaping headroom, terminating newline) — a handler
    must never successfully produce a result the library cannot wrap into a
    valid 8192-byte response line (§10.3, §11.2; proposal, Object Writers)
  * this task explicitly grants extending `src/support/Limits.h` with the
    named constants it needs — e.g. `kResponseEnvelopeReserveBytes`,
    `kMaxResultPayloadBytes` (derived from `kBoardTxMaxLineBytes` minus the
    reserve), `kMaxErrorMessageBytes` — values beyond capacity fail
    explicitly; no silent truncation; no arbitrary stack-buffer sizes
  * `CommandResult`: `ok()`/`error()` factories, the five named helpers,
    `isOk`/`errorCode`/`message` accessors; no exceptions cross the handler
    boundary (§7.3, §7.4). Five helpers for six §17 codes is intentional:
    `UNKNOWN_COMMAND` is dispatcher-emitted only (proposal, Command Results)
  * result data and error messages follow the proposal's Ownership and
    Lifetimes rules: copied into bounded library-owned storage before the
    handler returns; no handler-returned pointer dereferenced afterward
  * `CommandHandler`, `TelemetryProvider`, `SafetyHook` as function-pointer
    typedefs with the caller-owned `void* context` (no `std::function`, no
    allocation); context ownership per the proposal's Ownership rules
  * `CommandArgs`/`CommandContext` declared per the proposal; the
    parsed-document backing lands with the Phase-3 parse task — define the
    storage seam now, platform-free
  * hook semantics documented at the declaration site: provisional 100 ms
    budget, boolean safe-state truth, idempotency (§19.1, §21, §31.5)

  ## Do not implement

  * the registry, dispatch, schema generation, telemetry scheduling
  * JSON parsing (`CommandArgs` wiring is Phase 3)

  ## Tests should cover

  * BoundedJsonWriter: compact output (no inter-token whitespace, §10);
    string escaping for quotes, backslashes, and control characters; byte
    accounting exact against hand-computed lengths; overflow returns false
    and leaves prior content valid
  * double round-trip at the pinned policy: a high-precision controller
    timestamp (e.g. `1718210123.4567891`), `1.0`, negative values, very
    small finite values (denormal-range), very large finite values
    (near `DBL_MAX`) — each serializes then parses back to the identical
    double; NaN rejected; positive and negative infinity rejected
  * ObjectWriter: overflow at the reserved-envelope capacity, not at 8192;
    `board_proc_us` rejected
  * CommandResult factories and accessors, including each named helper;
    error-message lifetime — the message survives the caller's source
    buffer being overwritten after the factory call returns

  ## Files

  * `src/support/BoundedJsonWriter.h`
  * `src/api/CommandResult.h`, `src/api/TelemetryProvider.h`,
    `src/api/SafetyHooks.h`, writer/args headers per the proposal's layout
  * `tests/host/unit/test_bounded_json_writer.cpp`,
    `tests/host/unit/test_object_writer.cpp`,
    `tests/host/unit/test_command_result.cpp`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [ ] Task: Implement the fixed-capacity command registry

  ## Goal

  The single registration table behind §5: identity, command specs with
  handlers and contexts, telemetry/state field schemas, the provider and
  both hooks. Duplicate rejection, seal-at-startup. This is the one source
  of truth that both dispatch and schema generation read (§5.3) — there must
  never be a second table.

  ## Scope

  * read the fixed-capacity-cpp skill first; no dynamic allocation
  * capacities from `src/support/Limits.h`; registration past capacity
    fails with `StatusCode::CapacityExceeded`, no reallocation, no
    truncation. This task explicitly grants extending `Limits.h` with the
    named string and table capacities it needs — e.g. `kMaxBoardIdBytes`,
    `kMaxFirmwareVersionBytes`, `kMaxCommandNameBytes`, `kMaxArgNameBytes`,
    `kMaxFieldNameBytes`, `kMaxTelemetryFields`, `kMaxStateFields` —
    over-length values fail explicitly; no silent truncation
  * duplicate command name fails registration — never silent replacement
    (§5.2); name rules per §6.1
  * explicit `blocked_by_estop` required on every spec (§6.4)
  * argument schemas restricted to the four §6.2 types
  * sealing is the three-operation lifecycle pinned in the proposal's
    Registration Behavior section — this task and the schema task implement
    the same lifecycle; neither redesigns it. This task provides the two
    registry operations only: `validateMetadataForSeal()` (const,
    non-mutating completeness check) and `commitSeal()` (atomic transition
    to SEALED; post-seal registration fails with
    `StatusCode::RegistrationSealed`, §5.1). The schema-size check is
    `SchemaBuilder::validateMaximumSchemaSize(...)` in the schema task —
    the builder reads the registry; `CommandRegistry` must never depend on
    `SchemaBuilder`. Only the future startup/facade path sequences
    validate-metadata → validate-schema-size → commit; any validation
    failure leaves the registry MUTABLE
  * lookup by command name for the future dispatch path
  * ownership per the proposal's Ownership and Lifetimes section: the
    registry copies every registration string (board IDs, firmware
    versions, command names, argument names, field names) into
    library-owned fixed-capacity storage; it never retains a pointer to
    caller-owned data
  * dependency direction is pinned, not the agent's choice, and
    `check_invariants.py` enforces it: `CommandRegistry` lives in
    `src/core/CommandRegistry.h`; the full matrix is in the proposal's
    Dependency Direction section (support → std only; api → support;
    core → api + support; platform → all)

  ## Do not implement

  * schema serialization (next task), inbound argument validation, dispatch,
    the facade

  ## Tests should cover

  * the §28.4 registration subset: duplicate fails; fill to capacity then
    reject one more; explicit blocked_by_estop enforced
  * the seal lifecycle: `validateMetadataForSeal()` mutates nothing;
    registration after `commitSeal()` fails; registration still works
    after a failed validation (the registry remained MUTABLE)
  * lookup hit and miss; registered metadata round-trips intact
  * copied-string ownership: register from a mutable buffer, overwrite the
    buffer, verify the registry's stored strings are unaffected
  * over-length registration strings fail explicitly — never truncated

  ## Files

  * `src/core/CommandRegistry.h`
  * `tests/host/unit/test_command_registry.cpp`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [ ] Task: Generate the schema message from the registry

  ## Goal

  Build the compact §14 schema line from the registry plus board identity —
  generated from the same metadata dispatch uses, single source (§5.3) —
  and provide `SchemaBuilder::validateMaximumSchemaSize(...)`, the
  seal-time size check in the proposal's three-operation lifecycle. This
  task produces and validates the bytes; the Phase-4 session engine sends
  them.

  ## Scope

  * every §14 field, exact key strings from `core/Protocol.h` field
    constants, with the wrapper shape pinned exactly — top-level fields:
    `type`, `seq`, `timestamp`, `source`, `target`, `protocol_version`,
    `schema`; inside `schema`: `commands`, `telemetry`, `state`,
    `firmware_version`. None of the inner four may be flattened into the
    top-level object
  * schema `seq` is fixed to 1 (§14 shape; board-local, informational);
    `timestamp` is the board-local monotonic uptime integer from the
    injected clock — no wall-clock access in core
  * serialization goes through `support/BoundedJsonWriter.h` — no second
    serializer
  * deterministic canonical key ordering (registration order for commands
    and fields, fixed §14 order for envelope keys) so golden-byte tests are
    valid and reproducible
  * compact serialization, one line, newline-terminated (§10)
  * this task provides
    `SchemaBuilder::validateMaximumSchemaSize(const CommandRegistry&,
    const BoardIdentity&)`, completing the three-operation lifecycle
    pinned in the proposal's Registration Behavior section (same lifecycle
    the registry task implements; neither task redesigns it). The builder
    reads the registry; `CommandRegistry` must never depend on
    `SchemaBuilder`. The check is const/non-mutating and must not validate
    with small numbers and overflow at runtime — it reserves or serializes
    the maximum encoded width of every variable-width field (timestamp at
    maximum width, every registered string at its stored length); runtime
    serialization still checks the final encoded length including the
    terminating newline (§10.3, §11.2)
  * a failed schema-size validation fails startup and leaves the registry
    MUTABLE (`commitSeal()` is never reached; only the startup/facade path
    calls it) — an oversized schema is a startup failure, not a runtime
    surprise, and never leaves the registry sealed
  * bounded serialization buffer (§11.1)

  ## Do not implement

  * sending, sessions, telemetry frames, response serialization

  ## Tests should cover

  * golden-byte schema for a small fixed registry, written out from the §14
    text (not round-tripped through the serializer) — valid because key
    ordering is canonical
  * fits-8192 acceptance and a constructed rejection case
  * seal-time reservation vs runtime width: a schema accepted at seal time
    with maximum-width reservation never exceeds the limit at runtime
  * a failed validation leaves the registry MUTABLE and accepting
    registrations; a passed validation followed by `commitSeal()` rejects
    them
  * `source` equals the configured board ID; every command carries explicit
    blocked_by_estop (§28.4)

  ## Files

  * `src/core/SchemaBuilder.h`
  * `tests/host/unit/test_schema_builder.cpp`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [ ] Task: Implement the bounded NDJSON line framer

  ## Goal

  `src/core` incremental line accumulator for the inbound byte stream:
  newline-delimited, 1024-byte receive limit including the newline (§10.2),
  discard-through-newline oversize handling (§10.5). Pure bytes-in,
  lines-out — no JSON, no transport, no dispatch.

  ## Scope

  * read the ndjson-framing skill first
  * the boundary is pinned, not interpretable: maximum wire line is 1024
    bytes including the newline (§10.1, §10.2); maximum payload before the
    newline is 1023 bytes; the framer delivers the payload as a mutable,
    NUL-terminated buffer with the newline removed, so its storage holds
    1023 payload bytes plus one NUL byte (the reserved untransmitted
    terminator)
  * the buffer lifecycle is an explicit acquire/release API, pinned (the
    Phase-3 parse task deserializes in place and retains string pointers
    into this buffer):

    ```cpp
    bool hasLine() const;
    MutableLineView acquireLine();
    void releaseLine();
    ```

    semantics: only one line may be acquired at a time; `acquireLine()`
    exposes a stable mutable NUL-terminated payload; the framer must not
    overwrite or reuse that payload before `releaseLine()`; parser state
    and `CommandArgs` string views remain valid only while the line is
    acquired; a second `acquireLine()` before `releaseLine()` fails;
    `releaseLine()` invalidates all views into the payload; session
    closure invalidates and releases any active payload
  * TCP read boundaries are not message boundaries (§10.4): one message
    split across reads; multiple complete messages in one read; a complete
    line followed by a partial line; closure during a partial line discards
    it
  * oversize: enter discard mode through the next newline, increment the
    oversized-line counter (`core/Counters.h`), never surface a truncated
    prefix, recover for the next valid line (§10.5)

  ## Do not implement

  * UTF-8/JSON validation (§10.6 belongs to the parse task)
  * transport reads, dispatch, responses

  ## Tests should cover

  * the §28.3 framing rows, table-driven at exact byte boundaries, with
    each case naming whether its length is payload bytes or total wire
    bytes: 1024 wire bytes (1023 payload + newline) accepted; 1025 wire
    bytes discarded
  * an oversized line and the next valid line arriving in the same
    transport chunk: the oversized line is discarded, the valid line is
    delivered
  * the acquire/release lifecycle: a second acquisition before release is
    rejected; the active payload remains byte-identical while later input
    arrives; release allows the next framed line to be acquired; session
    closure with an acquired line invalidates it safely
  * counter increments observable through the framer's seam

  ## Files

  * `src/core/LineFramer.h`
  * `tests/host/unit/test_line_framer.cpp`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [ ] Task: OPERATOR GATE — vendor ArduinoJson 6.21.5

  ## Goal

  Operator action recorded as a blocking ledger item: dependency ingestion
  is a supply-chain action and stays human. The orchestrator hard-stops any
  agent change under `third_party/` (no task keyword can grant it), so the
  parse task below cannot run until the operator lands this commit.

  Operator steps:

  * Download the single-header release `ArduinoJson-v6.21.5.h` from the
    upstream project (https://github.com/bblanchon/ArduinoJson, tag
    `v6.21.5`) and place it at
    `third_party/ArduinoJson/ArduinoJson-v6.21.5.h`.
  * Preserve the upstream MIT license file alongside it
    (`third_party/ArduinoJson/LICENSE.txt`).
  * Record in `docs/contracts/UPSTREAM_SOURCES.md`: exact upstream URL, the
    `v6.21.5` tag, the release commit hash, and the `sha256` of the vendored
    header (integrity hash).
  * Policy notes, recorded with the entry: agents never edit
    `third_party/` (orchestrator-enforced); the path is excluded from
    project formatting/lint rewrites; it remains included in secret/malware
    scanning and the invariant sweep.
  * Commit, then check this task off (`* [x]`) — the checkmark plus the
    UPSTREAM_SOURCES.md entry are the ingestion record.

  ## Agent instructions

  If you are an agent executing this task: it is not implementable by you.
  Make NO file changes. Report that the backlog is blocked on operator
  dependency ingestion and stop.

  ## Validation

  Operator runs the standard four commands after vendoring; all must pass
  with the vendored header present.

---

* [ ] Task: Parse inbound messages with fixed-capacity JSON

  ## Goal

  Bounded parse and classification of one framed line into a typed inbound
  message (command / estop / heartbeat) with §10.6 failure behavior, and the
  real `CommandArgs` backing. Uses a fixed-capacity ArduinoJson document per
  the fixed-capacity-cpp skill.

  ## Scope

  * the JSON dependency is pinned exactly: ArduinoJson 6.21.5 with
    `StaticJsonDocument` (the contract's named example, fixed-capacity by
    construction), consumed from the operator-vendored
    `third_party/ArduinoJson/` (the OPERATOR GATE above). A "v7
    equivalent" is not acceptable. Do not modify anything under
    `third_party/` or `docs/contracts/` — wire include paths through the
    build scripts instead
  * the storage model is mutable-input zero-copy parsing, pinned: the line
    framer provides a mutable, NUL-terminated payload buffer;
    `deserializeJson` receives that mutable buffer and may retain string
    pointers into it; the line buffer and the `StaticJsonDocument` remain
    unchanged until validation, dispatch, and the current handler
    invocation have completed — the framer's pinned
    `acquireLine()`/`releaseLine()` lifecycle is the mechanism, and
    `releaseLine()` happens only after those complete; `CommandArgs`
    string views are valid only during that handler invocation
  * `StaticJsonDocument` capacity is sized from the maximum structural
    overhead of the permitted inbound message shapes (§15 command with
    `kMaxArgsPerCommand` args, §19 estop, §22 heartbeat) — not simply from
    the 1024-byte wire limit. This task explicitly grants updating the
    `kCommandJsonDocumentBytes` formula in `src/support/Limits.h`, with the
    derivation in a comment
  * `ARDUINOJSON_USE_LONG_LONG` is enabled consistently on host and Teensy
    builds so `uint64_t` seq values are preserved; `controller_ts` is
    parsed as a finite double and preserved for later response
    serialization through the round-trip-safe policy already defined by
    `support::BoundedJsonWriter` (this task defines no serializer policy
    of its own). Lexical identity of the source token is NOT required
    (§16.2 requires the value echoed without semantic transformation, not
    byte-identical text); integer argument validation rejects fractional
    values and overflow
  * no `DynamicJsonDocument`, no per-message allocation; steady-state
    no-allocation is proven, not assumed: the host test binary instruments
    global `new`/`delete`; warm up framework/library state first, reset the
    allocation counter immediately before the measured loop, measure only
    repeated parsing/validation, and assert zero allocations
  * explicit bounded UTF-8 validation of the line before or alongside JSON
    parsing — JSON parsing alone is not assumed to implement the §10.6
    invalid-UTF-8 behavior
  * the result is a `ParseOutcome` that preserves what Phase 6 needs so raw
    JSON is never reparsed during response construction:
    valid typed message / malformed with no trustworthy seq / structurally
    invalid with trustworthy seq / unsupported message type. Recoverable
    structurally-invalid outcomes carry seq, controller_ts when valid, and
    the suggested contract §17 error code
  * unsupported message type is pinned: invoke no command handler, e-stop
    hook, or heartbeat handler; increment the invalid-message counter;
    send no response; carry NO suggested error code. `UNKNOWN_COMMAND` is
    reserved for an unknown command name inside an otherwise valid
    `type:"command"` message — it is never the unsupported-type outcome
  * classify by `type` against the closed `core/Protocol.h` vocabulary;
    extract seq, controller_ts (to be echoed untouched, §16), command name,
    args
  * invalid UTF-8 or malformed JSON: no handler runs, invalid-message
    counter increments, board keeps running, no response when no trustworthy
    seq is recoverable (§10.6)
  * wire `CommandArgs` to the parsed document: has/getInt/getFloat/getBool/
    getString with §6.2 semantics (int is signed 32-bit; float finite);
    returned views are valid only for the current invocation (proposal,
    Ownership and Lifetimes)

  ## Do not implement

  * dispatch order (§23), response building, validation against a
    registered command's argument schema (Phase 6)

  ## Tests should cover

  * §28.3 malformed-input rows: invalid UTF-8 and malformed JSON execute
    nothing and count — including overlong encodings, bare continuation
    bytes, and a truncated multi-byte sequence at end of line
  * the four-way ParseOutcome classification table across all inbound
    `type` values plus unknown; preserved seq/controller_ts/error code on
    recoverable structurally-invalid messages; the unsupported-type
    outcome carries no suggested error code, triggers no handler or hook,
    produces no response, and increments the invalid-message counter
  * steady-state allocation proof, with the pinned methodology: warm up
    first, reset the counter immediately before the measured loop, measure
    only repeated parsing/validation, assert zero allocations
  * integer-argument discipline, exactly these rows: `1` accepted; `1.0`,
    `1e0`, and `1.5` rejected as fractional/non-integer forms; `INT32_MIN`
    and `INT32_MAX` accepted; one below `INT32_MIN` and one above
    `INT32_MAX` rejected as overflow
  * `uint64_t` seq boundaries supported by the implementation (with
    `ARDUINOJSON_USE_LONG_LONG`)
  * `controller_ts` numeric round-trip through the writer's pinned policy:
    parse, then serialize with `support::BoundedJsonWriter`, then re-parse
    yields a double exactly equal to the first parsed value — asserted as
    numeric equality, never as identical source-token bytes (include a
    high-precision value like `1718210123.4567891` and the `1`/`1.0`/`1e0`
    equivalence rows, which for this double field all parse to the same
    accepted value)
  * zero-copy lifetime: parsed string views point into the framer's
    acquired payload and stay valid exactly until `releaseLine()`; views
    are invalid after release
  * CommandArgs type discipline: wrong-type lookups return false;
    non-finite float rejection

  ## Files

  * `src/core/InboundParser.h` (build-script include wiring for
    `third_party/ArduinoJson/` as needed)
  * `src/support/Limits.h` (document-capacity formula only)
  * `tests/host/unit/test_inbound_parser.cpp`

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

<!-- Planned future tasks, appended as ledger entries when their
     predecessors complete (phase roadmap from the bootstrap plan;
     Phases 2-3 ledgered above 2026-06-12):

     Phase 4 — session lifecycle: state machine driver; schema-first within
       the 2 s window (§8.4, §9.3); close-old supersession ordering (§9.1);
       controller-loss path with hook budget measurement (§21).
       The ledgered task MUST explicitly define, from contract text only:
       network initialization retry behavior; link loss while LISTENING;
       link loss during SESSION_CONNECTED; link loss during SESSION_ACTIVE;
       whether self-transitions are legal; whether shutdown is out of scope
       or represented externally. The agent may not invent states beyond
       the six in SessionState.h or transitions beyond its table; a gap in
       the contract is a stop-and-report, not a judgment call.
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
