# tests/host — platform-free C++ tests

Compiled and run by `./tools/run_host_tests.sh` with the system C++ toolchain
(`c++ -std=c++17`). No Arduino headers, no QNEthernet, no wall-clock sleeps —
read the `host-conformance-testing` skill first.

* `unit/` — one component with fakes (`test_<component>.cpp`)
* `integration/` — wired flows, still platform-free (`test_<flow>.cpp`)
* `fakes/` — shared `FakeTransport`, `FakeClock`, `FakeHooks`,
  `FakeTelemetryProvider` (created in backlog Phase 1)

Each test file is self-contained with its own `main`; failures via nonzero
exit. `unit/test_smoke.cpp` proves the harness and is replaced by real tests
from Phase 2 onward.
