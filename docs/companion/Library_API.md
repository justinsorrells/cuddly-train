# Library API Proposal

Status: **PENDING OPERATOR REVIEW**. This companion document gates Phase 2:
no `src/api/` header may be written until the operator has reviewed this
proposal.

Authority: `docs/contracts/V1_Networking_Decisions.md` wins first, then
`docs/contracts/Teensy_Command_Server_Contract.md`, then
`docs/contracts/Board_Developer_Guide.md`. This document proposes exact public
C++ signatures for the board-application API; it does not change the contract.

## Public Surface

Phase 2 should expose exactly one sketch-facing facade plus platform-free API
types:

```text
src/TeensyCommandServer.h
src/api/BoardIdentity.h
src/api/CommandTypes.h
src/api/CommandResult.h
src/api/TelemetryProvider.h
src/api/SafetyHooks.h
src/api/ServerStatus.h
```

All names below live in:

```cpp
namespace teensy_command_server::api
```

The public API must not expose QNEthernet, Arduino networking types, Redis,
GUI communication, or cross-board routing. Board applications include only
`TeensyCommandServer.h`; the facade re-exports the API types needed by
sketches.

## Core Value Types

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace teensy_command_server::api {

enum class ValueType : std::uint8_t {
    Int,
    Float,
    Bool,
    String,
};

enum class ErrorCode : std::uint8_t {
    MissingField,
    InvalidType,
    UnknownCommand,
    InvalidArgument,
    InternalError,
    EstopActive,
};

enum class StatusCode : std::uint8_t {
    Ok,
    DuplicateRegistration,
    RegistrationSealed,
    CapacityExceeded,
    InvalidName,
    InvalidArgumentSchema,
    InvalidConfiguration,
    NetworkStartFailed,
    NoTelemetryProvider,
    NoEstopHook,
    NoControllerLossHook,
};

struct Status {
    StatusCode code;
    const char* message;

    bool ok() const;
};

struct BoardIdentity {
    const char* board_id;
    const char* protocol_version;
    const char* firmware_version;
};

struct NetworkConfig {
    enum class Mode : std::uint8_t {
        Dhcp,
        StaticIpv4,
    };

    Mode mode;
    std::uint16_t listen_port;
    std::uint8_t ip[4];
    std::uint8_t gateway[4];
    std::uint8_t subnet[4];
};

struct ServerConfig {
    BoardIdentity identity;
    NetworkConfig network;
};

}  // namespace teensy_command_server::api
```

`ErrorCode` is intentionally limited to board-emittable contract §17 codes.
Controller-owned outcomes such as `BOARD_BUSY`, `COMMAND_TIMEOUT`, and
`PROTOCOL_VERSION_MISMATCH` are not board API values.

`Status` reports registration, configuration, and startup outcomes. It is not
a wire status. Board response statuses remain exactly `ok` and `error`, owned
by the library serializer.

`NetworkConfig` contains plain bytes and scalars so the public API stays free
of Arduino networking types. For DHCP, the IPv4 arrays are ignored.

## Command Metadata

```cpp
namespace teensy_command_server::api {

struct ArgumentSpec {
    const char* name;
    ValueType type;
};

struct CommandSpec {
    const char* name;
    const ArgumentSpec* args;
    std::size_t arg_count;
    bool blocked_by_estop;
};

struct FieldSpec {
    const char* name;
    ValueType type;
};

}  // namespace teensy_command_server::api
```

`CommandSpec::blocked_by_estop` is explicit and required by the API. Phase 2
should reject a command registration that does not supply a complete
`CommandSpec`; the controller still treats an absent field as blocked on the
wire, but this library API should not rely on absence.

The registry is fixed capacity. The backing capacities come from
`src/support/Limits.h`: `kMaxRegisteredCommands` and `kMaxArgsPerCommand`.
Registration fails with `StatusCode::CapacityExceeded` rather than reallocating
or truncating.

## Command Arguments

```cpp
namespace teensy_command_server::api {

class CommandArgs {
public:
    bool has(const char* name) const;

