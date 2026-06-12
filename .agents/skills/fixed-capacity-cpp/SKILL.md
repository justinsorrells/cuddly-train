---
name: fixed-capacity-cpp
description: >
  Read before writing C++ that touches buffers, JSON documents, strings, or
  queues. Fixed-capacity storage rules, integer widths, overflow handling, and
  the no-per-message-allocation requirement (contract §11).
---

# Fixed-Capacity C++

The steady-state protocol path performs **no unbounded dynamic allocation per
message** (contract §11). The Teensy has no MMU and runs for days; heap
fragmentation is a real failure mode.

## Storage rules

* Every protocol buffer has a compile-time capacity defined in
  `src/core/Limits.h` (single source for all sizes): inbound line accumulator
  (1024 + terminator slack), parsed command document, response/schema/telemetry
  serialization buffers (sized against 8192), critical-message state.
* JSON parsing uses a fixed-capacity ArduinoJson document
  (`StaticJsonDocument<N>` or the v7 fixed-allocator equivalent).
  `DynamicJsonDocument` is forbidden — the invariant checker errors on it.
* No `std::string`, `String`, `std::vector`, or `std::map` growth in the
  steady-state path. Fixed arrays + lengths, or capacity-checked wrappers.
* The command registry is a fixed-capacity table sized at registration time;
  registration happens before the server starts and is immutable after (§5.1).

## Integer rules

* `seq` values are uint64 — use `uint64_t`, never `int`/`long`.
* Contract `int` arguments are signed 32-bit (`int32_t`); validate range when
  extracting from JSON numbers (§6.2).
* `float` arguments must be finite — reject NaN/Inf as `INVALID_ARGUMENT`.
* Durations: `micros()`/`millis()` deltas computed with unsigned subtraction
  so wraparound stays correct (`uint32_t now - uint32_t then`).

## Overflow behavior

* Buffer-capacity checks happen **before** writes, never after.
* Inbound overflow ⇒ discard-through-newline (see `ndjson-framing`), count.
* Outbound overflow ⇒ never send a truncated line: compact `INTERNAL_ERROR`
  for responses, drop+count for telemetry (§11.2).
* Counters are bounded unsigned types; saturating or wrapping is fine, silent
  UB is not.

## Style

* No C++ exceptions across the library boundary (§7.4); error returns are
  status enums / result structs.
* `constexpr` capacities, not `#define`.
* Avoid hidden allocation: no `snprintf` into temporaries that grow, no
  lambdas that capture by heap, no `std::function` in the hot path — function
  pointers or non-owning callable refs.
