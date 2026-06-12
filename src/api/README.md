# src/api — public board-application interfaces

Platform-free. No QNEthernet/Arduino networking includes (invariant-checked).
This plus `src/TeensyCommandServer.h` is the entire public surface.

Planned files (created by backlog Phase 1, not pre-created):

```text
BoardRegistry.h       identity + command/telemetry/state registration
CommandTypes.h        argument types (int32/float/bool/string), command metadata
CommandResult.h       handler ok/error result types (§7.3, §17)
TelemetryProvider.h   fast non-blocking snapshot provider interface (§18.2)
SafetyHooks.h         e-stop + controller-loss hook interfaces (§19–§21)
```

API design rules: exact public operations map one-to-one to contract §5;
do not finalize C++ signatures before the Library API task
(`docs/companion/Library_API.md`) is reviewed.
