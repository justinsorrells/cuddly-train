# Phase 16 session audit record (for independent verification)

**Purpose.** A complete, skeptical-review-ready account of the work done in this
session: what was claimed, the evidence, the firmware change, and the specific
points an auditing agent should scrutinize. Nothing here is simulated; every
hardware claim is backed by a transcript in this directory.

Repos:
- Firmware (board): `cuddly-train` @ `3b6d9f9` + uncommitted working-tree changes.
- Controller: `special-lamp` @ `cf347cc` (unmodified except a runtime TOML config).

---

## 1. Objective

Run the Phase-16 OPERATOR GATE: prove end-to-end interop between the **real
controller** and a **real Teensy 4.1** for five behaviors — schema-first
registration, one client command routed through the controller to the board
with a correlated response, telemetry reaching the controller, disconnect/
reconnect with schema re-registration, and software e-stop with `estop_ack`.

## 2. What was found (the headline)

The vertical slice **did not work out of the box**. Two real bugs in the public
firmware facade `src/TeensyCommandServer.h` blocked it. Both are *facade-wiring*
defects: the inner components (`CommandDispatcher`, `TelemetryScheduler`) are
correct and unit-tested, but the facade never wired them in. That is why the
host unit suite was green while live interop failed — **no host test exercised
a registered command or telemetry through the real facade `service()` path.**

### Bug A — telemetry was never pushed
`facadeRoutes()` never set `telemetry_due`/`telemetry_inactive`, and there was
no `TelemetryScheduler` member. `setTelemetryProvider()` stored a provider that
nothing ever called. `TelemetryScheduler` was dead code — constructed nowhere
in the repo (verify: `grep -rn "TelemetryScheduler(" src tests`).

Observable symptom: board sent its schema then went silent; a passive TCP
observer saw `schema:1` and zero telemetry in 1.5 s. Because telemetry is the
liveness signal, the controller dropped the board with `BOARD_UNAVAILABLE`.

### Bug B — only the `get_counters` diagnostic was dispatched
`routeCommand` returned `true` (which `ServiceLoop::routeValid` treats as
*teardown_requested*) for every command except the diagnostic, so any real
command tore the session down before reaching its handler.

Observable symptom: routing `test_echo` to the board made the controller report
`board ... went down`; a direct probe showed the board RST/abort the session on
the command.

## 3. Diagnostic trail (reproducible)

1. Board was reachable at `192.168.10.3` but ran an unrelated ethernet-tester;
   nothing on 5050/5051. Flashed `command_server_conformance` (static IP for the
   bench).
2. Passive connect → schema received, **no telemetry** → located the gap in
   `ServiceLoop::runTelemetryDueCheck` (route nullptr) and confirmed
   `facadeRoutes()` never sets it.
3. With liveness disabled, registration held but `test_echo` still dropped the
   board → traced to `routeCommand` returning teardown for non-diagnostic
   commands; confirmed `CommandDispatcher.dispatch` is the correct general path
   and that `get_counters` is a normal registry command
   (`DiagnosticsCommand::registerCommand` → `registry.registerCommand`).

## 4. The fix

See `git diff src/TeensyCommandServer.h` (also summarized in `README.md`).

- Added a placement-new `TelemetryScheduler telemetry_scheduler_` member
  (mirroring the existing `session_driver_` pattern), constructed in `start()`
  from `registry_.telemetryProvider()`, destroyed in the destructor and before
  reconstruction. Wired `routeTelemetryDue` / `routeTelemetryInactive`.
  - Safe because `validateMetadataForSeal()` already requires a telemetry
    provider, so `start()` always has a non-null provider; and `start()` only
    constructs it inside the existing `network_ != nullptr && clock_ != nullptr`
    block, so the no-clock default constructor is unaffected (pointer stays
    null, routes no-op).
- `routeCommand` now always runs `CommandDispatcher` (handles all registered
  commands incl. `get_counters`; unknown → `UNKNOWN_COMMAND` error, not
  teardown). Removed the dead `counters_diagnostic_enabled_` member (it was only
  read by the old gate; registry membership is the real gate).

No core component logic changed; only facade wiring. No new heap allocation
(dispatcher is stack-constructed per command, as the diagnostic path already
did; scheduler is placement-new into a member, like `session_driver_`).

## 5. Test change (and why the old test was wrong)

