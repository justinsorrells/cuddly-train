#include "core/CommandDispatcher.h"
#include "core/InboundParser.h"
#include "core/ServiceLoop.h"
#include "fakes/FakeClock.h"
#include "support/Limits.h"

#ifndef ARDUINOJSON_USE_LONG_LONG
#define ARDUINOJSON_USE_LONG_LONG 1
#endif
#ifndef ARDUINOJSON_ENABLE_STD_STRING
#define ARDUINOJSON_ENABLE_STD_STRING 0
#endif
#include "../../../third_party/ArduinoJson/ArduinoJson-v6.21.5.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace api = teensy_command_server::api;
namespace core = teensy_command_server::core;
namespace fakes = teensy_command_server::host::fakes;
namespace support = teensy_command_server::support;

struct HandlerState {
    unsigned calls = 0;
    api::CommandResult result = api::CommandResult::ok();
    bool write_result = true;
    bool write_reserved = false;
    bool reserved_rejected = false;
    bool write_large = false;
    core::Clock* clock = nullptr;
    std::uint64_t handler_advance_us = 0;
};

api::CommandResult handler(const api::CommandContext&,
                           api::ObjectWriter& result,
                           void* raw) {
    auto* state = static_cast<HandlerState*>(raw);
    ++state->calls;
    if (state->clock != nullptr && state->handler_advance_us > 0) {
        auto* fake = static_cast<fakes::FakeClock*>(state->clock);
        fake->advanceMicroseconds(state->handler_advance_us);
    }
    if (state->write_reserved) {
        state->reserved_rejected = !result.addInt("board_proc_us", 999);
    }
    if (state->write_large) {
        static char payload[support::kMaxResultPayloadBytes + 1]{};
        std::memset(payload, 'x', sizeof(payload));
        assert(!result.addString("payload", payload, sizeof(payload)));
    } else if (state->write_result) {
        assert(result.addBool("accepted", true));
        assert(result.addInt("rpm", 1200));
    }
    return state->result;
}

api::CommandResult invalidArgumentHandler(const api::CommandContext&,
                                          api::ObjectWriter&,
                                          void* raw) {
    auto* calls = static_cast<unsigned*>(raw);
    ++(*calls);
    return api::CommandResult::invalidArgument("rpm is outside range");
}

struct AdvancingArgs {
    ArduinoJson::JsonObject args;
    bool active = false;
    fakes::FakeClock* clock = nullptr;
    std::uint64_t validation_advance_us = 0;
};

bool advancingHas(const void*, const char* name) {
    return std::strcmp(name, "rpm") == 0;
}

bool advancingGetInt(const void* raw, const char* name, std::int32_t& out) {
    auto* args = static_cast<const AdvancingArgs*>(raw);
    if (args->clock != nullptr) {
        args->clock->advanceMicroseconds(args->validation_advance_us);
    }
    if (std::strcmp(name, "rpm") != 0) {
        return false;
    }
    out = 12;
    return true;
}

bool advancingGetFloat(const void*, const char*, float&) {
    return false;
}

bool advancingGetBool(const void*, const char*, bool&) {
    return false;
}

bool advancingGetString(const void*, const char*, const char*&, std::size_t&) {
    return false;
}

struct DrainSession {
    std::string sent;
    core::OutboundSendResult result = core::OutboundSendResult::Sent;

    core::OutboundSendResult sendActiveLine(core::ConstLineView line,
                                            core::MessageClass) {
        sent.assign(line.data, line.size);
        return result;
    }
};

void assertOk(api::Status status) {
    assert(status.code == api::StatusCode::Ok);
}

core::CommandRegistry registryWithHandler(HandlerState& state) {
    core::CommandRegistry registry;
    const api::ArgumentSpec args[] = {
        {"rpm", api::ValueType::Int},
        {"enabled", api::ValueType::Bool},
        {"label", api::ValueType::String},
    };
    assertOk(registry.registerCommand(
        {"set_speed", args, 3, true, true, handler, &state}));
    assertOk(registry.registerCommand(
        {"get_status", nullptr, 0, false, true, handler, &state}));
    return registry;
}

core::ParseOutcome parseCommand(core::InboundParser& parser, char* line) {
    core::ParseOutcome outcome = parser.parse({line, std::strlen(line)});
    assert(outcome.kind == core::ParseOutcomeKind::Valid);
    assert(outcome.message_kind == core::InboundMessageKind::Command);
    return outcome;
}

