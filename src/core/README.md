# src/core — platform-free implementation

No QNEthernet/Arduino networking includes (invariant-checked). Time and I/O
enter only through injected interfaces so everything here is host-testable.

Planned files (created by their backlog phases, not pre-created):

```text
Limits.h               all compile-time capacities and timing defaults (Phase 1)
Protocol.h/.cpp        message vocabulary, shapes, validation (Phase 1/3)
Counters.h             bounded contract §25 counters (Phase 1)
SessionState.h/.cpp    lifecycle state machine §8, supersession §9.1 (Phase 4)
CommandRegistry.h/.cpp fixed-capacity registry, duplicate rejection (Phase 2)
SchemaBuilder.h/.cpp   schema generated from registry metadata §5.3 (Phase 2)
LineFramer.h/.cpp      bounded incremental framing §10 (Phase 3)
MessageParser.h/.cpp   fixed-capacity JSON parse + validation (Phase 3)
OutboundScheduler.h/.cpp  serialized priority write path §13 (Phase 5)
TelemetryScheduler.h/.cpp 50 ms coalescing scheduler §18 (Phase 7)
CommandServerCore.h/.cpp  dispatch + wiring (Phase 6/8)
```
