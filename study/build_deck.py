#!/usr/bin/env python3
"""Build an Anki study deck for the Teensy Command Server walkthrough.

Writes two files next to this script:
  * teensy_command_server.tsv   — tab-separated (Front<TAB>Back<TAB>Tags),
                                   import via Anki: File -> Import.
  * teensy_command_server.apkg  — ready-to-open deck (only if genanki installed).

Field text avoids raw < > & so it renders whether or not Anki treats the field
as HTML. Line breaks in answers use <br>.
"""
from __future__ import annotations

from pathlib import Path

HERE = Path(__file__).resolve().parent
DECK_NAME = "Teensy Command Server"
BASE_TAG = "TeensyCommandServer"

# (front, back, category-tag)
CARDS: list[tuple[str, str, str]] = [
    # ── Architecture / mental model ──
    ("What kind of library is the Teensy Command Server?",
     "A copy-and-extend firmware template. A board dev copies it and registers plain functions as commands/telemetry/hooks; the library owns all networking: TCP, NDJSON framing, schema, telemetry push, e-stop, heartbeat.",
     "architecture"),
    ("What is the one rule for how the controller talks to a board?",
     "The controller only interacts with the board through registered functions. If a capability is not in the schema, it cannot be called.",
     "architecture"),
    ("Do board developers write any networking or JSON code?",
     "No. The library handles TCP, framing, JSON parse/serialize, schema, dispatch, response formatting, telemetry push, e-stop and heartbeat. Devs supply identity, registrations, handlers, telemetry values, and safe-state hooks.",
     "architecture"),
    ("How is the board's schema produced?",
     "It is GENERATED from the same registration metadata used for dispatch (contract 5.3), never hand-written. That prevents the declared schema from drifting from callable behavior.",
     "architecture"),
    ("Name the four source layers and their dependency direction.",
     "support (std lib only) then api (public types) then core (protocol engine) then platform (QNEthernet adapter). A layer may depend only on layers to its left.",
     "architecture"),
    ("What is the critical layering invariant?",
     "src/core and src/api never include QNEthernet or Arduino networking types. All platform I/O lives in src/platform/qnethernet. Time enters core via an injected Clock, I/O via an injected Transport, so core is host-testable.",
     "architecture"),
    ("What is the entire public surface of the library?",
     "src/TeensyCommandServer.h (the facade) plus src/api/*. Everything in src/core and src/platform is a private implementation detail.",
     "architecture"),
    ("Why fixed-capacity with no per-message heap allocation?",
     "The Teensy has no MMU and runs for days; heap fragmentation is a real failure mode (contract 11). All buffers are sized at compile time in src/support/Limits.h.",
     "fixed-capacity"),

    # ── Protocol ──
    ("What does 'schema-first' mean?",
     "The schema is the FIRST message the board sends on every controller connection, unsolicited. It advertises the board's commands, telemetry, and state.",
     "protocol"),
    ("List the wire message types.",
     "schema, command, response, telemetry, event (estop_triggered / estop_ack), and heartbeat.",
     "protocol"),
    ("How is telemetry delivered, and why does it matter?",
     "Unsolicited push about every 50 ms while a session is active; never requested. It IS the liveness signal: if it stops, the controller declares the board lost. It must keep flowing even during e-stop.",
     "protocol"),
    ("What are the only two board response statuses?",
     "Exactly ok and error. Never timeout. New failure modes are contract 17 error codes, never new statuses.",
     "protocol"),
    ("What is the wire framing format?",
     "Newline-delimited JSON (NDJSON): one compact JSON object per line, terminated by a newline.",
     "protocol"),
    ("What does the board do with controller_ts?",
     "Copies it into the response untouched: never interprets, compares, or persists it. It is a controller round-trip token and is not exposed to handlers.",
     "protocol"),

    # ── API value types / errors ──
    ("What are the four V1 argument/field value types?",
     "Int (signed 32-bit), Float (finite JSON number; reject NaN/Inf), Bool, String. No nested objects.",
     "api"),
    ("List the six contract 17 board error codes.",
     "MISSING_FIELD, INVALID_TYPE, UNKNOWN_COMMAND, INVALID_ARGUMENT, INTERNAL_ERROR, ESTOP_ACTIVE.",
     "error-codes"),
    ("Which error codes does the LIBRARY emit before a handler runs?",
     "MISSING_FIELD (declared arg absent), INVALID_TYPE (wrong JSON type), UNKNOWN_COMMAND (no such registered command), and INVALID_ARGUMENT for an undeclared extra argument.",
     "error-codes"),
    ("Which error code does the HANDLER own?",
     "INVALID_ARGUMENT for domain failures, e.g. a value outside the hardware-safe range. Structural validation is already done by the library.",
     "error-codes"),
    ("Why is there no CommandResult helper for UNKNOWN_COMMAND?",
     "Only the library dispatcher emits it, before any handler runs; a command that reaches a handler is by definition known. So there are five helpers for six codes.",
     "error-codes"),

    # ── Context pattern ──
    ("What is the void* context pattern?",
     "Every registration takes a void* context that the library hands back to the callback untouched. It lets a plain function pointer reach board state without name-globals or std::function/heap. Recover it with static_cast to your state type.",
     "api"),
    ("Why function pointers instead of std::function?",
     "To avoid heap allocation in the steady-state path. context carries the state, so no captures are needed.",
     "fixed-capacity"),

    # ── Registration functions ──
    ("setIdentity(...) — what does it do?",
     "Validates board_id, protocol_version, firmware_version are non-empty, then copies them into fixed library storage. board_id becomes 'source' on every outbound message and is how the controller routes to the board.",
     "functions"),
    ("registerCommand(spec, handler, context) — what does it do?",
     "Copies the command and arg names into the fixed registry; rejects duplicates (5.2) and over-capacity (16 cmds / 8 args) via a Status; wires handler+context for dispatch; and records the metadata that becomes the schema 'commands' entry (same data as dispatch, 5.3).",
     "functions"),
    ("What is in a CommandSpec?",
     "name, args (an ArgumentSpec array), arg_count, and blocked_by_estop. blocked_by_estop is explicit and required.",
     "functions"),
    ("registerTelemetrySchema(fields, count) vs setTelemetryProvider(fn, ctx)?",
     "The first DECLARES the shape (field names and types, into the schema telemetry section). The second SUPPLIES the values (the snapshot function pushed every 50 ms).",
     "functions"),
    ("setEstopHook / setControllerLossHook — what do they store?",
     "The two SafetyHook function pointers plus context. The e-stop hook runs when an estop message arrives; the loss hook when the controller link drops. Their return value gates the estop_ack.",
     "functions"),
    ("enableCountersDiagnosticCommand() — what does it do?",
     "Registers a built-in get_counters command exposing the contract 25 counters (dropped frames, oversized lines, over-budget hooks, bytes). It appears in the schema like any command.",
     "functions"),

    # ── Lifecycle ──
    ("What is the registration-to-serve sequence?",
     "construct, setIdentity, setNetworkConfig, registerCommand(s), registerTelemetrySchema, setTelemetryProvider, setEstopHook, setControllerLossHook, start(), then loop { service() }.",
     "lifecycle"),
    ("What are the four steps of start()?",
     "1) validate registration completeness; 2) generate the schema and verify it fits 8192 bytes; 3) commitSeal() so the registry becomes immutable; 4) construct the telemetry scheduler and session driver (placement-new, no heap) and open the TCP listener.",
     "lifecycle"),
    ("What happens if you call register*/set* after start()?",
     "It returns StatusCode::RegistrationSealed. Registration is immutable after start(); there is no dynamic command registration in V1.",
     "lifecycle"),
    ("What does service() do, and how often is it called?",
     "Called every loop iteration, non-blocking. In one pass it accepts/replaces the session, receives+frames+parses inbound lines, dispatches commands, pushes due telemetry, answers heartbeats, runs safety hooks, and flushes outbound writes under the transmit deadline.",
     "lifecycle"),
    ("Why must handlers and loop() never block?",
     "service() must keep turning; if it stalls, telemetry stops and the controller declares the board lost. Handlers record intent and return immediately; the loop does timed work.",
     "lifecycle"),

    # ── Handler contract ──
    ("What is the command handler signature?",
     "api::CommandResult (const api::CommandContext&, api::ObjectWriter& result, void* context). It is a raw function-pointer type (api::CommandHandler).",
     "functions"),
    ("What has the library already validated before a handler runs?",
     "The command name is registered, all declared args are present, and each arg's JSON type matches; undeclared extra args are rejected. The handler only does domain validation.",
     "functions"),
    ("What are the handler rules for blocking, networking, and allocation?",
     "Non-blocking (start the action and return), no networking inside the handler, no exceptions across the boundary, and no dynamic allocation.",
     "functions"),
    ("Who owns board_proc_us?",
     "The library. Handlers must not add it; ObjectWriter silently refuses a field named board_proc_us. The library times the handler and inserts it into the result object.",
     "functions"),

    # ── CommandArgs / ObjectWriter / CommandResult ──
    ("What are the CommandArgs accessors and their return value?",
     "has(name), and getInt/getFloat/getBool/getString(name, out). Each returns bool, false if the arg is absent or the wrong type.",
     "api"),
    ("What is the lifetime of a string obtained from CommandArgs?",
     "Valid ONLY during the current handler call (while the framer line is acquired). Copy out anything you keep; never retain a reference into the parsed document.",
     "api"),
    ("What are the ObjectWriter accessors and what does a false return mean?",
     "addInt / addUInt64 / addFloat / addBool / addString(name, value), each returns bool. false means it would not fit the fixed buffer; surface it as INTERNAL_ERROR. The library never sends a truncated line.",
     "api"),
    ("Why is ObjectWriter's capacity smaller than the 8 KB line?",
     "Its budget is the 8 KB line minus a reserved envelope (status, result/error structure, board_proc_us, escaping, newline), so a handler can never produce a result the library cannot wrap into a valid response.",
     "api"),
    ("What are the CommandResult factories?",
     "ok(); error(code, msg); and helpers missingField / invalidType / invalidArgument / internalError / estopActive. Error messages are copied into a fixed 160-byte buffer with an 'exceeds capacity' fallback.",
     "api"),

    # ── Telemetry ──
    ("What is the telemetry provider signature and its rules?",
     "bool (api::ObjectWriter& telemetry, void* context). It must be fast and non-blocking, with no sensor waits or networking. Copy the latest snapshot; return true to send the frame or false to skip it.",
     "functions"),
    ("How does the library schedule telemetry?",
     "On its own ~50 ms scheduler while a session is active, coalescing delayed periods to the latest snapshot. It is never solicited by a controller request.",
     "functions"),

    # ── Safety ──
    ("What is the SafetyHook signature and the return-value contract?",
     "bool (void* context). Return true ONLY when board-local safe state has actually been applied. estop_ack is sent only after the e-stop hook returns true; on false no ack. Hooks must be idempotent, within about a 100 ms budget.",
     "safety"),
    ("What is 'honest e-stop'?",
     "The board sends estop_ack with details.state exactly 'safe' only after the e-stop hook confirms safe state (returns true). On failure, no ack. There is no board-side software e-stop latch.",
     "safety"),
    ("What is blocked_by_estop, and who enforces the gating?",
     "Per-command metadata: true for anything that actuates hardware, false only for diagnostics safe during e-stop. E-stop gating is enforced by the CONTROLLER dispatcher, not the board. If absent, the controller treats it as blocked (fail-safe).",
     "safety"),
    ("May a board keep its own software e-stop latch?",
     "No. estop_reset is client-to-controller and never sent to boards, so a board latch could never be cleared. A board may reject with ESTOP_ACTIVE only for a live board-local hardware safety condition, never a latched prior estop.",
     "safety"),
    ("Name the two safety hooks and when each fires.",
     "on_estop_received (e-stop hook) fires on a controller-commanded e-stop; on_controller_lost (controller-loss hook) fires when the telemetry/heartbeat link drops. Same signature; both drive local safe state.",
     "safety"),

    # ── Limits / fixed-capacity ──
    ("What are the key fixed capacities?",
     "16 commands max, 8 args per command, 24 telemetry fields, 24 state fields, 1 KB inbound line, 8 KB outbound line. All constexpr in src/support/Limits.h; exceeding fails with CapacityExceeded, never realloc/truncate.",
     "limits"),
    ("What are the integer rules?",
     "seq is uint64. Contract int is int32 (validate range). float must be finite (reject NaN/Inf as INVALID_ARGUMENT). Durations use unsigned millis()/micros() subtraction so wraparound stays correct.",
     "fixed-capacity"),
    ("What is the inbound line-overflow behavior (LineFramer)?",
     "Discard through the next newline, increment the oversized_lines counter, and recover on the next valid line. Never a truncated parse.",
     "fixed-capacity"),
    ("What is the outbound overflow behavior?",
     "Never send a truncated line: a response becomes a compact INTERNAL_ERROR; telemetry is dropped and counted (contract 11.2).",
     "fixed-capacity"),
    ("What are the two line-length limits and why do they differ?",
     "Controller-to-board is 1 KB (keep command args small and flat); board-to-controller is 8 KB, because the full schema is sent outbound and must fit the controller receive limit.",
     "limits"),

    # ── Verification / conventions ──
    ("What are the four verification commands?",
     "python3 tools/check_invariants.py (layering guardrails); python3 tools/check_contract_sync.py (contract hashes); ./tools/run_host_tests.sh (host C++ tests); ./tools/build_teensy.sh (Teensy compile). Hardware tests are manual only.",
     "verification"),
    ("How is the architecture enforced, not just by convention?",
     "check_invariants.py fails the build if core/api include QNEthernet, if DynamicJsonDocument appears, and similar; contracts are hash-pinned by check_contract_sync.py.",
     "verification"),
    ("What are the naming conventions?",
     "Types PascalCase; methods/functions camelCase; constants kPascalCase (or UPPER_SNAKE for contract-vocabulary strings); members trailing_underscore_; files PascalCase.h, one concern per pair.",
     "conventions"),
    ("What is the comment-style rule?",
     "Comments state constraints the code cannot show (e.g. a contract ref like '13.6: deadline-bounded'), never narrate the code.",
     "conventions"),
    ("What is the error-handling style across the library boundary?",
     "No C++ exceptions and no RTTI; errors are return values: Status enums for registration/config, CommandResult for handlers.",
     "conventions"),

    # ── Demo stack ──
    ("How does the demo stack find a board?",
     "/demo up (tools/demo_stack.py) scans the subnet for anything answering TCP :5051 with a schema-first frame, then starts Redis, the special-lamp controller, and the dashboard GUI at http://127.0.0.1:8000/.",
     "demo"),
    ("What makes a board discoverable by the demo stack?",
     "start() opens the :5051 listener and the board sends its schema as the first line on connect, which is exactly what the discovery probe matches.",
     "demo"),
]


