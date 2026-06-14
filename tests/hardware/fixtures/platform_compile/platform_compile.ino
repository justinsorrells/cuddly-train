#include <TeensyCommandServer.h>
#include <platform/qnethernet/Platform.h>

namespace tcs = teensy_command_server;
namespace qnp = teensy_command_server::platform::qnethernet;

tcs::api::NetworkConfig network_config{
    tcs::api::NetworkConfig::Mode::Dhcp,
    5050,
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
};

qnp::QNEthernetNetworkServer network(network_config);
qnp::QNEthernetClock platform_clock;
tcs::TeensyCommandServer server(network, platform_clock);

void setup() {
    (void)server.setNetworkConfig(network_config);
}

void loop() {
    server.service();
}
