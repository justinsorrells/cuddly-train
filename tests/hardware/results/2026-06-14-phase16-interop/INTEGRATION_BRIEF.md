# Phase-16 demo session — integration brief (firmware + controller)

**Sessions:** 2026-06-14 → 2026-06-15, operator-run + agent-assisted.
**Repos:** `cuddly-train` (Teensy firmware/library) and `special-lamp` (Python
controller). This brief is the authoritative record of everything changed and
observed to reach the live demo. It was a fast push to a demo; the follow-up
audit tasks (cuddly-train `backlog.md`, special-lamp `backlog.md`) reference it.

> Honesty boundary: the interop was operator-run on real hardware; this is
> evidence, not an autonomous sign-off. Several changes are demo/bench-scoped and
> must NOT be committed as-is (called out below).

---

## 1. What we set out to do, and what actually happened

Goal: run the Phase-16 vertical slice — real controller ⇄ real Teensy 4.1 — proving
schema-first registration, a client command routed through the controller to the
board with a correlated response, telemetry to the controller, disconnect/reconnect
with schema re-registration, and software e-stop + `estop_ack`. Then a web GUI;
then telemetry on the GUI; then a visual redesign; then a live "add a command +
reflash" loop.

It did **not** work out of the box. Two real firmware bugs blocked it, both fixed.
Then a series of controller-side observability/UX additions and findings followed.

## 2. Bench / environment (for reproduction)

| Item | Value |
|---|---|
| Board | Teensy 4.1, static IP `192.168.10.3:5051`, serial `/dev/cu.usbmodem163411201` |
| Host | macOS, `192.168.10.2/24` on `en6` (direct link, **no DHCP server** → static IP required) |
| Firmware sketch | `sketches/command_server_conformance` (board id `command_server_conformance`, fw `0.1.0`, proto `1`) |
| arduino-cli | `1.5.0` (Build/Flash guide pins `1.3.1`) |
| Teensy core | `teensy:avr 1.60.0` (guide pins `1.59.0`) |
| QNEthernet | `0.35.0` (matches pin) · ArduinoJson vendored 6.21.5 |
| Controller venv | Python 3.14, FastAPI 0.136, uvicorn; **`redis` 8.0.0 installed this session** |
| Redis | `redis-server` (homebrew), ephemeral (`--save "" --appendonly no`) |

Reflash command (from `cuddly-train` root; stages `src/` as an Arduino lib like
`tools/build_teensy.sh`):
```bash
BUILD_ROOT=$(mktemp -d); LIB="$BUILD_ROOT/libraries/TeensyCommandServer"; mkdir -p "$LIB"; ln -s "$PWD/src" "$LIB/src"
printf 'name=TeensyCommandServer\nversion=0.0.0\narchitectures=*\nincludes=TeensyCommandServer.h\n' > "$LIB/library.properties"
arduino-cli compile --fqbn teensy:avr:teensy41 --library "$LIB" --upload -p /dev/cu.usbmodem163411201 --build-path "$BUILD_ROOT/out" sketches/command_server_conformance
```

---

## 3. FIRMWARE changes (`cuddly-train`)

### 3.1 Bug A — telemetry was never pushed (FIXED) — `src/TeensyCommandServer.h`
- **Symptom:** board sent its schema then went silent; a passive TCP observer saw
  `schema:1` and **zero** telemetry. Telemetry is the controller's liveness signal,
  so the board was dropped as `BOARD_UNAVAILABLE`.
- **Root cause:** the public `TeensyCommandServer` facade never wired telemetry.
  `facadeRoutes()` set `command/estop/heartbeat/safety_due/outbound_outcome` but
  **never `telemetry_due`/`telemetry_inactive`**, and there was **no
  `TelemetryScheduler` member at all** — `core::TelemetryScheduler` was constructed
  nowhere in the repo (dead code). `setTelemetryProvider()` stored a provider that
  nothing ever called.
- **Fix:** added a placement-new `TelemetryScheduler telemetry_scheduler_` member
  (mirrors the existing `session_driver_` pattern), constructed in `start()` from
  `registry_.telemetryProvider()` (guaranteed non-null — `validateMetadataForSeal`
  requires it), destroyed in dtor + before reconstruct; wired `routeTelemetryDue`
  and `routeTelemetryInactive` into `facadeRoutes()`.

### 3.2 Bug B — only `get_counters` dispatched (FIXED) — `src/TeensyCommandServer.h`
- **Symptom:** routing any real command (`test_echo`) to the board made it "go down";
  a direct probe showed the board RST/abort the session on the command.
