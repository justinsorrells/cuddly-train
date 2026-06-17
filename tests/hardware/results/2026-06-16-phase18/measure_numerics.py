#!/usr/bin/env python3
"""Phase-18 hardware measurement rig for the four contract §31 numerics.

Drives the live command_server_conformance firmware over TCP and measures the
real Teensy/QNEthernet behaviour behind each provisional numeric:

  1. 100 ms e-stop hook budget            (§19.1)
  2. 100 ms controller-loss hook budget   (§21)
  3. 100 ms transmit deadline             (§13.6)
  4. 10-frame telemetry teardown          (§13.6)

Never simulates: every number printed comes off the board. Run:

    python3 measure_numerics.py <host:port> [repeats]
"""
from __future__ import annotations

import errno
import json
import select
import socket
import statistics
import sys
import time

sys.path.insert(0, "tests/conformance")
from protocol_client import BoardClient, assert_response_ok  # noqa: E402

OVER_BUDGET_HOOK_DELAY_MS = 150  # kOverBudgetDelayMs in the conformance sketch
BUDGET_MS = 100                  # kHookBudgetMs / kTransmitDeadlineMs (§31)
TEARDOWN_FAILURES = 10           # kTelemetryDeadlineFailuresToTeardown (§31)


def parse_addr(text):
    host, port = text.rsplit(":", 1)
    return host, int(port)


# ---------------------------------------------------------------- numeric 1
def measure_estop_budget(host, port):
    c = BoardClient(host, port)
    c.read_message()  # schema
    before = assert_response_ok(c.command("get_counters"), "gc")

    # fast (in-budget) hook
    assert_response_ok(c.command("test_estop_success"), "estop_success")
    t0 = time.monotonic()
    ack_fast = c.estop()
    fast_ms = (time.monotonic() - t0) * 1000.0
    mid = assert_response_ok(c.command("get_counters"), "gc")

    # over-budget (150 ms) hook
    assert_response_ok(c.command("test_estop_over_budget"), "estop_over_budget")
    t0 = time.monotonic()
    ack_ob = c.estop()
    ob_ms = (time.monotonic() - t0) * 1000.0
    after = assert_response_ok(c.command("get_counters"), "gc")
    c.close()

    return {
        "fast_ack_latency_ms": round(fast_ms, 2),
        "fast_ack_details": ack_fast.get("details"),
        "fast_over_budget_counter_delta": mid["estop_hook_over_budget"] - before["estop_hook_over_budget"],
        "overbudget_ack_latency_ms": round(ob_ms, 2),
        "overbudget_ack_details": ack_ob.get("details"),
        "overbudget_counter_delta": after["estop_hook_over_budget"] - mid["estop_hook_over_budget"],
        "estop_ack_sent_delta": after["estop_ack_sent"] - before["estop_ack_sent"],
    }


# ---------------------------------------------------------------- numeric 2
def measure_loss_budget(host, port):
    a = BoardClient(host, port)
    a.read_message()  # schema
    before = assert_response_ok(a.command("get_counters"), "gc")
    assert_response_ok(a.command("test_loss_over_budget", {"enabled": True}), "enable_loss_ob")

    # Supersede A by opening B; applyPendingTeardown() runs the controller-loss
    # hook (150 ms) before promoting B and sending B's schema.
    t0 = time.monotonic()
    b = BoardClient(host, port)
    b_schema = b.read_message()
    repl_latency_ms = (time.monotonic() - t0) * 1000.0

    after = assert_response_ok(b.command("get_counters"), "gc")
    assert_response_ok(b.command("test_loss_over_budget", {"enabled": False}), "disable_loss_ob")
    try:
        a.close()
    except OSError:
        pass
    b.close()

    return {
        "replacement_got_schema_first": b_schema.get("type") == "schema",
        "replacement_schema_latency_ms": round(repl_latency_ms, 2),
        "loss_over_budget_counter_delta": after["controller_loss_hook_over_budget"]
        - before["controller_loss_hook_over_budget"],
        "sessions_superseded_delta": after["sessions_superseded"] - before["sessions_superseded"],
        "controller_disconnects_delta": after["controller_disconnects"] - before["controller_disconnects"],
    }


