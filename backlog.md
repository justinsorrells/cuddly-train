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

* [x] Task: OPERATOR GATE — review and accept the Library API proposal

  ACCEPTED 2026-06-12 by the operator (Justin), conveyed in session,
  following a read-only 14-point audit of the document (verdict PASS,
  12/14 clean; the two minor findings — CommandArgs validity tied to the
  acquired framer payload, and releaseLine() invalidation semantics — were
  applied to the document at acceptance). Status line updated to
  ACCEPTED 2026-06-12 in the same change.

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

* [x] Task: Define the public API value and metadata types

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

* [x] Task: Define the handler boundary types (results, writers, hooks)

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

* [x] Task: Implement the fixed-capacity command registry

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

* [x] Task: Generate the schema message from the registry

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

* [x] Task: Implement the bounded NDJSON line framer

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

* [x] Task: OPERATOR GATE — vendor ArduinoJson 6.21.5

  VENDORED 2026-06-12 by the operator (Justin), conveyed in session:
  `ArduinoJson-v6.21.5.h` (verified `ARDUINOJSON_VERSION "6.21.5"`) and the
  upstream MIT `LICENSE.txt` placed under `third_party/ArduinoJson/`;
  release commit `40ee05c0`, header sha256 `47eca798…12e1962`, full
  metadata recorded in `docs/contracts/UPSTREAM_SOURCES.md` in the same
  change. Invariant sweep passes over the vendored file.

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

* [x] Task: Parse inbound messages with fixed-capacity JSON

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

# Phases 4-18

Status: PROPOSAL for operator audit. Nothing here is in `backlog.md` yet.
Revised after external audit (2026-06-13): added a network/server seam
prerequisite, pinned the lifecycle policy, gave outbound priority an owner,
tightened every phase, restored the licensing and hardware operator gates, and
reopened D1/D5/D6. Eighth correction round (2026-06-13): corrected Transport
ownership, split parser cleanup from SessionDriver cleanup, and updated
critical-failure teardown wording. When accepted, these entries are appended
to `backlog.md` (replacing the commented-out roadmap block at its tail) and
executed strictly in order.

The general rules at the top of `backlog.md` apply to every task (read
AGENTS.md + contracts + skills; preserve firmware invariants; fixed capacity;
honest e-stop; schema-first; stop-and-report on contradiction). Each task
follows the AGENTS.md framing: contract sections, required skills, allowed
files, forbidden files, acceptance criteria, tests, validation.

## Structural decisions (kept from the prior draft)

* **Transmit-first ordering** and the **driver/facade split** are retained.
* **New prerequisite (Phase 4):** a platform-free network/server seam, because
  the existing `Transport` exposes only per-connection byte I/O and the new
  tasks need network progress/yield, listen, accept, and close/abort.
* **Outbound scheduling (§13.4) has an explicit owner:** a bounded
  `OutboundScheduler` implemented in the facade/pump phase (Phase 7).
* **Four operator gates:** Phase 9 (API-extension review), Phase 13
  (dependency version pinning), Phase 16 (actual-controller vertical-slice integration), and Phase 18 (hardware validation + contract
  FREEZE).
* **Dedicated conformance firmware** added (Phase 15) distinct from the minimal
  `ethernet_smoke` example.

## Ordered tasks

| # | Task | Gate? | Depends on |
|---|---|---|---|
| 4  | Network/server seam + fake extensions | — | seams (done) |
| 5  | Serialized outbound transmit path | — | 4 |
| 6  | Session state-machine driver | — | 4, 5 |
| 7  | Public facade + service pump + outbound scheduler | — | 6 |
| 8  | Command dispatch + response building | — | 7 |
| 9  | **OPERATOR GATE** — API-extension review | gate | 8 |
| 10 | Counters diagnostic command (`get_counters`) | — | 9 |
| 11 | Telemetry scheduler | — | 7 |
| 12 | Software e-stop + heartbeat + `estop_triggered` API | — | 9 |
| 13 | **OPERATOR GATE** — dependency version pinning | gate | 4–12 |
| 14 | QNEthernet platform adapter | — | 13 |
| 15 | `ethernet_smoke` + `command_server_conformance` sketches + CI | — | 14 |
| 16 | **OPERATOR GATE** — actual-controller vertical-slice integration | gate | 15 |
| 17 | Python conformance client + harness | — | 15 |
| 18 | **OPERATOR GATE** — hardware validation → contract FROZEN | gate | 16, 17 |

## Open / reopened decisions

* **D1 — QNEthernet version (REOPENED).** `v0.35.0` is the most recent tag but
  is published as a **pre-release**, not a stable release. It may still be the
  selected tested tag; the docs must describe it accurately as the *selected
  tested pre-release tag* and pin its **full commit SHA**. Resolved at the
  Phase 13 gate, before adapter implementation.
* **D5 — Contract FREEZE (REOPENED → operator gate).** Phase 18 is restored as
  an operator/hardware gate. An autonomous agent cannot claim physical
  validation or freeze a contract. A provisional-numeric change updates every
  exact contract-body occurrence, `Limits.h`, and tests — not just the Status
  line.
* **D6 — Repository license (RESOLVED).** The project owner accepts the
  downstream copyleft obligations for this specialized project. Upstream
  AGPL-3.0-or-later license and notices are preserved. The repository adopts
  AGPL-3.0-or-later for firmware that links QNEthernet. The premature MIT
  `LICENSE` was removed; the correct AGPL-3.0-or-later `LICENSE` will be added
  at the Phase 13 dependency-pin gate.
* **D2/D3 (resolved):**
  NetworkServer owns the two anonymous fixed Transport/connection slots and creates/validates generation-tagged ConnectionHandles.
  SessionDriver owns the active-handle and replacement-handle role variables, LineFramer, OutboundWriter, Clock, and SessionState.
  SessionDriver does not own the underlying Transport slot objects.
  Keep `InboundParser`, `CommandRegistry`, routing, and `OutboundScheduler` owned by the facade/ServiceLoop as already pinned.
  smoke sketch defaults to DHCP.

## Pinned lifecycle policy (resolves the Phase 6 gap up front)

Pinned **from contract text now** so no implementation agent discovers a gap
and stops. **Conclusion: the six-state `SessionState` table needs no
amendment; no new state or edge is introduced.** Every case below maps onto the
existing states and edges.

* **Network-init retry (§26.1, §3.4):** on Ethernet init / address-acquisition
  failure, the driver remains in `NETWORK_STARTING` (board-local safe), executes
  no command, and retries initialization on a bounded fixed interval
  (`support::kNetworkInitRetryMs`, compile-time constant, default 1000 ms)
  until an address is acquired. Remaining in a state across retries is not a
  transition; no self-edge is added.
* **Link loss while `LISTENING` (§8.3, §3.4):** no active session exists, so no
  controller-loss hook runs. The driver stays in `LISTENING`; `accept()`
  yields nothing while the link/address is unavailable and resumes when the
  platform re-establishes it. No state change, no command executes meanwhile.
* **Link loss while `SESSION_CONNECTED` (§21):** an active session is being
  established; treat as controller loss — run the controller-loss hook, take the
  existing `SESSION_CONNECTED → SESSION_CLOSING` edge, then `→ LISTENING`.
* **Link loss while `SESSION_ACTIVE` (§21):** controller loss — run the hook,
  take `SESSION_ACTIVE → SESSION_CLOSING → LISTENING`.
* **Link restoration (§21):** after a loss-driven return to `LISTENING`, the
  driver re-listens; while the link is physically down, `LISTENING` is the held
  posture and `accept()` simply yields nothing until the platform restores the
  link/address. No new state.
* **Explicit shutdown (§21):** V1 exposes **no graceful-shutdown facade API**;
  "explicit server shutdown" denotes externally driven teardown / power-down.
  If a session is active when it occurs, controller-loss handling runs first;
  the board then remains board-local safe. No new state, no `stop()` in the §5
  surface.
