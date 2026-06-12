# tests/conformance — Python client run against a real board

A host-side Python client that speaks the controller's wire protocol at a
real Teensy running the `command_server_conformance` sketch, and asserts the
contract §28 behaviors that host tests cannot prove (real sockets, real
timing, real QNEthernet).

**Requires physical hardware. Never run, simulated, or claimed in hosted CI.**

Planned layout (backlog Phase 10):

```text
runner.py            discovers and executes cases against a board address
protocol_client.py   newline-JSON client matching the controller's shapes
cases/               schema_first, framing, command_dispatch,
                     telemetry_liveness, estop, heartbeat,
                     session_supersession, transmit_deadline
```

Entry point: `./tools/run_conformance.sh <board-ip:port>`. Results feed
`tests/hardware/results/` and validate the four provisional timing defaults
(contract §31).
