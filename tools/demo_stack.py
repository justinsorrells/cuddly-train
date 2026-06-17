#!/usr/bin/env python3
"""Bring up the full command-server demo stack for boards on the network.

Discovers command-server boards (anything that answers TCP :5051 with a
schema-first frame), then starts Redis + the special-lamp controller + the web
dashboard GUI and health-checks the result. A developer convenience for live
demos; it is NOT part of the firmware library or its conformance suite.

    python3 tools/demo_stack.py discover      # scan the network for boards
    python3 tools/demo_stack.py up            # discover + start the whole stack
    python3 tools/demo_stack.py status        # what's running + board registration
    python3 tools/demo_stack.py down          # stop the stack

Environment overrides:
    DEMO_CONTROLLER_DIR   special-lamp repo (default ~/special-lamp)
    DEMO_SUBNETS          comma list of /24 prefixes to scan, e.g. "192.168.10,10.0.0"
    DEMO_BOARDS           comma list of host[:port] to use instead of scanning
    DEMO_BOARD_PORT       board TCP port (default 5051)
"""
from __future__ import annotations

import argparse
import json
import os
import re
import signal
import socket
import subprocess
import sys
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

DEFAULT_PORT = int(os.environ.get("DEMO_BOARD_PORT", "5051"))
CONTROLLER_DIR = Path(os.environ.get("DEMO_CONTROLLER_DIR", str(Path.home() / "special-lamp")))
WORKDIR = Path("/tmp/demo_stack")
SOCKET_PATH = "/tmp/hyperloop-controller.sock"
REDIS_URL = "redis://127.0.0.1:6379/0"
DASHBOARD_HOST, DASHBOARD_PORT = "127.0.0.1", 8000
DASHBOARD_URL = f"http://{DASHBOARD_HOST}:{DASHBOARD_PORT}/"
LIVENESS_TIMEOUT_S = 0.25

PROCS = ("controller", "dashboard", "proxy")  # demo_stack-managed, in teardown order


def log(msg: str) -> None:
    print(msg, flush=True)