* **Self-transitions:** illegal, matching `isValidTransition`. The driver never
  requests a from==to transition.

---

* [x] Task: Define the network/server seam and fake (Phase 4)

  ## Goal

  A platform-free seam for network-stack lifecycle and TCP server acceptance,
  with a deterministic host fake. `Transport` (per-connection byte I/O) already
  exists; this adds the operations the transmit path and session driver need
  that `Transport` does not expose.

  ## Contract sections

  §3.4, §9.1, §10.4, §12, §13.6, §24.

  ## Required skills

  `qnethernet-transport`, `command-server-contract`, `host-conformance-testing`,
  `repository-conventions`, `fixed-capacity-cpp`.

  ## Scope

  * abstract, header-only, no Arduino/QNEthernet types; time enters via the
    existing `Clock` seam
  * **concrete connection-handle model (no heap allocation):**
    ```cpp
    struct ConnectionHandle {
      uint8_t slot;
      uint32_t generation;
    };
    constexpr ConnectionHandle kInvalidConnection = ...;

    ConnectionHandle accept();
    Transport* transport(ConnectionHandle);
    void close(ConnectionHandle);
    void abort(ConnectionHandle);
    ```
    `NetworkServer` owns two anonymous fixed Transport/connection slots, creates and validates generation-tagged handles, knows nothing about active/replacement roles. Handles
    remain valid until `close`/`abort`. Stale handles fail closed.
    No heap allocation or owning raw pointers. Every transport/close/abort lookup validates slot and generation.
    - Generation zero is invalid. Increment generation whenever a slot is invalidated.
    - If the generation wraps to zero, retire the slot until reboot rather than allowing a stale handle to regain validity.
    - Pin invalid `ConnectionHandle` as generation zero.
  * **network availability enum** (not a single boolean):
    ```
    UNINITIALIZED → LINK_DOWN → ADDRESS_PENDING → READY
    ```
    `READY` is defined as initialized + link up + usable address, with `LINK_DOWN` taking precedence over `ADDRESS_PENDING`.
  * `NetworkServer` exposes: **progress/yield** (let lwIP make progress — used
    by Phase 5 between write retries and called every `service()`, §12);
    **availability** via the enum above (§3.4); **begin/listen** on the
    configured port; **pending-connection detection**; **accept** returning a
    `ConnectionHandle`; **close** (graceful) and **abort** (immediate) of a
    connection handle
  * the four §24 connection states remain queryable through `Transport`; the
    seam never collapses them into a single boolean
  * **close vs. abort behavior (pinned):**
    - use **abort** on link loss, stale/half-open replacement, and critical
      transmit failure — never block waiting for graceful close
    - normal healthy closure may use `close` only if it is non-blocking
    - fixed connection slots become reusable **immediately** after abort
  * extend the shared fakes (no per-test variants): `FakeNetworkServer` scripts
    link/address availability via the enum, queued inbound connections, accept
    into handles, per-handle close/abort; `FakeTransport` gains any hooks these
    need (progress accounting, abort)

  ## Do not implement

  * the transmit path (Phase 5), the driver (Phase 6), or any QNEthernet
    implementation (Phase 14)

  ## Allowed files

  * `src/core/NetworkServer.h`
  * `tests/host/fakes/FakeNetworkServer.h`, `tests/host/fakes/FakeTransport.h`
    (extend), `tests/host/fakes/FakeClock.h` (read-only)
  * `tests/host/unit/test_network_server.cpp`

  ## Forbidden files

  Everything else, especially `docs/contracts/`, `third_party/`, `src/api/`.

  ## Tests should cover

  * scripted availability enum transitions: `UNINITIALIZED` → `LINK_DOWN` →
    `ADDRESS_PENDING` → `READY` observed through the availability query
  * two simultaneous handles resolve independently; close/abort invalidates only the selected handle; slot reuse returns a different generation; stale generation fails closed.
  * test generation changes after both close and abort.
  * abort vs. close are distinguishable; abort makes the slot immediately
    reusable; a stale handle fails closed; progress/yield is callable and
    counted

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [x] Task: Implement the serialized outbound transmit path (Phase 5)

  ## Goal

  The single serialized board-to-controller write path (§13): one path, no byte
  interleaving. Only write acceptance is deadline-bounded; non-blocking flush is called exactly once afterward. Pure bytes-out over `Transport`, driving network progress
  between retries.

  ## Contract sections

  §13 (all), §10.1, §10.3, §11.2, §26.3, §26.4, §28.6.

  ## Required skills

  `command-server-contract`, `repository-conventions`, `qnethernet-transport`,
  `fixed-capacity-cpp`.

  ## Scope

  * input is a **const** outbound line view (`ConstLineView{const char*, size}`),
    never the inbound `MutableLineView`
  * the line SHALL carry **exactly one trailing newline**; the 8192-byte limit
    **includes that newline** (§10.1, §10.3); refuse anything larger and any
    line lacking the sole terminating `
` (§11.2)
  * `sendLine(ConstLineView, MessageClass)` with `MessageClass ∈ {Critical,
    Telemetry}`: retry `writeSome()` calls within one deadline of
    `support::kTransmitDeadlineMs` until all bytes are accepted by the TCP
    stack; call `NetworkServer` **progress** between retries; call non-blocking
    `flush()` **exactly once** after all bytes are accepted (§13.2) — a flush
    call is not evidence of peer receipt; never busy-wait, never
    `print()`/`println()` (§13.1, §13.6)
  * elapsed-time is computed **wrap-safe** via unsigned subtraction on the
    monotonic clock; no signed comparison that breaks at wraparound
  * one **reentrancy/serialization guard**: a send in progress rejects a nested
    send so bytes from two messages can never interleave (§13)
  * deadline policy (§13.6, §26.3–.4):
    A critical transmit deadline miss increments tx_failures and returns failure.
    The caller requests deferred teardown through SessionDriver::requestTeardown(CriticalTransmitFailure).
    No caller directly closes/aborts the socket or invokes the controller-loss hook.
    A `Telemetry` miss → drop + `telemetry_dropped`++ and advance a consecutive-
    telemetry-failure streak, raising a teardown signal at
    `support::kTelemetryDeadlineFailuresToTeardown`.
    The telemetry ten-failure signal must use the same requestTeardown path.
  * the telemetry failure streak **resets on any successful telemetry send and
    on a new session** (the streak is per-session, never global)
  * fixed capacity, no allocation, no second serializer

  ## Do not implement

  * cross-type priority scheduling (Phase 7), telemetry coalescing (Phase 11),
    `setNoDelay` (Phase 14), the QNEthernet `Transport` (Phase 14)

  ## Allowed files

  * `src/core/OutboundWriter.h`
  * `tests/host/unit/test_outbound_writer.cpp`
  * `tests/host/fakes/FakeTransport.h`, `FakeNetworkServer.h`, `FakeClock.h`
    (extend as needed — explicitly allowed)

  ## Forbidden files

  Everything else, especially `docs/contracts/`, `src/api/`, `third_party/`.

  ## Tests should cover (§28.6)

  * partial-write acceptance counted across scripted splits; progress called
    between retries
  * peer that stops reading: `Critical` fails at the deadline when
    `writeSome()` cannot make progress, `Telemetry` drops and counts; the fake
    records exactly one `flush()` call per successful send
  * deadline elapsed-time correct across a simulated clock wraparound
  * ten consecutive telemetry misses raise teardown; a successful telemetry send
    resets the streak; a new session resets the streak
  * a line of exactly 8192 bytes (incl. `
`) accepted; 8193 refused; a line
    with no trailing `
` or with two `
` refused
  * nested send rejected by the reentrancy guard; `tx_failures` only on
    `Critical` deadline expiry

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [x] Task: Implement the session state-machine driver (Phase 6)

  ## Goal

  The §8 lifecycle driver implementing the **pinned lifecycle policy above**
  (no table amendment): acceptance, schema-first, the four §24 states,
  controller-loss with a budget-measured hook, and close-old supersession — over
  the Phase-4 seam, host-tested with the fakes.

  ## Contract sections

  §3.4, §8 (all), §9 (all), §12, §21, §24, §26.1, §26.2, §28.2.

  ## Required skills

  `command-server-contract`, `qnethernet-transport`, `teensy-safety-hooks`,
  `repository-conventions`, `fixed-capacity-cpp`.

  ## Scope

  * implement exactly the **Pinned lifecycle policy** section: network-init
    retry; link loss at `LISTENING`/`SESSION_CONNECTED`/`SESSION_ACTIVE`; link
    restoration; explicit shutdown; no new state or edge. A contradiction with
    that policy is a stop-and-report, not a new invention.
  * owns `NetworkServer`, the active-handle and replacement-handle role variables, controls supersession and promotion, owns `LineFramer`, `OutboundWriter`, and `SessionState`, does not own the underlying `Transport` slot objects. Also owns `Clock`.
  * **Dependency boundary:** The facade owns CommandRegistry, BoardIdentity, and registration metadata. `SessionDriver` receives non-owning references needed for schema creation: `const CommandRegistry&`, `const BoardIdentity&`, `SchemaBuilder&`, and `controller-loss callback + context`. It must not duplicate or own registration metadata.
  * **Exact later-phase seams to SessionDriver:**
    ```cpp
    SendOutcome sendActiveLine(ConstLineView, MessageClass);
    void requestTeardown(TeardownReason reason);
    bool teardownPending() const;
    TeardownReason pendingTeardownReason() const;
    void applyPendingTeardown();
    bool sessionActive() const;
    ```
  * `BOOT_SAFE`→`NETWORK_STARTING`→`LISTENING` on bring-up; on accept,
    `SESSION_CONNECTED`, send schema **first** via `SchemaBuilder` +
    `OutboundWriter` inside the 2 s window (§8.4, §9.3), then `SESSION_ACTIVE`,
    `schemas_sent`++; schema-send failure closes the session → `LISTENING`
    (§9.3, §26.2)

  * controller-loss path (§21, §24): detect via the four states, discard partial
    inbound, run the idempotent loss hook, return to `LISTENING`;
    never carry input across sessions
  * **session counter semantics (pinned):**
    - `sessions_accepted`: increments once for every successfully accepted TCP connection, including initial and replacement connections
    - `sessions_superseded`: additionally increments when a replacement displaces an old session
    - `controller_disconnects`: increments once when an accepted session ends through loss, forced teardown, schema-send failure, or supersession
    - `sessions_rejected`: increments only when a pending connection cannot be accepted or represented
  * **ownership (pinned):** `SessionDriver` owns the `OutboundWriter` and
    binds/resets it per active connection; session replacement resets the
    telemetry transmit-failure streak
  * add `support::kNetworkInitRetryMs` (compile-time constant, default 1000 ms)
    to `Limits.h` for the network-init retry interval — this is not part of
    the accepted public API and is not configurable at runtime
  * exposes the pump seam: `bool nextLine(MutableLineView&)` + `releaseLine()`.
    nextLine() may acquire only while SESSION_ACTIVE and teardown is not pending.
    releaseLine() remains valid exactly once for a payload acquired before the session entered SESSION_CLOSING.
    The acquired payload and CommandArgs views are not invalidated until releaseLine() returns.
  * `SessionDriver` cannot clear `OutboundScheduler` because it is owned by `ServiceLoop` (Phase 7).
  * Required sequence for generic teardown and supersession:
    ServiceLoop teardown preparation:
    - release any acquired line
    - reset InboundParser and router session-local state
    - cancel OutboundScheduler entries
    - route all CanceledBySessionEnd outcomes

    SessionDriver::applyPendingTeardown():
    - run the controller-loss hook exactly once and measure it
    - reset LineFramer and driver-owned session state
    - abort and invalidate the active connection handle
    - increment controller_disconnects once
    - transition SESSION_CLOSING -> LISTENING
    - for supersession, promote the held replacement afterward and send schema first

  * The replacement handle remains held and valid throughout old-session teardown.

  ## Do not implement

  * the §5 API / `service()` (Phase 7); parsing, target/source validation, §23
    routing (Phase 7); command/telemetry/estop/heartbeat behavior (Phases 8–12)

  ## Allowed files

  * `src/core/SessionDriver.h`
  * `src/core/OutboundWriter.h` (allowed to resolve the `SessionDriver` name collision)
  * `src/support/Limits.h` (`kNetworkInitRetryMs` only)
  * `tests/host/unit/test_session_driver.cpp`
  * `tests/host/fakes/*` (extend as needed)

  ## Forbidden files

  Everything else, especially `docs/contracts/`, `src/TeensyCommandServer.h`,
  `src/core/SessionState.h` (read-only — the table is **not** amended).

  ## Tests should cover (§28.2)

  * starts safe; `LISTENING` after init; network-init retry stays
    `NETWORK_STARTING` and runs no command
  * schema is the first bytes out within 2 s; reconnect resends it; schema-send
    failure → `LISTENING`
  * supersession runs the old loss hook, clears framer state, aborts the old handle, schema-first on
    the replacement handle, delivers no superseded line, still promotes when the
    loss hook is over budget
  * each pinned link-loss case (`LISTENING` no-op; `SESSION_CONNECTED` and
    `SESSION_ACTIVE` → loss path → `LISTENING`); link restoration re-listens
  * connection loss runs the hook; no line delivered after detected loss

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [x] Task: Implement the facade, service pump, and outbound scheduler
  (Phase 7)

  ## Goal

  The §5 public surface, the per-loop pump, and the **owner of §13.4 outbound
  scheduling**. Routes each inbound line by §23 type after target **and source**
  validation; command/telemetry/estop/heartbeat bodies are fail-closed stubs
  until their phases.

  ## Contract sections

  §3.1, §5 (all), §10.6, §12, §13.4, §15, §19, §22, §23, §8.4–8.6.

  ## Required skills

  `command-server-contract`, `repository-conventions`, `fixed-capacity-cpp`.

  ## Scope

  * `src/TeensyCommandServer.h` maps the §5 operations one-to-one per the
    accepted `Library_API.md`; pre-start registration delegates to
    `CommandRegistry`. `start()` runs the three-op seal lifecycle
    (`validateMetadataForSeal` → `SchemaBuilder::validateMaximumSchemaSize` →
    `commitSeal`) then `NETWORK_STARTING`; any failure leaves the registry
    MUTABLE and `start()` fails (§5.1)
  * **outbound scheduling** — implement a bounded `OutboundScheduler` with two
    structures: a **fixed-capacity critical-message queue** and a **single
    replaceable latest-telemetry slot**:
    - `enum class OutboundKind { CommandResponse, EstopAck, EstopTriggered, HeartbeatAck, Telemetry };`
    - `OutboundKind` identifies the producer/completion route; `Telemetry` is represented even though it is not stored in the critical queue.
    - `enum class OutboundCompletion { Sent, SendFailed, CanceledBySessionEnd };`
    - `struct OutboundOutcome { OutboundKind kind; OutboundCompletion completion; };`
    - each queued critical message owns a fixed-capacity **copy** of its
      serialized bytes (no retained view into a builder buffer)
    - each critical entry records its priority class; queue capacity and per-
      entry RAM cost are constants in `Limits.h`
    - `constexpr size_t kOutboundCriticalQueueCapacity = 4;`
    - `constexpr size_t kOutboundCancellationBatchCapacity = kOutboundCriticalQueueCapacity + 1;`
    - Document that this covers 4 critical queue entries + 1 telemetry slot.
    - `constexpr size_t kOutboundCriticalEntryMaxBytes = kBoardTxMaxLineBytes;`
    - `constexpr size_t kOutboundSchedulerMaxBytes = 48 * 1024;`
    - `static_assert(sizeof(OutboundScheduler) <= kOutboundSchedulerMaxBytes, "OutboundScheduler exceeds the pinned RAM budget");`
    - Document the approximate composition: 4 x 8192-byte critical buffers, 1 x 8192-byte telemetry buffer, metadata/alignment.
    - The scheduler’s large fixed buffers must live in the long-lived server object or static storage, never as a large temporary/automatic local object.
    - next-send selection among critical entries follows: safety/estop >
      command response > heartbeat ack. **FIFO ordering within one priority class.**
    - **no silent overwrite:** a second response, heartbeat ack, or safety event
      is queued, not dropped. Queue entries own their bytes.
    - **critical queue saturation** (queue full) **requests deferred teardown**
    - `drainOne()` reports `OutboundOutcome` (Sent or SendFailed).
    - Session-clear operation returns a fixed-capacity batch of `CanceledBySessionEnd` outcomes.
    - Cancellation behavior:
      - EstopTriggered returns to its persistent pending state.
      - CommandResponse, EstopAck, and HeartbeatAck are discarded.
      - Telemetry slot is cleared.
      - No sent counter increments.
    - `telemetry_sent`, `estop_ack_sent`, and `heartbeat_ack_sent` increment only after successful wire transmission (never upon enqueue). Failed enqueue or transmission does not increment a `*_sent` counter.
    - Session teardown clears session-specific queued bytes. `estop_triggered` source state survives session teardown and is rebuilt after reconnect. The telemetry slot is cleared on session replacement.
    - the telemetry slot holds the latest snapshot only; replacing a pending
      snapshot increments `telemetry_coalesced`.
    - **schema is not a scheduler slot** — schema is sent synchronously and
      exclusively in `SESSION_CONNECTED` before `SESSION_ACTIVE`, so its
      highest priority is structural
    - **an already-started TCP write is not preempted** — priority applies
      only before the next message begins
  * `OutboundScheduler` submits through `SessionDriver`; it does not own a
    second writer. Schema bypasses the post-registration scheduler (sent
    synchronously in `SESSION_CONNECTED`).
  * `service()` (§12) exact service policy:
    1. progress network and step SessionDriver
    2. if teardownPending() and no inbound line is acquired: cancel scheduler entries, route cancellation outcomes, applyPendingTeardown(), return
    3. run telemetry due check
    4. if outbound work is pending: drain at most one, route completion, if completion requests teardown: cancel remaining scheduler entries, route cancellations, applyPendingTeardown(), return
    5. process at most one inbound line
    6. releaseLine()
    7. if teardownPending(): cancel scheduler entries, route cancellations, applyPendingTeardown(), return
    8. drain at most one item created by that line, route its completion, if it requests teardown: cancel remaining entries, route cancellations, applyPendingTeardown()
    Routes completion from `drainOne()` by `OutboundKind`. An already-started write remains non-preemptible. This V1 policy keeps worst-case `service()` duration well below the 250 ms liveness window without starving telemetry. Safety and critical priority remain unchanged.
    No telemetry frame may be generated, no additional line acquired, and no old session output transmitted after a teardown request has reached its applicable barrier.
    (Remove `kMaxInboundLinesPerService` from `Limits.h` — it is no longer
    needed.)
  * per line: parse with `InboundParser`; validate `target == board_id` (§3.1)
    **and** `source == "controller"` (§15, §19 shape, §22) for command, estop,
    and heartbeat — a mismatch on either drops the line, `invalid_targets`++,
    no response, no hook/seam; then route by `type` (§23) to the fail-closed
    seam; release the line after the seam returns (zero-copy lifetime)
  * **recoverable structurally-invalid** parser outcomes (valid trustworthy
    `seq`, suggested §17 code): hand to the command-response seam to emit a
    structured error echoing the retained `seq` (§10.6); malformed/untrustworthy
    outcomes already counted by the parser produce no response
  * **counter ownership (no double counting):** the **parser** solely owns
    `invalid_json` (malformed JSON, invalid UTF-8, unsupported-but-valid type,
    untrustworthy structural); the **pump** owns `invalid_targets`; dispatch
    owns command counters (Phase 8), safety/heartbeat own theirs (Phase 12). The
    pump never re-increments `invalid_json`
  * **fail-closed stubs (before Phases 8–12 install real seams):**
    - the **command stub** cannot emit `INTERNAL_ERROR` because the response
      builder is Phase 8 — instead it invokes no board handler, emits no fake
      success, and **requests deferred teardown**; the stub is testable through
      injected routing callbacks
    - only **recoverable structurally-invalid** `type:"command"` outcomes
      (trustworthy `seq`) are routed to the future structured-error seam
    - **malformed e-stop and heartbeat** messages are dropped and counted (not
      routed to a structured-error seam)
    - the **estop stub** sends **no** `estop_ack` and counts
      `estop_apply_failed` (never a false safe)
    - the **heartbeat stub** sends no ack
    - no stub performs a silent no-op that looks like success or safety

  ## Do not implement

  * argument validation/response bytes (Phase 8); `get_counters` (Phase 10);
    telemetry scheduling (Phase 11); estop/heartbeat bodies + acks (Phase 12);
    the adapter (Phase 14)

  ## Allowed files

  * `src/TeensyCommandServer.h`, `src/core/ServiceLoop.h`,
    `src/core/OutboundScheduler.h`, `src/support/Limits.h`
  * `tests/host/unit/test_service_loop.cpp`,
    `tests/host/unit/test_outbound_scheduler.cpp`

  ## Forbidden files

  Everything else, especially `docs/contracts/`.

  ## Tests should cover

  * §5 surface maps to documented operations; post-`start()` registration fails
    (`RegistrationSealed`); a failed seal leaves the registry MUTABLE
  * critical queue drains in priority order (safety > response > heartbeat); FIFO within class;
    an in-flight write is not preempted; a second critical entry is queued, not
    silently overwritten; queue saturation requests teardown; telemetry slot
    coalesces (replacement increments `telemetry_coalesced`); telemetry never
    blocks a queued critical
  * teardown requested while a line is acquired waits for `releaseLine()`
  * no second line is acquired
  * cancellation precedes connection abortion
  * the controller-loss hook runs once
  * supersession promotes the replacement only after old-session teardown
  * send-failure teardown is applied before `service()` returns
  * a test clearing a completely full scheduler: verify five outcomes are returned, every kind is routed correctly, no sent counter increments, EstopTriggered returns to `PENDING_NOT_QUEUED`, telemetry and session-specific acknowledgements/responses are discarded
  * under a flood, continuous command input proves telemetry still reaches the wire within the 250 ms liveness window without being starved; each `service()` processes at most one inbound line and sends at most one outbound message; the telemetry due-check runs every call
  * §23 routing across every `type`; `source != "controller"` and wrong
    `target` both drop + `invalid_targets`++ with no seam; recoverable
    structural outcome yields one error response echoing `seq`
  * counter ownership: a malformed line bumps only `invalid_json`; a
    wrong-target line bumps only `invalid_targets`; no path double-counts
  * fail-closed stubs behave as specified: command stub requests teardown (no
    INTERNAL_ERROR emission before Phase 8); estop stub → no false ack;
    heartbeat stub → no ack; malformed estop/heartbeat dropped and counted

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [x] Task: Implement command dispatch and response building (Phase 8)

  ## Goal

  The command seam: validate against the registered schema, invoke exactly one
  handler, and build the **full §16 response envelope** with `seq`/
  `controller_ts` echo and library-owned `board_proc_us`.

  ## Contract sections

  §3.1, §6.2–6.3, §7, §11.2, §15, §16 (all), §17, §23, §26.3, §26.6, §28.5.

  ## Required skills

  `command-server-contract`, `fixed-capacity-cpp`, `repository-conventions`.

  ## Scope

  * validate per §15 (target/source already checked by the pump): command
    registered (else `UNKNOWN_COMMAND`, `unknown_commands`++); `args` an object;
    required args present (`MISSING_FIELD`); types match §6.2 (`INVALID_TYPE`);
    **any undeclared/extra argument → `INVALID_ARGUMENT`** (pinned
    interpretation — §6.3/§15 require rejection but leave the code unstated);
    handler domain failure → `INVALID_ARGUMENT`
  * invoke exactly one handler; non-blocking is a conformance expectation (§7.1)
  * **Dispatcher / Service loop split:** Dispatcher builds the response, enqueues it, and if enqueue fails -> requests deferred teardown. Service loop (Phase 7) drains scheduler, if send fails -> requests teardown. Routes completion by `OutboundKind`.
  * **full response envelope (§16):** `type:"response"`, echoed `seq` (§16.1),
    echoed `controller_ts` untouched (§16.2), board-local `timestamp`,
    `source = board_id`, `target = "controller"`, `status` ∈ `{ok,error}`
    (§16.4), `result`, `error`. On `ok`: `result` object containing the
    library-inserted `board_proc_us` (`micros`, §16.3), `error: null`. On
    `error`: `result: null`, `error: {code,message}` mapped via
    `ErrorCodeMapping`, and **no `board_proc_us` anywhere** (§16.3 is ok-only)
  * Pin `board_proc_us` timing:
    - Capture `parse_completed_us` immediately after parsing succeeds and pass it into dispatch.
    - Compute `board_proc_us = response_ready_us - parse_completed_us` using wrap-safe unsigned arithmetic.
    - The duration includes lookup, argument validation, handler execution, and response construction.
    - It is inserted only into successful results and remains absent from error responses.
  * **structurally recoverable command errors** (from the pump) uses the
    **retained `seq`** and echoed `controller_ts` when present (§10.6)
  * outbound overflow (§11.2, §26.6): if `result` cannot fit 8192, send a
    compact `INTERNAL_ERROR` with the same `seq`
  * **teardown coupling (§26.3):** a `Critical` transmit failure for the
    response requests session teardown; **failure to send the fallback
    `INTERNAL_ERROR` also requests teardown**. Distinguish fallback INTERNAL_ERROR build failure, enqueue failure, and later send failure; all request teardown through their owning component.
  * counters: `commands_received` (on accepted command entering dispatch, before
    handler lookup). `commands_ok` increments after handler success and successful response enqueue. `commands_error` increments after successful error-response enqueue. Later wire failure increments `tx_failures` and does not rewrite the logical command outcome. `invalid_arguments`

  ## Do not implement

  * `get_counters` (Phase 10); telemetry/estop/heartbeat; transmit mechanics;
    target/source validation (pump)

  ## Allowed files

  * `src/core/CommandDispatcher.h`
  * `tests/host/unit/test_command_dispatcher.cpp`
  * `src/core/ServiceLoop.h` (wire the command seam only)

  ## Forbidden files

  Everything else, especially `docs/contracts/`.

  ## Tests should cover (§28.5)

  * one handler invoked; `MISSING_FIELD`; `INVALID_TYPE`; **extra arg →
    `INVALID_ARGUMENT`**; domain `INVALID_ARGUMENT`; `seq` echoed;
    `controller_ts` echoed untouched
  * full envelope present on ok and error; `board_proc_us` inside `result` on
    ok and **absent on error**
  * **reserved-field test (corrected):** a handler call to
    `ObjectWriter.add*("board_proc_us", …)` **returns false**, and the final
    library response still contains the library-measured `board_proc_us` —
    no impossible "overwrite" is required
  * fake-clock test that advances separately during validation, handler execution, and response construction and asserts the total `board_proc_us` matches the sum
  * never emits `timeout` or a controller-owned code; long-running starts and
    returns; oversized result → `INTERNAL_ERROR` same `seq`; a response
    transmit failure and a fallback-send failure each request teardown

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [x] Task: OPERATOR GATE — API-extension review (Phase 9)

  ## Goal

  Operator review and acceptance of all public-API extensions required by
  later phases, before any implementation agent edits `Library_API.md`.

  ## Extensions to review

  * **`get_counters` enable option** — a facade option (off by default) that
    registers a library-owned diagnostic command before seal:
    ```cpp
    Status enableDiagnosticsCommand(); // must be set before start()
    ```
  * **`ObjectWriter` unsigned-number support** — an unsigned-number insertion:
    ```cpp
    bool addUnsigned(StringView key, uint32_t value);
    ```
  * **`requestEstopTriggered(reason)`** — a facade API for board-detected
    safety conditions, normal-context only and not ISR-safe. An ISR applies hardware safety and sets a flag; loop context requests the event. The library does not perform the hardware action.
    ```cpp
    Status requestEstopTriggered(StringView reason);
    ```
  * the bounded pending-event mechanism for `estop_triggered`
  * Pin specific existing local status outcomes for failures:
    - record exact existing `StatusCode` names for every failure (do not leave placeholder prose in the accepted `Library_API` document text).
    - diagnostics enable attempted after start -> existing specific local error status
    - pending safety-event slot already occupied -> existing specific local capacity/busy status
    - overlength reason -> existing specific local error status
    - invalid reason -> existing specific local error status
  * Do not use `void` or an undifferentiated `bool` where callers need to know why the operation failed.

  ## Operator steps

  * review each extension against the contract vocabulary rules (§27: no new
    counter, status, error code, message type, or event name)
  * approve the exact function signatures and their placement in
    `Library_API.md`
  * record the accepted signatures in `docs/companion/Library_API.md`

  ## Agent instructions

  If an agent reaches this task: make NO code changes. Report that the backlog
  is blocked on operator API-extension review and stop.

  After this gate, Phases 10 and 12 implement only the accepted signatures and
  **must not edit `Library_API.md`**.

  ## Validation

  None (operator action).

