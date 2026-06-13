import re

with open('/Users/justinsorrells/cuddly-train/backlog_proposal.md', 'r') as f:
    content = f.read()

# Patch 1: Header update
content = content.replace(
    "reopened D1/D5/D6. Seventh correction round (2026-06-13): split teardown request\nfrom application, added scheduler cancellation outcomes, corrected the estop\nstate machine, restored session-counter semantics, removed promotion from Phase 4,\npinned scheduler storage, corrected gate metadata/dependencies, and tightened\nscopes. When accepted, these entries are appended to `backlog.md`",
    "reopened D1/D5/D6. Eighth correction round (2026-06-13): corrected Transport\nownership, split parser cleanup from SessionDriver cleanup, and updated\ncritical-failure teardown wording. When accepted, these entries are appended\nto `backlog.md`"
)

# Patch 2: D2/D3 ownership
content = re.sub(
    r"\* \*\*D2/D3 \(resolved, unchanged\):\*\* driver owns `Transport`\+`LineFramer` and\n  `SessionState`; pump owns `CommandRegistry`\+`InboundParser`\+§23 router; smoke\n  sketch defaults to DHCP\.",
    "* **D2/D3 (resolved):**\n  NetworkServer owns the two anonymous fixed Transport/connection slots and creates/validates generation-tagged ConnectionHandles.\n  SessionDriver owns the active-handle and replacement-handle role variables, LineFramer, OutboundWriter, Clock, and SessionState.\n  SessionDriver does not own the underlying Transport slot objects.\n  Keep `InboundParser`, `CommandRegistry`, routing, and `OutboundScheduler` owned by the facade/ServiceLoop as already pinned.\n  smoke sketch defaults to DHCP.",
    content
)

# Patch 3: Phase 5 deadline
content = content.replace(
    "  * deadline policy (§13.6, §26.3–.4): a `Critical` miss → `tx_failures`++ and a\n    failure result (caller closes the session + runs controller-loss); a\n    `Telemetry` miss → drop + `telemetry_dropped`++ and advance a consecutive-\n    telemetry-failure streak, raising a teardown signal at\n    `support::kTelemetryDeadlineFailuresToTeardown`",
    "  * deadline policy (§13.6, §26.3–.4):\n    A critical transmit deadline miss increments tx_failures and returns failure.\n    The caller requests deferred teardown through SessionDriver::requestTeardown(CriticalTransmitFailure).\n    No caller directly closes/aborts the socket or invokes the controller-loss hook.\n    A `Telemetry` miss → drop + `telemetry_dropped`++ and advance a consecutive-\n    telemetry-failure streak, raising a teardown signal at\n    `support::kTelemetryDeadlineFailuresToTeardown`.\n    The telemetry ten-failure signal must use the same requestTeardown path."
)

# Patch 4: Phase 6 nextLine / releaseLine
content = content.replace(
    "  * exposes the pump seam: `bool nextLine(MutableLineView&)` + `releaseLine()`,\n    valid only while `SESSION_ACTIVE`; closure invalidates an outstanding line",
    "  * exposes the pump seam: `bool nextLine(MutableLineView&)` + `releaseLine()`.\n    nextLine() may acquire only while SESSION_ACTIVE and teardown is not pending.\n    releaseLine() remains valid exactly once for a payload acquired before the session entered SESSION_CLOSING.\n    The acquired payload and CommandArgs views are not invalidated until releaseLine() returns."
)

# Patch 5: Phase 6 Sequence
content = content.replace(
    "  * Required sequence for generic teardown and supersession:\n    1. Mark SESSION_CLOSING and stop accepting/delivering inbound lines.\n    2. ServiceLoop cancels session-specific scheduler entries and routes every cancellation outcome.\n    3. SessionDriver runs the controller-loss hook exactly once and records its duration.\n    4. Discard the old framer/parser/session state.\n    5. Abort the active connection and invalidate its handle.\n    6. Increment controller_disconnects exactly once.\n    7. Transition to LISTENING.\n    8. For supersession only, promote the held replacement handle, transition to SESSION_CONNECTED, and perform schema-first registration.",
    "  * Required sequence for generic teardown and supersession:\n    ServiceLoop teardown preparation:\n    - release any acquired line\n    - reset InboundParser and router session-local state\n    - cancel OutboundScheduler entries\n    - route all CanceledBySessionEnd outcomes\n\n    SessionDriver::applyPendingTeardown():\n    - run the controller-loss hook exactly once and measure it\n    - reset LineFramer and driver-owned session state\n    - abort and invalidate the active connection handle\n    - increment controller_disconnects once\n    - transition SESSION_CLOSING -> LISTENING\n    - for supersession, promote the held replacement afterward and send schema first"
)

with open('/Users/justinsorrells/cuddly-train/backlog_proposal.md', 'w') as f:
    f.write(content)

