#include "TeensyCommandServer.h"
#include "core/CommandDispatcher.h"
#include "core/DiagnosticsCommand.h"
#include "core/InboundParser.h"
#include "core/SchemaBuilder.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeNetworkServer.h"
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
#include <limits>
#include <string>

namespace api = teensy_command_server::api;
namespace core = teensy_command_server::core;
namespace fakes = teensy_command_server::host::fakes;
namespace support = teensy_command_server::support;
using teensy_command_server::TeensyCommandServer;

api::CommandResult handler(const api::CommandContext&, api::ObjectWriter&, void*) {
    return api::CommandResult::ok();
}

bool telemetry(api::ObjectWriter&, void*) {
    return true;
}

bool hook(void*) {
    return true;
}

void assertOk(api::Status status) {
    assert(status.code == api::StatusCode::Ok);
}

void configureServer(TeensyCommandServer& server) {
    assertOk(server.setIdentity({"board", "1", "0.1.0"}));
    assertOk(server.setNetworkConfig(
        {api::NetworkConfig::Mode::Dhcp, 5050, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}));
    assertOk(server.registerTelemetrySchema(nullptr, 0));
    assertOk(server.setTelemetryProvider(telemetry, nullptr));
    assertOk(server.setEstopHook(hook, nullptr));
    assertOk(server.setControllerLossHook(hook, nullptr));
}

ArduinoJson::StaticJsonDocument<support::kBoardTxMaxLineBytes> parseJsonLine(
    const std::string& line) {
    ArduinoJson::StaticJsonDocument<support::kBoardTxMaxLineBytes> doc;
    assert(!ArduinoJson::deserializeJson(doc, line.c_str(), line.size()));
    return doc;
}

std::string firstLine(const std::string& bytes) {
    const std::size_t newline = bytes.find('\n');
    assert(newline != std::string::npos);
    return bytes.substr(0, newline + 1);
}

core::ParseOutcome parseCommand(core::Counters& counters, char* line) {
    core::InboundParser parser(counters);
    const core::ParseOutcome outcome = parser.parse({line, std::strlen(line)});
    assert(outcome.kind == core::ParseOutcomeKind::Valid);
    assert(outcome.message_kind == core::InboundMessageKind::Command);
    return outcome;
}

struct DrainSession {
    std::string sent;

    core::OutboundSendResult sendActiveLine(core::ConstLineView line, core::MessageClass) {
        sent.assign(line.data, line.size);
        return core::OutboundSendResult::Sent;
    }
};

std::string drain(core::OutboundScheduler& scheduler) {
    DrainSession session;
    core::OutboundOutcome outcome{};
    assert(scheduler.drainOne(session, outcome));
    assert(outcome.kind == core::OutboundKind::CommandResponse);
    assert(outcome.completion == core::OutboundCompletion::Sent);
    return session.sent;
}

void enabledCommandAppearsInSchemaAndDisabledLeavesNameFree() {
    fakes::FakeClock clock;
    fakes::FakeNetworkServer network;
    TeensyCommandServer enabled(network, clock);
    network.scriptAvailability(core::NetworkAvailability::Ready);
    network.advanceAvailabilityScript();
    configureServer(enabled);
    assertOk(enabled.enableCountersDiagnosticCommand());
    assertOk(enabled.start());

    enabled.service();
    network.queueInboundConnection();
    enabled.service();
    fakes::FakeTransport* transport = network.fakeTransport({0, 1});
    assert(transport != nullptr);
    const ArduinoJson::StaticJsonDocument<support::kBoardTxMaxLineBytes> schema =
        parseJsonLine(firstLine(transport->writtenString()));
    ArduinoJson::JsonObjectConst command =
        schema["schema"]["commands"][core::DiagnosticsCommand::kName];
    assert(!command.isNull());
    assert(command["args"].is<ArduinoJson::JsonObjectConst>());
    assert(command["args"].size() == 0);
    assert(command["blocked_by_estop"].as<bool>() == false);

    fakes::FakeClock disabled_clock;
    fakes::FakeNetworkServer disabled_network;
    TeensyCommandServer disabled(disabled_network, disabled_clock);
    disabled_network.scriptAvailability(core::NetworkAvailability::Ready);
    disabled_network.advanceAvailabilityScript();
    configureServer(disabled);
    assertOk(disabled.start());
    disabled.service();
    disabled_network.queueInboundConnection();
    disabled.service();
    fakes::FakeTransport* disabled_transport = disabled_network.fakeTransport({0, 1});
    assert(disabled_transport != nullptr);
    const ArduinoJson::StaticJsonDocument<support::kBoardTxMaxLineBytes> disabled_schema =
        parseJsonLine(firstLine(disabled_transport->writtenString()));
    assert(disabled_schema["schema"]["commands"][core::DiagnosticsCommand::kName].isNull());

    TeensyCommandServer free_name;
    configureServer(free_name);
    assertOk(free_name.registerCommand({core::DiagnosticsCommand::kName, nullptr, 0, false},
                                       handler, nullptr));
}

