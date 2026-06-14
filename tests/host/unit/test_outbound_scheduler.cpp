#include "core/OutboundScheduler.h"
#include "core/Counters.h"
#include "core/OutboundWriter.h"

#include <cassert>
#include <cstring>
#include <string>

namespace {

namespace core = teensy_command_server::core;
namespace limits = teensy_command_server::support;

struct FakeSession {
    core::OutboundSendResult result = core::OutboundSendResult::Sent;
    unsigned sends = 0;
    std::string written;
    core::MessageClass last_class = core::MessageClass::Critical;

    core::OutboundSendResult sendActiveLine(core::ConstLineView line,
                                            core::MessageClass message_class) {
        ++sends;
        last_class = message_class;
        written.assign(line.data, line.size);
        return result;
    }
};

core::ConstLineView line(const char* value) {
    return {value, std::strlen(value)};
}

void priorityAndFifoOrdering() {
    core::OutboundScheduler scheduler;
    FakeSession session;
    core::OutboundOutcome outcome{};

    assert(scheduler.enqueueCritical(core::OutboundKind::HeartbeatAck, line("{\"h\":1}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse, line("{\"r\":1}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::EstopAck, line("{\"s\":1}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse, line("{\"r\":2}\n")) ==
           core::OutboundEnqueueResult::Queued);

    assert(scheduler.drainOne(session, outcome));
    assert(outcome.kind == core::OutboundKind::EstopAck);
    assert(session.written == "{\"s\":1}\n");

    assert(scheduler.drainOne(session, outcome));
    assert(outcome.kind == core::OutboundKind::CommandResponse);
    assert(session.written == "{\"r\":1}\n");

    assert(scheduler.drainOne(session, outcome));
    assert(outcome.kind == core::OutboundKind::CommandResponse);
    assert(session.written == "{\"r\":2}\n");

    assert(scheduler.drainOne(session, outcome));
    assert(outcome.kind == core::OutboundKind::HeartbeatAck);
    assert(session.written == "{\"h\":1}\n");
    assert(!scheduler.drainOne(session, outcome));
}

void secondCriticalEntryIsQueuedAndTelemetryDoesNotBlockCritical() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    FakeSession session;
    core::OutboundOutcome outcome{};

    assert(scheduler.replaceTelemetry(line("{\"t\":1}\n"), counters) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.replaceTelemetry(line("{\"t\":2}\n"), counters) ==
           core::OutboundEnqueueResult::Queued);
    assert(counters.telemetry_coalesced == 1);
    assert(scheduler.enqueueCritical(core::OutboundKind::HeartbeatAck, line("{\"h\":1}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::HeartbeatAck, line("{\"h\":2}\n")) ==
           core::OutboundEnqueueResult::Queued);

    assert(scheduler.drainOne(session, outcome));
    assert(outcome.kind == core::OutboundKind::HeartbeatAck);
    assert(session.written == "{\"h\":1}\n");
    assert(session.last_class == core::MessageClass::Critical);

    assert(scheduler.drainOne(session, outcome));
    assert(outcome.kind == core::OutboundKind::HeartbeatAck);
    assert(session.written == "{\"h\":2}\n");

    assert(scheduler.drainOne(session, outcome));
    assert(outcome.kind == core::OutboundKind::Telemetry);
    assert(session.written == "{\"t\":2}\n");
    assert(session.last_class == core::MessageClass::Telemetry);
}

void queueSaturationRequestsTeardown() {
    core::OutboundScheduler scheduler;
    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse, line("{\"a\":1}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse, line("{\"a\":2}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::HeartbeatAck, line("{\"a\":3}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::EstopAck, line("{\"a\":4}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.criticalCount() == limits::kOutboundCriticalQueueCapacity);
    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse, line("{\"a\":5}\n")) ==
           core::OutboundEnqueueResult::TeardownRequested);
}

void cancellationReturnsFiveOutcomesAndRestoresPersistentEstopTriggered() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    scheduler.markEstopTriggeredPending();
    assert(scheduler.enqueueCritical(core::OutboundKind::EstopTriggered, line("{\"e\":1}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse, line("{\"r\":1}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::EstopAck, line("{\"a\":1}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::HeartbeatAck, line("{\"h\":1}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.replaceTelemetry(line("{\"t\":1}\n"), counters) ==
           core::OutboundEnqueueResult::Queued);

    const core::OutboundCancellationBatch batch = scheduler.clearForSessionEnd();
    assert(batch.count() == limits::kOutboundCancellationBatchCapacity);
    bool saw_estop_triggered = false;
    bool saw_response = false;
    bool saw_estop_ack = false;
    bool saw_heartbeat = false;
    bool saw_telemetry = false;
    for (std::size_t i = 0; i < batch.count(); ++i) {
        assert(batch.at(i).completion == core::OutboundCompletion::CanceledBySessionEnd);
        saw_estop_triggered |= batch.at(i).kind == core::OutboundKind::EstopTriggered;
        saw_response |= batch.at(i).kind == core::OutboundKind::CommandResponse;
        saw_estop_ack |= batch.at(i).kind == core::OutboundKind::EstopAck;
        saw_heartbeat |= batch.at(i).kind == core::OutboundKind::HeartbeatAck;
        saw_telemetry |= batch.at(i).kind == core::OutboundKind::Telemetry;
    }
    assert(saw_estop_triggered);
    assert(saw_response);
    assert(saw_estop_ack);
    assert(saw_heartbeat);
    assert(saw_telemetry);
    assert(scheduler.estopTriggeredState() ==
           core::EstopTriggeredPendingState::PendingNotQueued);
    assert(!scheduler.hasPending());
    assert(counters.telemetry_sent == 0);
    assert(counters.estop_ack_sent == 0);
    assert(counters.heartbeat_ack_sent == 0);
}

void sendFailureReportsOutcome() {
    core::OutboundScheduler scheduler;
    FakeSession session;
    session.result = core::OutboundSendResult::DeadlineExpired;
    core::OutboundOutcome outcome{};

    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse, line("{\"r\":1}\n")) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.drainOne(session, outcome));
    assert(outcome.kind == core::OutboundKind::CommandResponse);
    assert(outcome.completion == core::OutboundCompletion::SendFailed);
}

}  // namespace

int main() {
    static_assert(sizeof(core::OutboundScheduler) <= limits::kOutboundSchedulerMaxBytes,
                  "scheduler RAM budget");
    priorityAndFifoOrdering();
    secondCriticalEntryIsQueuedAndTelemetryDoesNotBlockCritical();
    queueSaturationRequestsTeardown();
    cancellationReturnsFiveOutcomesAndRestoresPersistentEstopTriggered();
    sendFailureReportsOutcome();
    return 0;
}
