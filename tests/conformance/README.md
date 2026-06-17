# tests/conformance — Python client run against a real board

A host-side Python client that speaks the controller's wire protocol at a
real Teensy running the `command_server_conformance` sketch, and asserts the
contract §28 behaviors that host tests cannot prove (real sockets, real
timing, real QNEthernet).

**Requires physical hardware. Never run, simulated, or claimed in hosted CI.**

Layout:

```text
runner.py              discovers and executes cases against a board address
protocol_client.py     newline-JSON client matching the controller's shapes
fixtures/*.ndjson      recorded parser fixtures used by --self-test only
```

Entry point:

```bash
./tools/run_conformance.sh <board-ip:port>
./tools/run_conformance.sh --self-test
```

The live run maps every §28 row to a `PASS`, `FAIL`, or explicit `SKIP
(hardware only)` and emits a §29 acceptance summary. The `--self-test` mode
validates only the Python parser, encoder, fixture handling, and summary logic;
it does not connect to a board and must not be recorded as hardware
conformance.

Live results feed `tests/hardware/results/` and validate the four provisional
timing defaults (contract §31) during the hardware phase.
