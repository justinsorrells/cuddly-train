#include <TeensyCommandServer.h>
#include <platform/qnethernet/Platform.h>

namespace tcs = teensy_command_server;
namespace api = teensy_command_server::api;
namespace qnp = teensy_command_server::platform::qnethernet;

constexpr std::uint16_t kListenPort = 5050;

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
//     {192, 168, 1, 50},
//     {192, 168, 1, 1},
//     {255, 255, 255, 0},
// };

qnp::QNEthernetNetworkServer network(network_config);
qnp::QNEthernetClock platform_clock;
tcs::TeensyCommandServer server(network, platform_clock);

api::CommandResult smokePing(const api::CommandContext&,
                             api::ObjectWriter& result,
                             void*) {
    if (!result.addBool("pong", true)) {
        return api::CommandResult::internalError("smoke result overflow");
    }
    return api::CommandResult::ok();
}

bool writeTelemetry(api::ObjectWriter& telemetry, void*) {
    return telemetry.addInt("uptime_ms", static_cast<std::int32_t>(millis()));
}

bool applySafeState(void*) {
    return true;
}

void setup() {
    (void)server.setIdentity({"ethernet_smoke", "1", "0.1.0"});
    (void)server.setNetworkConfig(network_config);

    const api::CommandSpec ping_spec{"smoke_ping", nullptr, 0, false};
    (void)server.registerCommand(ping_spec, smokePing, nullptr);

    const api::FieldSpec telemetry_fields[]{{"uptime_ms", api::ValueType::Int}};
    (void)server.registerTelemetrySchema(telemetry_fields, 1);
    (void)server.setTelemetryProvider(writeTelemetry, nullptr);
    (void)server.setEstopHook(applySafeState, nullptr);
    (void)server.setControllerLossHook(applySafeState, nullptr);
    (void)server.start();
}

void loop() {
    server.service();
}
