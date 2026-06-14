#pragma once

#include "api/BoardIdentity.h"
#include "api/SafetyHooks.h"
#include "api/ServerStatus.h"
#include "core/Clock.h"
#include "core/Counters.h"
#include "core/InboundParser.h"
#include "core/OutboundScheduler.h"
#include "core/Protocol.h"
#include "support/BoundedJsonWriter.h"
#include "support/Limits.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace teensy_command_server::core {

class EstopHandler {
public:
    enum class PendingState : std::uint8_t {
        Empty,
        PendingNotQueued,
        QueuedAwaitingCompletion,
    };

    api::Status requestEstopTriggered(const char* reason,
                                      const Clock* clock,
                                      OutboundScheduler& scheduler) {
        if (state_ != PendingState::Empty) {
            return status(api::StatusCode::CapacityExceeded,
                          "estop_triggered event is already pending");
        }
        const std::size_t length = boundedLength(reason, support::kEstopTriggeredReasonMaxBytes);
        if (reason == nullptr || length > support::kEstopTriggeredReasonMaxBytes) {
            return status(api::StatusCode::CapacityExceeded,
                          "estop_triggered reason exceeds capacity");
        }
        std::memcpy(reason_, reason, length);
        reason_[length] = '\0';
        reason_length_ = length;
        timestamp_ms_ = clock == nullptr ? 0 : clock->monotonicMilliseconds();
        state_ = PendingState::PendingNotQueued;
        scheduler.markEstopTriggeredPending();
        return status(api::StatusCode::Ok, "ok");
    }

    bool handleEstop(const ParsedEstop&,
                     Counters& counters,
                     const api::BoardIdentity& identity,
                     const Clock* clock,
                     api::SafetyHook hook,
                     void* hook_context,
                     OutboundScheduler& scheduler) {
        counters.increment(&Counters::estop_received);
        if (hook == nullptr) {
            counters.increment(&Counters::estop_apply_failed);
            return false;
        }

        const std::uint64_t start_us = clock == nullptr ? 0 : clock->monotonicMicroseconds();
        const bool safe = hook(hook_context);
        const std::uint64_t elapsed_us =
            clock == nullptr ? 0 : static_cast<std::uint64_t>(clock->monotonicMicroseconds() -
                                                              start_us);
        if (elapsed_us > static_cast<std::uint64_t>(support::kHookBudgetMs) * 1000ULL) {
            counters.increment(&Counters::estop_hook_over_budget);
        }
        if (!safe) {
            counters.increment(&Counters::estop_apply_failed);
            return false;
        }

        char line[support::kEventJsonBufferBytes]{};
        std::size_t size = 0;
        if (!buildEstopAckLine(identity, clock, line, sizeof(line), size)) {
            counters.increment(&Counters::estop_apply_failed);
            return true;
        }
        const OutboundEnqueueResult result =
            scheduler.enqueueCritical(OutboundKind::EstopAck, {line, size});
        return result != OutboundEnqueueResult::Queued;
    }

    bool service(OutboundScheduler& scheduler, const api::BoardIdentity& identity) {
        if (state_ != PendingState::PendingNotQueued) {
            return false;
        }

        char line[support::kEventJsonBufferBytes]{};
        std::size_t size = 0;
        if (!buildEstopTriggeredLine(identity, line, sizeof(line), size)) {
            return true;
        }

        scheduler.markEstopTriggeredPending();
        const OutboundEnqueueResult result =
            scheduler.enqueueCritical(OutboundKind::EstopTriggered, {line, size});
        if (result == OutboundEnqueueResult::Queued) {
            state_ = PendingState::QueuedAwaitingCompletion;
            return false;
        }
        scheduler.markEstopTriggeredPending();
        return result == OutboundEnqueueResult::TeardownRequested;
    }

    void onOutboundOutcome(const OutboundOutcome& outcome, OutboundScheduler& scheduler) {
        if (outcome.kind != OutboundKind::EstopTriggered ||
            state_ != PendingState::QueuedAwaitingCompletion) {
            return;
        }
        if (outcome.completion == OutboundCompletion::Sent) {
            clearPending();
            return;
        }
        state_ = PendingState::PendingNotQueued;
        scheduler.markEstopTriggeredPending();
    }

    PendingState pendingState() const {
        return state_;
    }

    const char* pendingReasonForTest() const {
        return reason_;
    }

    std::uint64_t pendingTimestampForTest() const {
        return timestamp_ms_;
    }

private:
    static api::Status status(api::StatusCode code, const char* message) {
        return {code, message};
    }

    static std::size_t boundedLength(const char* value, std::size_t capacity) {
        if (value == nullptr) {
            return capacity + 1;
        }
        std::size_t length = 0;
        while (length <= capacity && value[length] != '\0') {
            ++length;
        }
        return length;
    }

    void clearPending() {
        state_ = PendingState::Empty;
        reason_[0] = '\0';
        reason_length_ = 0;
        timestamp_ms_ = 0;
    }

    static bool buildEstopAckLine(const api::BoardIdentity& identity,
                                  const Clock* clock,
                                  char* buffer,
                                  std::size_t capacity,
                                  std::size_t& size) {
        support::BoundedJsonWriter writer(buffer, capacity);
        writer.reserveTailBytes(1);
        if (!writer.beginObject() ||
            !writer.addString(field::kType, toString(MessageType::Event)) ||
            !writer.addUInt64(field::kTimestamp,
                              clock == nullptr ? 0 : clock->monotonicMilliseconds()) ||
            !writer.addString(field::kSource, identity.board_id) ||
            !writer.addString(field::kTarget, "controller") ||
            !writer.addString(field::kEvent, toString(EventName::EstopAck)) ||
            !writer.beginObjectField(field::kDetails) ||
            !writer.addString(field::kState, kEstopAckSafeState) ||
            !writer.endObject() ||
            !writer.endObject()) {
            return false;
        }
        writer.releaseTailReserve();
        if (!writer.appendNewline() || writer.size() > support::kBoardTxMaxLineBytes) {
            return false;
        }
        size = writer.size();
        return true;
    }

    bool buildEstopTriggeredLine(const api::BoardIdentity& identity,
                                 char* buffer,
                                 std::size_t capacity,
                                 std::size_t& size) const {
        support::BoundedJsonWriter writer(buffer, capacity);
        writer.reserveTailBytes(1);
        if (!writer.beginObject() ||
            !writer.addString(field::kType, toString(MessageType::Event)) ||
            !writer.addUInt64(field::kTimestamp, timestamp_ms_) ||
            !writer.addString(field::kSource, identity.board_id) ||
            !writer.addString(field::kTarget, "controller") ||
            !writer.addString(field::kEvent, toString(EventName::EstopTriggered)) ||
            !writer.beginObjectField(field::kDetails) ||
            !writer.addString(field::kReason, reason_, reason_length_) ||
            !writer.endObject() ||
            !writer.endObject()) {
            return false;
        }
        writer.releaseTailReserve();
        if (!writer.appendNewline() || writer.size() > support::kBoardTxMaxLineBytes) {
            return false;
        }
        size = writer.size();
        return true;
    }

    PendingState state_ = PendingState::Empty;
    char reason_[support::kEstopTriggeredReasonMaxBytes + 1]{};
    std::size_t reason_length_ = 0;
    std::uint64_t timestamp_ms_ = 0;
};

}  // namespace teensy_command_server::core
