#pragma once

#include <cstdint>

namespace teensy_command_server::core {

class Clock {
public:
    virtual ~Clock() = default;

    virtual std::uint64_t monotonicMilliseconds() const = 0;
    virtual std::uint64_t monotonicMicroseconds() const = 0;
};

}  // namespace teensy_command_server::core
