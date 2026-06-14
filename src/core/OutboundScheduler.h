#pragma once

#include "core/OutboundWriter.h"
#include "support/Limits.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace teensy_command_server::core {

enum class OutboundKind : std::uint8_t {
    CommandResponse,
    EstopAck,
    EstopTriggered,
    HeartbeatAck,
    Telemetry,
};

enum class OutboundCompletion : std::uint8_t {
    Sent,
    SendFailed,
    CanceledBySessionEnd,
};

struct OutboundOutcome {
    OutboundKind kind;
    OutboundCompletion completion;
};

enum class OutboundEnqueueResult : std::uint8_t {
    Queued,
    InvalidLine,
    TeardownRequested,
};

enum class EstopTriggeredPendingState : std::uint8_t {
    NotPending,
    PendingNotQueued,
    Queued,
};

class OutboundCancellationBatch {
public:
    bool append(OutboundOutcome outcome) {
        if (count_ >= support::kOutboundCancellationBatchCapacity) {
            return false;
        }
        outcomes_[count_] = outcome;
        ++count_;
        return true;
    }

    std::size_t count() const {
        return count_;
    }

    const OutboundOutcome& at(std::size_t index) const {
        return outcomes_[index];
    }

private:
    OutboundOutcome outcomes_[support::kOutboundCancellationBatchCapacity]{};
    std::size_t count_ = 0;
};

class OutboundScheduler {
public:
    OutboundScheduler() {
        clear();
    }

    OutboundEnqueueResult enqueueCritical(OutboundKind kind, ConstLineView line) {
        if (kind == OutboundKind::Telemetry || !isValidLine(line)) {
            return OutboundEnqueueResult::InvalidLine;
        }
        if (critical_count_ >= support::kOutboundCriticalQueueCapacity) {
            return OutboundEnqueueResult::TeardownRequested;
        }

        CriticalEntry& entry = critical_[critical_count_];
        entry.kind = kind;
        entry.priority = priorityFor(kind);
        entry.size = line.size;
        std::memcpy(entry.bytes, line.data, line.size);
        entry.active = true;
        ++critical_count_;
        if (kind == OutboundKind::EstopTriggered) {
            estop_triggered_state_ = EstopTriggeredPendingState::Queued;
        }
        return OutboundEnqueueResult::Queued;
    }

    void markEstopTriggeredPending() {
        if (estop_triggered_state_ == EstopTriggeredPendingState::NotPending) {
            estop_triggered_state_ = EstopTriggeredPendingState::PendingNotQueued;
        }
    }

    EstopTriggeredPendingState estopTriggeredState() const {
        return estop_triggered_state_;
    }

    OutboundEnqueueResult replaceTelemetry(ConstLineView line, Counters& counters) {
        if (!isValidLine(line)) {
            return OutboundEnqueueResult::InvalidLine;
        }
        if (telemetry_active_) {
            counters.increment(&Counters::telemetry_coalesced);
        }
        std::memcpy(telemetry_bytes_, line.data, line.size);
        telemetry_size_ = line.size;
        telemetry_active_ = true;
        return OutboundEnqueueResult::Queued;
    }

    bool hasPending() const {
        return critical_count_ > 0 || telemetry_active_;
    }

    std::size_t criticalCount() const {
        return critical_count_;
    }

    bool telemetryPending() const {
        return telemetry_active_;
    }

    template <typename SessionDriverLike>
    bool drainOne(SessionDriverLike& session, OutboundOutcome& outcome) {
        const int critical_index = selectCriticalIndex();
        if (critical_index >= 0) {
            const std::size_t index = static_cast<std::size_t>(critical_index);
            const OutboundKind kind = critical_[index].kind;
            const OutboundSendResult result =
                session.sendActiveLine({critical_[index].bytes, critical_[index].size},
                                       MessageClass::Critical);
            removeCritical(index);
            outcome = {kind, completionFor(result)};
            return true;
        }

        if (telemetry_active_) {
            const std::size_t size = telemetry_size_;
            const OutboundSendResult result =
                session.sendActiveLine({telemetry_bytes_, size}, MessageClass::Telemetry);
            telemetry_active_ = false;
            telemetry_size_ = 0;
            outcome = {OutboundKind::Telemetry, completionFor(result)};
            return true;
        }

        return false;
    }

