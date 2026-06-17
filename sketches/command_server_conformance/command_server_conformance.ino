#include <Arduino.h>
#include <TeensyCommandServer.h>
#include <platform/qnethernet/Platform.h>

#include <cstdint>
#include <cstring>

namespace tcs = teensy_command_server;
namespace api = teensy_command_server::api;
namespace qnp = teensy_command_server::platform::qnethernet;

constexpr std::uint16_t kListenPort = 5051;
constexpr std::size_t kLargePayloadBytes = 8100;
constexpr std::uint32_t kOverBudgetDelayMs = 150;

enum class TelemetryMode : std::int32_t {
    Normal = 0,
    Oversized = 1,
    ProviderFailure = 2,
};

struct FixtureState {
    std::int32_t echo_value = 0;
    TelemetryMode telemetry_mode = TelemetryMode::Normal;
    bool next_estop_safe = true;
    bool next_estop_over_budget = false;
    bool controller_loss_over_budget = false;
    bool estop_trigger_pending = false;
    const char* estop_reason = "fixture_requested";
    char large_payload[kLargePayloadBytes + 1]{};
};

api::NetworkConfig network_config{
    api::NetworkConfig::Mode::Dhcp,
    kListenPort,
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
};

// Static IPv4 example:
// api::NetworkConfig network_config{
//     api::NetworkConfig::Mode::StaticIpv4,
//     kListenPort,
//     {192, 168, 1, 51},
//     {192, 168, 1, 1},
//     {255, 255, 255, 0},
// };

FixtureState fixture;
qnp::QNEthernetNetworkServer network(network_config);
qnp::QNEthernetClock platform_clock;
tcs::TeensyCommandServer server(network, platform_clock);

api::CommandResult testEcho(const api::CommandContext& command,
                            api::ObjectWriter& result,
                            void* context) {
    auto* state = static_cast<FixtureState*>(context);
    std::int32_t value = 0;
    if (!command.args.getInt("value", value)) {
        return api::CommandResult::invalidType("value must be an int");
    }
    state->echo_value = value;
    if (!result.addInt("value", value)) {
        return api::CommandResult::internalError("echo result overflow");
    }
    return api::CommandResult::ok();
}

api::CommandResult testOversizedResult(const api::CommandContext&,
                                       api::ObjectWriter& result,
                                       void* context) {
    auto* state = static_cast<FixtureState*>(context);
    (void)result.addString("payload", state->large_payload);
    return api::CommandResult::ok();
}

api::CommandResult testEstopSuccess(const api::CommandContext&,
                                    api::ObjectWriter& result,
                                    void* context) {
    auto* state = static_cast<FixtureState*>(context);
    state->next_estop_safe = true;
    state->next_estop_over_budget = false;
    return result.addBool("next_estop_safe", true)
               ? api::CommandResult::ok()
               : api::CommandResult::internalError("estop mode result overflow");
}

api::CommandResult testEstopFail(const api::CommandContext&,
                                 api::ObjectWriter& result,
                                 void* context) {
    auto* state = static_cast<FixtureState*>(context);
    state->next_estop_safe = false;
    state->next_estop_over_budget = false;
    return result.addBool("next_estop_safe", false)
               ? api::CommandResult::ok()
               : api::CommandResult::internalError("estop mode result overflow");
}

api::CommandResult testEstopOverBudget(const api::CommandContext&,
                                       api::ObjectWriter& result,
                                       void* context) {
    auto* state = static_cast<FixtureState*>(context);
    state->next_estop_safe = true;
    state->next_estop_over_budget = true;
    return result.addBool("next_estop_over_budget", true)
               ? api::CommandResult::ok()
               : api::CommandResult::internalError("estop mode result overflow");
}

api::CommandResult testLossOverBudget(const api::CommandContext& command,
                                      api::ObjectWriter& result,
                                      void* context) {
    auto* state = static_cast<FixtureState*>(context);
    bool enabled = false;
    if (!command.args.getBool("enabled", enabled)) {
        return api::CommandResult::invalidType("enabled must be a bool");
    }
    state->controller_loss_over_budget = enabled;
    return result.addBool("enabled", enabled)
               ? api::CommandResult::ok()
               : api::CommandResult::internalError("loss mode result overflow");
}

api::CommandResult testTelemetryMode(const api::CommandContext& command,
                                     api::ObjectWriter& result,
                                     void* context) {
    auto* state = static_cast<FixtureState*>(context);
    std::int32_t mode = 0;
    if (!command.args.getInt("mode", mode)) {
        return api::CommandResult::invalidType("mode must be an int");
    }
    if (mode < 0 || mode > 2) {
        return api::CommandResult::invalidArgument("mode must be 0, 1, or 2");
    }
    state->telemetry_mode = static_cast<TelemetryMode>(mode);
    return result.addInt("mode", mode)
               ? api::CommandResult::ok()
               : api::CommandResult::internalError("telemetry mode result overflow");
}