def write_tsv() -> Path:
    out = HERE / "teensy_command_server.tsv"
    lines = []
    for front, back, cat in CARDS:
        tags = f"{BASE_TAG} {BASE_TAG}::{cat.replace('-', '_')}"
        # guard against structural characters
        assert "\t" not in front and "\t" not in back
        assert "\n" not in front and "\n" not in back
        lines.append(f"{front}\t{back}\t{tags}")
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return out


def write_apkg() -> Path | None:
    try:
        import genanki  # type: ignore
    except ImportError:
        return None

    model = genanki.Model(
        1607392319,  # stable, arbitrary
        "TCS Basic",
        fields=[{"name": "Front"}, {"name": "Back"}],
        templates=[{
            "name": "Card 1",
            "qfmt": "{{Front}}",
            "afmt": '{{FrontSide}}<hr id="answer">{{Back}}',
        }],
        css=(".card{font-family:-apple-system,Segoe UI,Roboto,sans-serif;"
             "font-size:19px;line-height:1.5;color:#222;background:#fff;"
             "text-align:left;max-width:44rem;margin:1.2rem auto;padding:0 1rem;}"
             "hr#answer{border:0;border-top:2px solid #d0d0d0;margin:1rem 0;}"),
    )
    deck = genanki.Deck(2059400110, DECK_NAME)
    for front, back, cat in CARDS:
        deck.add_note(genanki.Note(
            model=model,
            fields=[front, back],
            tags=[BASE_TAG, f"{BASE_TAG}::{cat.replace('-', '_')}"],
        ))
    out = HERE / "teensy_command_server.apkg"
    genanki.Package(deck).write_to_file(str(out))
    return out


def main() -> None:
    tsv = write_tsv()
    print(f"cards: {len(CARDS)}")
    print(f"wrote {tsv}")
    apkg = write_apkg()
    if apkg:
        print(f"wrote {apkg}")
    else:
        print("genanki not installed - skipped .apkg (import the .tsv instead)")


if __name__ == "__main__":
    main()
