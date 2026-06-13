#pragma once

#include <cstddef>
#include <cstdint>

#include "core/Clock.h"
#include "core/Counters.h"
#include "core/NetworkServer.h"
#include "core/Transport.h"
#include "support/Limits.h"

namespace teensy_command_server::core {

struct ConstLineView {
    const char* data = nullptr;
    std::size_t size = 0;
};

enum class MessageClass {
    Critical,
    Telemetry,
};

enum class OutboundSendResult {
    Sent,
    InvalidLine,
    Busy,
    DeadlineExpired,
};

enum class SessionTeardownReason {
    CriticalTransmitFailure,
    TelemetryTransmitFailureStreak,
};

class SessionDriver {
public:
    virtual ~SessionDriver() = default;

    virtual void requestTeardown(SessionTeardownReason reason) = 0;
};

class OutboundWriter {
public:
    OutboundWriter(Transport& transport,
                   NetworkServer& network,
                   Clock& clock,
                   Counters& counters,
                   SessionDriver& session_driver)
        : transport_(transport),
          network_(network),
          clock_(clock),
          counters_(counters),
          session_driver_(session_driver) {}

    OutboundSendResult sendLine(ConstLineView line, MessageClass message_class) {
        if (send_in_progress_) {
            return OutboundSendResult::Busy;
        }

        if (!isValidWireLine(line)) {
            return OutboundSendResult::InvalidLine;
        }

        send_in_progress_ = true;
        const OutboundSendResult result = sendValidLine(line, message_class);
        send_in_progress_ = false;
        return result;
    }

    void beginSession() {
        consecutive_telemetry_failures_ = 0;
    }

    std::uint8_t consecutiveTelemetryFailures() const {
        return consecutive_telemetry_failures_;
    }

private:
    static bool isValidWireLine(ConstLineView line) {
        if (line.data == nullptr || line.size == 0 || line.size > support::kBoardTxMaxLineBytes) {
            return false;
        }
        if (line.data[line.size - 1] != '\n') {
            return false;
        }
        for (std::size_t index = 0; index + 1 < line.size; ++index) {
            if (line.data[index] == '\n') {
                return false;
            }
        }
        return true;
    }

    OutboundSendResult sendValidLine(ConstLineView line, MessageClass message_class) {
        const std::uint64_t start_ms = clock_.monotonicMilliseconds();
        std::size_t accepted = 0;

        while (accepted < line.size) {
            if (deadlineExpired(start_ms)) {
                return handleDeadline(message_class);
            }

            const std::size_t written = transport_.writeSome(
                reinterpret_cast<const std::uint8_t*>(line.data + accepted), line.size - accepted);
            accepted += written;

            if (accepted == line.size) {
                transport_.flush();
                if (message_class == MessageClass::Telemetry) {
                    consecutive_telemetry_failures_ = 0;
                }
                return OutboundSendResult::Sent;
            }

            network_.progress();
        }

        return OutboundSendResult::Sent;
    }

    bool deadlineExpired(std::uint64_t start_ms) const {
        return static_cast<std::uint64_t>(clock_.monotonicMilliseconds() - start_ms) >=
               support::kTransmitDeadlineMs;
    }

    OutboundSendResult handleDeadline(MessageClass message_class) {
        if (message_class == MessageClass::Critical) {
            counters_.increment(&Counters::tx_failures);
            session_driver_.requestTeardown(SessionTeardownReason::CriticalTransmitFailure);
            return OutboundSendResult::DeadlineExpired;
        }

        counters_.increment(&Counters::telemetry_dropped);
        const bool below_teardown_threshold =
            consecutive_telemetry_failures_ < support::kTelemetryDeadlineFailuresToTeardown;
        if (below_teardown_threshold) {
            ++consecutive_telemetry_failures_;
        }
        if (below_teardown_threshold &&
            consecutive_telemetry_failures_ >= support::kTelemetryDeadlineFailuresToTeardown) {
            session_driver_.requestTeardown(
                SessionTeardownReason::TelemetryTransmitFailureStreak);
        }
        return OutboundSendResult::DeadlineExpired;
    }

    Transport& transport_;
    NetworkServer& network_;
    Clock& clock_;
    Counters& counters_;
    SessionDriver& session_driver_;
    std::uint8_t consecutive_telemetry_failures_ = 0;
    bool send_in_progress_ = false;
};

}  // namespace teensy_command_server::core