    bool getInt(const char* name, std::int32_t& out) const;
    bool getFloat(const char* name, float& out) const;
    bool getBool(const char* name, bool& out) const;
    bool getString(const char* name, const char*& out, std::size_t& length) const;
};

struct CommandContext {
    std::uint64_t seq;
    const CommandArgs& args;
};

}  // namespace teensy_command_server::api
```

The library validates declared arguments before handler invocation:
missing argument -> `MISSING_FIELD`, wrong type -> `INVALID_TYPE`, extra
argument -> `INVALID_ARGUMENT`. Handlers use `CommandArgs` for board-specific
domain validation and should return `INVALID_ARGUMENT` for values outside the
safe operating range.

`CommandContext::seq` is the controller-owned `board_seq`; it is exposed only
for diagnostics. Handlers must not replace or reinterpret it. `controller_ts`
is intentionally not exposed to handlers because the library must echo it
untouched.

## Object Writers

```cpp
namespace teensy_command_server::api {

class ObjectWriter {
public:
    bool addInt(const char* name, std::int32_t value);
    bool addFloat(const char* name, float value);
    bool addBool(const char* name, bool value);
    bool addString(const char* name, const char* value);
    bool addString(const char* name, const char* value, std::size_t length);
};

}  // namespace teensy_command_server::api
```

`ObjectWriter` is a bounded JSON-object builder supplied by the library. It is
used only inside callbacks. A `false` return means the value could not fit in
the fixed-capacity outbound buffer; handlers should then return
`CommandResult::internalError(...)` or a more specific board-side error.

There is exactly one bounded JSON writer implementation in the library:
`src/support/BoundedJsonWriter.h`, in the neutral dependency layer so both
the public API and the core engine can use it without an api → core
dependency. `api::ObjectWriter` wraps or delegates to it,
and schema generation, command responses, telemetry frames, events, and
heartbeat acknowledgements use the same primitive. Escaping, compact
formatting, overflow behavior, and byte accounting are implemented once;
no phase introduces a second serializer.

`ObjectWriter` does not get the full 8192-byte transmit budget. Its capacity
is the response budget minus reserved envelope overhead: the response keys,
`status`, `result`/`error` structure, the library-owned `board_proc_us`,
escaping headroom, and the terminating newline. The writer either writes
directly into a response builder that has already reserved that envelope, or
exposes a fixed maximum application-result capacity derived from it. Either
way, a handler can never successfully produce a result that the library
cannot wrap into a valid response line (§10.3, §11.2).

Handlers must not add `board_proc_us`. The library owns that field and
overwrites any attempted handler-provided value before serialization.

## Command Results and Handler Signature

```cpp
namespace teensy_command_server::api {

class CommandResult {
public:
    static CommandResult ok();
    static CommandResult error(ErrorCode code, const char* message);

    static CommandResult missingField(const char* message);
    static CommandResult invalidType(const char* message);
    static CommandResult invalidArgument(const char* message);
    static CommandResult internalError(const char* message);
    static CommandResult estopActive(const char* message);

    bool isOk() const;
    ErrorCode errorCode() const;
    const char* message() const;
};

using CommandHandler = CommandResult (*)(
    const CommandContext& command,
    ObjectWriter& result,
    void* context);

}  // namespace teensy_command_server::api
```

A successful handler writes zero or more fields to `result` and returns
`CommandResult::ok()`. The library serializes those fields as the response
`result` object and inserts `board_proc_us` inside the same object.

A failed handler returns `CommandResult::error(...)` or one of the named
helpers. There are intentionally five named helpers for six §17 codes:
`UNKNOWN_COMMAND` is emitted only by the library dispatcher when no
registered command matches — a command that reaches a handler is by
definition known, so no handler helper exists for it (a handler could still
construct it through the generic `error(...)` factory, but should not).
The library serializes `status:"error"`, `result:null`, and the
contract §17 error object. Command handlers must not throw exceptions across
this boundary; Phase 2 implementation should compile without relying on
exceptions.

`CommandHandler` receives a caller-owned `context` pointer provided at
registration. This avoids `std::function` allocation in the steady-state
command path while still allowing object-oriented board code to dispatch
through static thunks.

## Telemetry Provider

```cpp
namespace teensy_command_server::api {

using TelemetryProvider = bool (*)(
    ObjectWriter& telemetry,
    void* context);

}  // namespace teensy_command_server::api
```

The telemetry provider writes the latest snapshot to the supplied bounded
object writer and returns `true` when the snapshot is valid. It must be fast,
non-blocking, and must not perform network operations or wait for sensors.
Slow acquisition belongs elsewhere in firmware; this callback only copies the
latest completed values.

The library pushes telemetry on its own 50 ms schedule while a session is
active, coalescing delayed periods to the latest snapshot. The provider is not
a command and is never solicited by a controller request.

## Safety Hooks

```cpp
namespace teensy_command_server::api {

using SafetyHook = bool (*)(void* context);

}  // namespace teensy_command_server::api
```

The same signature is used for:

```text
on_estop_received()
on_controller_lost()
```

The hook returns `true` only when the board-local safe state has actually been
applied. E-stop acknowledgement is emitted only after the e-stop hook returns
`true`; on `false`, no `estop_ack` is sent. The `estop_ack` payload remains
exactly `details.state == "safe"`.

Both hooks must be idempotent and must complete within the provisional 100 ms
contract budget. The library measures duration and increments the corresponding
over-budget counter, but a slow successful e-stop hook still produces a
truthful ack.

## Facade Class

`TeensyCommandServer.h` should expose this sketch-facing class:

```cpp
#pragma once