- **Root cause:** `routeCommand` returned `true` (= teardown, per
  `ServiceLoop::routeValid`) for every non-diagnostic command, so commands never
  reached their handler and instead tore the session down. Only `get_counters` was
  let through.
- **Fix:** `routeCommand` now always runs `CommandDispatcher` (which resolves every
  registered command via the registry — including `get_counters`, which
  `DiagnosticsCommand::registerCommand` registers there; unknown commands →
  `UNKNOWN_COMMAND` error, not teardown). Removed the now-dead
  `counters_diagnostic_enabled_` member.

> Both bugs were **facade-wiring** defects. The inner components
> (`CommandDispatcher`, `TelemetryScheduler`) were correct and unit-tested; **no host
> test drove a registered command or telemetry through the facade `service()` path**,
> so the suite stayed green while real interop was broken. This is the single most
> important lesson for the firmware audit.

### 3.3 Test change — `tests/host/unit/test_service_loop.cpp`
- `facadeServicePumpHandlesSafetyAndHeartbeatRoutes` **asserted the bug**: it
  registered `set_speed` and then asserted `command_calls == 0` + a teardown
  (`loss_calls==1`, `abortCount==1`, `controller_disconnects==1`). Rewrote it to
  assert correct dispatch (handler called, OK response written, no teardown) and the
  now-present first telemetry frame; **added an unknown-command regression**
  (`UNKNOWN_COMMAND`, no teardown). **This is the one test changed to pass — verify
  the new assertions describe contract-correct behavior, not behavior fitted to code.**
- Host suite after fix: **24/24 pass**. `check_invariants` OK, `check_contract_sync`
  OK, `build_teensy` OK.

### 3.4 Sketch changes — `sketches/command_server_conformance/command_server_conformance.ino` (DEMO/BENCH — do not commit as-is)
- `network_config` set to **static `192.168.10.3`** (committed default is DHCP) —
  bench-only because the link has no DHCP server.
- Added a demo command **`test_sum(a:int,b:int) → {sum}`** (handler `testSum` +
  registration) to demonstrate the live add-command → reflash → schema-driven UI
  update loop. This is a contract fixture; decide whether to keep, move to a
  dedicated demo sketch, or revert.

### 3.5 Firmware behavior verified live on hardware
- Schema-first registration; `test_echo 42→42` correlated response; telemetry at
  **50 ms / ~20 Hz** (controller-measured interval 49.7 ms, jitter ~0.4 ms);
  disconnect/reconnect + schema re-registration; software e-stop chain
  (`estop_triggered → estop → estop_ack {state:"safe"}`); `get_counters`
  (`telemetry_dropped:0`, `tx_failures:0`, `estop_received==estop_ack_sent`); unknown
  command → `UNKNOWN_COMMAND` (no teardown); bad-arg → `INVALID_TYPE` (no teardown);
  live `test_sum` after reflash (`schema_revision` 1→2, `3+4→7`).

---

## 4. CONTROLLER changes (`special-lamp`)

> I added only the three `demos/` files below + installed `redis`. The tracked
> diffs to `local_socket.py` and `protocol.py` and the untracked
> `docs/contracts/Teensy_Command_Server_Contract.md` are **pre-existing operator
> edits, not from this session** — flagged here so the audit doesn't misattribute them.

### 4.1 New: `demos/webapp_dashboard.py` — schema-driven mission-control GUI
- One persistent Unix-socket connection to the controller, seq-correlated requests.
- Builds a typed form per command from `get_schemas`; **coerces form strings to the
  schema-declared type server-side** (`"42"`→int) before sending; bad input →
  `BAD_INPUT`, never forwarded to a board.
- "Live values / registers" store **overwrites by bare field name** (responses +
  telemetry collapse to one current value per field). Internal `get_schemas` polls
  excluded.
- **Telemetry from Redis** (see 4.3): tails `board:telemetry:<id>` (`XREVRANGE`),
  shows values + controller-measured rate/interval/jitter; `/api/history` returns a
  series (telemetry json fields **and** top-level stream fields like
  `telemetry_interval_ms`) for the canvas line chart.
- **Connection liveness derived from telemetry freshness** (`age_s`, `live<1.5s`),
  not just `conn_state`, plus board/system state hashes from Redis (`system:state`
  for the e-stop safety banner, `board:state:<id>` for latency p50/p95/p99, queue,
  estop_ack).
