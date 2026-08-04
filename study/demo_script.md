# Demo Script — Teensy Command Server (live walkthrough)

Audience: embedded developers joining the project.
Goal: they leave knowing how to **register a function**, **add telemetry**, and
why the **schema generates itself** — seen live on real hardware + the GUI.

Total: ~20 min core, +10 min optional under-the-hood.

---

## 0 · Pre-flight (do this BEFORE the audience is watching)

```bash
cd ~/cuddly-train
./tools/flash_demo.sh                       # flash demo_board (self-contained: sets toolchain PATH, auto-detects port)
python3 tools/demo_stack.py up              # redis + controller + dashboard
python3 tools/demo_stack.py status          # confirm demo_board REGISTERED / available
```

- [ ] Browser open at **http://127.0.0.1:8000/** — `demo_board` ONLINE, telemetry ~20 Hz
- [ ] `sketches/demo_board/demo_board.ino` open in your editor
- [ ] A terminal ready in `~/cuddly-train`
- [ ] LED visible to the room

Teardown when done: `python3 tools/demo_stack.py down`

---

## 1 · The big idea  (2 min · no code)

**SAY:** "Every board on the network runs a *command server*. The controller
only ever talks to a board through functions the board **registers**. The board
developer writes **zero** networking — no TCP, no JSON, no schema."

**SHOW:** the GUI. "This is a real Teensy on the bench. The schema, the 20 Hz
telemetry stream, the command console — the firmware author wrote none of that
plumbing. They wrote a handful of plain C++ functions."

---

## 2 · The sketch tour — the three acts  (5 min · `demo_board.ino`)

**SAY:** "A board dev only ever does three things."

- **A handler's three parameters** (open `cmdSetLed`):
  - `const CommandContext& command` → **what was asked** (`command.args.getBool(...)`)
  - `ObjectWriter& result` → **what you answer** (`result.addBool(...)`)
  - `void* context` → **your board's state** (`static_cast<BoardState*>(context)`)
  - return `CommandResult::ok()` / `::invalidArgument(...)` → **status** (only `ok`/`error`)

- **ACT 1 — register a command:** show `cmdSetLed` + its `registerCommand(...)`
  in `setup()`. Point at `ArgumentSpec{"on", Bool}` → "a `(name, type)` pair."

- **ACT 2 — telemetry:** show `writeTelemetry` + `registerTelemetrySchema(...)`.
  "Same `ObjectWriter`, same `context`. But nobody *calls* this — the library
  pushes it every 50 ms. That stream is the board's liveness signal."

- **ACT 3 — there is none:** "I never wrote a schema. Watch —"

```bash
# show the schema the board actually generated:
curl -s http://127.0.0.1:8000/api/schema | python3 -m json.tool | sed -n '/"schema"/,/firmware_version/p'
```
  **SAY:** "Nobody typed that JSON. It's the registrations from acts 1 and 2,
  serialized. The schema *is* the registry, printed."

---

## 3 · Fire commands live  (3 min · GUI command console)

Run each from the **Commands** panel; narrate the response envelope.

| Do | Watch | Say |
|---|---|---|
| `echo` value=123 | `ECHO VALUE` tile → 123 | "Response has `board_proc_us` — library-owned timing, ~9 µs. `board_seq` is the controller's sequence." |
| `set_led` on=true | LED on the bench | "That's the handler flipping a GPIO." |
| `set_blink` period_ms=300 | LED blinks, `MODE`→1 | "Handler just *records intent*; the `loop()` does the timed work — handlers never block." |
| `set_blink` period_ms=999999 | `error INVALID_ARGUMENT` | "Library validated structure; the **handler** owns domain range-checks." |

---

## 4 · Add a new function — the money shot  (5 min)

**SAY:** "Let's add a command live. I'll touch the sketch — never the library."

**Edit `demo_board.ino`** — add a handler next to the others:
```cpp
// toggle_led {} — flip the LED. Actuates hardware → blocked_by_estop = true.
api::CommandResult cmdToggleLed(const api::CommandContext& command,
                                api::ObjectWriter& result, void* context) {
    auto* state = static_cast<BoardState*>(context);
    state->led_on = !state->led_on;
    state->blink_period_ms = 0;
    digitalWriteFast(LED_BUILTIN, state->led_on ? HIGH : LOW);
    return result.addBool("led_on", state->led_on)
               ? api::CommandResult::ok()
               : api::CommandResult::internalError("result overflow");
}
```
Register it in `setup()` (no args → `nullptr, 0`, the write_67 lesson):
```cpp
(void)server.registerCommand({"toggle_led", nullptr, 0, /*blocked_by_estop=*/true},
                             cmdToggleLed, &board_state);
```
Then:
```bash
./tools/flash_demo.sh          # ~15s; board reboots, controller reconnects
```
**DO:** refresh the browser → a `toggle_led` button has appeared. Click it → LED flips.

**SAY:** "One handler, one `registerCommand`. The schema regenerated itself and
the controller picked it up on reconnect. That's the whole workflow."

---

## 5 · Add telemetry  (3 min)

**Edit `demo_board.ino`:**
- add to `BoardState`:            `std::int32_t blink_count = 0;`
- add to the `FieldSpec` array:   `{"blink_count", api::ValueType::Int},`  and bump the count `5 → 6`
- add to `writeTelemetry`:        `telemetry.addInt("blink_count", state->blink_count) &&`
- increment in `loop()` at the toggle: `board_state.blink_count++;`

```bash
./tools/flash_demo.sh
```
**DO:** refresh → `blink_count` in the Registers panel ticks up while blinking.

**SAY:** "Telemetry is the same shape: one `(name,type)` pair + one `addInt`.
Declare the pair, emit the value — done."

---

## 6 · Under the hood  (optional · 5 min)

- **The registry table → serialization:** each command is one row holding
  *public* columns (name, args, blocked_by_estop → the schema) **and** *private*
  columns (handler, context → dispatch). One row, two readers → schema can't
  drift from behavior (contract §5.3).
- **Dependency injection:** the sketch injects `QNEthernetNetworkServer` and
  `QNEthernetClock` (concrete) into the server, which only sees the abstract
  `core::NetworkServer` / `core::Clock`. On the bench = QNEthernet; in CI = fakes
  with a hand-advanced clock → the engine is 100% host-testable, no hardware.

---

## Recovery / gotchas

- **Board not found by `demo_stack`:** `arduino-cli board list` to confirm USB;
  `python3 tools/demo_stack.py discover`; set `DEMO_SUBNETS=192.168.10` if it's on
  the direct link. Reflash with `./tools/flash_demo.sh`.
- **Command shows in sketch but not on the board:** a `registerCommand` returned
  a non-`ok` Status that the `(void)` swallowed (the write_67 null-arg bug). Check
  the `ArgumentSpec`/`arg_count` match.
- **New firmware "not showing up":** confirm you ran `./tools/flash_demo.sh`
  (which uploads) and not `./tools/build_teensy.sh` (compile-only, no flash).
- **Board mid-reconnect after a flash:** wait ~5 s; `demo_stack status` should go
  back to REGISTERED.

## Command cheat card

```
./tools/flash_demo.sh                  # flash demo_board (compile + upload)
./tools/flash_demo.sh <sketch>         # flash a different sketch by name
./tools/build_teensy.sh                # compile ALL sketches (verify, no flash)
python3 tools/demo_stack.py up|status|down|discover
GUI:  http://127.0.0.1:8000/
```