void duplicateBoardCommandFailsWhenDiagnosticsEnabled() {
    TeensyCommandServer enabled_first;
    configureServer(enabled_first);
    assertOk(enabled_first.enableCountersDiagnosticCommand());
    assert(enabled_first
               .registerCommand({core::DiagnosticsCommand::kName, nullptr, 0, false}, handler,
                                nullptr)
               .code == api::StatusCode::DuplicateRegistration);

    TeensyCommandServer board_first;
    configureServer(board_first);
    assertOk(board_first.registerCommand({core::DiagnosticsCommand::kName, nullptr, 0, false},
                                         handler, nullptr));
    assert(board_first.enableCountersDiagnosticCommand().code ==
           api::StatusCode::DuplicateRegistration);
}

void dispatchReturnsLiveUnsignedCountersWithPinnedSnapshotTiming() {
    core::Counters counters;
    counters.sessions_accepted = 1;
    counters.sessions_rejected = 2;
    counters.sessions_superseded = 3;
    counters.schemas_sent = 4;
    counters.commands_received = 2147483648UL;
    counters.commands_ok = 6;
    counters.commands_error = 7;
    counters.unknown_commands = 8;
    counters.invalid_arguments = 9;
    counters.invalid_json = 10;
    counters.invalid_targets = 11;
    counters.oversized_lines = 12;
    counters.telemetry_sent = 13;
    counters.telemetry_coalesced = 14;
    counters.telemetry_dropped = 15;
    counters.estop_received = 16;
    counters.estop_ack_sent = 17;
    counters.estop_apply_failed = 18;
    counters.estop_hook_over_budget = 19;
    counters.controller_loss_hook_over_budget = 20;
    counters.heartbeat_received = 21;
    counters.heartbeat_ack_sent = 22;
    counters.tx_failures = 23;
    counters.controller_disconnects = 24;

    core::CommandRegistry registry;
    assertOk(core::DiagnosticsCommand::registerCommand(registry, counters));
    core::OutboundScheduler scheduler;
    fakes::FakeClock clock;
    clock.setMonotonicMilliseconds(1234);
    clock.setMonotonicMicroseconds(6000);
    core::CommandDispatcher dispatcher(counters, registry, {"board", "1", "0.1.0"}, clock,
                                       scheduler);
    char command[] =
        "{\"type\":\"command\",\"seq\":42,\"controller_ts\":77.5,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"get_counters\",\"args\":{}}";

    assert(!dispatcher.dispatch(parseCommand(counters, command).command, 5000));
    assert(counters.commands_received == 2147483649UL);
    assert(counters.commands_ok == 7);

    const std::string response = drain(scheduler);
    assert(response.size() < support::kBoardTxMaxLineBytes);
    const ArduinoJson::StaticJsonDocument<support::kBoardTxMaxLineBytes> doc =
        parseJsonLine(response);
    assert(std::strcmp(doc["status"], "ok") == 0);
    assert(doc["error"].isNull());
    ArduinoJson::JsonObjectConst result = doc["result"];
    assert(result["sessions_accepted"].as<std::uint64_t>() == 1);
    assert(result["sessions_rejected"].as<std::uint64_t>() == 2);
    assert(result["sessions_superseded"].as<std::uint64_t>() == 3);
    assert(result["schemas_sent"].as<std::uint64_t>() == 4);
    assert(result["commands_received"].as<std::uint64_t>() == 2147483649ULL);
    assert(result["commands_ok"].as<std::uint64_t>() == 6);
    assert(result["commands_error"].as<std::uint64_t>() == 7);
    assert(result["unknown_commands"].as<std::uint64_t>() == 8);
    assert(result["invalid_arguments"].as<std::uint64_t>() == 9);
    assert(result["invalid_json"].as<std::uint64_t>() == 10);
    assert(result["invalid_targets"].as<std::uint64_t>() == 11);
    assert(result["oversized_lines"].as<std::uint64_t>() == 12);
    assert(result["telemetry_sent"].as<std::uint64_t>() == 13);
    assert(result["telemetry_coalesced"].as<std::uint64_t>() == 14);
    assert(result["telemetry_dropped"].as<std::uint64_t>() == 15);
    assert(result["estop_received"].as<std::uint64_t>() == 16);
    assert(result["estop_ack_sent"].as<std::uint64_t>() == 17);
    assert(result["estop_apply_failed"].as<std::uint64_t>() == 18);
    assert(result["estop_hook_over_budget"].as<std::uint64_t>() == 19);
    assert(result["controller_loss_hook_over_budget"].as<std::uint64_t>() == 20);
    assert(result["heartbeat_received"].as<std::uint64_t>() == 21);
    assert(result["heartbeat_ack_sent"].as<std::uint64_t>() == 22);
    assert(result["tx_failures"].as<std::uint64_t>() == 23);
    assert(result["controller_disconnects"].as<std::uint64_t>() == 24);
    assert(result["board_proc_us"].as<std::uint64_t>() == 1000);
}

