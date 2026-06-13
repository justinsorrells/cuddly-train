#pragma once

#include <cstdint>

#include "core/Transport.h"

namespace teensy_command_server::core {

enum class NetworkAvailability {
    Uninitialized,
    LinkDown,
    AddressPending,
    Ready,
};

struct ConnectionHandle {
    std::uint8_t slot;
    std::uint32_t generation;
};

constexpr ConnectionHandle kInvalidConnection{0xFFU, 0U};

constexpr bool isValidConnectionHandle(ConnectionHandle handle) {
    return handle.generation != 0U && handle.slot != kInvalidConnection.slot;
}

constexpr bool sameConnection(ConnectionHandle left, ConnectionHandle right) {
    return left.slot == right.slot && left.generation == right.generation;
}

class NetworkServer {
public:
    static constexpr std::uint8_t kConnectionSlotCount = 2;

    virtual ~NetworkServer() = default;

    virtual void progress() = 0;
    virtual NetworkAvailability availability() const = 0;
    virtual bool begin(std::uint16_t port) = 0;
    virtual bool isListening() const = 0;
    virtual bool hasPendingConnection() const = 0;
    virtual ConnectionHandle accept() = 0;
    virtual Transport* transport(ConnectionHandle handle) = 0;
    virtual void close(ConnectionHandle handle) = 0;
    virtual void abort(ConnectionHandle handle) = 0;
};

}  // namespace teensy_command_server::core
