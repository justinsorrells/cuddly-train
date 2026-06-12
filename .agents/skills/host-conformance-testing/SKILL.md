---
name: host-conformance-testing
description: >
  Read before writing anything under tests/. Deterministic host tests with
  fake clock/transport (no wall-clock sleeps), partial-read/write simulation,
  supersession and peer-stops-reading scenarios, and the hardware-test
  honesty rules.
---

# Host and Conformance Testing

## Layout

* `tests/host/unit/` — one component, fakes only.
* `tests/host/integration/` — components wired together, still platform-free.
* `tests/host/fakes/` — `FakeTransport`, `FakeClock`, `FakeHooks`,
  `FakeTelemetryProvider`. Fakes are shared; don't fork per-test variants.
* `tests/conformance/` — Python client run against a real board over TCP.
* `tests/hardware/` — manual procedures + recorded results.

Host tests compile with the plain system C++ toolchain via
`./tools/run_host_tests.sh` — no Arduino headers, no QNEthernet (the
invariant checker enforces this).

## Determinism rules

* **No wall-clock sleeps, ever.** All time comes from `FakeClock`; advance it
  explicitly. The 50 ms telemetry tick, 100 ms budgets/deadlines, 250 ms
  liveness window, and 10-frame teardown are all driven by clock steps.
* `FakeTransport` scripts exact byte sequences: deliver a message split at
  arbitrary offsets, deliver several lines in one read, accept only N bytes
  per write (partial writes), stop accepting bytes entirely
  (peer-stops-reading), report closure mid-line.
* Assert on counters as well as behavior — every contract counter (§25) is a
  test surface.

## Scenarios the suite must keep covering (contract §28)

* framing: boundary sizes, splits, oversized-then-valid, closure mid-line
* dispatch: wrong target (no handler, no response, counter), unknown command,
  missing/extra/wrong-type args, seq + controller_ts echo, `board_proc_us`
  inside result and overwritten if handler-provided
* sessions: schema-first within 2 s, supersession ordering, no command from a
  superseded session, over-budget loss hook still promotes the new session
* transmit: deadline at a non-reading peer, critical-failure closes session,
  10 consecutive telemetry failures close session, no interleaved bytes
* e-stop: hook invoked, repeated estop safe, ack only on confirmed safe,
  failure ⇒ no ack but servicing resumes, telemetry within liveness window,
  estop-as-first-message-after-schema
* heartbeat: exact echo shape, no application handler, wrong-target ignored

## Hardware honesty

`tests/conformance/` and `tests/hardware/` require a physical Teensy. Never
run, simulate, or claim them from a host environment. Hardware results are
recorded as files in `tests/hardware/results/` with date and firmware
version; the four provisional timing numbers (contract §31) are validated
there and nowhere else.
