---
name: ndjson-framing
description: >
  Read before touching the line framer, message parser, or serializer.
  Incremental receive, fragmented lines, discard-through-newline, byte limits
  including the newline, and UTF-8/JSON failure behavior (contract §10).
---

# Newline-JSON Framing

## Limits (exact, both include the terminating `\n`)

* Inbound (controller→board): **1024 bytes**. The accumulator additionally
  reserves room for an implementation terminator that never goes on the wire.
* Outbound (board→controller): **8192 bytes**. Enforce at serialization time;
  an oversized line is never sent (§10.3, §11.2).

## Incremental receive (§10.4)

The framer must handle, in one code path:

* one message split across multiple TCP reads
* multiple complete messages in a single read
* a complete line followed by a partial next line (keep the partial)
* connection closure mid-line (discard the partial, never parse it)

TCP read boundaries are never message boundaries. The framer consumes raw
bytes and emits complete lines; nothing downstream sees partial data.

## Overflow: discard-through-newline (§10.5)

When 1024 bytes accumulate with no newline:

1. enter discard mode
2. throw away bytes through the next newline
3. increment `oversized_lines`
4. never parse or execute any prefix of the discarded line
5. resume normal framing on the byte after the newline

## Parse failures (§10.6)

* Invalid UTF-8 or malformed JSON: no handler runs, increment `invalid_json`,
  board stays up, **no response** (no trustworthy `seq` exists).
* Valid JSON, usable `seq`, invalid command structure: structured error
  response with the appropriate §17 code.
* Wrong `target`: drop + `invalid_targets`, no response, before any type
  dispatch (§3.1, §23).

## Serialization

* Compact JSON only — no pretty-printing, no inter-token whitespace —
  matching the controller's serializer.
* Exactly one `\n` per message, appended by the serializer, counted in the
  limit.
* Field presence rules come from the contract message shapes (§14–§22);
  on `ok` responses `error` is null/absent; on `error` responses `result` is
  null and `error.code`/`error.message` are required.

## Testing hooks

The framer takes bytes, not sockets — it must be fully exercisable from host
tests with hand-built byte sequences (see `host-conformance-testing`):
boundary sizes (1023/1024/1025), split at every offset of a small message,
oversized-then-valid, closure mid-line.
