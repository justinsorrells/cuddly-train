#pragma once

#include "core/Counters.h"
#include "support/Limits.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace teensy_command_server::core {

struct MutableLineView {
    char* data = nullptr;
    std::size_t size = 0;

    bool valid() const {
        return data != nullptr;
    }
};

class LineFramer {
public:
    static constexpr std::size_t kMaxWireLineBytes = support::kBoardRxMaxLineBytes;
    static constexpr std::size_t kMaxPayloadBytes = kMaxWireLineBytes - 1;

    explicit LineFramer(Counters& counters) : counters_(counters) {}

    bool hasLine() const {
        return active_ready_ && !active_acquired_;
    }

    MutableLineView acquireLine() {
        if (active_acquired_ || !active_ready_) {
            return {};
        }

        active_acquired_ = true;
        return {active_line_, active_length_};
    }

    void releaseLine() {
        if (!active_acquired_) {
            return;
        }

        active_acquired_ = false;
        active_ready_ = false;
        active_length_ = 0;
        active_line_[0] = '\0';
        promotePendingIfPossible();
    }

    void closeSession() {
        active_ready_ = false;
        active_acquired_ = false;
        active_length_ = 0;
        active_line_[0] = '\0';

        pending_ready_ = false;
        pending_length_ = 0;
        pending_line_[0] = '\0';

        accumulator_length_ = 0;
        discard_until_newline_ = false;
    }

    std::size_t append(const std::uint8_t* data, std::size_t size) {
        if (data == nullptr) {
            return 0;
        }

        std::size_t consumed = 0;
        while (consumed < size) {
            if (pending_ready_) {
                break;
            }

            const char byte = static_cast<char>(data[consumed]);
            ++consumed;

            if (discard_until_newline_) {
                if (byte == '\n') {
                    discard_until_newline_ = false;
                }
                continue;
            }

            if (byte == '\n') {
                completeAccumulatedLine();
                continue;
            }

            if (accumulator_length_ == kMaxPayloadBytes) {
                counters_.increment(&Counters::oversized_lines);
                accumulator_length_ = 0;
                pending_line_[0] = '\0';
                discard_until_newline_ = true;
                continue;
            }

            pending_line_[accumulator_length_] = byte;
            ++accumulator_length_;
            pending_line_[accumulator_length_] = '\0';
        }

        return consumed;
    }

    std::size_t append(const char* data, std::size_t size) {
        return append(reinterpret_cast<const std::uint8_t*>(data), size);
    }

    void discardPartialLine() {
        accumulator_length_ = 0;
        pending_line_[0] = '\0';
        discard_until_newline_ = false;
    }

private:
    void completeAccumulatedLine() {
        pending_length_ = accumulator_length_;
        pending_line_[pending_length_] = '\0';
        accumulator_length_ = 0;
        pending_ready_ = true;
        promotePendingIfPossible();
    }

    void promotePendingIfPossible() {
        if (!pending_ready_ || active_ready_ || active_acquired_) {
            return;
        }

        std::memcpy(active_line_, pending_line_, pending_length_ + 1);
        active_length_ = pending_length_;
        active_ready_ = true;

        pending_ready_ = false;
        pending_length_ = 0;
        pending_line_[0] = '\0';
    }

    Counters& counters_;
    char active_line_[kMaxPayloadBytes + 1]{};
    char pending_line_[kMaxPayloadBytes + 1]{};
    std::size_t active_length_ = 0;
    std::size_t pending_length_ = 0;
    std::size_t accumulator_length_ = 0;
    bool active_ready_ = false;
    bool active_acquired_ = false;
    bool pending_ready_ = false;
    bool discard_until_newline_ = false;
};

}  // namespace teensy_command_server::core
