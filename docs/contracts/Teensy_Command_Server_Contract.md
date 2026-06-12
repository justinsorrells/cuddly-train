# Teensy 4.1 Command Server Contract (v1 PROPOSED)

**Status:** Proposed V1 normative companion contract — not frozen. Ratify via
backlog before firmware work begins.
**Target:** Teensy 4.1 using QNEthernet.
**Authority:** `V1_Networking_Decisions.md` (v3 FROZEN) remains authoritative
wherever this document conflicts with it. `Board_Developer_Guide.md` (v2
FROZEN) is the developer-facing companion; this document is the firmware
*library* contract that makes that guide true. Parenthetical refs like (1.9)
point at the governing decision in the frozen contract.

---

## 0. Purpose and authority

This contract defines the required behavior of the command-server library
running inside each Teensy 4.1 firmware image. It is the template every board
firmware copies; board developers implement their code on top of it without
touching the networking layer.

The command server is responsible for:

* Ethernet and TCP server lifecycle
* accepting the controller connection
* newline-delimited JSON framing
* schema transmission
* command lookup and dispatch
* command response generation
* telemetry transmission
* software e-stop handling and `estop_ack` framing
* controller-loss handling
* heartbeat acknowledgement
* fixed-capacity memory and bounded queues

Board application developers are responsible for (Board_Developer_Guide.md):

* board identity
* command registration metadata
* command handler functions
* telemetry values
* board-local state
* local safe-state hooks

The command server must keep these responsibilities separate. Board application
functions must not implement TCP, JSON framing, Redis communication, GUI
communication, or cross-board routing.

---

## 1. Normative language

The words **SHALL**, **MUST**, **SHALL NOT**, and **MUST NOT** are mandatory.

The words **SHOULD** and **SHOULD NOT** describe the expected implementation
unless a documented technical reason requires otherwise.

The word **MAY** describes optional behavior.

---

## 2. System assumptions

V1 assumes (contract 0):

* a closed, trusted network
* one configured controller
* one Teensy command server per board
* persistent TCP communication
* newline-delimited JSON
* no authentication or encryption in V1
* hardwired interlock and power removal as the actual safety guarantee
* software e-stop as convergence and coordination only (1.13)

The command server shall not claim that successful TCP delivery provides a
hard safety guarantee.

---

## 3. Board identity and static configuration

Each firmware image SHALL provide:

```text
board_id
protocol_version
firmware_version
listen_port
network_configuration
```

### 3.1 Board ID

`board_id` SHALL:

* be non-empty
* be stable across restarts
* match the controller configuration (contract 0: board IDs are statically
  configured; the controller rejects unknown ids at connect)
* be used as `source` on every board-originated protocol message
* be used as `target` validation on every controller-originated message

A controller-originated message whose `target` does not equal the configured
`board_id` SHALL be dropped and counted (`invalid_targets`) without invoking
any command handler, e-stop hook, heartbeat handler, or other application
behavior. The board SHALL send no response for the mismatched-target message.

This applies to `estop` as well: board IDs are statically configured and
matched at registration (contract 0), so a mismatched target is a
misconfiguration to surface, not honor — and the hardwired interlock, not
message routing, is the safety guarantee (1.13). `UNKNOWN_TARGET` remains a
controller-owned error code and is never emitted board-side.

The controller silently ignores inbound board messages whose `source` does not
equal the configured `board_id`, and FAULTs the connection if the schema
`source` mismatches. A wrong `source` therefore also starves the liveness
signal (section 18.6); it is not a recoverable misconfiguration.

### 3.2 Protocol version

The board protocol version SHALL be the string `"1"` for this contract.

It SHALL NOT be changed independently of the controller (1.16). A mismatch
causes the controller to FAULT the board and never register it.

### 3.3 Firmware version

The firmware version SHALL identify the board application build.

Changing the firmware version SHALL NOT by itself change the board protocol
version.

### 3.4 Network configuration

The command server MAY support:

* DHCP
* static IPv4 configuration

The selected mode SHALL be explicit configuration. Board application code
shall not duplicate network configuration.

The command server SHALL remain in a board-local safe state until Ethernet is
initialized and the server is ready to accept a controller connection.

---

## 4. Source portability

The committed source SHALL consist of the original project files, such as:

```text
.ino
.cpp
.h
```

Generated Arduino preprocessing output SHALL NOT be committed.

In particular, committed source SHALL NOT contain generated directives such as:

```cpp
#line 55 "/Users/name/.../sketch.ino"
```

Committed source SHALL NOT contain developer-specific absolute filesystem paths.

Generated build directories, temporary preprocessed C++ files, and Arduino
cache files SHALL be excluded from version control.

The project SHALL document or pin the tested QNEthernet version. A QNEthernet
upgrade requires rerunning the command-server conformance tests.

---

## 5. Public board-application boundary

The command server library SHALL expose operations equivalent to:

```text
set board identity
register command
register telemetry schema
register state schema
set telemetry provider
set e-stop hook
set controller-loss hook
start command server
service command server
```

Exact C++ spelling may differ, but the mapping between the public API and these
operations SHALL be documented and one-to-one.

### 5.1 Registration completion

All identity, command, telemetry, state, and hook registration SHALL complete
before the command server begins accepting connections.

Registration SHALL be immutable after server startup in V1.

Dynamic command registration during an active session is out of scope.

### 5.2 Duplicate registration

Duplicate command names SHALL cause startup or registration failure.

The server SHALL NOT silently replace one command handler with another.

### 5.3 Schema generation

The transmitted schema SHALL be generated from the same registration metadata
used for command dispatch.

There SHALL NOT be one manually maintained command table for dispatch and a
different manually maintained schema representation.

