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
    NoActiveTransport,
    Busy,
    DeadlineExpired,
};

enum class TeardownReason {
    CriticalTransmitFailure,
    TelemetryTransmitFailureStreak,
    ConnectionLost,
    LinkLost,
    SchemaSendFailure,
    ExplicitShutdown,
    Superseded,
};

using SessionTeardownReason = TeardownReason;

class OutboundWriter {
public:
    OutboundWriter() = default;

    template <typename TeardownSink>
    OutboundWriter(NetworkServer& network,
                   Clock& clock,
                   Counters& counters,
                   TeardownSink& teardown_sink)
        : network_(&network),
          clock_(&clock),
          counters_(&counters),
          teardown_context_(&teardown_sink),
          teardown_callback_(&requestTeardownFrom<TeardownSink>) {}

    template <typename TeardownSink>
    OutboundWriter(Transport& transport,
                   NetworkServer& network,
                   Clock& clock,
                   Counters& counters,
                   TeardownSink& teardown_sink)
        : transport_(&transport),
          network_(&network),
          clock_(&clock),
          counters_(&counters),
          teardown_context_(&teardown_sink),
          teardown_callback_(&requestTeardownFrom<TeardownSink>) {}

    OutboundSendResult sendLine(ConstLineView line, MessageClass message_class) {
        if (send_in_progress_) {
            return OutboundSendResult::Busy;
        }

        if (!isValidWireLine(line)) {
            return OutboundSendResult::InvalidLine;
        }

        if (transport_ == nullptr || network_ == nullptr || clock_ == nullptr ||
            counters_ == nullptr) {
            return OutboundSendResult::NoActiveTransport;
        }

        send_in_progress_ = true;
        const OutboundSendResult result = sendValidLine(line, message_class);
        send_in_progress_ = false;
        return result;
    }

    void beginSession() {
        consecutive_telemetry_failures_ = 0;
    }

    void bindTransport(Transport& transport) {
        transport_ = &transport;
        beginSession();
    }

    void clearTransport() {
        transport_ = nullptr;
        send_in_progress_ = false;
        beginSession();
    }

    std::uint8_t consecutiveTelemetryFailures() const {
        return consecutive_telemetry_failures_;
    }

private:
    using TeardownCallback = void (*)(void*, TeardownReason);

    template <typename TeardownSink>
    static void requestTeardownFrom(void* context, TeardownReason reason) {
        static_cast<TeardownSink*>(context)->requestTeardown(reason);
    }

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
        const std::uint64_t start_ms = clock_->monotonicMilliseconds();
        std::size_t accepted = 0;

        while (accepted < line.size) {
            if (deadlineExpired(start_ms)) {
                return handleDeadline(message_class);
            }

            const std::size_t written = transport_->writeSome(
                reinterpret_cast<const std::uint8_t*>(line.data + accepted), line.size - accepted);
            accepted += written;

            if (accepted == line.size) {
                transport_->flush();
                if (message_class == MessageClass::Telemetry) {
                    consecutive_telemetry_failures_ = 0;
                }
                return OutboundSendResult::Sent;
            }

            network_->progress();
        }

        return OutboundSendResult::Sent;
    }

    bool deadlineExpired(std::uint64_t start_ms) const {
        return static_cast<std::uint64_t>(clock_->monotonicMilliseconds() - start_ms) >=
               support::kTransmitDeadlineMs;
    }

    OutboundSendResult handleDeadline(MessageClass message_class) {
        if (message_class == MessageClass::Critical) {
            counters_->increment(&Counters::tx_failures);
            requestTeardown(TeardownReason::CriticalTransmitFailure);
            return OutboundSendResult::DeadlineExpired;
        }

        counters_->increment(&Counters::telemetry_dropped);
        const bool below_teardown_threshold =
            consecutive_telemetry_failures_ < support::kTelemetryDeadlineFailuresToTeardown;
        if (below_teardown_threshold) {
            ++consecutive_telemetry_failures_;
        }
        if (below_teardown_threshold &&
            consecutive_telemetry_failures_ >= support::kTelemetryDeadlineFailuresToTeardown) {
            requestTeardown(TeardownReason::TelemetryTransmitFailureStreak);
        }
        return OutboundSendResult::DeadlineExpired;
    }

    void requestTeardown(TeardownReason reason) {
        if (teardown_callback_ != nullptr && teardown_context_ != nullptr) {
            teardown_callback_(teardown_context_, reason);
        }
    }

    Transport* transport_ = nullptr;
    NetworkServer* network_ = nullptr;
    Clock* clock_ = nullptr;
    Counters* counters_ = nullptr;
    void* teardown_context_ = nullptr;
    TeardownCallback teardown_callback_ = nullptr;
    std::uint8_t consecutive_telemetry_failures_ = 0;
    bool send_in_progress_ = false;
};

}  // namespace teensy_command_server::core