---

* [x] Task: Implement the counters diagnostic command (Phase 10)

  ## Goal

  Expose the §25 counter set through a library-owned `get_counters` command.
  Implements the Phase-9-accepted API extensions (the enable option and an
  unsigned-integer `ObjectWriter` method). Phase 10 **must not edit**
  `Library_API.md`.

  ## Contract sections

  §5.3, §6.4, §14, §16, §25, §28.4.

  ## Required skills

  `command-server-contract`, `fixed-capacity-cpp`, `repository-conventions`.

  ## Scope

  * Verify `support::BoundedJsonWriter::addUInt64` covers the full width (it
    uses `%llu`, `kMaxUInt64LiteralBytes = 24`).
  * when enabled, the facade registers one library-owned command (name
    `get_counters`, reserved; `blocked_by_estop = false`, diagnosis-safe §6.4,
    empty `args`) into the **same registry before seal** so it appears in the
    schema and is callable (§5.3, §14)
  * handler reads `Counters::snapshot()` and writes every §25 field as an
    **unsigned** JSON number; `board_proc_us` is still inserted by the Phase-8
    response builder
  * **snapshot timing pinned:** the snapshot is taken inside the handler, i.e.
    **after `commands_received`++ for this very `get_counters` request and
    before its `commands_ok`++** — so the reported `commands_received` includes
    this request and `commands_ok` does not yet
  * a board command also named `get_counters` while enabled → `Duplicate
    Registration` at registration (§5.2); enabling is explicit and **off by
    default**
  * **the Phase-15 conformance firmware enables this command** (Phase 17
    depends on it)
  * no new counter, status, code, message type, or event name (§27)

  ## Do not implement

  * counters-over-telemetry (the other §25 option, not chosen); counter reset;
    any new wire vocabulary

  ## Allowed files

  * `src/core/DiagnosticsCommand.h`, `src/api/ObjectWriter.h` (add the unsigned
    method only), `src/TeensyCommandServer.h`, `src/core/ServiceLoop.h`
  * `tests/host/unit/test_diagnostics_command.cpp`,
    `tests/host/unit/test_object_writer.cpp` (extend)

  ## Forbidden files

  `docs/contracts/`, `docs/companion/Library_API.md`, `src/core/Counters.h`
  (read-only).

  ## Tests should cover

  * enabled → `get_counters` in the schema with `blocked_by_estop=false` and
    empty `args` (§28.4); disabled → absent and the name free
  * dispatch returns `ok` with every §25 field at live values **as unsigned
    numbers**, including a counter value above `INT32_MAX`, plus library
    `board_proc_us`
  * the pinned snapshot timing (this request counted in `commands_received`, not
    yet in `commands_ok`)
  * a board `get_counters` while enabled → `DuplicateRegistration`; result fits
    8192 at maximum counter widths

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [x] Task: Implement the telemetry scheduler (Phase 11)

  ## Goal

  The 50 ms one-way push scheduler (§18) with pinned sequencing, failure, and
  liveness semantics.

  ## Contract sections

  §8.3, §8.5, §11.2, §13.5, §13.6, §18 (all), §26.4, §28.7.

  ## Required skills

  `command-server-contract`, `fixed-capacity-cpp`, `repository-conventions`.

  ## Scope

  * non-blocking 50 ms elapsed check via `Clock` (§18.1); pull latest snapshot
    from the provider into an `ObjectWriter` (fast, non-blocking, §18.2); build
    the §18.3 message
  * **sequence semantics (pinned):** `seq` is a board-local `uint64` that is
    strictly increasing across the entire board boot. Initialize at boot. Sequence resets on board restart, not on TCP session changes.
    **Allocate a `seq` when a successfully built frame is accepted into the
    telemetry slot.** Never reset on disconnect, reconnect, or supersession.
    Clear the pending telemetry slot on session loss. Replacing a pending telemetry frame in the scheduler slot may create a
    **sequence gap**. No command-correlation semantics (§18.3).
  * **`telemetry_sent` increments only after successful complete transmission**
    by `OutboundWriter` (never upon enqueue); pending-slot replacement increments
    `telemetry_coalesced`; provider/serialization failure does **not** allocate
    a `seq` and increments `telemetry_dropped`
  * **provider failure** (returns false / overflows the writer): drop the frame,
    `telemetry_dropped`++, send nothing — this is **not** a transmit-deadline
    miss and does **not** advance the 10-frame teardown streak (that streak is
    transmit-deadline only, §13.6)
  * **teardown signal path (pinned):** the 10-consecutive-deadline signal is
    raised by `OutboundWriter` (Phase 5) and observed by the pump after the
    telemetry send, which asks `SessionDriver` to run controller-loss/teardown
    (§26.4) — the scheduler does not close sessions itself
  * coalesce missed periods to the single latest snapshot; never burst (§13.5)
  * **field validation (pinned):** V1 does **not** validate telemetry keys/types
    against `schema.telemetry` per frame (the provider is the trusted fast path,
    §18.2); drift is a board-app bug caught in conformance, documented here
  * **first frame timing (pinned):** the first telemetry frame is due on the
    first `service()` after `SESSION_ACTIVE` and is sent within the 250 ms
    liveness window; thereafter every 50 ms (§18.6)
  * emit only while `SESSION_ACTIVE`, never before schema (§8.3, §8.5); keep
    flowing while e-stop is active and **resume on the first `service()` after
    the synchronous e-stop hook returns, within the 250 ms window** (§18.6)

  ## Do not implement

  * the e-stop hook; transmit-deadline mechanics (Phase 5); command path

  ## Allowed files

  * `src/core/TelemetryScheduler.h`
  * `tests/host/unit/test_telemetry_scheduler.cpp`
  * `src/core/ServiceLoop.h` (wire the telemetry tick only)

  ## Forbidden files

  Everything else, especially `docs/contracts/`.

  ## Tests should cover (§28.7)

  * ~50 ms cadence; not before schema; first frame within 250 ms; continues
    during e-stop; resumes within 250 ms after the hook
  * `seq` is strictly increasing within a boot but may be non-contiguous after
    slot replacement; a replaced pending frame creates a gap;
    provider/serialization failure allocates no `seq`
  * `telemetry_sent` only after complete transmission; `telemetry_coalesced` on
    slot replacement; `telemetry_dropped` on provider/oversized failure (no
    deadline-streak advance); missed periods coalesce (no burst)
  * continuous command input proving telemetry still reaches the wire within the 250 ms liveness window without being starved
  * the 10-deadline teardown signal reaches the driver via the pump

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [x] Task: Implement software e-stop, heartbeat, and the estop-trigger path
  (Phase 12)

  ## Goal

  The safety and heartbeat seams plus a **public board-facing path to raise
  `estop_triggered`**. Implements the Phase-9-accepted `requestEstopTriggered`
  API. Phase 12 **must not edit** `Library_API.md`.

  ## Contract sections

  §6.5, §9.4, §13.4, §19 (all), §20, §22, §23, §26.3, §27, §28.8, §28.9.

  ## Required skills

  `teensy-safety-hooks`, `command-server-contract`, `repository-conventions`.

  ## Scope

  * e-stop handled ahead of ordinary messages (§19, §23): stop dispatching
    ordinary messages until the hook runs; invoke the idempotent hook; measure
    duration vs `kHookBudgetMs`, `estop_hook_over_budget`++ (§19.1)
  * send `estop_ack` with `details.state` exactly `"safe"` **only** when the
    hook reports safe (over-budget-but-safe still acks). `estop_ack_sent` increments only after successful wire transmission (never upon enqueue). On failure send **no** ack, never a non-`"safe"` variant, `estop_apply_failed`++
    (§19.2, §27); `estop_received`++ on a valid e-stop
  * resume servicing incl. telemetry regardless of the hook result (§19, §18.6);
    repeated `estop` safe (§19.3); first-post-schema `estop` handled before any
    command (§9.4); no board-side latched gate (§6.5, §27)
  * **`estop_triggered` path (Phase-9-accepted API):** implement
    `requestEstopTriggered(reason)` with a **lossless bounded pending-event
    slot**. Define the internal state machine:
    - Transitions:
      EMPTY -- request accepted --> PENDING_NOT_QUEUED
      PENDING_NOT_QUEUED -- enqueue succeeds --> QUEUED_AWAITING_COMPLETION
      PENDING_NOT_QUEUED -- enqueue fails --> PENDING_NOT_QUEUED
      QUEUED_AWAITING_COMPLETION -- wire send succeeds --> EMPTY
      QUEUED_AWAITING_COMPLETION -- send failure or session cancellation --> PENDING_NOT_QUEUED
    - Explicitly state: A failed enqueue never enters `QUEUED_AWAITING_COMPLETION`.
    - Do not enqueue another copy while `QUEUED_AWAITING_COMPLETION`.
    - Copy and store the board-local occurrence timestamp when the request is accepted. Reconnection must preserve the original event timestamp and reason.
    - `requestEstopTriggered` returns an **explicit capacity/busy failure** if the slot is already occupied — the existing pending reason is **not silently overwritten**.
    - `reason` is **copied into fixed storage** with an explicit maximum length (`support::kEstopTriggeredReasonMaxBytes` in `Limits.h`).
    - clear `estop_triggered` pending state only after confirmed successful wire send (using the OutboundKind completion report from drainOne).
    - a critical transmit failure requests teardown but does **not silently erase** the pending event.
    - the board-local hardware safety action happens **immediately** in the caller (§20); serialization/transmission occurs from `service()` via the scheduler's safety queue entry — **never from an ISR** (§12).
  * heartbeat (§22): echo `seq`, `source = board_id`, `target = "controller"`,
    no application handler, no effect on telemetry sequencing, produced in the
    service loop well under 1 s; `heartbeat_received`++ on valid heartbeat. `heartbeat_ack_sent` increments only after successful wire transmission.
  * **teardown coupling (§26.3, §13.4):** a `Critical` send failure for
    `estop_ack`, `estop_triggered`, or a heartbeat ack requests session teardown
  * wrong-target/source already dropped by the pump (assert no hook/ack runs)

  ## Do not implement

  * command dispatch, telemetry scheduling, transmit mechanics

  ## Allowed files

  * `src/core/EstopHandler.h`, `src/core/HeartbeatHandler.h`,
    `src/TeensyCommandServer.h` (the `requestEstopTriggered` API),
    `src/core/ServiceLoop.h`, `src/support/Limits.h`
    (`kEstopTriggeredReasonMaxBytes` only)
  * `tests/host/unit/test_estop_handler.cpp`,
    `tests/host/unit/test_heartbeat.cpp`

  ## Forbidden files

  Everything else, especially `docs/contracts/`, `docs/companion/Library_API.md`.

  ## Tests should cover (§28.8, §28.9)

  * e-stop bypasses registration; hook invoked; repeated safe; ack only after
    confirmed safe; failure → no ack + telemetry continues; over-budget but safe
    still acks; no latched gate after `estop`; wrong-target `estop` runs no hook
  * `estop_triggered` emitted from `service()` (not the caller); a second
    request while the slot is occupied returns a capacity failure (no silent
    overwrite); the event survives a session absence and is sent after the next
    schema; a critical send failure requests teardown but does not erase the
    pending event; clear state only on success; hardware action not delayed
  * heartbeat `seq` echoed; ack `source`/`target` correct; no app handler; ack
    in the service loop; malformed heartbeat rejected safely; wrong-target
    heartbeat → no ack

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [x] Task: OPERATOR GATE — dependency version pinning (Phase 13)

  ## Goal

  Operator action recorded as a blocking ledger item. Dependency selection is
  an owner decision; no QNEthernet code is written (Phase 14) until this lands.
  Resolves **D1**. Adds the AGPL-3.0-or-later `LICENSE` file (per resolved
  **D6**).

  ## Operator steps

  * **License (D6, resolved):** add the AGPL-3.0-or-later `LICENSE` file to the
    repository, consistent with the resolved decision that this project adopts
    AGPL-3.0-or-later for firmware that links QNEthernet.
  * **Version (D1):** pin the **selected tested QNEthernet tag** (e.g.
    `v0.35.0`, described accurately as a pre-release) **and its full commit
    SHA**; pin the **arduino-cli version**, the **Teensy core / Teensyduino
    version**, the **GitHub Action immutable commit SHA**, and the **exact
    install command + source metadata**, in `UPSTREAM_SOURCES.md` and
    `Build_and_Flash_Guide.md`.
  * decide how `setNoDelay(true)` failure is surfaced **without inventing an
    unapproved §25 counter** — V1 uses a bounded serial log line (§13.3 permits
    logging); no new counter is added.

  ## Agent instructions

  If an agent reaches this task: make NO file changes. Report that the backlog
  is blocked on the operator's dependency-pinning decision and stop.

  ## Validation

  None (operator action); the standard four commands must still pass afterward.