This rule prevents the declared schema from drifting from callable behavior.

---

## 6. Command registration contract

Each command SHALL register:

```text
name
argument schema
blocked_by_estop
handler
```

### 6.1 Command names

Command names SHALL be:

* non-empty strings
* unique within one board
* stable for the lifetime of protocol version 1

### 6.2 Supported V1 argument types

V1 command arguments SHALL use only (Board_Developer_Guide.md section 5):

```text
int
float
bool
string
```

For V1:

* `int` means a signed 32-bit integer
* `float` means a finite JSON number representable by the board implementation
* `bool` means a JSON Boolean
* `string` means a JSON string that fits within the inbound line and parser
  capacities

Complex nested command arguments are out of scope unless this contract is
extended.

### 6.3 Required arguments

Every argument declared in a command schema SHALL be required.

Arguments not declared in the schema SHALL be rejected.

Missing required arguments SHALL produce `MISSING_FIELD`.

Wrong JSON types SHALL produce `INVALID_TYPE`.

Handler-specific domain failures, such as an integer outside a hardware-safe
range, SHALL produce `INVALID_ARGUMENT`.

### 6.4 E-stop metadata

Every command SHALL explicitly declare:

```json
"blocked_by_estop": true
```

or:

```json
"blocked_by_estop": false
```

If the field is absent, the controller treats the command as blocked
(fail-safe, contract 1.17). Registration SHOULD require the field explicitly
rather than relying on the default.

Commands that can cause motion, energize hardware, change actuator output, or
otherwise affect physical behavior SHALL use `true`.

Only commands safe for diagnosis during e-stop may use `false`.

### 6.5 Gating is controller-owned

E-stop command gating is enforced by the **controller dispatcher** (1.17), not
by the board. The board SHALL NOT maintain a latched software e-stop gate of
its own: `estop_reset` is a client-to-controller message that is never sent to
boards (1.13), so a board-side latch could never be cleared over the protocol.

The board MAY reject a command with `ESTOP_ACTIVE` only when its **own
board-local hardware safety condition** (interlock input, fault line) currently
prevents the action. That rejection SHALL be condition-based, never latched on
a previously received `estop` message.

---

## 7. Handler behavior

A command handler SHALL:

1. receive already parsed arguments
2. validate board-specific argument constraints
3. initiate or perform one board-local action or query
4. return a structured success or error result
5. return without waiting for long-running physical completion

### 7.1 Non-blocking requirement

Normal command handlers SHALL be non-blocking (Board_Developer_Guide.md
section 6). The controller runs one command in flight per board with a 2 s
default execution timeout and a 10 s hard ceiling (1.2, 1.5); a blocking
handler stalls every other command to this board and trips the timeout.

A long-running action, such as a motor movement, calibration, ramp, or actuator
sequence, SHALL be started and acknowledged. Progress and completion SHALL be
reported through telemetry, state, or a later query.

A normal command handler SHALL NOT wait for the physical operation to finish.

### 7.2 No networking inside handlers

Command handlers SHALL NOT:

* read from or write to the TCP client
* serialize protocol JSON
* flush the socket
* modify command-server connection state
* communicate with Redis
* communicate with the GUI
* communicate directly with another board

### 7.3 Result shape

A successful handler result SHALL be representable as a JSON object.

A failed handler result SHALL contain:

```json
{
  "code": "<error code from section 17>",
  "message": "<human-readable explanation>"
}
```

Handlers SHALL NOT introduce new terminal status strings (3.12) and SHALL NOT
introduce new error codes (section 17).

`board_proc_us` is reserved command-server metadata (section 16.3). Handlers
SHALL NOT return a result field named `board_proc_us`; the library owns that
field and SHALL overwrite any handler-provided value with its own measurement.

### 7.4 Exceptions and allocation

The command-server boundary SHALL NOT depend on C++ exceptions.

Command handlers SHOULD avoid dynamic allocation in the steady-state command
path.

---

## 8. Command-server lifecycle

The local command server has these conceptual states:

```text
BOOT_SAFE
NETWORK_STARTING
LISTENING
SESSION_CONNECTED
SESSION_ACTIVE
SESSION_CLOSING
```

These are local firmware lifecycle states. They do not replace the controller's
board connection axis (2.1); the controller's view of this board is derived
from the connection and the schema, never reported by the board.

### 8.1 BOOT_SAFE

Before networking is ready, the board SHALL remain in its board-local safe
startup condition.

### 8.2 NETWORK_STARTING

The server initializes QNEthernet and obtains its configured network address.

No command may execute in this state.

### 8.3 LISTENING

The TCP server is listening, but there is no active controller session.

Telemetry SHALL NOT be transmitted without an active session.

### 8.4 SESSION_CONNECTED

A TCP client has been accepted, but the schema has not yet been successfully
sent.

No command SHALL execute before the schema is sent as the first application
message.

The controller FAULTs and reconnects if it does not receive the schema within
its **2 s registration timeout** (1.3). The server SHALL therefore send the
schema immediately on accept, not lazily.

### 8.5 SESSION_ACTIVE

The schema has been sent and normal command, telemetry, event, e-stop, and
heartbeat processing may occur.

Telemetry SHALL begin promptly after the schema is sent (section 18.6).

### 8.6 SESSION_CLOSING

The server is closing or discarding the active session.

No new commands SHALL execute after session closure begins.

---

## 9. Controller connection policy

The board SHALL operate as a TCP server. The controller SHALL be the TCP
client (frozen topology). Reconnect backoff is controller-owned (1.4); the
board's job is to keep listening.

### 9.1 One active controller session (close-old on replacement)

