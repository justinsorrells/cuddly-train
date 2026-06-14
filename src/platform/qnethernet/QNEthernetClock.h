#pragma once

#include <cstdint>

#include <Arduino.h>

#include "core/Clock.h"

namespace teensy_command_server::platform::qnethernet {

class QNEthernetClock final : public core::Clock {
public:
    std::uint64_t monotonicMilliseconds() const override {
        return static_cast<std::uint64_t>(millis());
    }

    std::uint64_t monotonicMicroseconds() const override {
        return static_cast<std::uint64_t>(micros());
    }
};

}  // namespace teensy_command_server::platform::qnethernet