void maximumCounterWidthsFitResponseLine() {
    core::Counters counters;
    counters.sessions_accepted = std::numeric_limits<core::Counters::Value>::max();
    counters.sessions_rejected = std::numeric_limits<core::Counters::Value>::max();
    counters.sessions_superseded = std::numeric_limits<core::Counters::Value>::max();
    counters.schemas_sent = std::numeric_limits<core::Counters::Value>::max();
    counters.commands_received = std::numeric_limits<core::Counters::Value>::max() - 1U;
    counters.commands_ok = std::numeric_limits<core::Counters::Value>::max();
    counters.commands_error = std::numeric_limits<core::Counters::Value>::max();
    counters.unknown_commands = std::numeric_limits<core::Counters::Value>::max();
    counters.invalid_arguments = std::numeric_limits<core::Counters::Value>::max();
    counters.invalid_json = std::numeric_limits<core::Counters::Value>::max();
    counters.invalid_targets = std::numeric_limits<core::Counters::Value>::max();
    counters.oversized_lines = std::numeric_limits<core::Counters::Value>::max();
    counters.telemetry_sent = std::numeric_limits<core::Counters::Value>::max();
    counters.telemetry_coalesced = std::numeric_limits<core::Counters::Value>::max();
    counters.telemetry_dropped = std::numeric_limits<core::Counters::Value>::max();
    counters.estop_received = std::numeric_limits<core::Counters::Value>::max();
    counters.estop_ack_sent = std::numeric_limits<core::Counters::Value>::max();
    counters.estop_apply_failed = std::numeric_limits<core::Counters::Value>::max();
    counters.estop_hook_over_budget = std::numeric_limits<core::Counters::Value>::max();
    counters.controller_loss_hook_over_budget =
        std::numeric_limits<core::Counters::Value>::max();
    counters.heartbeat_received = std::numeric_limits<core::Counters::Value>::max();
    counters.heartbeat_ack_sent = std::numeric_limits<core::Counters::Value>::max();
    counters.tx_failures = std::numeric_limits<core::Counters::Value>::max();
    counters.controller_disconnects = std::numeric_limits<core::Counters::Value>::max();

    core::CommandRegistry registry;
    assertOk(core::DiagnosticsCommand::registerCommand(registry, counters));
    core::OutboundScheduler scheduler;
    fakes::FakeClock clock;
    core::CommandDispatcher dispatcher(counters, registry, {"board", "1", "0.1.0"}, clock,
                                       scheduler);
    char command[] =
        "{\"type\":\"command\",\"seq\":1,\"controller_ts\":1,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"get_counters\",\"args\":{}}";

    assert(!dispatcher.dispatch(parseCommand(counters, command).command, 0));
    const std::string response = drain(scheduler);
    assert(response.size() < support::kBoardTxMaxLineBytes);
    assert(response.find("\"commands_received\":4294967295") != std::string::npos);
    assert(response.find("\"commands_ok\":4294967295") != std::string::npos);
}

int main() {
    enabledCommandAppearsInSchemaAndDisabledLeavesNameFree();
    duplicateBoardCommandFailsWhenDiagnosticsEnabled();
    dispatchReturnsLiveUnsignedCountersWithPinnedSnapshotTiming();
    maximumCounterWidthsFitResponseLine();

    std::puts("test_diagnostics_command: ok");
    return 0;
}