---

* [x] Task: Implement the QNEthernet platform adapter (Phase 14)

  ## Goal

  The only platform layer: `NetworkServer`, `Transport`, and `Clock`
  implementations over QNEthernet/lwIP, satisfying the four §24 states and the
  §12 servicing rules, using the Phase-13 pinned version.

  ## Contract sections

  §3.4, §9.1, §10.4, §12, §13.2, §13.3, §24.

  ## Required skills

  `qnethernet-transport`, `arduino-cli-builds`, `command-server-contract`,
  `fixed-capacity-cpp`.

  ## Scope

  * implement in `src/platform/qnethernet/` only: the Phase-4 `NetworkServer`
    seam (progress/yield, availability, begin/listen, pending detection, accept,
    close/abort); Phase 14 implements anonymous generation-tagged slots and handle lookup only. Role assignment belongs exclusively to SessionDriver. `Transport` (non-blocking read,
    write-some, flush, the four §24 states); `Clock` (`millis`/`micros`)
  * The adapter task must compile and instantiate the platform classes on the Teensy toolchain before it is considered complete.
  * `setNoDelay(true)` after accept; failure surfaced via bounded, best-effort, and non-blocking serial logging (no new counter), non-fatal (§13.3)
  * DHCP + static IPv4 from `NetworkConfig` (§3.4); board-local safe until the
    interface is up; no network I/O from an ISR (§12)
  * no QNEthernet/Arduino types escape `src/platform/` (enforced)

  ## Do not implement

  * any `src/core`/`src/api` change; sketches/CI (Phase 15); conformance client
    (Phase 17)

  ## Allowed files

  * `src/platform/qnethernet/*.h`
  * `src/platform/qnethernet/*.cpp`
  * a minimal compile-only Teensy fixture (e.g. under `tests/hardware/fixtures/`)
  * required build-harness changes
  * `tools/build_teensy.sh`

  ## Forbidden files

  `src/core/`, `src/api/`, `src/support/`, `docs/contracts/`, `third_party/`.

  ## Tests should cover

  * `check_invariants.py` confirms no networking types escape the platform dir;
    host tests stay green; hardware behavior is validated in Phases 15–18, never
    simulated here

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [x] Task: Add the smoke and conformance sketches and enable Teensy CI
  (Phase 15)

  ## Goal

  A minimal example sketch plus a **dedicated, deterministic conformance
  firmware fixture**, and the enabled arduino-cli CI pinned to the Phase-13
  versions.

  ## Contract sections

  §4, §12, §28.1 (and the §28 behaviors the fixture must expose).

  ## Required skills

  `arduino-cli-builds`, `qnethernet-transport`, `repository-conventions`.

  ## Scope

  * `sketches/ethernet_smoke/ethernet_smoke.ino` — minimal: identity + DHCP
    (commented static block), one trivial command, one telemetry field, the two
    hooks, loop `service()` (§12)
  * `sketches/command_server_conformance/command_server_conformance.ino` — the
    §28 fixture, deterministically controllable (e.g. via registered commands /
    compile flags) to exercise: e-stop hook **success / failure / over-budget**;
    controller-loss hook budget; `estop_triggered`; an **oversized handler
    result**; **telemetry overflow / failure** modes; **`get_counters` enabled**;
    and reconnect / supersession. Phase 17 targets **this** sketch.
  * the conformance sketch uses **clearly test-only command names** (e.g.
    `test_echo`, `test_oversized_result`, `test_estop_fail`) — never names that
    could collide with real board commands
  * enable `arduino/setup-arduino-cli` + core/lib install in `ci.yml`, pinned to
    the Phase-13 tag/commit/versions/Action SHA; no `#line`/absolute paths
    (§4, §28.1)

  ## Do not implement

  * core/api/platform behavior changes; the conformance client (Phase 17)

  ## Allowed files

  * `sketches/ethernet_smoke/ethernet_smoke.ino`,
    `sketches/command_server_conformance/command_server_conformance.ino`,
    `.github/workflows/ci.yml`, `docs/companion/Build_and_Flash_Guide.md`

  ## Forbidden files

  `src/`, `docs/contracts/`, `third_party/`.

  ## Tests should cover (§28.1)

  * `./tools/build_teensy.sh` compiles **both** sketches for
    `teensy:avr:teensy41`; clean checkout, no developer paths, no cache
    dependence; pinned QNEthernet version documented

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [x] Task: Audit + harden the Phase-16 demo firmware changes (post-demo cleanup)

  ## Context

  A fast operator-run Phase-16 demo (2026-06-14/15) reached live interop but
  required two firmware fixes made under time pressure. This task is a thorough,
  unrushed audit of everything changed and thought about on the **firmware/library
  side**, plus the integration-test coverage that would have caught the bugs
  before hardware. Full session record:
  `tests/hardware/results/2026-06-14-phase16-interop/INTEGRATION_BRIEF.md` and
  `SESSION_AUDIT.md` (read both first).

  ## Why this matters

  Both demo bugs were **facade-wiring** defects in `src/TeensyCommandServer.h`
  (telemetry never pushed; only `get_counters` dispatched). The inner components
  were correct and unit-tested, but **no host test exercised a registered command
  or telemetry through the public facade `service()` path**, so the suite was green
  while real interop was broken. The audit must close that altitude gap, not just
  re-check the diff.

  ## Audit scope (review every change for correctness, not just "it passes")

  * `src/TeensyCommandServer.h`:
    - `TelemetryScheduler` member wired via placement-new in `start()`: review
      lifetime/ownership vs `session_driver_`, the dtor/`destroyTelemetryScheduler`
      ordering, provider/identity/clock reference validity, and behavior across
      reconnect / `onSessionInactive` / re-`start()`.
    - `routeCommand` now always dispatches: confirm teardown happens **only** on a
      genuine critical transmit failure (dispatcher returns true only when
      `enqueueCritical != Queued`); confirm removal of `counters_diagnostic_enabled_`
      left no dead/contradictory state.
    - Confirm no per-message dynamic allocation was introduced (the dispatcher is
      stack-constructed per command — verify against the fixed-capacity invariant).
  * `tests/host/unit/test_service_loop.cpp`: this is the **one test changed to make
    the suite pass**. Independently verify the new assertions describe
    contract-correct behavior (dispatch + correlated response, no teardown; first
    telemetry frame is the liveness signal; unknown → `UNKNOWN_COMMAND`), not
    behavior fitted to the new code. Cross-check `CommandDispatcher.dispatch`
    semantics and the § contract command-dispatch rules.
  * Sketch (`sketches/command_server_conformance/command_server_conformance.ino`):
    the demo left a **bench static IP** and a demo command **`test_sum`** in a
    Phase-17 contract fixture. Decide and execute: revert the static IP (commit
    default is DHCP), and either remove `test_sum`, or move the
    "register-a-command" demo into a dedicated demo sketch so the conformance
    fixture's schema stays canonical. Re-run Phase-17 expectations after any change.

  ## New integration coverage to add (the real gap)

  * Host-level **facade integration tests** that drive `TeensyCommandServer.service()`
    end to end (the layer the unit tests skipped): a registered command produces a
    correlated response without teardown; telemetry is pushed on the 50 ms schedule
    and resumes after a session replacement; `estop`→`estop_ack`; `get_counters`;
    unknown/bad-arg paths; oversized response/telemetry; provider-failure telemetry.
    Treat "schema sent but no telemetry/command-response follows" as a regression
    oracle.
  * A guard/invariant (check_invariants or a test) that **every `ServiceLoop::Routes`
    field the facade is expected to wire is non-null after construction**, so a
    future missing-route regression fails loudly.
  * Firmware robustness for the **single-client session wedge** observed on hardware
    (controller⇄board TCP half-open ~116 s, board stuck not re-serving until an
    external connection forced replacement): review QNEthernet/`SessionDriver`
    handling of half-open / stale peers and rapid reconnect churn; add a
    self-recovery path and a host test that simulates a half-open/abandoned peer.

  ## Hardware (operator-run; the full rig is available)

  * Reflash `command_server_conformance` and run the Phase-17 conformance client +
    the §28/§29 checklist; record under `tests/hardware/results/` with toolchain
    versions. NB: the demo ran on **arduino-cli 1.5.0 / teensy core 1.60.0**, which
    deviate from the Build/Flash pins (1.3.1 / 1.59.0) — rerun on pinned toolchain
    before any §31 claims.
  * Stress the session layer: rapid connect/disconnect churn, telemetry-stall →
    liveness, reconnect storms; confirm no wedge and bounded recovery.

  ## Do not

  * Edit `docs/contracts/` or claim §31/freeze results (that's Phase 18).

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```
  plus the recorded operator hardware run.

---

* [x] Task: OPERATOR GATE — actual-controller vertical-slice integration (Phase 16)

  ## Goal

  Operator-run integration test using the **real controller repository** (not
  only the Python mock controller). Proves end-to-end interop before declaring
  demo readiness.

  ## What to prove and record

  * schema-first registration observed by the real controller
  * one Unix-socket client command routed through the controller to Teensy and
    a correlated response returned to that client
  * telemetry received by the real controller
  * disconnect/reconnect and schema re-registration
  * software e-stop and `estop_ack`

  ## Record in `tests/hardware/results/`

  Controller commit, firmware commit, board/network configuration, and logs.
  Operator may add Phase-16 logs/results under `tests/hardware/results`.
  Autonomous agents still make no integration claims.

  ## Agent instructions

  If an agent reaches this task: make NO file changes and claim no integration
  result. Report that the backlog is blocked on operator-run integration
  testing and stop.

  ## Validation

  Operator-run; never simulated.

---

* [x] Task: Build the Python conformance client and harness (Phase 17)

  ## Goal

  An automatable controller-side client that drives the **conformance sketch**
  over TCP and checks the §28 behaviors reachable without hardware-specific
  rigs, plus the §29 acceptance checklist.

  ## Contract sections

  §28 (all subsections), §29.

  ## Required skills

  `host-conformance-testing`, `command-server-contract`, `ndjson-framing`,
  `repository-conventions`.

  ## Scope

  * fill in `tools/run_conformance.sh` + `tests/conformance/`: connect as the
    controller, assert schema-first within the window, exercise framing /
    dispatch / telemetry / e-stop / heartbeat / supersession rows, and read
    `get_counters` to verify counter movement
  * target `command_server_conformance`; map every §28 row to a check or an
    explicit `SKIP (hardware only)` with a reason; emit a §29 summary
  * never fakes hardware results; never run in hosted CI (AGENTS.md)

  ## Do not implement

  * firmware behavior changes; hardware result claims

  ## Allowed files

  * `tools/run_conformance.sh`, `tests/conformance/*`

  ## Forbidden files

  `src/`, `docs/contracts/`, `third_party/`.

  ## Tests should cover

  * the client's own logic is unit-testable against recorded fixtures; the live
    run is operated against hardware in Phase 18

  ## Validation

  ```bash
  python3 tools/check_invariants.py
  python3 tools/check_contract_sync.py
  ./tools/run_host_tests.sh
  ./tools/build_teensy.sh
  ```

---

* [ ] Task: OPERATOR GATE — hardware validation of §31 numerics and contract
  FREEZE (Phase 18)

  ## Goal

  Operator/hardware action recorded as a blocking ledger item. An autonomous
  agent cannot claim physical validation or freeze a contract. Resolves **D5**.

  ## Operator steps

  * flash `command_server_conformance` to a physical Teensy 4.1, run the
    Phase-17 client, and confirm the four §31 numerics (100 ms e-stop and
    controller-loss hook budgets, 100 ms transmit deadline, 10-frame telemetry
    teardown) against real Teensy/QNEthernet behavior
  * **record** in `tests/hardware/results/`: Teensy/core version, QNEthernet tag
    + commit, firmware commit, network topology, raw timings, and pass/fail per
    §28 row
  * **if a provisional numeric changes:** update **every exact contract-body
    occurrence** of that value (§13/§19/§31), `src/support/Limits.h`, and all
    affected tests, then **rerun conformance** — status-line-only editing is
    insufficient
  * only the operator approves the measurements, regenerates the contract hash
    manifest (`check_contract_sync.py --update`), and stamps the contract Status
    line **FROZEN**

  ## Agent instructions

  If an agent reaches this task: make NO file changes and claim no hardware
  result. Report that the backlog is blocked on operator hardware validation and
  stop.

  ## Validation

  The standard four commands plus the recorded hardware conformance run
  (operator-run; never simulated).

