#!/usr/bin/env python3
"""Controller-side NDJSON protocol client for manual conformance runs.

The live path speaks to a real Teensy running command_server_conformance.
The fixture path feeds recorded lines to the same parsers without claiming
hardware conformance.
"""

from __future__ import annotations

import json
import socket
import time
from dataclasses import dataclass, field
from typing import Callable, Iterable

BOARD_ID = "command_server_conformance"
CONTROLLER = "controller"
BOARD_RX_LIMIT = 1024
BOARD_TX_LIMIT = 8192


class ProtocolError(Exception):
    """Raised when the board stream violates the protocol expected by tests."""


def encode_line(message: dict, limit: int = BOARD_RX_LIMIT) -> bytes:
    line = json.dumps(message, separators=(",", ":"), sort_keys=False).encode("utf-8") + b"\n"
    if len(line) > limit:
        raise ProtocolError(f"outbound line is {len(line)} bytes; limit is {limit}")
    return line


def decode_line(line: bytes, limit: int = BOARD_TX_LIMIT) -> dict:
    if len(line) > limit:
        raise ProtocolError(f"inbound line is {len(line)} bytes; limit is {limit}")
    if not line.endswith(b"\n"):
        raise ProtocolError("inbound line is not newline terminated")
    try:
        parsed = json.loads(line[:-1].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ProtocolError(f"inbound line is not valid UTF-8 JSON: {exc}") from exc
    if not isinstance(parsed, dict):
        raise ProtocolError("inbound line is not a JSON object")
    return parsed


def parse_address(address: str) -> tuple[str, int]:
    if ":" not in address:
        raise ValueError("address must be <host:port>")
    host, port_text = address.rsplit(":", 1)
    if not host:
        raise ValueError("address host is empty")
    try:
        port = int(port_text, 10)
    except ValueError as exc:
        raise ValueError("address port must be an integer") from exc
    if port <= 0 or port > 65535:
        raise ValueError("address port is outside 1..65535")
    return host, port


@dataclass
class RecordedClient:
    """Parser-only client used by --self-test fixtures."""

    lines: list[bytes]
    sent: list[bytes] = field(default_factory=list)

    @classmethod
    def from_text(cls, text: str) -> "RecordedClient":
        return cls([line.encode("utf-8") for line in text.splitlines(keepends=True) if line])

    def send(self, message: dict) -> None:
        self.sent.append(encode_line(message))

    def read_message(self, timeout_s: float = 0.0) -> dict:
        del timeout_s
        if not self.lines:
            raise TimeoutError("recorded fixture exhausted")
        return decode_line(self.lines.pop(0))


class BoardClient:
    def __init__(self, host: str, port: int, connect_timeout_s: float = 2.0):
        self.host = host
        self.port = port
        self.socket = socket.create_connection((host, port), timeout=connect_timeout_s)
        self.socket.settimeout(0.05)
        self.buffer = bytearray()
        self.next_seq = 1
        self.unsolicited: list[dict] = []

    def close(self) -> None:
        try:
            self.socket.close()
        except OSError:
            pass

    def send(self, message: dict) -> None:
        self.socket.sendall(encode_line(message))

    def read_message(self, timeout_s: float = 2.0) -> dict:
        deadline = time.monotonic() + timeout_s
        while True:
            newline = self.buffer.find(b"\n")
            if newline >= 0:
                raw = bytes(self.buffer[: newline + 1])
                del self.buffer[: newline + 1]
                return decode_line(raw)
            if len(self.buffer) >= BOARD_TX_LIMIT:
                raise ProtocolError("inbound line exceeded board-to-controller limit")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("timed out waiting for board message")
            self.socket.settimeout(min(0.05, remaining))
            try:
                chunk = self.socket.recv(512)
            except socket.timeout:
                continue
            if not chunk:
                raise ConnectionError("board closed the connection")
            self.buffer.extend(chunk)

    def read_until(
        self,
        predicate: Callable[[dict], bool],
        timeout_s: float = 2.0,
    ) -> dict:
        deadline = time.monotonic() + timeout_s
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("timed out waiting for expected board message")
            message = self.read_message(remaining)
            if predicate(message):
                return message
            self.unsolicited.append(message)

    def command(
        self,
        name: str,
        args: dict | None = None,
        timeout_s: float = 2.0,
        controller_ts: float | None = None,
    ) -> dict:
        seq = self.next_seq
        self.next_seq += 1
        sent_ts = time.monotonic() if controller_ts is None else controller_ts
        self.send(
            {
                "type": "command",
                "seq": seq,
                "controller_ts": sent_ts,
                "source": CONTROLLER,
                "target": BOARD_ID,
                "command": name,
                "args": {} if args is None else args,
            }
        )
        response = self.read_until(
            lambda msg: msg.get("type") == "response" and msg.get("seq") == seq,
            timeout_s,
        )
        response["_expected_controller_ts"] = sent_ts
        return response

    def heartbeat(self, seq: int = 9001, timeout_s: float = 1.0) -> dict:
        self.send({"type": "heartbeat", "seq": seq, "source": CONTROLLER, "target": BOARD_ID})
        return self.read_until(
            lambda msg: msg.get("type") == "heartbeat" and msg.get("seq") == seq,
            timeout_s,
        )

    def estop(self, timeout_s: float = 2.0) -> dict:
        self.send({"type": "estop", "source": CONTROLLER, "target": BOARD_ID})
        return self.read_until(
            lambda msg: msg.get("type") == "event" and msg.get("event") == "estop_ack",
            timeout_s,
        )

    def send_raw(self, payload: bytes) -> None:
        self.socket.sendall(payload)


def assert_response_ok(response: dict, command: str) -> dict:
    if response.get("type") != "response":
        raise AssertionError(f"{command}: expected response, got {response!r}")
    if response.get("status") != "ok":
        raise AssertionError(f"{command}: expected ok, got {response!r}")
    if response.get("source") != BOARD_ID or response.get("target") != CONTROLLER:
        raise AssertionError(f"{command}: wrong response source/target: {response!r}")
    result = response.get("result")
    if not isinstance(result, dict):
        raise AssertionError(f"{command}: ok response result is not an object")
    if "board_proc_us" not in result:
        raise AssertionError(f"{command}: board_proc_us missing from result")
    return result


def assert_response_error(response: dict, code: str, command: str) -> dict:
    if response.get("type") != "response":
        raise AssertionError(f"{command}: expected response, got {response!r}")
    if response.get("status") != "error":
        raise AssertionError(f"{command}: expected error, got {response!r}")
    error = response.get("error")
    if not isinstance(error, dict) or error.get("code") != code:
        raise AssertionError(f"{command}: expected {code}, got {response!r}")
    return error


def messages_from_lines(lines: Iterable[str]) -> list[dict]:
    return [decode_line(line.encode("utf-8")) for line in lines if line]