api::CommandResult testTriggerEstop(const api::CommandContext&,
                                    api::ObjectWriter& result,
                                    void* context) {
    auto* state = static_cast<FixtureState*>(context);
    state->estop_trigger_pending = true;
    return result.addBool("queued", true)
               ? api::CommandResult::ok()
               : api::CommandResult::internalError("trigger result overflow");
}

bool writeTelemetry(api::ObjectWriter& telemetry, void* context) {
    auto* state = static_cast<FixtureState*>(context);
    if (state->telemetry_mode == TelemetryMode::ProviderFailure) {
        return false;
    }
    if (!telemetry.addInt("echo_value", state->echo_value) ||
        !telemetry.addInt("telemetry_mode",
                          static_cast<std::int32_t>(state->telemetry_mode))) {
        return false;
    }
    if (state->telemetry_mode == TelemetryMode::Oversized) {
        return telemetry.addString("overflow_payload", state->large_payload);
    }
    return true;
}

bool handleEstop(void* context) {
    auto* state = static_cast<FixtureState*>(context);
    if (state->next_estop_over_budget) {
        delay(kOverBudgetDelayMs);
    }
    const bool safe = state->next_estop_safe;
    state->next_estop_safe = true;
    state->next_estop_over_budget = false;
    return safe;
}

bool handleControllerLoss(void* context) {
    auto* state = static_cast<FixtureState*>(context);
    if (state->controller_loss_over_budget) {
        delay(kOverBudgetDelayMs);
    }
    return true;
}

void fillLargePayload() {
    std::memset(fixture.large_payload, 'x', kLargePayloadBytes);
    fixture.large_payload[kLargePayloadBytes] = '\0';
}

void registerCommands() {
    const api::ArgumentSpec echo_args[]{{"value", api::ValueType::Int}};
    const api::CommandSpec echo_spec{"test_echo", echo_args, 1, false};
    (void)server.registerCommand(echo_spec, testEcho, &fixture);

    const api::CommandSpec oversized_spec{"test_oversized_result", nullptr, 0, false};
    (void)server.registerCommand(oversized_spec, testOversizedResult, &fixture);

    const api::CommandSpec estop_success_spec{"test_estop_success", nullptr, 0, false};
    (void)server.registerCommand(estop_success_spec, testEstopSuccess, &fixture);

    const api::CommandSpec estop_fail_spec{"test_estop_fail", nullptr, 0, false};
    (void)server.registerCommand(estop_fail_spec, testEstopFail, &fixture);

    const api::CommandSpec estop_over_budget_spec{"test_estop_over_budget", nullptr, 0, false};
    (void)server.registerCommand(estop_over_budget_spec, testEstopOverBudget, &fixture);

    const api::ArgumentSpec loss_args[]{{"enabled", api::ValueType::Bool}};
    const api::CommandSpec loss_spec{"test_loss_over_budget", loss_args, 1, false};
    (void)server.registerCommand(loss_spec, testLossOverBudget, &fixture);

    const api::ArgumentSpec telemetry_args[]{{"mode", api::ValueType::Int}};
    const api::CommandSpec telemetry_spec{"test_telemetry_mode", telemetry_args, 1, false};
    (void)server.registerCommand(telemetry_spec, testTelemetryMode, &fixture);

    const api::CommandSpec trigger_spec{"test_trigger_estop", nullptr, 0, false};
    (void)server.registerCommand(trigger_spec, testTriggerEstop, &fixture);
}

void setup() {
    fillLargePayload();

    (void)server.setIdentity({"command_server_conformance", "1", "0.1.0"});
    (void)server.setNetworkConfig(network_config);
    registerCommands();

    const api::FieldSpec telemetry_fields[]{
        {"echo_value", api::ValueType::Int},
        {"telemetry_mode", api::ValueType::Int},
        {"overflow_payload", api::ValueType::String},
    };
    (void)server.registerTelemetrySchema(telemetry_fields, 3);
    (void)server.setTelemetryProvider(writeTelemetry, &fixture);
    (void)server.setEstopHook(handleEstop, &fixture);
    (void)server.setControllerLossHook(handleControllerLoss, &fixture);
    (void)server.enableCountersDiagnosticCommand();
    (void)server.start();
}

void loop() {
    if (fixture.estop_trigger_pending) {
        fixture.estop_trigger_pending = false;
        (void)server.requestEstopTriggered(fixture.estop_reason);
    }
    server.service();
}