- Full visual redesign to the supplied dark industrial spec (shell, status header,
  safety banner, nav, device card that desaturates offline, real-time chart, metric
  cards, registers, activity rail; Inter/JetBrains Mono). Console only rebuilds on
  **schema change** (no input-wipe) and announces "schema updated".
- Endpoints: `GET /`, `/api/schema`, `/api/state`, `/api/history`,
  `POST /api/command`, `POST /api/estop_reset`.

### 4.2 New: `demos/run_controller_redis.py` — controller launcher with Redis
- `runtime.main()` builds the controller with `redis=None`, so telemetry/state are
  mirrored nowhere. This launcher uses the public
  `create_runtime(config, redis=<redis.asyncio client>)` seam (no `runtime.py`
  change) so the controller actually populates Redis. Config must set
  `[observability] enabled = true`.

### 4.3 New: `demos/webapp_dashboard.README.md`
- Documents the GUI, the **socket=command-path / Redis=telemetry-observability**
  split, and run order (redis → redis launcher → dashboard).

### 4.4 Controller behaviors observed (NOT changed — for the audit)
1. **E-stop events are not forwarded to local clients.** `_emit_controller_event(...,
   broadcast_local=True)` appears exactly once (schema-change on registration);
   `estop_triggered`/`estop_ack` use observability-only `observe_controller_event`.
   The e-stop chain is real on the controller⇄board wire but a Unix-socket client/GUI
   can't observe it. `local_socket.py` lists those as critical event names, implying
   intent to forward — possible gap. (We proved e-stop via a wire proxy instead.)
2. **`runtime.py` never wires a Redis client** (`create_runtime(config)` with
   `redis=None`), so out of the box the documented Redis observability path is inert.
3. **`get_schemas` does not expose `last_telemetry`** even though the controller
   keeps it in board state; dashboards must use Redis for telemetry.
4. **Single-client board session can wedge under reconnect churn.** During the
   session the controller⇄board TCP session sat **half-open for ~116 s** (no
   telemetry, board stuck `CONNECTING`) and did **not** self-recover until an
   external connection forced the board's newest-connection-wins replacement.
   Telemetry-based liveness only faults a **`REGISTERED`** board, so a board stuck in
   the `CONNECTING`/registration-timeout loop has no liveness watchdog. Contributing
   factors: heavy churn (briefly two controllers at once, intruder/disconnect tests,
   repeated restarts, direct `:5051` probes), and a likely half-open TCP socket with
   no RST/FIN reaching the controller. No TCP keepalive on the board connection.

### 4.5 Operational footguns hit (worth a runbook note)
- Killing/restarting the controller races the **socket unlink**: a previously-killed
  controller's async shutdown `unlink`'d `/tmp/hyperloop-controller.sock` *after* the
  new one bound it → new controller listened on an orphaned fd no client could reach.
  Mitigation: kill, wait ≥3 s for shutdown, `rm -f` socket, then start exactly one.
- **Two controllers fighting one single-client board** produced confusing flapping;
  always run exactly one controller.
- **Don't probe `:5051` directly while the controller runs** — each connect bumps the
  controller's session (single-client board).

---

## 5. Verification status (what is/ isn't proven)

| Area | Proven live | Notes |
|---|---|---|
| Firmware telemetry push fix | ✅ | 50 ms cadence, 24/24 host tests |
| Firmware command dispatch fix | ✅ | dispatch + unknown/bad-arg, no teardown |
| Schema-first / reconnect / e-stop | ✅ | wire proxy transcript |
| GUI command path + type coercion | ✅ | incl. bad-input rejection |
| GUI telemetry via Redis | ✅ | chart + metrics + freshness |
| Disconnect → OFFLINE via freshness | ✅ | age grew 0→1.5s→offline→recover |
| Session-wedge recovery | ⚠️ | recovered only via external connection; not self-healed |
| Toolchain pins (§ Build/Flash) | ❌ | ran on cli 1.5.0 / core 1.60.0 — Phase-18 freeze needs pinned toolchain |
| E-stop visibility to GUI clients | ❌ | controller doesn't forward; open question |

## 6. Artifacts (this directory)
`README.md`, `SESSION_AUDIT.md`, `USER_GUIDE.md`, `proxy_transcript.log`,
`demo_client.log`, `controller_config.toml`, `phase16_proxy.py`, `phase16_demo.py`,
and this brief. Dashboard/launcher live in `special-lamp/demos/`.