#include "api/BoardIdentity.h"
#include "api/CommandResult.h"
#include "api/CommandTypes.h"
#include "api/SafetyHooks.h"
#include "api/ServerStatus.h"
#include "api/TelemetryProvider.h"

namespace teensy_command_server {

class TeensyCommandServer {
public:
    TeensyCommandServer();

    api::Status setIdentity(const api::BoardIdentity& identity);
    api::Status setNetworkConfig(const api::NetworkConfig& config);

    api::Status registerCommand(
        const api::CommandSpec& spec,
        api::CommandHandler handler,
        void* context);

    api::Status registerTelemetrySchema(
        const api::FieldSpec* fields,
        std::size_t field_count);

    api::Status registerStateSchema(
        const api::FieldSpec* fields,
        std::size_t field_count);

    api::Status setTelemetryProvider(
        api::TelemetryProvider provider,
        void* context);

    api::Status setEstopHook(
        api::SafetyHook hook,
        void* context);

    api::Status setControllerLossHook(
        api::SafetyHook hook,
        void* context);

    api::Status start();
    void service();

    bool isStarted() const;
};

}  // namespace teensy_command_server
```

`setNetworkConfig(...)` is included because contract §3.4 requires explicit
network configuration and contract §5 includes starting the command server.
It is not a board-application protocol operation and does not expose networking
objects.

`start()` seals registration. After `start()` succeeds, all registration and
configuration methods return `StatusCode::RegistrationSealed`. Dynamic command
registration during an active session is out of scope for V1.

`service()` must be called on every normal firmware loop iteration. It advances
network receive, command dispatch, telemetry scheduling, heartbeat replies,
session close handling, and deadline-bounded outbound writes. It must be
non-blocking beyond the finite write/hook budgets required by contract.

## Registration Behavior

Registration completes before startup:

```text
construct server
set identity
set network config
register command(s)
register telemetry schema
register state schema
set telemetry provider
set e-stop hook
set controller-loss hook
start
loop: service
```

Required behavior:

* `setIdentity(...)` fails if `board_id`, `protocol_version`, or
  `firmware_version` is null or empty.
* `setNetworkConfig(...)` fails if `listen_port` is zero or static IPv4 fields
  are invalid.
* `registerCommand(...)` fails on null name, null handler, duplicate command
  name, unsupported argument metadata, `arg_count > kMaxArgsPerCommand`, or
  full command table.
* `registerTelemetrySchema(...)` and `registerStateSchema(...)` fail on null
  field names or unsupported field metadata.
* `setTelemetryProvider(...)`, `setEstopHook(...)`, and
  `setControllerLossHook(...)` fail on null callback.
* `start()` fails if identity, network config, telemetry provider, e-stop hook,
  or controller-loss hook is missing.
* `start()` owns the validate-then-seal sequence through three explicit
  operations — the registry never depends on the schema builder:

  ```cpp
  Status CommandRegistry::validateMetadataForSeal() const;

  Status SchemaBuilder::validateMaximumSchemaSize(
      const CommandRegistry& registry,
      const BoardIdentity& identity);

  void CommandRegistry::commitSeal();
  ```

  1. The registry accepts and validates registration metadata while MUTABLE.
  2. `start()` calls `registry.validateMetadataForSeal()` (const,
     non-mutating completeness check).
  3. `start()` calls `SchemaBuilder::validateMaximumSchemaSize(...)`
     (const, non-mutating; the builder reads the registry, never the
     reverse).
  4. If either validation fails (e.g. the schema cannot fit the 8192-byte
     line), `start()` returns failure and the registry remains MUTABLE.
  5. If both succeed, `start()` calls `registry.commitSeal()` — the atomic
     transition to SEALED; no registration may occur afterward, regardless
     of later session reconnects (`RegistrationSealed`). Only the
     startup/facade path calls `commitSeal()`.
  6. Runtime schema serialization still checks the final encoded length,
     including the terminating newline.

  An oversized schema never leaves the registry sealed, and neither
  implementation agent redesigns this sequence independently.

The schema transmitted after each accepted controller connection is generated
from the same registered `CommandSpec` and field schemas used for dispatch.
There is no separate manually maintained schema table.

## Ownership and Lifetimes

Every pointer crossing the API boundary has a pinned owner and lifetime.

Registration metadata:

* The registry copies board IDs, firmware versions, command names, argument
  names, field names, and every other registration string into library-owned,
  fixed-capacity storage at registration time.
* Registration must not retain pointers to caller-owned temporary or
  stack-local data. After a registration call returns, the caller's buffers
  may be reused or destroyed without affecting the registry.

CommandArgs:

* Any string view or pointer returned from `CommandArgs` is valid only during
  the current handler invocation.
* Handlers must not retain references into the parsed document.

CommandResult and ObjectWriter:

* Result data written through `ObjectWriter` and error messages carried by
  `CommandResult` are copied into bounded library-owned storage before the
  handler returns, unless the API explicitly documents a static-lifetime
  string requirement for a specific parameter.
* No handler-returned pointer is dereferenced after the handler returns.

Callback context:

* The board application owns every `void* context` passed at registration and
  must keep it alive from registration until server shutdown. The library
  never frees, copies, or interprets it.

Dependency direction (enforced by `tools/check_invariants.py`):

```text
src/support   may depend only on the C++ standard library
src/api       may depend on src/support
src/core      may depend on src/api and src/support
src/platform  may depend on src/api, src/core, and src/support
```

* `src/support` is the neutral dependency layer: `Limits.h` (all
  compile-time capacities) and `BoundedJsonWriter.h` live there.
* `CommandRegistry` lives in `src/core/CommandRegistry.h`.
* `src/api` must not depend on `CommandRegistry`, session state, transport,
  QNEthernet, or any other implementation object. The public headers are
  data, callback typedefs, and the facade declaration only.

## One-To-One Contract Mapping

| Public operation | Contract §5 operation |
|---|---|
| `setIdentity(const api::BoardIdentity&)` | set board identity |
| `registerCommand(const api::CommandSpec&, api::CommandHandler, void*)` | register command |
| `registerTelemetrySchema(const api::FieldSpec*, std::size_t)` | register telemetry schema |
| `registerStateSchema(const api::FieldSpec*, std::size_t)` | register state schema |
| `setTelemetryProvider(api::TelemetryProvider, void*)` | set telemetry provider |
| `setEstopHook(api::SafetyHook, void*)` | set e-stop hook |
| `setControllerLossHook(api::SafetyHook, void*)` | set controller-loss hook |
| `start()` | start command server |
| `service()` | service command server |

`setNetworkConfig(...)` supports contract §3.4 and startup, but is not counted
as a separate §5 operation.

## Example Sketch Usage

```cpp
#include <TeensyCommandServer.h>

