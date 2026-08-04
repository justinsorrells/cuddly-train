// demo_board.ino — a minimal, heavily-annotated board for the live walkthrough.
//
// It is a *thin consumer* of the command-server library: it writes no
// networking, no JSON, and no schema by hand. It only does the three things a
// board developer ever does:
//
//   ACT 1  register COMMANDS  — plain functions the controller may call
//   ACT 2  register TELEMETRY — a fast snapshot the board pushes every 50 ms
//   ACT 3  (there is no act 3) — the SCHEMA is generated from acts 1 & 2 and
//                                sent, unsolicited, as the first line on every
//                                controller connection (contract §5.3).
//
// Hardware effect you can see on the bench: commands drive the Teensy 4.1
// on-board LED (pin 13); telemetry reports uptime + LED state so the dashboard
// GUI updates live.
//
// Bring-up:  ./tools/build_teensy.sh  &&  flash this sketch, then `/demo up`.
// The demo stack discovers any board that answers TCP :5051 with a schema
// frame — which is exactly what start() below makes this board do.

#include <Arduino.h>                             // millis(), pinMode, LED_BUILTIN
#include <TeensyCommandServer.h>                 // the ONE library header sketches include (the facade)
#include <platform/qnethernet/Platform.h>       // the QNEthernet adapter (the only place Arduino networking lives)

#include <cstdint>

// Short aliases so the API reads cleanly below.
namespace tcs = teensy_command_server;
namespace api = teensy_command_server::api;
namespace qnp = teensy_command_server::platform::qnethernet;

// ─────────────────────────────────────────────────────────────────────────────
// Board configuration
// ─────────────────────────────────────────────────────────────────────────────
constexpr std::uint16_t kListenPort = 5051;      // demo stack discovers boards on :5051
constexpr std::int32_t  kMaxBlinkPeriodMs = 5000;

// DHCP: the four IPv4 byte-arrays are ignored (see NetworkConfig). This struct
// is plain bytes/scalars on purpose — the public API never exposes an
// Arduino IPAddress or any QNEthernet type.
api::NetworkConfig network_config{
    api::NetworkConfig::Mode::Dhcp,
    kListenPort,
    {0, 0, 0, 0},   // ip      (ignored for DHCP)
    {0, 0, 0, 0},   // gateway (ignored for DHCP)
    {0, 0, 0, 0},   // subnet  (ignored for DHCP)
};

// ─────────────────────────────────────────────────────────────────────────────
// Board state — the "context" pattern.
//
// Every registration below takes a `void* context` that the library hands
// straight back to the callback, untouched. That is how you reach board state
// from a plain function pointer WITHOUT globals-by-name, and without
// std::function/heap allocation. We pass &board_state everywhere; each callback
// does `static_cast<BoardState*>(context)`. This is the single source of truth
// shared by the command handlers, the telemetry provider, and the safety hooks.
// ─────────────────────────────────────────────────────────────────────────────
struct BoardState {
    bool led_on = false;
    std::int32_t blink_period_ms = 0;            // 0 = solid (no blink); >0 = toggle every N ms
    std::uint32_t last_toggle_ms = 0;
    std::int32_t echo_value = 0;
};

// These are global objects with static lifetime on purpose: no `new`, no heap.
// The library never allocates per message either (contract §11) — everything it
// needs is sized at compile time in src/support/Limits.h.
BoardState board_state;
qnp::QNEthernetNetworkServer network(network_config);   // the transport (real sockets)
qnp::QNEthernetClock         platform_clock;            // injected time source (micros/millis)
tcs::TeensyCommandServer     server(network, platform_clock);  // core engine, given its I/O + clock

// Apply the board's local safe state. Called by BOTH safety hooks below.
static void enterSafeState(BoardState* state) {
    state->led_on = false;
    state->blink_period_ms = 0;
    digitalWriteFast(LED_BUILTIN, LOW);
}

// ═════════════════════════════════════════════════════════════════════════════
// ACT 1 — COMMAND HANDLERS
//
// Every handler has this exact signature (a raw function pointer type,
// api::CommandHandler): no networking, no blocking, returns a CommandResult.
//
//   api::CommandResult (const api::CommandContext&, api::ObjectWriter&, void*)
//
// By the time a handler runs, the library has ALREADY validated the message
// structure against the registered schema: the command name is known, every
// declared argument is present, and each argument's JSON type matches. Missing
// arg → MISSING_FIELD, wrong type → INVALID_TYPE, unknown command →
// UNKNOWN_COMMAND — all emitted by the library, never reaching here. A handler
// therefore only does DOMAIN validation (range checks, safety) and the work.
// ═════════════════════════════════════════════════════════════════════════════

