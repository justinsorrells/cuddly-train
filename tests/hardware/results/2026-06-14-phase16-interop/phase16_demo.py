"""Phase-16 interop demo driver: real controller <-> real Teensy board.

Drives the five Phase-16 bullets from a Unix-socket client. The transparent
proxy transcript is the wire-level evidence for telemetry and the e-stop
handshake. Run with PYTHONPATH=<special-lamp repo root>.
"""

import asyncio
import json
import socket
import time

from demos.client.client import UnixSocketDemoClient

SOCK = "/tmp/hyperloop-controller.sock"
BOARD = "command_server_conformance"
BOARD_ADDR = ("192.168.10.3", 5051)


def rec(tag, obj=None):
    print(f"\n=== {tag} ===" if obj is None else f"{tag}: {json.dumps(obj)}")


async def board_info(client):
    seq = await client.send_command(target="controller", command="get_schemas")
    resp = await client.read_response(seq, timeout_s=3)
    boards = resp.get("result", {}).get("boards", [])
    return boards[0] if boards else None


async def main():
    client = UnixSocketDemoClient(socket_path=SOCK, source="phase16_demo")
    await client.connect()

    rec("BULLET 1: schema-first registration (controller view)")
    b = await board_info(client)
    rec("  board", {k: b[k] for k in ("board_id", "conn_state", "available",
        "protocol_version", "firmware_version", "schema_revision")})
    assert b["conn_state"] == "REGISTERED"

    rec("BULLET 2: Unix-socket command -> controller -> Teensy -> response")
    seq = await client.send_command(target=BOARD, command="test_echo", args={"value": 42})
    resp = await client.read_response(seq, timeout_s=3)
    rec("  response", resp)
    assert resp["status"] == "ok" and resp["result"]["value"] == 42 and resp["seq"] == seq

    rec("BULLET 3: telemetry to controller (see proxy 'B->C telemetry' frames); hold 2s")
    await asyncio.sleep(2.0)
    b = await board_info(client)
    assert b["conn_state"] == "REGISTERED", "liveness dropped -> telemetry not arriving"
    rec("  still REGISTERED under 0.25s liveness", {"conn_state": b["conn_state"]})

    rec("BULLET 5: software e-stop (see proxy estop_triggered -> estop -> estop_ack)")
    seq = await client.send_command(target=BOARD, command="test_trigger_estop")
    resp = await client.read_response(seq, timeout_s=3)
    rec("  trigger response", resp)
    await asyncio.sleep(1.5)  # let the estop handshake complete on the wire
    seq = await client.send_estop_reset()
    resp = await client.read_response(seq, timeout_s=3)
    rec("  estop_reset response", resp)

    rec("BULLET 4: disconnect/reconnect + schema re-registration")
    rev_before = (await board_info(client))["schema_revision"]
    rec("  schema_revision before", rev_before)
    intruder = socket.create_connection(BOARD_ADDR, timeout=3)  # bump controller off board
    await asyncio.sleep(1.0)
    intruder.close()
    reregistered = False
    for _ in range(24):
        await asyncio.sleep(0.5)
        b = await board_info(client)
        if b["conn_state"] == "REGISTERED" and b["available"]:
            reregistered = True
            rec("  re-registered", {"conn_state": b["conn_state"],
                "schema_revision": b["schema_revision"]})
            break
    assert reregistered, "controller did not re-register the board"

    rec("ALL FIVE PHASE-16 BULLETS EXERCISED")
    await client.close()


if __name__ == "__main__":
    asyncio.run(main())
