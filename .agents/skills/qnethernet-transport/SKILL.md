---
name: qnethernet-transport
description: >
  Read before touching src/platform/qnethernet, sessions, sockets, or
  sketches. Single-session close-old supersession, schema-first, partial
  reads/writes, finite transmit deadline, flush/TCP_NODELAY, and the no-ISR
  rule (contract §9, §12, §13).
---

# QNEthernet Transport

`src/platform/qnethernet` is the **only** place QNEthernet/Arduino networking
types may appear (the invariant checker errors elsewhere). It adapts sockets
to the core transport interfaces; it makes no protocol decisions.

## Connection policy (§9)

* The board is the TCP server; the controller is the client and owns
  reconnect/backoff. The board's job is to keep listening.
* Exactly one active session. A replacement connection **supersedes** the old
  one (close-old), in this exact order (§9.1): mark old `SESSION_CLOSING` and
  stop its buffered commands → controller-loss handling → discard old
  parser/session state → close old socket → promote new connection → schema
  first → `sessions_superseded++`.
* Schema is sent immediately on accept — the controller FAULTs after 2 s
  without it. During an active e-stop the controller sends `estop` right
  after registration; it may be the first inbound message.

## Reads and writes

* Never trust one connection-status call to mean the input is gone (§24):
  distinguish open / bytes-available / closed / link-down.
* Writes go through the single serialized outbound path with priority order
  schema > estop_ack/safety > responses > heartbeat acks > telemetry (§13.4).
* Every write is bounded by the **transmit deadline** (default 100 ms, §13.6).
  Retry partial writes only until the deadline; service the network stack
  (`Ethernet.loop()`/`yield()`) between retries; never busy-wait. Raw
  unbounded `writeFully()` does not conform.
* Deadline expiry: critical message ⇒ count, close session, controller-loss
  handling. Telemetry ⇒ drop + count; 10 consecutive ⇒ close session.
* After each complete line: `flush()` (§13.2). On accept: `setNoDelay(true)`,
  log/count if it fails (§13.3).

## Servicing and ISRs (§12)

* The command-server `service` operation runs every loop iteration; no long
  busy-waits anywhere in firmware.
* **No network I/O, JSON work, or dispatch from an ISR.** Timers may set
  flags only.

## Failure mapping (§21, §26)

Map to controller-loss handling: TCP close, link loss, interface loss,
shutdown, critical-message deadline failure, supersession. After commitment:
discard unprocessed inbound bytes, no buffered command executes, return to
`LISTENING` when networking permits.