// set_led {on: bool} — turn the LED solid on/off. Energizes an output, so it is
// registered blocked_by_estop = true.
api::CommandResult cmdSetLed(const api::CommandContext& command,
                             api::ObjectWriter& result,
                             void* context) {
    auto* state = static_cast<BoardState*>(context);

    // args.getBool(name, out) reads a typed argument from the parsed message.
    // Returns false if absent/wrong-type. For a *declared* arg the library
    // already guaranteed it, so `false` here is defensive — but note the string
    // view / parsed document is only valid for THIS call; never stash it.
    bool on = false;
    if (!command.args.getBool("on", on)) {
        return api::CommandResult::invalidType("on must be a bool");
    }

    state->led_on = on;
    state->blink_period_ms = 0;                  // solid overrides any blink
    digitalWriteFast(LED_BUILTIN, on ? HIGH : LOW);

    // result.addBool(name, value) appends a field to the response `result`
    // object. It writes into a FIXED-capacity buffer and returns false if the
    // value would not fit (the library never sends a truncated line). The honest
    // pattern is to surface that as INTERNAL_ERROR rather than ignore it.
    if (!result.addBool("led_on", state->led_on)) {
        return api::CommandResult::internalError("result overflow");
    }
    return api::CommandResult::ok();             // library adds board_proc_us + wraps the envelope
}

// set_blink {period_ms: int} — blink the LED. period_ms == 0 stops blinking.
// Also actuates hardware → blocked_by_estop = true.
api::CommandResult cmdSetBlink(const api::CommandContext& command,
                               api::ObjectWriter& result,
                               void* context) {
    auto* state = static_cast<BoardState*>(context);

    // Contract "int" == signed 32-bit. getInt validates that width for you.
    std::int32_t period = 0;
    if (!command.args.getInt("period_ms", period)) {
        return api::CommandResult::invalidType("period_ms must be an int");
    }
    // DOMAIN validation the library can't do for us → INVALID_ARGUMENT.
    if (period < 0 || period > kMaxBlinkPeriodMs) {
        return api::CommandResult::invalidArgument("period_ms out of range");
    }

    state->blink_period_ms = period;
    state->last_toggle_ms = millis();
    if (period == 0) {                           // stop → hold whatever led_on is
        digitalWriteFast(LED_BUILTIN, state->led_on ? HIGH : LOW);
    }
    return result.addInt("period_ms", period)
               ? api::CommandResult::ok()
               : api::CommandResult::internalError("result overflow");
}

// echo {value: int} — pure round-trip, no hardware. Safe during e-stop, so
// blocked_by_estop = false. Great first thing to fire from the GUI.
api::CommandResult cmdEcho(const api::CommandContext& command,
                           api::ObjectWriter& result,
                           void* context) {
    auto* state = static_cast<BoardState*>(context);
    std::int32_t value = 0;
    if (!command.args.getInt("value", value)) {
        return api::CommandResult::invalidType("value must be an int");
    }
    state->echo_value = value;
    return result.addInt("value", value)
               ? api::CommandResult::ok()
               : api::CommandResult::internalError("result overflow");
}

api::CommandResult write67(const api::CommandContext& commnad, 
                           api::ObjectWriter& result,
                           void* context) {
  auto* state = static_cast<BoardState*>(context);
  state->echo_value = 67;
  return api::CommandResult::ok();
}

api::CommandResult write68(const api::CommandContext& commnad, 
                           api::ObjectWriter& result,
                           void* context) {
  auto* state = static_cast<BoardState*>(context);
  state->echo_value = 68;
  return api::CommandResult::ok();
}

// ═════════════════════════════════════════════════════════════════════════════
// ACT 2 — TELEMETRY PROVIDER
//
//   bool (api::ObjectWriter& telemetry, void* context)   (api::TelemetryProvider)
//
// The library calls this on ITS OWN 50 ms schedule while a controller session is
// active — it is never solicited by a request. This push IS the liveness signal:
// if it stops flowing, the controller declares the board lost. So this callback
// must be fast and non-blocking: copy the latest already-computed values and
// return. Slow sensor acquisition belongs elsewhere in firmware, not here.
//
// Return true to send the frame; return false to skip this one (e.g. a snapshot
// wasn't ready). Each field name/type here must match what registerTelemetrySchema
// declared in setup().
// ═════════════════════════════════════════════════════════════════════════════
bool writeTelemetry(api::ObjectWriter& telemetry, void* context) {
    auto* state = static_cast<BoardState*>(context);
    // Derived field for the dashboard MODE tile: 0 = solid, 1 = blinking.
    const std::int32_t telemetry_mode = (state->blink_period_ms > 0) ? 1 : 0;
    // addInt/addBool return false on overflow; && short-circuits so one failure
    // aborts the whole frame rather than emitting a partial object. Field
    // names/types must match the registerTelemetrySchema() call in setup().
    return telemetry.addInt("echo_value", state->echo_value) &&
           telemetry.addInt("telemetry_mode", telemetry_mode) &&
           telemetry.addBool("led_on", state->led_on) &&
           telemetry.addInt("blink_period_ms", state->blink_period_ms) &&
           telemetry.addInt("uptime_ms", static_cast<std::int32_t>(millis()));
}