`tests/host/unit/test_service_loop.cpp :: facadeServicePumpHandlesSafetyAndHeartbeatRoutes`
**asserted the bug**: it registered `set_speed` (line 185, `configure()`), sent
it, then asserted `command_calls == 0` AND a teardown (`loss_calls == 1`,
`abortCount == 1`, `controller_disconnects == 1`). A command server that never
runs its registered commands and kills the session on each one is non-functional
and contradicts the contract. Rewrote it to assert correct dispatch (handler
called, OK response written, no teardown) and added an unknown-command
regression (`UNKNOWN_COMMAND`, no teardown). Also asserts the now-present first
telemetry frame. Host suite: **24/24**.

> Auditor note: this is the one place I changed a test to pass. Verify the new
> assertions describe *contract-correct* behavior, not behavior fitted to my
> code. Cross-check against `CommandDispatcher.dispatch` (returns `false` on
> success, `true` only on enqueue failure) and contract command-dispatch rules.

## 6. Verification performed

| Check | Result |
|---|---|
| `python3 tools/check_invariants.py` | OK (0 warnings) |
| `python3 tools/check_contract_sync.py` | OK (3 files match pinned hashes) |
| `./tools/run_host_tests.sh` | 24/24 passed |
| `./tools/build_teensy.sh` | all sketches compiled |
| Live hardware interop | all 5 bullets — see `proxy_transcript.log` |
| Dispatcher audit (direct-to-board) | get_counters ok; unknown→error; bad-arg→error; survives all; `tx_failures:0`; `estop_received==estop_ack_sent` |

## 7. Claim → evidence map

| Claim | Where to verify |
|---|---|
| Telemetry now flows at 50 ms | `proxy_transcript.log` (frames #1,#21,… 1 s apart); `get_counters.telemetry_sent` climbing, `telemetry_dropped:0` |
| Command routed + correlated response | `proxy_transcript.log [3.03s]` C->B/B->C seq=1 value=42 |
| Honest e-stop | `proxy_transcript.log [5.03s]` estop_triggered → estop → `estop_ack {state:"safe"}` |
| Reconnect re-registers schema | `proxy_transcript.log [6.55s]/[6.88s]` |
| Host tests green | `./tools/run_host_tests.sh` |

## 8. Points an auditor should scrutinize (devil's advocate)

1. **Behavioral correctness of "never teardown on command".** Confirm a
   registered command should only tear down on a genuine critical transmit
   failure (dispatcher returns `true` only when `enqueueCritical != Queued`).
2. **TelemetryScheduler lifetime / references.** It holds refs to `counters_`,
   `identity_`, `*clock_`. Confirm `identity_` is finalized (it is set at
   `start()` line ~142, before the scheduler is constructed) and never moves.
3. **Was `TelemetryScheduler` truly dead before?** Confirm there was no other
   intended wiring path I bypassed.
4. **Controller-side gap (NOT fixed here, out of repo scope):** `special-lamp`
   only broadcasts a schema-change event to local clients
   (`_emit_controller_event(..., broadcast_local=True)` appears once); it does
   **not** forward `estop_triggered`/`estop_ack` to Unix-socket clients even
   though `local_socket` lists them as critical event names. The e-stop chain is
   real on the wire (proxy transcript), but a GUI/client would not see it.
   Worth confirming whether that is intended controller behavior.
5. **Toolchain deviation:** arduino-cli 1.5.0 (pin 1.3.1), teensy core 1.60.0
   (pin 1.59.0). Fine for a functional demo; Phase-18 freeze must use pins.
6. **Bench-only change not for commit:** the sketch `network_config` was set to
   static `192.168.10.3` for this bench; the committed default is DHCP. Do not
   commit that hunk.
7. **One transient anomaly:** an early direct probe once saw a TCP RST right at
   `test_trigger_estop`; it did not reproduce on the captured runs (board
   emitted response + estop_triggered cleanly). Worth a watch under load.

## 9. Artifacts in this directory

- `README.md` — concise result record (config, fix, per-bullet evidence).
- `proxy_transcript.log` — full controller↔board wire log (primary evidence).
- `demo_client.log` — Unix-socket client transcript.
- `controller_config.toml` — controller runtime config used.
- `phase16_proxy.py`, `phase16_demo.py` — operator driver scripts (reproduce).

## 10. Status / honesty boundary

Five bullets were exercised live with the real controller in the command path.
This is **operator-run, agent-assisted** evidence — not an autonomous sign-off.
The backlog item remains an operator gate: the operator accepts these results.
The firmware fix should get an independent review before Phase 17, and the
contract/§31 freeze remains Phase 18.