std::string drain(core::OutboundScheduler& scheduler,
                  core::OutboundSendResult result = core::OutboundSendResult::Sent,
                  core::OutboundOutcome* captured = nullptr) {
    DrainSession session;
    session.result = result;
    core::OutboundOutcome outcome{};
    assert(scheduler.drainOne(session, outcome));
    if (captured != nullptr) {
        *captured = outcome;
    }
    return session.sent;
}

ArduinoJson::StaticJsonDocument<support::kCommandJsonDocumentBytes> parseResponse(
    const std::string& response) {
    ArduinoJson::StaticJsonDocument<support::kCommandJsonDocumentBytes> doc;
    assert(!ArduinoJson::deserializeJson(doc, response.c_str(), response.size()));
    return doc;
}

void okEnvelopeEchoesSeqControllerTsAndBoardProcUs() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    fakes::FakeClock clock;
    clock.setMonotonicMilliseconds(1234);
    clock.setMonotonicMicroseconds(1500);
    HandlerState handler_state;
    core::CommandRegistry registry = registryWithHandler(handler_state);
    core::CommandDispatcher dispatcher(
        counters, registry, {"motor", "1", "0.1.0"}, clock, scheduler);
    core::InboundParser parser(counters);
    char command[] =
        "{\"type\":\"command\",\"seq\":42,\"controller_ts\":81234.567,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set_speed\","
        "\"args\":{\"rpm\":1200,\"enabled\":true,\"label\":\"axis\"}}";
    core::ParseOutcome outcome = parseCommand(parser, command);

    assert(!dispatcher.dispatch(outcome.command, 1000));
    assert(handler_state.calls == 1);
    assert(counters.commands_received == 1);
    assert(counters.commands_ok == 1);
    assert(counters.commands_error == 0);

    const std::string response = drain(scheduler);
    assert(response.find("\"timeout\"") == std::string::npos);
    ArduinoJson::StaticJsonDocument<support::kCommandJsonDocumentBytes> doc =
        parseResponse(response);
    assert(std::strcmp(doc["type"], "response") == 0);
    assert(doc["seq"].as<support::Seq>() == 42);
    assert(doc["controller_ts"].as<double>() == 81234.567);
    assert(doc["timestamp"].as<std::uint64_t>() == 1);
    assert(std::strcmp(doc["source"], "motor") == 0);
    assert(std::strcmp(doc["target"], "controller") == 0);
    assert(std::strcmp(doc["status"], "ok") == 0);
    assert(doc["error"].isNull());
    assert(doc["result"]["accepted"].as<bool>());
    assert(doc["result"]["rpm"].as<int>() == 1200);
    assert(doc["result"]["board_proc_us"].as<std::uint64_t>() == 500);
}

void validationErrorsBuildFullErrorEnvelopeWithoutBoardProcUs() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    fakes::FakeClock clock;
    HandlerState handler_state;
    core::CommandRegistry registry = registryWithHandler(handler_state);
    core::CommandDispatcher dispatcher(
        counters, registry, {"motor", "1", "0.1.0"}, clock, scheduler);
    core::InboundParser parser(counters);

    char missing[] =
        "{\"type\":\"command\",\"seq\":1,\"controller_ts\":1.25,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set_speed\","
        "\"args\":{\"rpm\":1200,\"enabled\":true}}";
    assert(!dispatcher.dispatch(parseCommand(parser, missing).command, 0));

    char wrong_type[] =
        "{\"type\":\"command\",\"seq\":2,\"controller_ts\":2.5,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set_speed\","
        "\"args\":{\"rpm\":\"fast\",\"enabled\":true,\"label\":\"axis\"}}";
    assert(!dispatcher.dispatch(parseCommand(parser, wrong_type).command, 0));

    char extra[] =
        "{\"type\":\"command\",\"seq\":3,\"controller_ts\":3.5,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set_speed\","
        "\"args\":{\"rpm\":1200,\"enabled\":true,\"label\":\"axis\",\"extra\":1}}";
    assert(!dispatcher.dispatch(parseCommand(parser, extra).command, 0));

    char unknown[] =
        "{\"type\":\"command\",\"seq\":4,\"controller_ts\":4.5,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"missing\","
        "\"args\":{}}";
    assert(!dispatcher.dispatch(parseCommand(parser, unknown).command, 0));

    assert(handler_state.calls == 0);
    assert(counters.commands_received == 4);
    assert(counters.commands_error == 4);
    assert(counters.commands_ok == 0);
    assert(counters.unknown_commands == 1);
    assert(counters.invalid_arguments == 1);

    const char* expected_codes[] = {
        "MISSING_FIELD",
        "INVALID_TYPE",
        "INVALID_ARGUMENT",
        "UNKNOWN_COMMAND",
    };
    for (const char* code : expected_codes) {
        const std::string response = drain(scheduler);
        ArduinoJson::StaticJsonDocument<support::kCommandJsonDocumentBytes> doc =
            parseResponse(response);
        assert(std::strcmp(doc["status"], "error") == 0);
        assert(doc["result"].isNull());
        assert(!doc["result"]["board_proc_us"]);
        assert(std::strcmp(doc["error"]["code"], code) == 0);
        assert(std::strcmp(doc["target"], "controller") == 0);
        assert(response.find("board_proc_us") == std::string::npos);
        assert(response.find("BOARD_BUSY") == std::string::npos);
        assert(response.find("COMMAND_TIMEOUT") == std::string::npos);
        assert(response.find("UNKNOWN_TARGET") == std::string::npos);
    }
}

