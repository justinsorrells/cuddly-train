#pragma once

#include <cstdint>

#include "core/Clock.h"

namespace teensy_command_server::host::fakes {

class FakeClock final : public core::Clock {
public:
    std::uint64_t monotonicMilliseconds() const override {
        if (explicit_ms_enabled_) {
            return now_ms_;
        }
        return now_us_ / 1000;
    }

    std::uint64_t monotonicMicroseconds() const override {
        return now_us_;
    }

    void advanceMilliseconds(std::uint64_t delta_ms) {
        if (explicit_ms_enabled_) {
            now_ms_ += delta_ms;
            return;
        }
        now_us_ += delta_ms * 1000;
    }

    void advanceMicroseconds(std::uint64_t delta_us) {
        explicit_ms_enabled_ = false;
        now_us_ += delta_us;
    }

    void setMonotonicMilliseconds(std::uint64_t now_ms) {
        now_ms_ = now_ms;
        explicit_ms_enabled_ = true;
    }

    void setMonotonicMicroseconds(std::uint64_t now_us) {
        now_us_ = now_us;
        explicit_ms_enabled_ = false;
    }

private:
    std::uint64_t now_us_ = 0;
    std::uint64_t now_ms_ = 0;
    bool explicit_ms_enabled_ = false;
};

}  // namespace teensy_command_server::host::fakes
