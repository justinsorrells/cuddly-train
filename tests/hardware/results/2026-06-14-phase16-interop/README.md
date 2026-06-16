# Phase 16 — actual-controller vertical-slice interop (operator-run)

**Date:** 2026-06-14
**Operator-run on real hardware** (real controller repo + real Teensy 4.1),
agent-assisted. This is the Phase-16 OPERATOR GATE record. Numbers below were
observed live on the wire, not simulated.

## Configuration

| Item | Value |
|---|---|
| Firmware repo (cuddly-train) commit | `3b6d9f9` + working-tree fix (see "Firmware fix") |
| Controller repo (special-lamp) commit | `cf347cc` |
| Sketch flashed | `sketches/command_server_conformance` (board id `command_server_conformance`, fw `0.1.0`, protocol `1`) |
| Board IP / port | `192.168.10.3:5051`, **static IPv4** (sketch `network_config` set to StaticIpv4 for this bench; committed default is DHCP) |
| Host / controller IP | `192.168.10.2` (iface `en6`, direct link) |
| Controller config | `controller_config.toml` (this dir); liveness enabled, 0.25 s; observability off |
| arduino-cli | `1.5.0` (guide pins 1.3.1) |
| Teensy core | `teensy:avr 1.60.0` (guide pins 1.59.0) |
| QNEthernet | `0.35.0` (matches pin) |
| ArduinoJson | vendored `third_party/ArduinoJson` (6.21.5) |

> Toolchain versions deviate from the Build_and_Flash guide pins (cli 1.5.0 vs
> 1.3.1, core 1.60.0 vs 1.59.0). Acceptable for a Phase-16 functional interop
> demo; the Phase-18 freeze must use the pinned toolchain.

## Firmware fix required to pass

The vertical slice surfaced **two facade-wiring bugs** in
`src/TeensyCommandServer.h` (the inner components were correct; only the public
`TeensyCommandServer` facade mis-wired them, so host unit tests of the inner
classes passed while real interop failed):

1. **Telemetry was never pushed.** `facadeRoutes()` never set the
   `telemetry_due`/`telemetry_inactive` routes and there was no
   `TelemetryScheduler` member, so `setTelemetryProvider()` stored a provider
   that nothing ever called. The board sent its schema and then went silent;
   the controller's telemetry-driven liveness then dropped the board
   (`BOARD_UNAVAILABLE`). Fix: added a placement-new `TelemetryScheduler`
   (mirroring `session_driver_`), constructed in `start()` from the registry
   provider, and wired `routeTelemetryDue`/`routeTelemetryInactive`.

2. **Only the `get_counters` diagnostic was dispatched.** `routeCommand`
   returned `true` (= teardown) for every non-diagnostic command, so any real
   command tore the session down before reaching its handler. Fix: always run
   `CommandDispatcher` (it already resolves every registered command via the
   registry, including `get_counters`), and removed the now-dead
   `counters_diagnostic_enabled_` gate.

Also updated `tests/host/unit/test_service_loop.cpp`: the existing facade test
asserted the buggy behavior (a registered command produced `command_calls == 0`
and a teardown). Rewrote it to assert correct dispatch + response, and added an
unknown-command regression. Host suite: **24/24 pass**.

## What was proven (see `proxy_transcript.log` timestamps)

A transparent logging proxy sat between controller and board (controller still
the sole command authority); a Unix-socket client drove the commands.

| # | Bullet | Evidence |
|---|---|---|
| 1 | Schema-first registration | `[1.03s] B->C schema` — first frame on connect; controller reports `REGISTERED` |
| 2 | Client command → controller → board → correlated response | `[3.03s] C->B test_echo value=42` → `B->C response seq=1 value=42`; telemetry `echo_value` flips to 42 (real execution) |
| 3 | Telemetry to controller | continuous `B->C telemetry` at 50 ms (986 frames over the ~50 s session); `get_counters` shows `telemetry_sent` climbing, `telemetry_dropped: 0` |
| 4 | Disconnect / reconnect + schema re-registration | `[6.55s] session closed` → `[6.88s] reconnected` + fresh schema; controller returns board to `REGISTERED` |
| 5 | Software e-stop + `estop_ack` | `[5.03s]` `estop_triggered` → controller `estop` → `estop_ack {state:"safe"}`; client `estop_reset` returns `estop_active: false` |

### Dispatcher audit (direct-to-board)
- `get_counters` → `ok` (diagnostics still dispatch after the fix)
- unknown command → `UNKNOWN_COMMAND` error, **no teardown**
- `test_echo` wrong arg type → `INVALID_TYPE` error, **no teardown**
- valid command afterward → `ok` (session survives every error path)
- counters: `tx_failures: 0`, `estop_received == estop_ack_sent` (4/4)

## Reproduce

Driver scripts (`phase16_proxy.py`, `phase16_demo.py`) and `controller_config.toml`
are in this directory.

1. Flash `sketches/command_server_conformance` (static IP `192.168.10.3` for
   this bench).
2. `python -u phase16_proxy.py` (proxy `127.0.0.1:5052 -> board:5051`).
3. From the special-lamp repo: `python -u runtime.py --config <this>/controller_config.toml`.
4. From the special-lamp repo: `PYTHONPATH=. python -u <this>/phase16_demo.py`.

## Status

Five bullets exercised live with the real controller. **Backlog remains blocked
on the operator** to accept these results and to complete Phase 17/18; this
record is evidence, not an autonomous sign-off. The firmware fix should be
reviewed and committed before Phase 17.