// ═════════════════════════════════════════════════════════════════════════════
// SAFETY HOOKS
//
//   bool (void* context)                                 (api::SafetyHook)
//
// Return true ONLY once board-local safe state has actually been applied. The
// library sends estop_ack {state:"safe"} only after the e-stop hook returns
// true; on false it withholds the ack (honest e-stop — no software latch, no
// lying). Both hooks must be idempotent and finish within ~100 ms.
// ═════════════════════════════════════════════════════════════════════════════
bool onEstop(void* context) {                    // controller commanded e-stop
    enterSafeState(static_cast<BoardState*>(context));
    return true;                                 // safe state applied → ack allowed
}

bool onControllerLoss(void* context) {           // telemetry/heartbeat link lost
    enterSafeState(static_cast<BoardState*>(context));
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// REGISTRATION + LIFECYCLE
// ═════════════════════════════════════════════════════════════════════════════
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWriteFast(LED_BUILTIN, LOW);

    // Identity — three static strings the controller uses to route to this board.
    // Copied into fixed library storage; your buffers can go away after the call.
    (void)server.setIdentity({"demo_board", "1", "0.1.0"});
    (void)server.setNetworkConfig(network_config);

    // ── Register commands ──
    // registerCommand(spec, handler, context):
    //   * copies the name + arg names into the fixed-capacity registry,
    //   * rejects duplicates and over-capacity (16 cmds / 8 args max) with a Status,
    //   * wires the handler + context for dispatch, AND
    //   * becomes one entry in the generated schema's "commands" section.
    // The CommandSpec is { name, args-array, arg_count, blocked_by_estop }.
    const api::ArgumentSpec set_led_args[]{{"on", api::ValueType::Bool}, {"other", api::ValueType::Bool}};
    (void)server.registerCommand({"set_led", set_led_args, 2, /*blocked_by_estop=*/true},
                                 cmdSetLed, &board_state);

    const api::ArgumentSpec set_blink_args[]{{"period_ms", api::ValueType::Int}};
    (void)server.registerCommand({"set_blink", set_blink_args, 1, /*blocked_by_estop=*/true},
                                 cmdSetBlink, &board_state);

    const api::ArgumentSpec echo_args[]{{"value", api::ValueType::Int}};
    (void)server.registerCommand({"echo", echo_args, 1, /*blocked_by_estop=*/false},
                                 cmdEcho, &board_state);

    const api::ArgumentSpec const_args[]{nullptr};
    (void)server.registerCommand({"write_67", const_args, 0, false}, write67, &board_state);
    // ── Register telemetry ──
    // Two calls: declare the SHAPE (field names + types → schema "telemetry"
    // section), then supply the VALUES (the provider). Only the four V1 value
    // types exist: Int (int32), Float (finite), Bool, String.
    const api::FieldSpec telemetry_fields[]{
        {"echo_value",      api::ValueType::Int},
        {"telemetry_mode",  api::ValueType::Int},
        {"led_on",          api::ValueType::Bool},
        {"blink_period_ms", api::ValueType::Int},
        {"uptime_ms",       api::ValueType::Int},
    };
    (void)server.registerTelemetrySchema(telemetry_fields, 5);
    (void)server.setTelemetryProvider(writeTelemetry, &board_state);

    // ── Safety hooks (required before start) ──
    (void)server.setEstopHook(onEstop, &board_state);
    (void)server.setControllerLossHook(onControllerLoss, &board_state);

    // Built-in diagnostic: registers a `get_counters` command exposing the
    // library's §25 counters (bytes, dropped frames, over-budget hooks, …).
    (void)server.enableCountersDiagnosticCommand();

    // start() does the one-time transition to serving:
    //   1. checks registration is complete (identity, net, provider, both hooks),
    //   2. GENERATES the schema and verifies it fits the 8 KB line,
    //   3. SEALS the registry — every register*/set* call afterward returns
    //      RegistrationSealed (the schema can no longer drift from behavior),
    //   4. opens the TCP listener and arms the 50 ms telemetry scheduler.
    // A real sketch should check this Status and stay in safe state on failure.
    (void)server.start();
}

void loop() {
    // Non-blocking blink driver. Note the division of labor: the set_blink
    // handler only RECORDS intent (period + which state), and returns
    // immediately; the actual timed hardware work happens here in the loop. That
    // is why handlers must never block — the loop, and therefore service(), must
    // keep turning. Unsigned subtraction keeps millis() wraparound correct.
    if (board_state.blink_period_ms > 0) {
        const std::uint32_t now = millis();
        if (now - board_state.last_toggle_ms >=
            static_cast<std::uint32_t>(board_state.blink_period_ms)) {
            board_state.last_toggle_ms = now;
            board_state.led_on = !board_state.led_on;
            digitalWriteFast(LED_BUILTIN, board_state.led_on ? HIGH : LOW);
        }
    }

    // service() is the whole engine, advanced once per loop and non-blocking:
    // accept/replace the controller session, receive + frame + parse inbound
    // lines, dispatch commands to the handlers above, push due telemetry, answer
    // heartbeats, run safety hooks, and flush queued outbound writes under their
    // transmit deadline. Call it every iteration; never put a delay() in loop().
    server.service();
}