# ----------------------------------------------------------------- discovery
def _scan_prefixes() -> list[str]:
    if os.environ.get("DEMO_SUBNETS"):
        return [p.strip().rstrip(".") for p in os.environ["DEMO_SUBNETS"].split(",") if p.strip()]
    prefixes: list[str] = []
    try:
        out = subprocess.run(["ifconfig"], capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        try:
            out = subprocess.run(["ip", "-o", "-4", "addr"], capture_output=True, text=True, timeout=5).stdout
        except (OSError, subprocess.SubprocessError):
            out = ""
    for ip in re.findall(r"inet (?:addr:)?(\d+\.\d+\.\d+\.\d+)", out):
        if ip.startswith(("127.", "169.254.")):
            continue
        prefix = ".".join(ip.split(".")[:3])
        if prefix not in prefixes:
            prefixes.append(prefix)
    return prefixes


def _probe(ip: str, port: int, connect_timeout=0.3, read_timeout=1.2) -> dict | None:
    try:
        s = socket.create_connection((ip, port), timeout=connect_timeout)
    except OSError:
        return None
    s.settimeout(read_timeout)
    buf = b""
    try:
        while b"\n" not in buf and len(buf) < 16384:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
    except OSError:
        pass
    finally:
        s.close()
    if b"\n" not in buf:
        return None
    try:
        msg = json.loads(buf.split(b"\n", 1)[0])
    except (ValueError, UnicodeDecodeError):
        return None
    if msg.get("type") != "schema" or not msg.get("source"):
        return None
    schema = msg.get("schema") or {}
    return {
        "ip": ip,
        "port": port,
        "board_id": msg["source"],
        "firmware": schema.get("firmware_version"),
        "commands": sorted((schema.get("commands") or {}).keys()),
    }


def discover_boards(port: int = DEFAULT_PORT) -> list[dict]:
    if os.environ.get("DEMO_BOARDS"):
        targets = []
        for item in os.environ["DEMO_BOARDS"].split(","):
            item = item.strip()
            if not item:
                continue
            host, _, p = item.partition(":")
            targets.append((host, int(p) if p else port))
    else:
        prefixes = _scan_prefixes()
        log(f"scanning {len(prefixes)} subnet(s) for :{port} boards: {', '.join(p + '.0/24' for p in prefixes) or '(none)'}")
        targets = [(f"{prefix}.{host}", port) for prefix in prefixes for host in range(1, 255)]
    found: list[dict] = []
    with ThreadPoolExecutor(max_workers=128) as pool:
        for result in pool.map(lambda t: _probe(t[0], t[1]), targets):
            if result:
                found.append(result)
    found.sort(key=lambda b: b["ip"])
    return found


# ------------------------------------------------------------- process mgmt
def _pidfile(name: str) -> Path:
    return WORKDIR / f"{name}.pid"


def _alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _running(name: str) -> int | None:
    pf = _pidfile(name)
    if not pf.exists():
        return None
    try:
        pid = int(pf.read_text().strip())
    except (ValueError, OSError):
        return None
    return pid if _alive(pid) else None


def _spawn(name: str, argv: list[str], cwd: Path, env: dict) -> int:
    logf = open(WORKDIR / f"{name}.log", "a")
    proc = subprocess.Popen(
        argv, cwd=str(cwd), env=env,
        stdout=logf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
        start_new_session=True,
    )
    _pidfile(name).write_text(str(proc.pid))
    return proc.pid


def _port_open(host: str, port: int, timeout=0.4) -> bool:
    try:
        socket.create_connection((host, port), timeout=timeout).close()
        return True
    except OSError:
        return False


# ----------------------------------------------------------------- services
def ensure_redis() -> bool:
    if _port_open("127.0.0.1", 6379):
        return True
    if not _which("redis-server"):
        log("WARN redis-server not found; the dashboard telemetry panel will be empty")
        return False
    subprocess.run(["redis-server", "--daemonize", "yes", "--save", "", "--appendonly", "no"],
                   capture_output=True, text=True)
    for _ in range(20):
        if _port_open("127.0.0.1", 6379):
            return True
        time.sleep(0.1)
    return _port_open("127.0.0.1", 6379)


def _which(cmd: str) -> str | None:
    from shutil import which
    return which(cmd)


def _venv_python() -> Path:
    venv = CONTROLLER_DIR / ".venv" / "bin" / "python"
    return venv if venv.exists() else Path(sys.executable)


def write_controller_config(boards: list[dict]) -> Path:
    lines = [
        "# Generated by tools/demo_stack.py — not committed.",
        "[controller]",
        f'socket_path = "{SOCKET_PATH}"',
        "",
    ]
    for b in boards:
        lines += ["[[boards]]", f'id = "{b["board_id"]}"', f'host = "{b["ip"]}"', f"port = {b['port']}", ""]
    lines += ["[observability]", "enabled = true", "",
              "[liveness]", "enabled = true", f"timeout_s = {LIVENESS_TIMEOUT_S}", ""]
    cfg = WORKDIR / "controller.toml"
    cfg.write_text("\n".join(lines))
    return cfg


def _controller_env() -> dict:
    env = dict(os.environ)
    env["PYTHONPATH"] = str(CONTROLLER_DIR)
    env["PYTHONUNBUFFERED"] = "1"
    return env


def dashboard_state() -> dict | None:
    try:
        with urllib.request.urlopen(DASHBOARD_URL + "api/state", timeout=2) as r:
            return json.loads(r.read())
    except (OSError, ValueError):
        return None


# ------------------------------------------------------------------ commands
def cmd_discover(_args) -> int:
    boards = discover_boards()
    if not boards:
        log("no command-server boards found on the network.")
        return 1
    log(f"found {len(boards)} board(s):")
    for b in boards:
        log(f"  {b['ip']}:{b['port']}  id={b['board_id']}  fw={b['firmware']}  commands={len(b['commands'])}")
    return 0


def cmd_up(args) -> int:
    WORKDIR.mkdir(parents=True, exist_ok=True)
    if not CONTROLLER_DIR.exists():
        log(f"ERROR controller repo not found: {CONTROLLER_DIR} (set DEMO_CONTROLLER_DIR)")
        return 2

    boards = discover_boards()
    if not boards:
        log("no boards found — flash command_server_conformance (or set DEMO_BOARDS) and retry.")
        return 1
    log(f"using {len(boards)} board(s): " + ", ".join(f"{b['board_id']}@{b['ip']}" for b in boards))

    # restart any stack we previously started (idempotent); leave Redis alone
    _stop_managed()

    ensure_redis()
    cfg = write_controller_config(boards)
    env = _controller_env()
    py = str(_venv_python())

    _spawn("controller", [py, "-u", "demos/run_controller_redis.py",
                          "--config", str(cfg), "--redis-url", REDIS_URL], CONTROLLER_DIR, env)
    log("started controller")

    if not _port_open(DASHBOARD_HOST, DASHBOARD_PORT):
        _spawn("dashboard", [py, "-u", "demos/webapp_dashboard.py",
                             "--socket-path", SOCKET_PATH, "--redis-url", REDIS_URL], CONTROLLER_DIR, env)
        log("started dashboard")
    else:
        log("dashboard port already in use — reusing it")

    # health check: wait for boards to register
    deadline = time.time() + 20
    registered: list[str] = []
    while time.time() < deadline:
        state = dashboard_state()
        if state and state.get("connected"):
            registered = [b["board_id"] for b in state.get("boards", []) if b.get("conn_state") == "REGISTERED"]
            if len(registered) >= len(boards):
                break
        time.sleep(0.5)

    log("")
    log(f"  dashboard:  {DASHBOARD_URL}")
    state = dashboard_state() or {}
    for b in state.get("boards", []):
        log(f"  board:      {b.get('board_id')}  {b.get('conn_state')}  available={b.get('available')}  fw={b.get('firmware_version')}")
    log(f"  controller socket: {SOCKET_PATH}")
    log("  logs: /tmp/demo_stack/*.log   stop: python3 tools/demo_stack.py down")
    if not registered:
        log("  NOTE no board reached REGISTERED yet — check /tmp/demo_stack/controller.log")
        return 1
    return 0


def _stop_managed() -> list[str]:
    stopped = []
    for name in PROCS:
        pid = _running(name)
        if pid:
            try:
                os.killpg(os.getpgid(pid), signal.SIGTERM)
            except OSError:
                try:
                    os.kill(pid, signal.SIGTERM)
                except OSError:
                    pass
            stopped.append(f"{name}({pid})")
        _pidfile(name).unlink(missing_ok=True)
    return stopped


def cmd_down(_args) -> int:
    stopped = _stop_managed()
    try:
        os.unlink(SOCKET_PATH)
    except OSError:
        pass
    log("stopped: " + (", ".join(stopped) if stopped else "(nothing was running)"))
    log("(Redis left running; stop with `redis-cli shutdown nosave` if you want)")
    return 0


def cmd_status(_args) -> int:
    log(f"redis:      {'up' if _port_open('127.0.0.1', 6379) else 'down'}")
    for name in PROCS:
        pid = _running(name)
        log(f"{name + ':':11} {'up pid=' + str(pid) if pid else 'down'}")
    state = dashboard_state()
    if state and state.get("connected"):
        for b in state.get("boards", []):
            log(f"  board: {b.get('board_id')}  {b.get('conn_state')}  available={b.get('available')}")
    elif _running("dashboard"):
        log("  dashboard up but /api/state not answering yet")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", nargs="?", default="up",
                        choices=["up", "down", "status", "discover"],
                        help="up (default), down, status, or discover")
    args = parser.parse_args(argv)
    return {"up": cmd_up, "down": cmd_down, "status": cmd_status, "discover": cmd_discover}[args.command](args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
