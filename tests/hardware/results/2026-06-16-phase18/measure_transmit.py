#!/usr/bin/env python3
"""Phase-18 transmit-path measurements (numerics 3 & 4) against real hardware.

macOS would not honour a small SO_RCVBUF (clamped to ~35 KB with autotuning),
so a trickle of small telemetry frames will not wedge the board quickly. Two
stronger rigs:

  numeric 3 (100 ms transmit deadline, §13.6): burst commands without reading
    the responses -> the board's Critical response writes fill the peer buffer
    then fail at the transmit deadline -> tx_failures + CriticalTransmitFailure
    teardown (RST).

  numeric 4 (10-frame telemetry teardown, §13.6): stop reading entirely and
    wait long enough for the receive buffer to fill purely from 50 ms
    telemetry, after which 10 consecutive deadline failures tear the session
    down (RST).

Every number printed comes off the board.
"""
from __future__ import annotations

import errno
import json
import select
import socket
import sys
import time

sys.path.insert(0, "tests/conformance")
from protocol_client import BoardClient, assert_response_ok, BOARD_ID, CONTROLLER  # noqa: E402


def parse_addr(text):
    host, port = text.rsplit(":", 1)
    return host, int(port)


def counters(host, port):
    c = BoardClient(host, port)
    c.read_message()
    g = assert_response_ok(c.command("get_counters"), "gc")
    c.close()
    return g


def reset_detected(sock):
    """Non-draining RST/FIN probe (draining would un-wedge the board)."""
    err = sock.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
    if err != 0:
        return errno.errorcode.get(err, str(err))
    r, _, x = select.select([sock], [], [sock], 0)
    if x:
        return "select-exception"
    if r:
        try:
            if sock.recv(1, socket.MSG_PEEK) == b"":
                return "FIN"
        except ConnectionResetError:
            return "ECONNRESET"
        except OSError as exc:
            return f"OSError:{exc.errno}"
    return None


def encode_cmd(seq, name="test_echo", args=None):
    msg = {"type": "command", "seq": seq, "controller_ts": 1.0, "source": CONTROLLER,
           "target": BOARD_ID, "command": name, "args": args if args is not None else {"value": seq}}
    return (json.dumps(msg, separators=(",", ":")) + "\n").encode()


def drain_schema(sock):
    sock.settimeout(2.0)
    buf = b""
    while b"\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            return False
        buf += chunk
    return True


# ----------------------------------------------------- numeric 3: Critical
def burst_critical(host, port, max_cmds=4000, timeout_s=15.0):
    before = counters(host, port)
    time.sleep(0.2)
    p = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    p.connect((host, port))
    if not drain_schema(p):
        p.close()
        return {"error": "no schema"}
    p.setblocking(False)
    t0 = time.monotonic()
    sent = 0
    signal = None
    seq = 1
    while sent < max_cmds and time.monotonic() - t0 < timeout_s:
        # push a batch of commands; never read the responses
        batch = b"".join(encode_cmd(seq + i) for i in range(50))
        seq += 50
        try:
            p.sendall(batch)
            sent += 50
        except (BrokenPipeError, ConnectionResetError) as exc:
            signal = f"send:{type(exc).__name__}"
            break
        except BlockingIOError:
            pass  # our send buffer full; board not draining our inbound either
        signal = reset_detected(p)
        if signal:
            break
        time.sleep(0.01)
    latency_ms = (time.monotonic() - t0) * 1000.0
    p.close()
    time.sleep(0.3)
    after = counters(host, port)
    return {
        "commands_sent_before_teardown": sent,
        "teardown_signal": signal,
        "teardown_latency_ms": round(latency_ms, 1) if signal else None,
        "tx_failures_delta": after["tx_failures"] - before["tx_failures"],
        "telemetry_dropped_delta": after["telemetry_dropped"] - before["telemetry_dropped"],
        "controller_disconnects_delta": after["controller_disconnects"] - before["controller_disconnects"],
    }


# -------------------------------------------------- numeric 4: telemetry
def telemetry_streak(host, port, timeout_s=30.0):
    before = counters(host, port)
    time.sleep(0.2)
    p = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # Pin the receive buffer to disable macOS receive autotuning, otherwise the
    # kernel grows the buffer without bound and the TCP window never closes.
    p.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)
    pinned = p.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
    p.connect((host, port))
    if not drain_schema(p):
        p.close()
        return {"error": "no schema"}
    # stop reading entirely; let 50 ms telemetry fill the buffer then streak-fail
    t0 = time.monotonic()
    signal = None
    while time.monotonic() - t0 < timeout_s:
        signal = reset_detected(p)
        if signal:
            break
        time.sleep(0.05)
    latency_ms = (time.monotonic() - t0) * 1000.0
    p.close()
    time.sleep(0.3)
    after = counters(host, port)
    return {
        "pinned_rcvbuf_bytes": pinned,
        "teardown_signal": signal,
        "teardown_latency_ms": round(latency_ms, 1) if signal else None,
        "telemetry_dropped_delta": after["telemetry_dropped"] - before["telemetry_dropped"],
        "tx_failures_delta": after["tx_failures"] - before["tx_failures"],
        "controller_disconnects_delta": after["controller_disconnects"] - before["controller_disconnects"],
    }


def main(argv):
    if len(argv) < 1:
        print("usage: measure_transmit.py <host:port> [repeats]", file=sys.stderr)
        return 2
    host, port = parse_addr(argv[0])
    repeats = int(argv[1]) if len(argv) > 1 else 3
    out = {"host": host, "port": port, "numeric3_critical": [], "numeric4_telemetry": []}

    print(f"# Phase-18 transmit-path measurements against {host}:{port}\n")
    for i in range(repeats):
        r = burst_critical(host, port)
        out["numeric3_critical"].append(r)
        print(f"[critical] run {i+1}/{repeats}: {json.dumps(r)}")
        time.sleep(0.5)
    print()
    for i in range(repeats):
        r = telemetry_streak(host, port)
        out["numeric4_telemetry"].append(r)
        print(f"[telemetry] run {i+1}/{repeats}: {json.dumps(r)}")
        time.sleep(0.5)

    with open("tests/hardware/results/2026-06-16-phase18/transmit_raw.json", "w") as fh:
        json.dump(out, fh, indent=2)
    print("\nwrote tests/hardware/results/2026-06-16-phase18/transmit_raw.json")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