The board SHALL support exactly one active controller session in V1.

When a new TCP connection is accepted while a session is active, the new
connection SHALL supersede the old one. The server SHALL:

1. mark the superseded session `SESSION_CLOSING` and stop processing its
   buffered commands (section 8.6)
2. invoke controller-loss handling for the superseded session (section 21)
3. discard the superseded session's parser and session state
4. close the superseded socket
5. promote the new connection and send the schema first on it (section 9.3)
6. increment `sessions_superseded`

No buffered command from the superseded session may execute after step 1; the
replacement session waits until the sequence completes.

Rationale: the controller is the only configured client on a closed network. A
replacement connection almost always means the controller host restarted
without cleanly closing the old socket (TCP half-open). Keeping the old
session authoritative would block reconnection until lwIP's write-path
retransmission timeout finally errors the stale socket — tens of seconds or
more, since heartbeat is disabled by default. The controller's reconnect
machinery cannot reach a stale socket the board refuses to give up.

If the firmware is configured with the controller's address, it MAY require
that a replacement connection originate from that address and close others
without disturbing the active session.

### 9.2 Persistent connection

The accepted connection SHALL remain open across multiple commands and
telemetry frames.

The server SHALL NOT require a new TCP connection for each command.

### 9.3 Schema-first rule

The schema SHALL be the first application-level message transmitted after every
accepted controller connection (1.3).

The board SHALL send the schema again after every reconnect.

The board SHALL NOT wait for a `get_schema` command. No board-level
`get_schema` command exists. (The controller's `get_schemas` API for local
clients is unrelated and controller-owned.)

If the schema cannot be transmitted, the session SHALL be closed and the board
SHALL return to `LISTENING`.

### 9.4 Reconnect during e-stop

When `system.estop_active` is latched, the controller sends `estop` to a board
**immediately after registration** (1.13 step 4). The server SHALL handle
`estop` arriving as the very first inbound message after the schema is sent,
before any command.

---

## 10. TCP and framing contract

All application messages SHALL use UTF-8 newline-delimited JSON.

Exactly one JSON object SHALL appear per line.

Every transmitted message SHALL end with exactly one line-feed byte:

```text
\n
```

Pretty-printed or multiline JSON SHALL NOT be used. Outbound serialization
SHALL be compact (no inter-token whitespace), matching the controller's
serializer.

### 10.1 Line-length definition

For this contract, line length includes the terminating newline byte. (This
matches the controller implementation, which counts the encoded line including
`\n` against the limit on both send and receive.)

### 10.2 Board receive limit

A controller-to-board line SHALL be no larger than (1.9):

```text
1024 bytes including the terminating newline
```

The internal input buffer SHALL also reserve storage for any implementation
terminator that is not transmitted on the wire.

### 10.3 Board transmit limit

A board-to-controller line SHALL be no larger than (1.9):

```text
8192 bytes including the terminating newline
```

This applies to:

* schema
* responses
* telemetry
* events
* heartbeat acknowledgements

An oversized board-to-controller line causes the controller to error the
connection (1.9); it is a session-fatal fault, not a dropped message.

### 10.4 Incremental receive behavior

The server SHALL correctly handle:

* one message split across multiple TCP reads
* multiple complete messages in one TCP read
* a complete line followed by a partial next line
* connection closure during a partial line

TCP read boundaries SHALL NOT be treated as message boundaries.

### 10.5 Oversized input

If an inbound line exceeds 1024 bytes before a newline is found, the server
SHALL (1.9):

1. enter discard mode
2. discard bytes through the next newline
3. increment an oversized-line counter
4. avoid parsing or executing the discarded content
5. remain operational for later valid lines unless another session fault occurs

The server SHALL NOT execute a truncated prefix of an oversized command.

### 10.6 Invalid UTF-8 or JSON

An unparseable line SHALL:

* execute no handler
* increment an invalid-message counter
* leave the board running
* not produce a response when no trustworthy `seq` can be recovered

A valid JSON object with a usable `seq` but invalid command structure SHOULD
receive a structured error response.

(The controller behaves symmetrically: a malformed board line within the size
limit is counted and dropped without killing the connection.)

---

## 11. Fixed-capacity memory

The inbound command path SHALL use fixed-capacity storage (1.9).

The implementation SHALL use a fixed-capacity JSON parser such as
`StaticJsonDocument` or an equivalent bounded representation.

The command server SHALL NOT perform unbounded dynamic allocation per received
message.

### 11.1 Buffer ownership

The implementation SHALL define bounded storage for:

* inbound line accumulation
* parsed command document
* outbound response serialization
* outbound schema serialization
* telemetry serialization
* critical outbound message state

### 11.2 Outbound overflow

If a handler result or telemetry snapshot cannot be serialized within the
8192-byte controller receive limit, the original oversized payload SHALL NOT be
sent.

For a command response, the server SHALL attempt to send a compact
`INTERNAL_ERROR` response using the original command `seq`.

For telemetry, the oversized frame SHALL be dropped and counted.

---

## 12. QNEthernet service requirements

The firmware SHALL allow the QNEthernet/lwIP stack to make regular progress.

The application SHALL NOT use long busy-wait loops that prevent `yield()` or
equivalent network servicing.

The command-server `service` operation SHALL be called on every normal firmware
loop iteration.

Network operations SHALL NOT be performed from an interrupt service routine.

Hardware timers MAY set flags or counters, but TCP reads, TCP writes, JSON
serialization, handler dispatch, and socket flushing SHALL occur in normal
execution context.

---

## 13. Serialized outbound writes