void handlerDomainFailureAndReservedFieldBehavior() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    fakes::FakeClock clock;
    unsigned domain_calls = 0;
    core::CommandRegistry domain_registry;
    assertOk(domain_registry.registerCommand(
        {"set_speed", nullptr, 0, true, true, invalidArgumentHandler, &domain_calls}));
    core::CommandDispatcher domain_dispatcher(
        counters, domain_registry, {"motor", "1", "0.1.0"}, clock, scheduler);
    core::InboundParser parser(counters);
    char domain_command[] =
        "{\"type\":\"command\",\"seq\":8,\"controller_ts\":8,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set_speed\","
        "\"args\":{}}";
    assert(!domain_dispatcher.dispatch(parseCommand(parser, domain_command).command, 0));
    assert(domain_calls == 1);
    assert(counters.invalid_arguments == 1);
    ArduinoJson::StaticJsonDocument<support::kCommandJsonDocumentBytes> error_doc =
        parseResponse(drain(scheduler));
    assert(std::strcmp(error_doc["status"], "error") == 0);
    assert(std::strcmp(error_doc["error"]["code"], "INVALID_ARGUMENT") == 0);
    assert(std::string(error_doc["error"]["message"].as<const char*>()) ==
           "rpm is outside range");

    HandlerState reserved_state;
    reserved_state.write_reserved = true;
    core::CommandRegistry reserved_registry = registryWithHandler(reserved_state);
    core::CommandDispatcher reserved_dispatcher(
        counters, reserved_registry, {"motor", "1", "0.1.0"}, clock, scheduler);
    char ok_command[] =
        "{\"type\":\"command\",\"seq\":9,\"controller_ts\":9,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"get_status\","
        "\"args\":{}}";
    assert(!reserved_dispatcher.dispatch(parseCommand(parser, ok_command).command, 0));
    assert(reserved_state.reserved_rejected);
    ArduinoJson::StaticJsonDocument<support::kCommandJsonDocumentBytes> ok_doc =
        parseResponse(drain(scheduler));
    assert(std::strcmp(ok_doc["status"], "ok") == 0);
    assert(ok_doc["result"]["board_proc_us"].is<std::uint64_t>());
    assert(!ok_doc["error"]);
}

void boardProcUsUsesWrapSafeUnsignedDuration() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    fakes::FakeClock clock;
    clock.setMonotonicMicroseconds(10);
    HandlerState handler_state;
    core::CommandRegistry registry = registryWithHandler(handler_state);
    core::CommandDispatcher dispatcher(
        counters, registry, {"motor", "1", "0.1.0"}, clock, scheduler);
    core::InboundParser parser(counters);
    char command[] =
        "{\"type\":\"command\",\"seq\":10,\"controller_ts\":10,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"get_status\","
        "\"args\":{}}";
    assert(!dispatcher.dispatch(parseCommand(parser, command).command,
                                UINT64_MAX - 4));
    ArduinoJson::StaticJsonDocument<support::kCommandJsonDocumentBytes> doc =
        parseResponse(drain(scheduler));
    assert(doc["result"]["board_proc_us"].as<std::uint64_t>() == 15);
}

