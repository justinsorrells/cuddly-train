# Phase-16 interop demo — user guide

How to recreate the controller↔Teensy interop demo from scratch. Pairs with
`README.md` (results) and `SESSION_AUDIT.md` (review record).

## 0. What this demo proves

A Unix-socket client talks **only** to the controller; the controller is the
sole authority that talks to the board over TCP:

```
client (phase16_demo.py) ──unix socket──► controller (special-lamp/runtime.py)
        └─ proxy (phase16_proxy.py, optional, for wire visibility) ─┐
                                                                     ▼
                                              Teensy 4.1 @ 192.168.10.3:5051
                                              (command_server_conformance fw)
```

It exercises: (1) schema-first registration, (2) a command routed to the board
with a correlated response, (3) 50 ms telemetry to the controller, (4)
disconnect/reconnect + schema re-registration, (5) software e-stop + `estop_ack`.

## 1. Prerequisites

- Teensy 4.1 on USB serial (this bench: `/dev/cu.usbmodem163411201`) and on the
  same L2 network as the host.
- `arduino-cli` with `teensy:avr` core and `QNEthernet` lib installed
  (see `docs/companion/Build_and_Flash_Guide.md`).
- Controller repo at `~/special-lamp` with its venv (`.venv`).
- Network (this bench): host `192.168.10.2/24` on a direct link (`en6`); board
  static `192.168.10.3`. No DHCP server on the link → use a **static IP** in the
  sketch. On a network with DHCP you can leave the sketch on DHCP and discover
  the board's address instead.

## 2. Build + flash the firmware

The board firmware is `sketches/command_server_conformance` (board id
`command_server_conformance`, listens on TCP **5051**).

For a no-DHCP bench, set its `network_config` to static (top of the `.ino`):

```cpp
api::NetworkConfig network_config{
    api::NetworkConfig::Mode::StaticIpv4,
    kListenPort,           // 5051
    {192, 168, 10, 3},     // board IP
    {192, 168, 10, 1},     // gateway (same-subnet comms ignore this)
    {255, 255, 255, 0},    // mask
};
```
> This static-IP edit is bench-only — do not commit it; the repo default is DHCP.

Compile-only sanity check: `./tools/build_teensy.sh`

Flash (the build wraps `src/` as an Arduino library, same as the build script):

```bash
BUILD_ROOT=$(mktemp -d); LIB="$BUILD_ROOT/libraries/TeensyCommandServer"
mkdir -p "$LIB"; ln -s "$PWD/src" "$LIB/src"
printf 'name=TeensyCommandServer\nversion=0.0.0\narchitectures=*\nincludes=TeensyCommandServer.h\n' > "$LIB/library.properties"
arduino-cli compile --fqbn teensy:avr:teensy41 --library "$LIB" \
  --upload -p /dev/cu.usbmodem163411201 \
  --build-path "$BUILD_ROOT/out" sketches/command_server_conformance
rm -rf "$BUILD_ROOT"
```

Verify it booted and speaks the protocol (schema on connect, 50 ms telemetry):

```bash
python3 - <<'PY'
import socket,json,time
s=socket.create_connection(("192.168.10.3",5051),3); s.settimeout(2); buf=b""; n=0; t=time.time()
while time.time()-t<1.5:
    try: c=s.recv(4096)
    except socket.timeout: break
    buf+=c
    while b"\n" in buf:
        ln,buf=buf.split(b"\n",1)
        m=json.loads(ln); 
        if m["type"]=="telemetry": n+=1
        elif m["type"]=="schema": print("schema source:",m["source"])
print("telemetry frames in 1.5s:",n)   # expect ~30 (50 ms cadence)
PY
```
If you see the schema and ~30 telemetry frames, the firmware (with the Phase-16
fix) is good. Zero telemetry means the telemetry-wiring fix is missing.

## 3. Configure the controller

`controller_config.toml` (in this directory):

```toml
[controller]
socket_path = "/tmp/hyperloop-controller.sock"

[[boards]]
id = "command_server_conformance"   # MUST equal the board's schema "source"
host = "127.0.0.1"                   # proxy host (use 192.168.10.3 to skip the proxy)
port = 5052                          # proxy port (use 5051 to skip the proxy)

[observability]
enabled = false                     # no Redis needed for the demo

[liveness]
enabled = true
timeout_s = 0.25                    # telemetry is the liveness signal
```

To run **without** the proxy, point `host/port` straight at `192.168.10.3:5051`.

## 4. Run it

Three terminals (or background jobs). The proxy is optional but gives you the
full wire transcript that proves telemetry + the e-stop handshake.

```bash
# (optional) wire-logging proxy: 127.0.0.1:5052 -> board:5051
python3 phase16_proxy.py

# controller (from the special-lamp repo root)
cd ~/special-lamp
PYTHONPATH=. .venv/bin/python -u runtime.py --config <path>/controller_config.toml

# driver client (from the special-lamp repo root)
cd ~/special-lamp
PYTHONPATH=. .venv/bin/python -u <path>/phase16_demo.py
```

`PYTHONPATH=.` is required so the demo/controller import the special-lamp
modules (`protocol`, `demos.client.client`, …).