All board-to-controller messages SHALL use one serialized write path. (This is
the board-side mirror of the controller's per-board write serialization, 1.19.)

Telemetry, responses, schema, heartbeat acknowledgements, and e-stop events
SHALL NOT write concurrently or interleave bytes.

### 13.1 Complete writes

The implementation SHALL use `writeFully()`-equivalent retry logic that
ensures the entire serialized line is accepted before the message is
considered written, bounded by the transmit deadline in section 13.6.

The protocol path SHALL NOT rely on `print()`, `println()`, or a single partial
`write()` call to transmit a complete message, and SHALL NOT use an unbounded
blocking write helper (raw `writeFully()` with no deadline does not conform).

### 13.2 Flush behavior

After one complete newline-delimited JSON message is written, the server SHALL
call `flush()` so the logical message is promptly submitted to the TCP stack.

### 13.3 TCP_NODELAY

After accepting a controller connection, the server SHOULD request
`TCP_NODELAY` using `setNoDelay(true)`.

Failure to enable it SHALL be observable through logging or a counter. It need
not invalidate an otherwise usable session.

### 13.4 Outbound priority

The command server SHALL prioritize outbound messages in this order:

1. schema during session establishment
2. `estop_ack` and safety-related events
3. command responses
4. heartbeat acknowledgements
5. telemetry

Critical messages SHALL NOT be silently dropped.

If a critical response or safety acknowledgement cannot be sent, the session
SHALL be closed and controller-loss handling SHALL run.

### 13.5 Telemetry backpressure

Telemetry SHALL NOT accumulate in an unbounded queue.

If multiple telemetry periods pass while a previous telemetry frame is still
pending, the server SHALL coalesce them into the latest snapshot.

The server SHALL count coalesced or dropped telemetry frames.

It SHALL NOT send a burst of stale telemetry frames to catch up.

### 13.6 Transmit deadline

Every outbound transmission SHALL have a finite transmit deadline (default
100 ms). The complete-write helper SHALL retry partial writes only until the
deadline, SHALL yield to or service the network stack between retries, and
SHALL NOT busy-wait without servicing.

The default sits below the controller's ~250 ms liveness window (18.6) so one
slow transmission cannot by itself look like board death.

On deadline expiry:

* **critical message** (schema, response, `estop_ack`, safety event,
  heartbeat acknowledgement): count the failure (`tx_failures`), close the
  session, and run controller-loss handling (section 26.3).
* **telemetry**: drop the frame and count it. If telemetry hits the deadline
  on 10 consecutive frames (default), the connection SHALL be treated as
  failed and the session closed — a peer that stops reading must not keep a
  session half-alive indefinitely (section 26.4).

---

## 14. Schema message

The schema SHALL have this shape (3.7):

```json
{
  "type": "schema",
  "seq": 1,
  "timestamp": 12345,
  "source": "<board_id>",
  "target": "controller",
  "protocol_version": "1",
  "schema": {
    "commands": {
      "<command_name>": {
        "args": {
          "<argument_name>": "<argument_type>"
        },
        "blocked_by_estop": true
      }
    },
    "telemetry": {
      "<field_name>": "<field_type>"
    },
    "state": {
      "<field_name>": "<field_type>"
    },
    "firmware_version": "<firmware_version>"
  }
}
```

Controller-validated requirements: `seq` is a uint64; `source`, `target`, and
`protocol_version` are strings; `schema` and `schema.commands` are objects;
each command entry is an object whose `args` is an object and whose
`blocked_by_estop`, if present, is a Boolean. `source` SHALL equal the
configured `board_id` or the controller FAULTs the connection without
registering.

`seq` on schema, telemetry, and event messages is board-local and
informational; it carries no command-correlation semantics.

The schema SHALL fit within the 8192-byte board-to-controller line limit. (It
is sized against the controller's 8 KB receive limit, not the board's 1 KB
limit, per 1.9.)

The schema SHALL describe every command callable through the command server.

A command not present in the transmitted schema SHALL NOT be callable.

---

## 15. Command message

A controller-to-board command has this shape (3.4):

```json
{
  "type": "command",
  "seq": 1042,
  "controller_ts": 81234.567,
  "source": "controller",
  "target": "<board_id>",
  "command": "<command_name>",
  "args": {}
}
```

`seq` here is the controller-owned `board_seq` (1.1). The board never sees the
originating client's seq.

The server SHALL validate:

* `type == "command"`
* `seq` is a uint64
* `controller_ts` is numeric
* `source == "controller"`
* `target == board_id`
* `command` is registered
* `args` is an object
* all required arguments exist
* all argument types match
* no unsupported arguments are present

An unregistered `command` SHALL produce `UNKNOWN_COMMAND`.

The server SHALL copy `controller_ts` without interpreting it (1.10). It is a
controller-monotonic round-trip token; the board never converts, compares, or
persists it.

---

## 16. Command response

A successful board-to-controller response has this shape:

```json
{
  "type": "response",
  "seq": 1042,
  "controller_ts": 81234.567,
  "timestamp": 12346,
  "source": "<board_id>",
  "target": "controller",
  "status": "ok",
  "result": {
    "board_proc_us": 850
  },
  "error": null
}
```

An error response has this shape:

```json
{
  "type": "response",
  "seq": 1042,
  "controller_ts": 81234.567,
  "timestamp": 12346,
  "source": "<board_id>",
  "target": "controller",
  "status": "error",
  "result": null,
  "error": {
    "code": "INVALID_ARGUMENT",
    "message": "argument is outside the allowed range"
  }
}
```

Controller-validated requirements: `seq` (uint64), `source`, `target`, and
`status` are required. On `"ok"`, `error` SHALL be `null` or absent. On
`"error"`, `error` SHALL be an object whose `code` is one of the contract error
codes and whose `message` is a string. `result` SHALL be an object or null.
`timestamp` is informational and optional.

### 16.1 Sequence echo

The board SHALL echo the controller-provided command `seq` (the `board_seq`)
unchanged.

The board SHALL NOT create a replacement sequence number for a command
response. A response whose `seq` matches no pending controller entry is
dropped and counted by the controller (1.8); it is never an error to the
board, but it means the response did nothing.

### 16.2 Controller timestamp echo

The board SHALL echo `controller_ts` unchanged (1.10).

The board SHALL NOT:

* convert it
* subtract from it
* interpret it as wall-clock time
* persist it

### 16.3 Board processing duration

`board_proc_us` is optional. If provided, it SHALL be a field **inside
`result`** on an `"ok"` response (3.5) — the controller forwards `result` to
clients and drops unknown top-level response fields, so a top-level
`board_proc_us` would be lost.

It SHALL represent the local duration from completed command parse to response
readiness, measured with `micros()` or equivalent. It SHALL NOT include
controller-side latency.

`board_proc_us` is reserved command-server metadata. Board application
handlers SHALL NOT return a result field named `board_proc_us`; the library
owns insertion of that field and SHALL overwrite any handler-provided value.
A successful result may contain both application fields and the library-owned
metadata:

```json
{
  "result": {
    "accepted": true,
    "requested_rpm": 1200,
    "board_proc_us": 850
  }
}
```

### 16.4 Board-generated statuses

The board SHALL generate only:

```text
ok
error
```

The board SHALL NOT generate `timeout`. Command timeout is owned by the
controller (1.5), and the controller rejects any board response that pairs
`timeout`/`COMMAND_TIMEOUT` incorrectly as malformed.

---

## 17. Board-side error codes

The board MAY return these existing error codes (3.11):

```text
MISSING_FIELD
INVALID_TYPE
UNKNOWN_COMMAND
INVALID_ARGUMENT
INTERNAL_ERROR
ESTOP_ACTIVE        (condition-based only, per section 6.5)
```

The board SHALL NOT invent new error codes without a coordinated contract
change. **Consequence:** the controller validates `error.code` against the
closed contract set; a response carrying an unknown code is malformed, is
dropped, and the command then resolves controller-side as `COMMAND_TIMEOUT`.
An invented code does not degrade gracefully — it loses the response.

`BOARD_UNAVAILABLE`, `BOARD_BUSY`, `COMMAND_TIMEOUT`, `CONTROLLER_SHUTDOWN`,
`UNKNOWN_TARGET`, and `PROTOCOL_VERSION_MISMATCH` are controller-owned
outcomes and SHALL NOT be emitted by board command handlers.

---

## 18. Telemetry

Telemetry SHALL be one-way push from the board (1.6). It SHALL NOT be
solicited by a command.

The nominal telemetry period is:

```text
50 milliseconds
```

### 18.1 Scheduling

The telemetry schedule SHALL use a non-blocking elapsed-time check or a
hardware timer flag.

A timer interrupt MAY mark telemetry as due, but SHALL NOT perform networking
or JSON serialization (section 12).

If transmission is delayed, the server SHALL send the latest snapshot rather
than replaying every missed interval.

### 18.2 Telemetry provider

The telemetry provider SHALL:

* return the latest values
* be fast
* be non-blocking
* perform no network operations
* avoid waiting for sensors
* avoid initiating long hardware operations

Slow sensor acquisition SHALL occur elsewhere in the firmware, with the latest
completed values exposed to the provider.

### 18.3 Telemetry message

```json
{
  "type": "telemetry",
  "seq": 441,
  "timestamp": 12350,
  "source": "<board_id>",
  "target": "controller",
  "telemetry": {}
}
```

Controller-validated requirements: `seq` (uint64), `source`, `target`, and a
`telemetry` object are required.

Telemetry `seq` SHALL be a board-local monotonic uint64 that resets on board
restart. Telemetry sequence numbers SHALL NOT share command-response
correlation semantics.

### 18.4 Timestamp

Board-originated `timestamp` values SHALL represent board-local uptime or
another documented board-local monotonic value.

They are informational only and SHALL NOT be used by the controller to compute
one-way latency (1.10).

### 18.5 State visibility

The command server SHALL NOT invent a new uncontracted state message type.

Board state values declared in `schema.state` SHALL be made observable through
one or both of:

* telemetry fields
* registered query commands

### 18.6 Telemetry is the liveness signal

Telemetry doubles as the board-to-controller liveness signal (1.6). The
controller FAULTs the board and reconnects after **~250 ms** (~5 missed
frames) with no valid inbound message.

Therefore, while a session is active:

* telemetry SHALL begin within one liveness window (250 ms) of the schema send
* telemetry SHALL keep flowing **including while e-stop is active** — a board
  that goes quiet during an e-stop looks dead to the controller and the
  operator
* the firmware SHALL NOT pause telemetry to "save bandwidth" while idle

Stopping telemetry mid-session is indistinguishable from board death and will
tear the session down.

---

## 19. Software e-stop

The controller sends exactly this shape (no `seq`, no `timestamp`):

```json
{
  "type": "estop",
  "source": "controller",
  "target": "<board_id>"
}
```

On receiving a valid `estop` message, the server SHALL:

1. stop dispatching ordinary messages until the e-stop hook has run
2. invoke the registered board-local e-stop hook
3. require the hook to be safe to call repeatedly (idempotent)
4. send `estop_ack` only after the hook confirms local safe state
5. resume protocol servicing (including telemetry) after the hook returns,
   whether or not it reported safe state

The e-stop hook is different from a normal command handler. It SHALL perform
the minimum work required to apply the local safe state before returning
(Board_Developer_Guide.md section 10). It is exempt from the non-blocking rule
to exactly the extent of the time budget below.

### 19.1 Hook time budget

The e-stop hook SHALL apply local safe state and return within **100 ms**.

The budget exists because the hook runs synchronously on a single-threaded
board while telemetry is the liveness signal (18.6): a hook that runs past the
~250 ms liveness window stalls telemetry and heartbeat servicing, and the
controller FAULTs the board mid-e-stop. 100 ms leaves margin for the write
path and at least one telemetry frame inside the window.

"Apply safe state" means commanding the safe condition (outputs cut, drive
disabled) — not waiting for physics. If the physical process outlasts the
budget (a ramp-down, a venting valve), the hook SHALL command it and return;
the hardwired interlock, not the hook, covers the hazardous case (1.13).

The library cannot preempt a running hook, so the budget is a conformance
requirement on the board application. The library SHALL measure hook duration
and count over-budget runs (`estop_hook_over_budget`).

The acknowledgement reports the truth of the safe state (1.20), not the hook's
timing: if the hook reports safe state applied, `estop_ack` SHALL be sent even
when the hook ran over budget. An over-budget hook is a conformance failure
surfaced through the counter and conformance tests, never through a withheld
ack. If the hook reports failure, no `estop_ack` SHALL be sent (19.2).

The same budget applies to the controller-loss hook (section 21).

### 19.2 Acknowledgement

Successful application of safe state SHALL produce (3.13, 1.20):

```json
{
  "type": "event",
  "timestamp": 12351,
  "source": "<board_id>",
  "target": "controller",
  "event": "estop_ack",
  "details": {
    "state": "safe"
  }
}
```

If safe state cannot be confirmed, the server SHALL NOT send an `estop_ack`
event **at all**. The controller rejects an `estop_ack` whose `details.state`
is anything other than `"safe"` as malformed, so a qualified or failed ack
cannot be expressed through this event. A missing acknowledgement is
observable controller-side and is preferable to a false acknowledgement
(1.20).

Event messages carry no `seq`; `source` and `event` are required, `target` and
`details` follow the shapes shown here.

### 19.3 Idempotency

Repeated `estop` messages SHALL be safe.

They MAY re-run the safe-state hook. A reconnect during an active e-stop
**will** re-deliver `estop` (section 9.4).

### 19.4 Safety claim

The command server SHALL document that software e-stop does not replace the
hardwired interlock or power cut (1.13). Assume drive may already be
physically cut when the hook runs.

---

## 20. Board-originated e-stop event

If the board detects its local hardware interlock or equivalent board-local
e-stop condition, it SHALL notify the controller using:

```json
{
  "type": "event",
  "timestamp": 12352,
  "source": "<board_id>",
  "target": "controller",
  "event": "estop_triggered",
  "details": {
    "reason": "<board-local reason>"
  }
}
```

This event latches `system.estop_active` controller-side (1.13 step 1-2).

The hardware safety action SHALL NOT wait for this message to be transmitted.

---

## 21. Controller-loss behavior

The board SHALL provide an idempotent controller-loss hook
(Board_Developer_Guide.md section 10).

The server SHALL invoke it when an active controller session is lost due to:

* TCP connection closure
* Ethernet link loss
* interface loss
* explicit server shutdown
* a critical-message transmit-deadline failure (13.6)
* supersession by a replacement controller connection (9.1)

Once session loss is detected:

* no additional buffered commands SHALL execute
* partial inbound messages SHALL be discarded
* the board SHALL enter its configured local safe state
* the server SHALL return to `LISTENING` when networking permits

The server SHALL NOT queue commands across controller sessions. (The
controller likewise never queues commands across a reconnect, 1.11.)

The controller-loss hook SHALL meet the same 100 ms budget as the e-stop hook
(19.1). The library SHALL measure its duration and count over-budget runs
(`controller_loss_hook_over_budget`, separate from the e-stop counter so slow
e-stop application and slow loss handling are distinguishable). The hook
cannot be preempted mid-call; an over-budget run is a conformance failure, and
session teardown or replacement-session promotion continues once the hook
returns. On supersession the hook runs before the new session's schema send,
which must still land inside the controller's 2 s registration window (8.4).

V1 limitation (documented, accepted): a silently dead controller host on a
healthy link (TCP half-open) is not detected promptly while the session is
idle. Recovery is bounded but not fast: a restarted controller supersedes the
stale session on connect (9.1), and sustained telemetry deadline failures
close a wedged session (13.6). The hardwired interlock, not this hook, is the
safety guarantee.

---

## 22. Heartbeat acknowledgement

Heartbeat is optional and controller-configured (1.6: RX-path insurance only;
disabled by default). The board command server SHALL understand it regardless.

The controller sends:

```json
{
  "type": "heartbeat",
  "seq": 7,
  "source": "controller",
  "target": "<board_id>"
}
```

The board SHALL reply with:

```json
{
  "type": "heartbeat",
  "seq": 7,
  "source": "<board_id>",
  "target": "controller"
}
```

The controller validates the ack strictly: `seq` SHALL be the echoed integer,
`source` SHALL be the board id, and `target` SHALL be `"controller"`; anything
else is counted as a malformed ack.

Controller defaults (for sizing the response budget): heartbeats every 5 s,
ack awaited for **1 s**, board marked suspect after 3 consecutive misses. The
ack SHALL therefore be sent from the normal service loop without waiting on
any application work — comfortably inside 1 s.

Heartbeat handling:

* SHALL bypass application command registration
* SHALL invoke no board application handler
* SHALL NOT allocate application command state
* SHALL NOT affect telemetry sequencing
* SHALL NOT be treated as a command response

Heartbeat acknowledgement provides controller-to-board receive-path evidence.
Telemetry, not heartbeat, is the primary board-to-controller liveness signal
(section 18.6).

---

## 23. Message dispatch order

Target validation (3.1) precedes dispatch: a mismatched `target` drops the
message regardless of type, with no response and no handler or hook invoked.

For each complete inbound line, the server SHALL dispatch by `type`:

```text
estop      -> local safe-state hook and estop_ack
heartbeat  -> heartbeat acknowledgement
command    -> registered command dispatch
other      -> reject or drop according to message validity
```

`schema`, `telemetry`, `response`, `event`, and `estop_reset` are not valid
controller-to-board messages.

If one of those is received from the controller, the server SHALL execute no
application command and SHOULD record an invalid-message counter.

---

## 24. Connection and receive semantics

The command server SHALL NOT rely on a single connection-status method to
infer that all buffered input is gone.

The implementation SHALL distinguish:

* connection is still open
* unread bytes are available
* connection has closed
* link or interface is unavailable

Once the server commits to controller-loss handling, it SHALL discard remaining
unprocessed command bytes rather than execute commands after the controller is
gone.

---

## 25. Logging and local counters

The command server SHALL maintain bounded local counters for at least:

```text
sessions_accepted
sessions_rejected
sessions_superseded
schemas_sent
commands_received
commands_ok
commands_error
unknown_commands
invalid_arguments
invalid_json
invalid_targets
oversized_lines
telemetry_sent
telemetry_coalesced
telemetry_dropped
estop_received
estop_ack_sent
estop_apply_failed
estop_hook_over_budget
controller_loss_hook_over_budget
heartbeat_received
heartbeat_ack_sent
tx_failures
controller_disconnects
```

Counters MAY be exposed through telemetry or a registered diagnostic command.

Logging SHALL be bounded and SHALL NOT block the control loop.

Serial logging SHALL NOT be required for correctness.

---

## 26. Failure behavior

### 26.1 Network startup failure

If Ethernet initialization or address acquisition fails, the board SHALL remain
safe and SHALL execute no remote command.

The retry policy MAY be configuration-specific.

### 26.2 Schema-send failure

If the schema cannot be sent, the session SHALL be closed.

### 26.3 Critical response failure

If a command response, `estop_ack`, or heartbeat acknowledgement cannot be
fully sent within the transmit deadline (13.6), the server SHALL count the
failure, close the session, and run controller-loss handling.

### 26.4 Telemetry failure

A telemetry frame that misses the transmit deadline SHALL be dropped and
counted.

Telemetry failure SHALL NOT block command handling indefinitely. Ten
consecutive deadline failures close the session (13.6); sustained telemetry
silence ends it controller-side anyway via the liveness timeout (18.6).

### 26.5 Parser failure

Parser failure SHALL execute no application handler.

### 26.6 Internal failure

An internal command-dispatch failure SHOULD produce a compact
`INTERNAL_ERROR` response when a valid command `seq` is available.

---

## 27. Prohibited behavior

The command server SHALL NOT:

* communicate with Redis
* communicate with the GUI
* route commands to another board
* accept commands not present in the schema
* execute commands before schema transmission
* perform network I/O inside an interrupt
* use TCP read boundaries as message boundaries
* silently truncate oversized input
* silently drop command responses
* emit a false `estop_ack`
* emit an `estop_ack` with any `details.state` other than `"safe"`
* latch a board-side software e-stop command gate (section 6.5)
* stop telemetry while a session is active (section 18.6)
* allocate an unbounded telemetry queue
* block waiting for a physical operation to complete
* retry a partial write without a finite deadline
* interpret `controller_ts`
* invent error codes, statuses, message types, or event names
* commit generated Arduino `#line` output as source

---

## 28. Required conformance tests

### 28.1 Build and portability

* clean checkout builds without developer-specific paths
* committed source contains no generated absolute-path `#line` directives
* tested QNEthernet version is documented
* build does not depend on generated local cache files

### 28.2 Connection lifecycle

* board starts safe
* board begins listening after network initialization
* one controller connection is accepted
* a replacement controller connection supersedes the active session:
  controller-loss handling runs for the old session, parser state is cleared,
  and schema is sent first on the new connection
* no command from a superseded session executes after supersession
* an over-budget controller-loss hook increments
  `controller_loss_hook_over_budget` and the replacement session is still
  promoted once the hook returns
* schema is the first message and is sent within the 2 s registration window
* reconnect sends schema again
* `estop` arriving immediately after schema (reconnect-during-e-stop) is
  handled before any command
* connection loss invokes the controller-loss hook
* no buffered command executes after detected session loss

### 28.3 Framing

* fragmented command line is reassembled
* multiple lines in one TCP read are handled independently
* partial line at disconnect is discarded
* 1024-byte accepted boundary is tested (limit includes the newline)
* oversized line is discarded through newline
* valid line after oversized input is still processed
* invalid UTF-8 executes no handler
* malformed JSON executes no handler

### 28.4 Schema and registration

* schema is generated from registered commands
* duplicate command name fails registration
* unknown command returns `UNKNOWN_COMMAND`
* all commands expose explicit `blocked_by_estop`
* schema fits within the 8192-byte limit
* schema includes board ID, protocol version `"1"`, and firmware version
* schema `source` equals the configured board ID

### 28.5 Command dispatch

* valid command invokes exactly one handler
* missing argument returns `MISSING_FIELD`
* wrong type returns `INVALID_TYPE`
* extra argument is rejected
* board-specific range error returns `INVALID_ARGUMENT`
* response echoes command `seq`
* response echoes `controller_ts` untouched
* `board_proc_us`, when provided, appears inside `result`
* a handler-returned result field named `board_proc_us` is overwritten by the
  library measurement
* wrong-target command invokes no handler, gets no response, and increments
  `invalid_targets`
* board never sends `timeout` status
* board never sends a controller-owned error code
* long-running action starts and returns without waiting for completion

### 28.6 Outbound transport

* complete write handles partial QNEthernet writes
* every JSON line is newline terminated and compact-serialized
* every completed logical message is flushed
* concurrent telemetry and response production cannot interleave bytes
* a peer that stops reading: the write helper gives up at the transmit
  deadline instead of stalling the firmware loop
* critical message transmit-deadline expiry closes the session
* ten consecutive telemetry deadline failures close the session
* telemetry backpressure coalesces to the latest snapshot

### 28.7 Telemetry

* telemetry is emitted at a nominal 50 ms period
* telemetry is not emitted before schema
* telemetry starts within 250 ms of schema send
* telemetry continues while e-stop is active
* telemetry resumes within the 250 ms liveness window after the e-stop hook
  runs
* telemetry getter is non-blocking
* missed periods do not cause a stale burst
* telemetry sequence is monotonic within one boot
* oversized telemetry is dropped and counted

### 28.8 E-stop

* e-stop bypasses command registration
* e-stop hook is invoked
* repeated e-stop is safe
* `estop_ack` occurs only after safe state is confirmed
* hook failure emits **no** `estop_ack` (never a non-"safe" variant)
* hook failure resumes protocol servicing: telemetry continues, no `estop_ack`
* hook duration is measured; an over-budget run increments
  `estop_hook_over_budget` and fails conformance
* an over-budget hook that did apply safe state still produces a truthful
  `estop_ack`
* hardware-originated e-stop event does not delay the hardware safety action
* no board-side latched command gate exists after `estop`
* wrong-target `estop` does not invoke the safe-state hook and increments
  `invalid_targets`

### 28.9 Heartbeat

* heartbeat `seq` is echoed unchanged
* ack carries `source = board_id`, `target = "controller"`
* heartbeat invokes no application handler
* ack is produced within the normal service loop (well under 1 s)
* malformed heartbeat is rejected safely
* wrong-target heartbeat produces no acknowledgement and increments
  `invalid_targets`

### 28.10 Resource behavior

* no unbounded queue growth
* no network calls occur from an ISR
* repeated commands do not cause steady-state heap growth
* repeated reconnects do not leak client/session resources
* logging remains bounded

---

## 29. Acceptance criteria

The Teensy 4.1 command server is conformant when:

1. it builds portably from committed source
2. it exposes only registered commands
3. its transmitted schema and dispatch table cannot drift
4. it sends schema first, within the registration window, on every connection
5. it correctly frames and bounds newline-delimited JSON
6. it serializes all outbound writes and bounds every transmission with a
   finite deadline
7. it handles partial reads and writes
8. it provides non-blocking command dispatch
9. it pushes telemetry without unbounded backlog and never starves the
   liveness signal mid-session
10. it applies and acknowledges software e-stop honestly
11. it enters local safe state on detected controller loss
12. it supports heartbeat acknowledgement
13. it performs no protocol networking from interrupts
14. it passes all required conformance tests
15. it does not conflict with the frozen V1 networking contract

---

## 30. Explicitly out of scope

The following are out of scope for V1:

* Redis on the Teensy
* GUI communication from the Teensy
* dynamic board discovery
* multiple simultaneous controllers
* authentication
* TLS
* UDP command or telemetry transport
* binary protocol encoding
* runtime command registration
* arbitrary nested argument schemas
* board-to-board routing
* board-level `get_schema`
* board-side latched software e-stop gating
* software claims of hard safety

---

## 31. Resolved judgment calls (ratify with the contract)

These were open in the first draft and are now pinned in the body. They are
listed here so ratification can veto them individually.

1. **Half-open controller detection** — accepted as a documented V1
   limitation (section 21), with two pinned mitigations: a replacement
   controller connection supersedes a stale session (9.1), and the transmit
   deadline plus the consecutive-telemetry-failure rule (13.6) tears down a
   wedged session in bounded time. No board-side inbound-activity timeout in
   V1; the hardwired interlock remains the guarantee.
2. **Reporting e-stop hook failure** — withhold `estop_ack` only;
   `estop_apply_failed` stays a board-local counter (19.2, 25). No new event
   name enters the shared vocabulary in V1.
3. **Board-side `ESTOP_ACTIVE`** — retained, condition-based only: the
   board's own live hardware safety condition, never a latched software gate
   (6.5, 17).
4. **Second-connection policy** — close-old: a replacement connection
   supersedes the active session (9.1). Close-new was rejected because the
   board's only stale-session recovery path without heartbeat is lwIP
   write-path teardown, which can take tens of seconds after a controller
   host restart, during which the restarted controller is locked out.
5. **E-stop hook execution model** — bounded synchronous hook with a 100 ms
   budget (19.1), not an asynchronous `ESTOP_APPLYING` phase. The async model
   adds a partial-safe intermediate state to the firmware lifecycle and a new
   window for a dishonest ack; V1 hooks are GPIO-scale actions that fit the
   budget. The ack is gated on the hook's reported truth, not its timing.

Numeric defaults pinned here (100 ms e-stop and controller-loss hook budgets,
100 ms transmit deadline, 10-frame telemetry failure threshold) SHALL be
confirmed against real Teensy/QNEthernet behavior during conformance testing
before this contract is frozen. If hardware testing shows a value is
unrealistic, adjust only the numeric value and its associated tests, not the
architecture.