void timingIncludesValidationHandlerAndResponseReadiness() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    fakes::FakeClock clock;
    clock.setMonotonicMicroseconds(1000);
    HandlerState handler_state;
    handler_state.clock = &clock;
    handler_state.handler_advance_us = 30;
    const api::ArgumentSpec args[] = {{"rpm", api::ValueType::Int}};
    core::CommandRegistry registry;
    assertOk(registry.registerCommand(
        {"timed", args, 1, true, true, handler, &handler_state}));
    core::CommandDispatcher dispatcher(
        counters, registry, {"motor", "1", "0.1.0"}, clock, scheduler);
    AdvancingArgs backing{ArduinoJson::JsonObject(), false, &clock, 7};
    api::CommandArgs command_args(&backing, advancingHas, advancingGetInt,
                                  advancingGetFloat, advancingGetBool,
                                  advancingGetString);
    const core::ParsedCommand command{1, 1.0, "timed", command_args};

    clock.advanceMicroseconds(20);
    assert(!dispatcher.dispatch(command, 1000));
    ArduinoJson::StaticJsonDocument<support::kCommandJsonDocumentBytes> doc =
        parseResponse(drain(scheduler));
    assert(doc["result"]["board_proc_us"].as<std::uint64_t>() == 57);
}

void oversizedResultFallsBackToInternalErrorAndKeepsSeq() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    fakes::FakeClock clock;
    HandlerState handler_state;
    handler_state.write_result = false;
    handler_state.write_large = true;
    core::CommandRegistry registry = registryWithHandler(handler_state);
    core::CommandDispatcher dispatcher(
        counters, registry, {"motor", "1", "0.1.0"}, clock, scheduler);
    core::InboundParser parser(counters);
    char command[] =
        "{\"type\":\"command\",\"seq\":55,\"controller_ts\":55.25,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"get_status\","
        "\"args\":{}}";
    assert(!dispatcher.dispatch(parseCommand(parser, command).command, 0));
    assert(handler_state.calls == 1);
    assert(counters.commands_error == 1);
    assert(counters.commands_ok == 0);
    ArduinoJson::StaticJsonDocument<support::kCommandJsonDocumentBytes> doc =
        parseResponse(drain(scheduler));
    assert(doc["seq"].as<support::Seq>() == 55);
    assert(doc["controller_ts"].as<double>() == 55.25);
    assert(std::strcmp(doc["status"], "error") == 0);
    assert(std::strcmp(doc["error"]["code"], "INTERNAL_ERROR") == 0);
    assert(std::string(doc["error"]["message"].as<const char*>()) ==
           "response exceeded fixed capacity");
}

void recoverableParserErrorUsesRetainedSeqAndControllerTs() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    fakes::FakeClock clock;
    HandlerState handler_state;
    core::CommandRegistry registry = registryWithHandler(handler_state);
    core::CommandDispatcher dispatcher(
        counters, registry, {"motor", "1", "0.1.0"}, clock, scheduler);
    assert(!dispatcher.recoverableCommandError(
        {77, 77.5, core::BoardErrorCode::MissingField}));
    ArduinoJson::StaticJsonDocument<support::kCommandJsonDocumentBytes> doc =
        parseResponse(drain(scheduler));
    assert(doc["seq"].as<support::Seq>() == 77);
    assert(doc["controller_ts"].as<double>() == 77.5);
    assert(std::strcmp(doc["error"]["code"], "MISSING_FIELD") == 0);
    assert(counters.commands_error == 1);
}

void enqueueFailureRequestsDeferredTeardown() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    fakes::FakeClock clock;
    HandlerState handler_state;
    core::CommandRegistry registry = registryWithHandler(handler_state);
    core::CommandDispatcher dispatcher(
        counters, registry, {"motor", "1", "0.1.0"}, clock, scheduler);
    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse,
                                     {"{\"a\":1}\n", 8}) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse,
                                     {"{\"a\":2}\n", 8}) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse,
                                     {"{\"a\":3}\n", 8}) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse,
                                     {"{\"a\":4}\n", 8}) ==
           core::OutboundEnqueueResult::Queued);

    core::InboundParser parser(counters);
    char command[] =
        "{\"type\":\"command\",\"seq\":90,\"controller_ts\":90,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"get_status\","
        "\"args\":{}}";
    assert(dispatcher.dispatch(parseCommand(parser, command).command, 0));
    assert(counters.commands_ok == 0);
    assert(counters.commands_error == 0);
}