using teensy_command_server::TeensyCommandServer;
namespace api = teensy_command_server::api;

TeensyCommandServer server;

api::CommandResult setSpeed(
    const api::CommandContext& command,
    api::ObjectWriter& result,
    void* context) {
    std::int32_t rpm = 0;
    if (!command.args.getInt("rpm", rpm)) {
        return api::CommandResult::invalidArgument("rpm is required");
    }

    // Board-specific range validation and non-blocking action start happen here.
    result.addBool("accepted", true);
    result.addInt("requested_rpm", rpm);
    return api::CommandResult::ok();
}

bool writeTelemetry(api::ObjectWriter& telemetry, void* context) {
    telemetry.addInt("rpm", 1180);
    telemetry.addFloat("temperature_c", 41.2f);
    telemetry.addFloat("voltage", 24.1f);
    return true;
}

bool applySafeState(void* context) {
    // Cut or disable local outputs, then return true only when applied.
    return true;
}

void setup() {
    server.setIdentity({"motor_controller", "1", "0.1.0"});
    server.setNetworkConfig({
        api::NetworkConfig::Mode::Dhcp,
        5000,
        {0, 0, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0},
    });

    static const api::ArgumentSpec set_speed_args[] = {
        {"rpm", api::ValueType::Int},
    };
    server.registerCommand(
        {"set_speed", set_speed_args, 1, true},
        setSpeed,
        nullptr);

    static const api::FieldSpec telemetry_fields[] = {
        {"rpm", api::ValueType::Int},
        {"temperature_c", api::ValueType::Float},
        {"voltage", api::ValueType::Float},
    };
    server.registerTelemetrySchema(telemetry_fields, 3);

    server.setTelemetryProvider(writeTelemetry, nullptr);
    server.setEstopHook(applySafeState, nullptr);
    server.setControllerLossHook(applySafeState, nullptr);
    server.start();
}

void loop() {
    server.service();
}
```

The example omits status checking for brevity. Real board sketches should check
each `api::Status` and remain in board-local safe state if configuration or
startup fails.

## Non-Goals

* No `src/api/` implementation in this phase.
* No dynamic command registration after `start()`.
* No C++ exceptions across the library boundary.
* No Arduino `String`, `IPAddress`, `EthernetClient`, `EthernetServer`, or
  QNEthernet types in the public API.
* No board-application TCP, JSON framing, serializer, Redis, GUI, or
  board-to-board routing hooks.
* No board-level `get_schema` command.
* No board-side latched software e-stop gate.
* No callback-owned `board_proc_us`, `controller_ts`, or wire response
  serialization.
