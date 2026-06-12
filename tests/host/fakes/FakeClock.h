#pragma once

#include <cstdint>

#include "core/Clock.h"

namespace teensy_command_server::host::fakes {

class FakeClock final : public core::Clock {
public:
    std::uint64_t monotonicMilliseconds() const override {
        return now_us_ / 1000;
    }

    std::uint64_t monotonicMicroseconds() const override {
        return now_us_;
    }

    void advanceMilliseconds(std::uint64_t delta_ms) {
        now_us_ += delta_ms * 1000;
    }

    void advanceMicroseconds(std::uint64_t delta_us) {
        now_us_ += delta_us;
    }

private:
    std::uint64_t now_us_ = 0;
};

}  // namespace teensy_command_server::host::fakes