struct ServiceLoopSession {
    char* raw = nullptr;
    bool pending_teardown = false;
    bool released = false;
    core::TeardownReason reason = core::TeardownReason::ExplicitShutdown;

    void poll() {}
    bool teardownPending() const { return pending_teardown; }
    bool nextLine(core::MutableLineView& out) {
        out = {raw, std::strlen(raw)};
        return true;
    }
    void releaseLine() { released = true; }
    void requestTeardown(core::TeardownReason requested) {
        pending_teardown = true;
        reason = requested;
    }
    void applyPendingTeardown() { pending_teardown = false; }
    core::OutboundSendResult sendActiveLine(core::ConstLineView, core::MessageClass) {
        return core::OutboundSendResult::Sent;
    }
};

struct TimedRouteState {
    unsigned calls = 0;
    std::uint64_t parse_completed_us = 0;
};

bool timedRoute(const core::ParsedCommand&, std::uint64_t parse_completed_us, void* raw) {
    auto* state = static_cast<TimedRouteState*>(raw);
    ++state->calls;
    state->parse_completed_us = parse_completed_us;
    return false;
}

void serviceLoopPassesParseCompletionToTimedCommandRoute() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    fakes::FakeClock clock;
    clock.setMonotonicMicroseconds(4321);
    TimedRouteState state;
    core::ServiceLoop::Routes routes;
    routes.context = &state;
    routes.command_with_timing = timedRoute;
    // ServiceLoop stores the identity by reference (the facade owns a long-lived
    // identity), so it must outlive the loop — use a named local, not a temporary.
    const api::BoardIdentity identity{"motor", "1", "0.1.0"};
    core::ServiceLoop loop(counters, identity, scheduler, routes, clock);
    char command[] =
        "{\"type\":\"command\",\"seq\":91,\"controller_ts\":91,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"get_status\","
        "\"args\":{}}";
    ServiceLoopSession session{command};
    loop.service(session);
    assert(state.calls == 1);
    assert(state.parse_completed_us == 4321);
    assert(!session.pending_teardown);
    assert(session.released);
}

void serviceLoopRequestsTeardownOnResponseSendFailure() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse,
                                     {"{\"type\":\"response\"}\n", 20}) ==
           core::OutboundEnqueueResult::Queued);
    // ServiceLoop holds the identity by reference; keep it in a named local so it
    // outlives the loop (see the note above).
    const api::BoardIdentity identity{"motor", "1", "0.1.0"};
    core::ServiceLoop loop(counters, identity, scheduler);
    struct Session {
        bool pending = false;
        bool applied = false;
        core::TeardownReason reason = core::TeardownReason::ExplicitShutdown;
        void poll() {}
        bool teardownPending() const { return pending; }
        bool nextLine(core::MutableLineView&) { return false; }
        void releaseLine() {}
        void requestTeardown(core::TeardownReason requested) {
            pending = true;
            reason = requested;
        }
        void applyPendingTeardown() {
            applied = true;
            pending = false;
        }
        core::OutboundSendResult sendActiveLine(core::ConstLineView, core::MessageClass) {
            return core::OutboundSendResult::DeadlineExpired;
        }
    } session;
    loop.service(session);
    assert(session.applied);
    assert(session.reason == core::TeardownReason::CriticalTransmitFailure);
}

int main() {
    okEnvelopeEchoesSeqControllerTsAndBoardProcUs();
    validationErrorsBuildFullErrorEnvelopeWithoutBoardProcUs();
    handlerDomainFailureAndReservedFieldBehavior();
    boardProcUsUsesWrapSafeUnsignedDuration();
    timingIncludesValidationHandlerAndResponseReadiness();
    oversizedResultFallsBackToInternalErrorAndKeepsSeq();
    recoverableParserErrorUsesRetainedSeqAndControllerTs();
    enqueueFailureRequestsDeferredTeardown();
    serviceLoopPassesParseCompletionToTimedCommandRoute();
    serviceLoopRequestsTeardownOnResponseSendFailure();

    std::puts("test_command_dispatcher: ok");
    return 0;
}
