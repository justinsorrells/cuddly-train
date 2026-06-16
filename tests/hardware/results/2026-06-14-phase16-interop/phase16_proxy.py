"""Transparent logging TCP proxy: real controller <-> real Teensy board.

Listens on 127.0.0.1:<lport>, forwards to <board>:<bport>, and logs every
newline-delimited JSON message in both directions. Telemetry frames are
counted (not dumped) to keep the transcript readable. The controller remains
the sole command authority; this only observes the wire.
"""

import asyncio
import json
import sys
import time

LPORT = 5052
BOARD = ("192.168.10.3", 5051)
START = time.time()
telem = {"B->C": 0}


def log(direction, raw):
    try:
        msg = json.loads(raw)
        mtype = msg.get("type")
    except Exception:
        print(f"[{time.time()-START:6.2f}s] {direction} (non-json) {raw[:80]!r}")
        return
    if mtype == "telemetry":
        telem[direction] = telem.get(direction, 0) + 1
        if telem[direction] % 20 == 1:
            print(f"[{time.time()-START:6.2f}s] {direction} telemetry "
                  f"(frame #{telem[direction]}) {json.dumps(msg.get('telemetry'))}")
        return
    label = mtype if mtype != "event" else f"event:{msg.get('event')}"
    print(f"[{time.time()-START:6.2f}s] {direction} {label}: {json.dumps(msg)}")


async def pump(reader, writer, direction):
    try:
        while True:
            line = await reader.readuntil(b"\n")
            log(direction, line.decode("utf-8", "replace").strip())
            writer.write(line)
            await writer.drain()
    except (asyncio.IncompleteReadError, ConnectionError):
        pass
    finally:
        writer.close()


async def handle(c_reader, c_writer):
    print(f"[{time.time()-START:6.2f}s] controller connected; dialing board {BOARD}")
    try:
        b_reader, b_writer = await asyncio.open_connection(*BOARD)
    except Exception as exc:
        print(f"board connect failed: {exc}")
        c_writer.close()
        return
    await asyncio.gather(
        pump(b_reader, c_writer, "B->C"),
        pump(c_reader, b_writer, "C->B"),
    )
    print(f"[{time.time()-START:6.2f}s] session closed "
          f"(telemetry frames B->C={telem.get('B->C', 0)})")


async def main():
    server = await asyncio.start_server(handle, "127.0.0.1", LPORT)
    print(f"proxy listening on 127.0.0.1:{LPORT} -> {BOARD[0]}:{BOARD[1]}")
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        sys.exit(0)