# ----------------------------------------------------------- numerics 3 & 4
def _wait_for_reset(sock, timeout_s):
    """Detect an incoming RST/FIN WITHOUT draining the recv buffer (draining
    would re-open the TCP window and un-wedge the board)."""
    start = time.monotonic()
    while time.monotonic() - start < timeout_s:
        err = sock.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
        if err != 0:
            return time.monotonic() - start, errno.errorcode.get(err, str(err))
        r, _, x = select.select([sock], [], [sock], 0.05)
        if x:
            return time.monotonic() - start, "select-exception"
        if r:
            try:
                peek = sock.recv(1, socket.MSG_PEEK)  # peek does not consume
            except ConnectionResetError:
                return time.monotonic() - start, "ECONNRESET"
            except OSError as exc:
                return time.monotonic() - start, f"OSError:{exc.errno}"
            if peek == b"":
                return time.monotonic() - start, "FIN"
            # buffered telemetry ahead of any reset; keep wedging, don't drain
        time.sleep(0.02)
    return None, "timeout"


def measure_wedge(host, port, drain_schema, timeout_s=12.0):
    # Baseline counters on a throwaway connection.
    base_c = BoardClient(host, port)
    base_c.read_message()
    before = assert_response_ok(base_c.command("get_counters"), "gc")
    base_c.close()
    time.sleep(0.2)

    p = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    p.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)  # kernel clamps to min
    p.connect((host, port))
    effective_rcvbuf = p.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)

    schema_drained = False
    if drain_schema:
        # read exactly the schema line, then stop reading forever
        p.settimeout(2.0)
        buf = b""
        while b"\n" not in buf:
            chunk = p.recv(4096)
            if not chunk:
                break
            buf += chunk
        schema_drained = b"\n" in buf

    t0 = time.monotonic()
    elapsed, reason = _wait_for_reset(p, timeout_s)
    try:
        p.close()
    except OSError:
        pass
    time.sleep(0.3)

    after_c = BoardClient(host, port)
    after_c.read_message()
    after = assert_response_ok(after_c.command("get_counters"), "gc")
    after_c.close()

    return {
        "drain_schema_first": drain_schema,
        "schema_drained": schema_drained,
        "effective_rcvbuf_bytes": effective_rcvbuf,
        "teardown_detected": elapsed is not None,
        "teardown_latency_ms": round(elapsed * 1000.0, 1) if elapsed is not None else None,
        "teardown_signal": reason,
        "telemetry_dropped_delta": after["telemetry_dropped"] - before["telemetry_dropped"],
        "tx_failures_delta": after["tx_failures"] - before["tx_failures"],
        "controller_disconnects_delta": after["controller_disconnects"] - before["controller_disconnects"],
    }


def main(argv):
    if len(argv) < 1:
        print("usage: measure_numerics.py <host:port> [repeats]", file=sys.stderr)
        return 2
    host, port = parse_addr(argv[0])
    repeats = int(argv[1]) if len(argv) > 1 else 3
    out = {"host": host, "port": port, "repeats": repeats,
           "constants": {"over_budget_hook_delay_ms": OVER_BUDGET_HOOK_DELAY_MS,
                         "budget_ms": BUDGET_MS, "teardown_failures": TEARDOWN_FAILURES}}

    print(f"# Phase-18 §31 hardware measurements against {host}:{port}\n")

    def run_many(label, fn):
        rows = []
        for i in range(repeats):
            r = fn()
            rows.append(r)
            print(f"[{label}] run {i+1}/{repeats}: {json.dumps(r)}")
            time.sleep(0.4)
        return rows

    out["estop_hook_budget"] = run_many("estop_budget", lambda: measure_estop_budget(host, port))
    print()
    out["loss_hook_budget"] = run_many("loss_budget", lambda: measure_loss_budget(host, port))
    print()
    out["telemetry_teardown"] = run_many("wedge(drain_schema)", lambda: measure_wedge(host, port, True))
    print()
    out["transmit_deadline_nodrain"] = run_many("wedge(no_drain)", lambda: measure_wedge(host, port, False))

    # ---- derived summary ----
    def median(rows, key):
        vals = [r[key] for r in rows if r.get(key) is not None]
        return round(statistics.median(vals), 1) if vals else None

    summary = {
        "estop_overbudget_ack_latency_ms_median": median(out["estop_hook_budget"], "overbudget_ack_latency_ms"),
        "estop_fast_ack_latency_ms_median": median(out["estop_hook_budget"], "fast_ack_latency_ms"),
        "loss_replacement_schema_latency_ms_median": median(out["loss_hook_budget"], "replacement_schema_latency_ms"),
        "teardown_latency_ms_median": median(out["telemetry_teardown"], "teardown_latency_ms"),
        "telemetry_dropped_delta_median": median(out["telemetry_teardown"], "telemetry_dropped_delta"),
    }
    out["summary"] = summary
    print("\n# summary\n" + json.dumps(summary, indent=2))

    with open("tests/hardware/results/2026-06-16-phase18/numerics_raw.json", "w") as fh:
        json.dump(out, fh, indent=2)
    print("\nwrote tests/hardware/results/2026-06-16-phase18/numerics_raw.json")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