    OutboundCancellationBatch clearForSessionEnd() {
        OutboundCancellationBatch batch;
        for (std::size_t i = 0; i < critical_count_; ++i) {
            batch.append({critical_[i].kind, OutboundCompletion::CanceledBySessionEnd});
            if (critical_[i].kind == OutboundKind::EstopTriggered) {
                estop_triggered_state_ = EstopTriggeredPendingState::PendingNotQueued;
            }
        }
        critical_count_ = 0;
        for (CriticalEntry& entry : critical_) {
            entry = {};
        }

        if (telemetry_active_) {
            batch.append({OutboundKind::Telemetry, OutboundCompletion::CanceledBySessionEnd});
            telemetry_active_ = false;
            telemetry_size_ = 0;
        }
        return batch;
    }

    void clear() {
        critical_count_ = 0;
        for (CriticalEntry& entry : critical_) {
            entry = {};
        }
        telemetry_active_ = false;
        telemetry_size_ = 0;
        estop_triggered_state_ = EstopTriggeredPendingState::NotPending;
    }

private:
    enum class Priority : std::uint8_t {
        Safety = 0,
        CommandResponse = 1,
        HeartbeatAck = 2,
    };

    struct CriticalEntry {
        OutboundKind kind = OutboundKind::CommandResponse;
        Priority priority = Priority::CommandResponse;
        char bytes[support::kOutboundCriticalEntryMaxBytes]{};
        std::size_t size = 0;
        bool active = false;
    };

    static Priority priorityFor(OutboundKind kind) {
        if (kind == OutboundKind::EstopAck || kind == OutboundKind::EstopTriggered) {
            return Priority::Safety;
        }
        if (kind == OutboundKind::HeartbeatAck) {
            return Priority::HeartbeatAck;
        }
        return Priority::CommandResponse;
    }

    static bool isValidLine(ConstLineView line) {
        if (line.data == nullptr || line.size == 0 ||
            line.size > support::kOutboundCriticalEntryMaxBytes) {
            return false;
        }
        return line.data[line.size - 1] == '\n';
    }

    int selectCriticalIndex() const {
        if (critical_count_ == 0) {
            return -1;
        }
        std::size_t selected = 0;
        for (std::size_t i = 1; i < critical_count_; ++i) {
            if (static_cast<std::uint8_t>(critical_[i].priority) <
                static_cast<std::uint8_t>(critical_[selected].priority)) {
                selected = i;
            }
        }
        return static_cast<int>(selected);
    }

    void removeCritical(std::size_t index) {
        if (index >= critical_count_) {
            return;
        }
        if (critical_[index].kind == OutboundKind::EstopTriggered) {
            estop_triggered_state_ = EstopTriggeredPendingState::NotPending;
        }
        for (std::size_t i = index; i + 1 < critical_count_; ++i) {
            critical_[i] = critical_[i + 1];
        }
        --critical_count_;
        critical_[critical_count_] = {};
    }

    static OutboundCompletion completionFor(OutboundSendResult result) {
        return result == OutboundSendResult::Sent ? OutboundCompletion::Sent
                                                  : OutboundCompletion::SendFailed;
    }

    // Approximate RAM: 4 x 8192-byte critical buffers, 1 x 8192-byte telemetry
    // buffer, plus metadata/alignment. Keep this object long-lived.
    CriticalEntry critical_[support::kOutboundCriticalQueueCapacity]{};
    std::size_t critical_count_ = 0;
    char telemetry_bytes_[support::kBoardTxMaxLineBytes]{};
    std::size_t telemetry_size_ = 0;
    bool telemetry_active_ = false;
    EstopTriggeredPendingState estop_triggered_state_ =
        EstopTriggeredPendingState::NotPending;
};

static_assert(sizeof(OutboundScheduler) <= support::kOutboundSchedulerMaxBytes,
              "OutboundScheduler exceeds the pinned RAM budget");

}  // namespace teensy_command_server::core
