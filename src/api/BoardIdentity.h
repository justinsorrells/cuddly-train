#pragma once

#include <cstdint>

namespace teensy_command_server::api {

struct BoardIdentity {
    const char* board_id;
    const char* protocol_version;
    const char* firmware_version;
};

struct NetworkConfig {
    enum class Mode : std::uint8_t {
        Dhcp,
        StaticIpv4,
    };

    Mode mode;
    std::uint16_t listen_port;
    std::uint8_t ip[4];
    std::uint8_t gateway[4];
    std::uint8_t subnet[4];
};

struct ServerConfig {
    BoardIdentity identity;
    NetworkConfig network;
};

}  // namespace teensy_command_server::api