## 5. What you should see

- **Client (`phase16_demo.py`)**: BULLET 1–5 lines; board `REGISTERED`;
  `test_echo` → `value: 42`; `estop_reset` → `estop_active: false`; final
  "ALL FIVE PHASE-16 BULLETS EXERCISED". Exit 0.
- **Proxy transcript** (the real evidence): `B->C schema`, steady
  `B->C telemetry`, `C->B test_echo` + `B->C response`, then
  `estop_triggered → C->B estop → estop_ack {state:"safe"}`, then a
  `session closed` / reconnect with a fresh schema. (Sample in
  `proxy_transcript.log`.)

## 6. Manual spot-checks (optional)

Send a single command yourself via the controller's demo client:

```bash
cd ~/special-lamp
PYTHONPATH=. .venv/bin/python demos/client/client.py \
  --socket-path /tmp/hyperloop-controller.sock \
  --target command_server_conformance --command test_echo value=7
```

Read board counters directly (no controller), to sanity-check dispatch/telemetry:
send `get_counters` to `192.168.10.3:5051` with `source:"controller"`,
`target:"command_server_conformance"` — expect `telemetry_sent` climbing,
`telemetry_dropped: 0`, `estop_received == estop_ack_sent`.

## 7. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Connection refused on 5051 | Wrong firmware flashed, or board still booting. Reflash `command_server_conformance`; wait ~2 s. |
| Board reachable (ping) but nothing on 5051 | Non-command-server sketch is flashed. Reflash. |
| `BOARD_UNAVAILABLE … went down` right after start | Telemetry not arriving → liveness drop. With the Phase-16 fix, telemetry flows; without it, the board is silent after schema. Verify with the step-2 probe. |
| Command returns `BOARD_UNAVAILABLE` only when routed to the board | Pre-fix `routeCommand` bug (commands tore down the session). Ensure the firmware includes the fix. |
| `ModuleNotFoundError: protocol` | Missing `PYTHONPATH=.` when running from `~/special-lamp`. |
| Board id mismatch / FAULTED | `[[boards]].id` must equal the schema `source` (`command_server_conformance`). |
| Second client can't connect to board | The board is a single-client TCP server; the controller (or proxy) holds the one connection. |
| No `estop_ack` on the Unix client | Expected — the controller does not forward e-stop events to local clients. Observe the handshake on the proxy transcript instead. |

## 8. Web dashboard (schema-driven GUI)

`~/special-lamp/demos/webapp_dashboard.py` is a tiny FastAPI GUI over the same
Unix socket. It reads each board's schema and **generates a typed form per
command**, sends commands through the controller, and shows responses in a live
panel that **overwrites values by field name** (so `get_counters`, `test_echo`,
etc. collapse onto a single current value per field instead of a growing log).

Telemetry on the dashboard comes from **Redis** (the controller's observability
read-replica), not the command socket — that is the stack's design (socket =
command path; Redis = telemetry fan-out). Full stack:

```bash
cd ~/special-lamp
redis-server --daemonize yes --save "" --appendonly no            # 1. Redis
# 2. controller WITH Redis (config needs [observability] enabled = true):
PYTHONPATH=. .venv/bin/python demos/run_controller_redis.py \
    --config /tmp/phase16_redis.toml --redis-url redis://127.0.0.1:6379/0
# 3. dashboard:
PYTHONPATH=. .venv/bin/python demos/webapp_dashboard.py \
    --socket-path /tmp/hyperloop-controller.sock --redis-url redis://127.0.0.1:6379/0
# open http://127.0.0.1:8000/
```
(Drop `--redis-url`/Redis and the dashboard still runs — just without the
telemetry panel. `runtime.py` itself wires `redis=None`, which is why the
`run_controller_redis.py` launcher exists.)

What it does:
- **Forms** are built from `schema.commands[*].args` — `int`/`float` render number
  inputs, `bool` a true/false select, `string` a text box. Submitted strings are
  **coerced to the declared type** server-side before sending (so `"42"` → int 42;
  bad input returns `BAD_INPUT` and is never forwarded to the board).
- **Live values** table overwrites by bare field name; common fields shared
  across commands (`board_proc_us`, `latency_ms`, …) show one always-current row.
- **Board status** (connection state) is polled via `get_schemas` once per second.
- Optional **auto `get_counters` (1 s)** checkbox makes the panel update
  continuously (counters move every poll) — a clear demo of value-overwrite.
- An **`estop_reset`** button sends the controller `estop_reset`.

API (browser uses these): `GET /api/schema`, `GET /api/state`,
`POST /api/command {target,command,args}`, `POST /api/estop_reset`.

Limitation (controller-side, not firmware): this controller does not forward
board **telemetry** to local clients, so the live panel is driven by command
responses + board state, not the 50 ms telemetry stream. To watch telemetry,
use the proxy transcript (step 4) or enable auto `get_counters`.

## 9. Clean up

```bash
pkill -f "runtime.py --config" ; pkill -f phase16_proxy.py
rm -f /tmp/hyperloop-controller.sock
```
