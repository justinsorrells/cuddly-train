#pragma once

#include <cstddef>
#include <cstdint>

namespace teensy_command_server::core {

enum class TransportConnectionState {
    Open,
    BytesAvailable,
    Closed,
    LinkDown,
};

class Transport {
public:
    virtual ~Transport() = default;

    virtual std::size_t readSome(std::uint8_t* buffer, std::size_t capacity) = 0;
    virtual std::size_t writeSome(const std::uint8_t* data, std::size_t length) = 0;
    virtual bool flush() = 0;
    virtual TransportConnectionState connectionState() const = 0;
};

}  // namespace teensy_command_server::core
