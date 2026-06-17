#!/usr/bin/env python3
"""Manual conformance runner for command_server_conformance.

Live runs require a physical Teensy and produce PASS/FAIL/SKIP rows mapped to
contract section 28 plus a section 29 acceptance summary. The --self-test mode validates the
runner and parser against recorded fixtures only.
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from protocol_client import (
    BOARD_ID,
    BOARD_RX_LIMIT,
    BoardClient,
    ProtocolError,
    RecordedClient,
    assert_response_error,
    assert_response_ok,
    encode_line,
    parse_address,
)

ROOT = Path(__file__).resolve().parent
REGISTRATION_TIMEOUT_S = 2.0
LIVENESS_WINDOW_S = 0.250


@dataclass
class Result:
    section: str
    name: str
    status: str
    detail: str


class CheckContext:
    def __init__(self, address: str | None = None):
        self.address = address
        self.schema: dict | None = None
        self.counters_before: dict[str, int] = {}
        self.counters_after: dict[str, int] = {}

    def connect(self) -> BoardClient:
        if self.address is None:
            raise RuntimeError("live board address is required")
        host, port = parse_address(self.address)
        return BoardClient(host, port)


def pass_result(section: str, name: str, detail: str) -> Result:
    return Result(section, name, "PASS", detail)


def skip_result(section: str, name: str, detail: str) -> Result:
    return Result(section, name, "SKIP", detail)


def fail_result(section: str, name: str, detail: str) -> Result:
    return Result(section, name, "FAIL", detail)


def run_case(section: str, name: str, fn: Callable[[CheckContext], str], ctx: CheckContext) -> Result:
    try:
        return pass_result(section, name, fn(ctx))
    except (AssertionError, OSError, TimeoutError, ProtocolError, RuntimeError, ValueError) as exc:
        return fail_result(section, name, str(exc))


def require_schema(message: dict) -> dict:
    if message.get("type") != "schema":
        raise AssertionError(f"first message was not schema: {message!r}")
    if message.get("source") != BOARD_ID:
        raise AssertionError(f"schema source mismatch: {message!r}")
    if message.get("target") != "controller":
        raise AssertionError(f"schema target mismatch: {message!r}")
    if message.get("protocol_version") != "1":
        raise AssertionError(f"protocol version mismatch: {message!r}")
    schema = message.get("schema")
    if not isinstance(schema, dict):
        raise AssertionError("schema payload is not an object")
    commands = schema.get("commands")
    if not isinstance(commands, dict):
        raise AssertionError("schema.commands is not an object")
    required = {
        "test_echo",
        "test_oversized_result",
        "test_estop_success",
        "test_estop_fail",
        "test_estop_over_budget",
        "test_loss_over_budget",
        "test_telemetry_mode",
        "test_trigger_estop",
        "get_counters",
    }
    missing = sorted(required - set(commands))
    if missing:
        raise AssertionError(f"schema missing conformance commands: {missing}")
    for command_name, command_schema in commands.items():
        if not isinstance(command_schema, dict):
            raise AssertionError(f"{command_name}: command schema is not an object")
        if "blocked_by_estop" not in command_schema:
            raise AssertionError(f"{command_name}: blocked_by_estop not explicit")
        if not isinstance(command_schema["blocked_by_estop"], bool):
            raise AssertionError(f"{command_name}: blocked_by_estop is not bool")
        if not isinstance(command_schema.get("args"), dict):
            raise AssertionError(f"{command_name}: args is not an object")
    if schema.get("firmware_version") != "0.1.0":
        raise AssertionError(f"unexpected firmware version: {schema.get('firmware_version')!r}")
    return schema


def read_schema_first(ctx: CheckContext) -> str:
    client = ctx.connect()
    try:
        start = time.monotonic()
        message = client.read_message(REGISTRATION_TIMEOUT_S)
        elapsed = time.monotonic() - start
        ctx.schema = require_schema(message)
        if elapsed > REGISTRATION_TIMEOUT_S:
            raise AssertionError(f"schema arrived after {elapsed:.3f}s")
        telemetry = client.read_until(lambda msg: msg.get("type") == "telemetry", LIVENESS_WINDOW_S)
        if telemetry.get("source") != BOARD_ID or not isinstance(telemetry.get("telemetry"), dict):
            raise AssertionError(f"invalid first telemetry: {telemetry!r}")
        return f"schema first in {elapsed:.3f}s; telemetry within {LIVENESS_WINDOW_S:.3f}s"
    finally:
        client.close()


def command_dispatch(ctx: CheckContext) -> str:
    client = ctx.connect()
    try:
        require_schema(client.read_message(REGISTRATION_TIMEOUT_S))
        response = client.command("test_echo", {"value": 42}, controller_ts=1000.0)
        result = assert_response_ok(response, "test_echo")
        if result.get("value") != 42:
            raise AssertionError(f"echo result mismatch: {response!r}")
        if response.get("controller_ts") != 1000.0:
            raise AssertionError(f"controller_ts not echoed as sent: {response!r}")
        assert_response_error(client.command("does_not_exist"), "UNKNOWN_COMMAND", "unknown")
        assert_response_error(client.command("test_echo", {}), "MISSING_FIELD", "missing arg")
        assert_response_error(client.command("test_echo", {"value": "bad"}), "INVALID_TYPE", "bad type")
        assert_response_error(
            client.command("test_echo", {"value": 1, "extra": 2}),
            "INVALID_ARGUMENT",
            "extra arg",
        )
        counters = assert_response_ok(client.command("get_counters"), "get_counters")
        if counters.get("commands_ok", 0) < 1 or counters.get("commands_error", 0) < 4:
            raise AssertionError(f"counter movement missing: {counters!r}")
        ctx.counters_after = counters
        return "valid, unknown, missing, wrong-type, extra-arg, and counters checks passed"
    finally:
        client.close()


def heartbeat(ctx: CheckContext) -> str:
    client = ctx.connect()
    try:
        require_schema(client.read_message(REGISTRATION_TIMEOUT_S))
        ack = client.heartbeat(seq=77)
        if ack != {"type": "heartbeat", "seq": 77, "source": BOARD_ID, "target": "controller"}:
            raise AssertionError(f"heartbeat ack shape mismatch: {ack!r}")
        counters = assert_response_ok(client.command("get_counters"), "get_counters")
        if counters.get("heartbeat_received", 0) < 1 or counters.get("heartbeat_ack_sent", 0) < 1:
            raise AssertionError(f"heartbeat counters did not move: {counters!r}")
        return "heartbeat seq/source/target echoed and counters moved"
    finally:
        client.close()


def estop(ctx: CheckContext) -> str:
    client = ctx.connect()
    try:
        require_schema(client.read_message(REGISTRATION_TIMEOUT_S))
        assert_response_ok(client.command("test_estop_success"), "test_estop_success")
        ack = client.estop()
        if ack.get("details") != {"state": "safe"}:
            raise AssertionError(f"estop_ack details mismatch: {ack!r}")
        telemetry = client.read_until(lambda msg: msg.get("type") == "telemetry", LIVENESS_WINDOW_S)
        if not isinstance(telemetry.get("telemetry"), dict):
            raise AssertionError(f"telemetry did not continue after estop: {telemetry!r}")
        counters = assert_response_ok(client.command("get_counters"), "get_counters")
        if counters.get("estop_received", 0) < 1 or counters.get("estop_ack_sent", 0) < 1:
            raise AssertionError(f"estop counters did not move: {counters!r}")
        return "estop_ack is truthful safe shape and telemetry continues"
    finally:
        client.close()


def estop_failure_no_ack(ctx: CheckContext) -> str:
    client = ctx.connect()
    try:
        require_schema(client.read_message(REGISTRATION_TIMEOUT_S))
        assert_response_ok(client.command("test_estop_fail"), "test_estop_fail")
        client.send({"type": "estop", "source": "controller", "target": BOARD_ID})
        deadline = time.monotonic() + LIVENESS_WINDOW_S
        saw_telemetry = False
        while time.monotonic() < deadline:
            message = client.read_message(deadline - time.monotonic())
            if message.get("type") == "event" and message.get("event") == "estop_ack":
                raise AssertionError(f"hook failure produced false ack: {message!r}")
            if message.get("type") == "telemetry":
                saw_telemetry = True
                break
        if not saw_telemetry:
            raise AssertionError("telemetry did not resume inside liveness window")
        counters = assert_response_ok(client.command("get_counters"), "get_counters")
        if counters.get("estop_apply_failed", 0) < 1:
            raise AssertionError(f"estop_apply_failed did not move: {counters!r}")
        return "failed hook withheld ack and telemetry resumed"
    finally:
        client.close()


def board_originated_estop(ctx: CheckContext) -> str:
    client = ctx.connect()
    try:
        require_schema(client.read_message(REGISTRATION_TIMEOUT_S))
        assert_response_ok(client.command("test_trigger_estop"), "test_trigger_estop")
        event = client.read_until(
            lambda msg: msg.get("type") == "event" and msg.get("event") == "estop_triggered",
            timeout_s=2.0,
        )
        if event.get("details", {}).get("reason") != "fixture_requested":
            raise AssertionError(f"estop_triggered reason mismatch: {event!r}")
        return "board-originated estop_triggered emitted with fixture reason"
    finally:
        client.close()


def framing_and_limits(ctx: CheckContext) -> str:
    client = ctx.connect()
    try:
        require_schema(client.read_message(REGISTRATION_TIMEOUT_S))
        oversized = b'{"type":"command","seq":999,"controller_ts":1,"source":"controller","target":"' + (
            BOARD_ID.encode("utf-8")
        ) + b'","command":"test_echo","args":{"value":"' + (b"x" * BOARD_RX_LIMIT) + b'"}}\n'
        client.send_raw(oversized)
        response = client.command("test_echo", {"value": 7})
        result = assert_response_ok(response, "post-oversized test_echo")
        if result.get("value") != 7:
            raise AssertionError(f"valid line after oversized did not process: {response!r}")
        counters = assert_response_ok(client.command("get_counters"), "get_counters")
        if counters.get("oversized_lines", 0) < 1:
            raise AssertionError(f"oversized_lines did not move: {counters!r}")
        malformed = b'{"type":"command","seq":1000,"controller_ts":1,"source":"controller"\n'
        client.send_raw(malformed)
        counters_after = assert_response_ok(client.command("get_counters"), "get_counters")
        if counters_after.get("invalid_json", 0) <= counters.get("invalid_json", 0):
            raise AssertionError(f"invalid_json did not move: {counters_after!r}")
        return "oversized discard-through-newline and malformed JSON counters moved"
    finally:
        client.close()


def wrong_target(ctx: CheckContext) -> str:
    client = ctx.connect()
    try:
        require_schema(client.read_message(REGISTRATION_TIMEOUT_S))
        counters_before = assert_response_ok(client.command("get_counters"), "get_counters")
        client.send(
            {
                "type": "command",
                "seq": 7788,
                "controller_ts": 1.0,
                "source": "controller",
                "target": "not_this_board",
                "command": "test_echo",
                "args": {"value": 5},
            }
        )
        deadline = time.monotonic() + 0.25
        while time.monotonic() < deadline:
            try:
                message = client.read_message(deadline - time.monotonic())
            except TimeoutError:
                break
            if message.get("type") == "response" and message.get("seq") == 7788:
                raise AssertionError(f"wrong-target command received a response: {message!r}")
        counters_after = assert_response_ok(client.command("get_counters"), "get_counters")
        if counters_after.get("invalid_targets", 0) <= counters_before.get("invalid_targets", 0):
            raise AssertionError(f"invalid_targets did not move: {counters_after!r}")
        return "wrong target produced no response and invalid_targets moved"
    finally:
        client.close()


def supersession(ctx: CheckContext) -> str:
    first = ctx.connect()
    second = None
    try:
        require_schema(first.read_message(REGISTRATION_TIMEOUT_S))
        assert_response_ok(first.command("test_loss_over_budget", {"enabled": False}), "loss mode")
        second = ctx.connect()
        require_schema(second.read_message(REGISTRATION_TIMEOUT_S))
        counters = assert_response_ok(second.command("get_counters"), "get_counters")
        if counters.get("sessions_superseded", 0) < 1 or counters.get("controller_disconnects", 0) < 1:
            raise AssertionError(f"supersession counters did not move: {counters!r}")
        try:
            first.command("test_echo", {"value": 11}, timeout_s=0.25)
        except (ConnectionError, TimeoutError, OSError):
            pass
        else:
            raise AssertionError("superseded session still accepted commands")
        return "replacement connection got schema first and old session stopped"
    finally:
        first.close()
        if second is not None:
            second.close()


def telemetry_modes(ctx: CheckContext) -> str:
    client = ctx.connect()
    try:
        require_schema(client.read_message(REGISTRATION_TIMEOUT_S))
        assert_response_ok(client.command("test_telemetry_mode", {"mode": 0}), "telemetry normal")
        first = client.read_until(lambda msg: msg.get("type") == "telemetry", timeout_s=0.5)
        second = client.read_until(lambda msg: msg.get("type") == "telemetry", timeout_s=0.5)
        if second.get("seq", 0) <= first.get("seq", 0):
            raise AssertionError(f"telemetry seq did not increase: {first!r} then {second!r}")
        counters_before = assert_response_ok(client.command("get_counters"), "get_counters")
        assert_response_ok(client.command("test_telemetry_mode", {"mode": 1}), "telemetry oversized")
        deadline = time.monotonic() + 0.2
        while time.monotonic() < deadline:
            try:
                client.read_message(deadline - time.monotonic())
            except TimeoutError:
                break
        assert_response_ok(client.command("test_telemetry_mode", {"mode": 0}), "telemetry normal restore")
        counters_after = assert_response_ok(client.command("get_counters"), "get_counters")
        if counters_after.get("telemetry_dropped", 0) <= counters_before.get("telemetry_dropped", 0):
            raise AssertionError(f"oversized telemetry was not dropped/counted: {counters_after!r}")
        return "telemetry monotonic seq and oversized telemetry drop counter checked"
    finally:
        client.close()


LIVE_CASES: list[tuple[str, str, Callable[[CheckContext], str]]] = [
    ("28.2", "schema-first and telemetry-start window", read_schema_first),
    ("28.4", "schema and registration metadata", read_schema_first),
    ("28.5", "command dispatch and error responses", command_dispatch),
    ("28.9", "heartbeat acknowledgement", heartbeat),
    ("28.8", "software e-stop acknowledgement", estop),
    ("28.8", "e-stop hook failure withholds ack", estop_failure_no_ack),
    ("28.8", "board-originated e-stop event", board_originated_estop),
    ("28.3", "framing oversized and malformed input", framing_and_limits),
    ("28.5", "wrong target ignored and counted", wrong_target),
    ("28.2", "session supersession", supersession),
    ("28.7", "telemetry sequence and oversized drop", telemetry_modes),
]

SKIPS: list[Result] = [
    skip_result("28.1", "clean checkout builds", "covered by build_teensy.sh, not the live TCP client"),
    skip_result("28.1", "portable source and no generated paths", "covered by check_invariants.py"),
    skip_result("28.1", "QNEthernet version pin", "document/build audit item, not a live board message"),
    skip_result("28.2", "board starts safe before listening", "requires firmware/local hardware observation"),
    skip_result("28.2", "network/link loss variants", "requires physical link manipulation or firmware rig"),
    skip_result("28.2", "over-budget controller-loss hook still promotes", "requires timed firmware hook run"),
    skip_result("28.2", "estop as first inbound after reconnect", "requires controller-latched e-stop reconnect sequence"),
    skip_result("28.3", "fragmented command at every offset", "requires byte-splitting proxy or transport fixture"),
    skip_result("28.3", "multiple lines in one TCP read", "TCP coalescing is not deterministic from this client"),
    skip_result("28.3", "partial line at disconnect", "requires disconnect timing/proxy rig"),
    skip_result("28.3", "invalid UTF-8", "requires raw byte fixture; live JSON client sends UTF-8"),
    skip_result("28.4", "duplicate command registration fails", "startup-time fixture behavior, not reachable over TCP"),
    skip_result("28.5", "board-specific range error", "no range-limited conformance command exists in the sketch"),
    skip_result("28.5", "handler-returned board_proc_us overwrite", "requires a dedicated fixture command"),
    skip_result("28.5", "long-running action starts and returns", "requires hardware-specific command fixture"),
    skip_result("28.6", "peer stops reading / transmit deadline", "requires a non-reading TCP peer rig"),
    skip_result("28.6", "partial QNEthernet write internals", "requires instrumented transport or hardware trace"),
    skip_result("28.6", "concurrent telemetry and response byte interleaving", "requires transport capture/proxy rig"),
    skip_result("28.6", "ten consecutive telemetry deadline failures", "requires non-reading peer rig"),
    skip_result("28.6", "telemetry coalesces while write pending", "requires controlled write backpressure"),
    skip_result("28.7", "50 ms timing qualification", "recorded as hardware results in Phase 18"),
    skip_result("28.7", "telemetry getter non-blocking", "firmware implementation property, not wire-observable alone"),
    skip_result("28.7", "missed periods do not burst stale frames", "requires controlled telemetry delay fixture"),
    skip_result("28.8", "repeated e-stop is safe", "can be run manually with this client; not claimed without hardware"),
    skip_result("28.8", "over-budget hook timing qualification", "requires hardware timing run"),
    skip_result("28.8", "wrong-target estop", "covered by wrong-target dispatch shape only; hook non-invocation needs fixture observation"),
    skip_result("28.9", "malformed heartbeat", "requires raw malformed heartbeat variants; not run by default live suite"),
    skip_result("28.9", "wrong-target heartbeat", "same target validation mechanism as wrong-target command"),
    skip_result("28.10", "steady-state heap growth", "requires firmware memory instrumentation"),
    skip_result("28.10", "no unbounded queue growth", "requires long-duration/instrumented firmware run"),
    skip_result("28.10", "no network calls from ISR", "static/code-review property, not wire-observable"),
    skip_result("28.10", "reconnect resource leaks", "requires long-duration hardware run"),
    skip_result("28.10", "bounded logging", "requires firmware logging configuration audit"),
]

ACCEPTANCE_MAP: list[tuple[str, str, list[str]]] = [
    ("29.1", "builds portably", ["28.1"]),
    ("29.2", "exposes only registered commands", ["28.4", "28.5"]),
    ("29.3", "schema and dispatch table cannot drift", ["28.4", "28.5"]),
    ("29.4", "schema first on every connection", ["28.2"]),
    ("29.5", "frames and bounds NDJSON", ["28.3"]),
    ("29.6", "serialized bounded outbound writes", ["28.6"]),
    ("29.7", "partial reads and writes", ["28.3", "28.6"]),
    ("29.8", "non-blocking command dispatch", ["28.5"]),
    ("29.9", "push telemetry without starving liveness", ["28.7", "28.8"]),
    ("29.10", "honest software e-stop", ["28.8"]),
    ("29.11", "safe state on controller loss", ["28.2"]),
    ("29.12", "heartbeat acknowledgement", ["28.9"]),
    ("29.13", "no protocol networking from interrupts", ["28.10"]),
    ("29.14", "passes conformance tests", ["28.1", "28.2", "28.3", "28.4", "28.5", "28.6", "28.7", "28.8", "28.9", "28.10"]),
    ("29.15", "no conflict with frozen V1 contract", ["28.1", "28.4", "28.5", "28.8"]),
]


def section_status(results: list[Result], sections: list[str]) -> str:
    selected = [r for r in results if r.section in sections]
    if any(r.status == "FAIL" for r in selected):
        return "FAIL"
    if not selected or any(r.status == "SKIP" for r in selected):
        return "PARTIAL"
    return "PASS"


def print_results(results: list[Result]) -> None:
    for result in results:
        print(f"{result.status:4} {result.section:5} {result.name} - {result.detail}")
    print("")
    print("section 29 acceptance summary")
    for section, name, sources in ACCEPTANCE_MAP:
        print(f"{section:5} {section_status(results, sources):7} {name}")


def run_live(address: str) -> int:
    ctx = CheckContext(address)
    results: list[Result] = []
    for section, name, fn in LIVE_CASES:
        results.append(run_case(section, name, fn, ctx))
    results.extend(SKIPS)
    print_results(results)
    return 1 if any(result.status == "FAIL" for result in results) else 0


def self_test() -> int:
    fixture = (ROOT / "fixtures" / "recorded_session.ndjson").read_text(encoding="utf-8")
    client = RecordedClient.from_text(fixture)
    schema = require_schema(client.read_message())
    if "test_echo" not in schema["commands"]:
        raise AssertionError("fixture schema missing test_echo")
    telemetry = client.read_message()
    if telemetry.get("type") != "telemetry":
        raise AssertionError("fixture telemetry not parsed")
    response = client.read_message()
    result = assert_response_ok(response, "fixture test_echo")
    if result.get("value") != 42:
        raise AssertionError("fixture response value mismatch")
    event = client.read_message()
    if event.get("event") != "estop_triggered":
        raise AssertionError("fixture estop_triggered missing")
    ack = client.read_message()
    if ack.get("details") != {"state": "safe"}:
        raise AssertionError("fixture estop_ack safe shape missing")
    encoded = encode_line(
        {
            "type": "command",
            "seq": 1,
            "controller_ts": 1.0,
            "source": "controller",
            "target": BOARD_ID,
            "command": "test_echo",
            "args": {"value": 1},
        }
    )
    if len(encoded) > BOARD_RX_LIMIT or not encoded.endswith(b"\n"):
        raise AssertionError("encoded command line violates board RX limit")
    sample_results = [
        pass_result("28.2", "fixture schema", "ok"),
        skip_result("28.6", "fixture skip", "hardware only"),
    ]
    if section_status(sample_results, ["28.2"]) != "PASS":
        raise AssertionError("acceptance mapper failed PASS section")
    if section_status(sample_results, ["28.6"]) != "PARTIAL":
        raise AssertionError("acceptance mapper failed PARTIAL section")
    print("conformance self-test: parser, fixture, encoder, and summary mapper OK")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", nargs="?", help="real board address as <host:port>")
    parser.add_argument("--self-test", action="store_true", help="run parser/fixture checks only")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.address:
        parser.error("live conformance requires <board-ip:port>; use --self-test for fixture checks")
    return run_live(args.address)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
