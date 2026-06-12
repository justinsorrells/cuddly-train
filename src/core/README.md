# src/core — platform-free implementation

No QNEthernet/Arduino networking includes (invariant-checked). Time and I/O
enter only through injected interfaces so everything here is host-testable.

Planned files (created by their backlog tasks — see `backlog.md` — not
pre-created):

```text
Protocol.h             closed wire vocabulary (parsing/validation come later)
Counters.h             bounded contract §25 counters
Clock.h, Transport.h   injected time and I/O seams (§24)
SessionState.h         lifecycle states + transition predicate §8
                       (state-machine driver is a later backlog task)
CommandRegistry.h/.cpp fixed-capacity registry, duplicate rejection §5.2
SchemaBuilder.h/.cpp   schema generated from registry metadata §5.3
LineFramer.h/.cpp      bounded incremental framing §10
MessageParser.h/.cpp   fixed-capacity JSON parse + validation
OutboundScheduler.h/.cpp  serialized priority write path §13
TelemetryScheduler.h/.cpp 50 ms coalescing scheduler §18
CommandServerCore.h/.cpp  dispatch + wiring §16, §19–§23
```
