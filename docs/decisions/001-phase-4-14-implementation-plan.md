# 001: Phase 4–18 implementation plan and its decisions

Date: 2026-06-13
Status: ledgered

(Filename retains the original `4-14` slug; the plan now spans Phases 4–18
after eight external audit rounds added a network seam, API-extension and
dependency-pin gates, a conformance fixture, actual-controller demo
integration, and full hardware freeze gates. Status is ledgered; the entries
have been appended to `backlog.md`.)

Context:

Phases 0–3 (types, vocabulary, framer, parser, registry, schema builder, JSON
writer) are complete and audited as faithful. The remaining runtime existed
only as a commented roadmap. A first plan (Phases 4–14) was drafted, audited
externally six times, and corrected into the current Phases 4–18 proposal in
`backlog_proposal.md`.

Decision:

Structural (kept): transmit-first ordering; the session engine split into a
state-machine driver and a facade/service pump.

Structural (added by the audits):
* **Network/server seam (Phase 4)** with generation-tagged `ConnectionHandle`
  (slot + uint32_t generation struct, no heap), a `NetworkAvailability` enum,
  and pinned close-vs-abort behavior. Wrap-to-zero logic retires the slot.
  `NetworkServer` owns the two anonymous fixed Transport/connection slots and creates/validates handles.
* **SessionDriver Boundary:** owns active/replacement role variables, `LineFramer`,
  `OutboundWriter`, `Clock`, and `SessionState`. It does NOT own the `Transport` slots.
  Receives non-owning references to registry and identity; exposes `sendActiveLine`
  and `requestTeardown`.
* **Teardown Orchestration:**
  `ServiceLoop` prep: release line, reset `InboundParser` and router state, cancel scheduler entries, route `CanceledBySessionEnd`.
  `SessionDriver::applyPendingTeardown()`: run hook, reset `LineFramer` and session state, abort handle, increment `controller_disconnects`, transition to `LISTENING` (promote replacement and send schema if superseding).
* **Outbound scheduler redesign:** fixed-capacity critical queue (capacity=4,
  entry=8192 bytes, max RAM 48KB pinned). Reports `OutboundOutcome` 
  (`Sent`, `SendFailed`, `CanceledBySessionEnd`). Priority: safety > response >
  heartbeat.
* **Anti-starvation service-loop policy:** step driver -> check telemetry due -> 
  if outbound pending, consume NO inbound and drain ONE outbound -> otherwise 
  consume ONE inbound and drain ONE outbound.
* **Four operator gates:** Phase 9 (API-extension review with exact `StatusCode`),
  Phase 13 (dependency version pinning), Phase 16 (demo integration before
  freeze), Phase 18 (hardware validation + FREEZE).

Lifecycle policy — pinned from contract text. **The six-state transition table
needs no amendment.** Network-init retry uses a compile-time constant
(`support::kNetworkInitRetryMs`, 1000 ms default, not public API). Link loss
at `LISTENING` is a no-op hold; at `SESSION_CONNECTED`/`SESSION_ACTIVE` is
controller loss via existing edges. Self-transitions remain illegal. `READY`
defined strictly as init + link + usable address.

Corrected flush semantics: the transmit deadline covers `writeSome()` retries;
`flush()` is called exactly once after all bytes are accepted; a flush call is
not evidence of peer receipt.

Telemetry sequence policy: sequence initializes at boot and is strictly
increasing across the entire boot. Allocates `seq` only when accepted into
slot. Never resets on reconnect/supersession. Replacement may create gaps.

`estop_triggered` state machine: `EMPTY`, `PENDING_NOT_QUEUED`, 
`QUEUED_AWAITING_COMPLETION`. Enqueue failure returns to `PENDING_NOT_QUEUED`.
Canceled by session end returns to `PENDING_NOT_QUEUED` (persistent).

Session counter semantics: `sessions_accepted` on every accepted connection
(including replacements); `sessions_superseded` additionally increments when a 
replacement displaces an old session; `controller_disconnects` on loss,
teardown, schema-fail, or supersession; `sessions_rejected` on handle exhaustion.

Sent counter semantics: `telemetry_sent`, `estop_ack_sent`, `heartbeat_ack_sent`
increment ONLY after successful wire transmission (never on enqueue).
Canceled outcomes increment no sent counters.
`commands_ok` on successful handler + enqueue; later wire transmission failures
increment `tx_failures`, not command counters.

`board_proc_us` timing logic: `response_ready_us - parse_completed_us`
using wrap-safe unsigned arithmetic, inserted only into successful results.

Resolved decisions:
* **D1 (reopened → Phase 13 gate):** QNEthernet `v0.35.0` is a pre-release.
  Pin its full commit SHA at the Phase 13 gate.
* **D2/D3 (resolved, unchanged):** driver owns `Transport`+`LineFramer` and
  `SessionState`; pump owns `CommandRegistry`+`InboundParser`+§23 router;
  smoke sketch defaults to DHCP.
* **D5 (reopened → Phase 18 gate):** operator/hardware gate.
* **D6 (RESOLVED):** the project owner accepts downstream copyleft obligations.
  The repository adopts AGPL-3.0-or-later for firmware that links QNEthernet.

Contract refs: §3.1, §3.4, §4, §5, §8, §9, §10, §11, §12, §13, §16, §18,
§19, §20, §21, §22, §23, §24, §25, §26, §28, §29, §31.
