#include "core/OutboundWriter.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeNetworkServer.h"
#include "fakes/FakeTransport.h"
#include "support/Limits.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string>

namespace {

using teensy_command_server::core::ConstLineView;
using teensy_command_server::core::Counters;
using teensy_command_server::core::MessageClass;
using teensy_command_server::core::OutboundSendResult;
using teensy_command_server::core::OutboundWriter;
using teensy_command_server::core::SessionTeardownReason;
using teensy_command_server::host::fakes::FakeClock;
using teensy_command_server::host::fakes::FakeNetworkServer;
using teensy_command_server::host::fakes::FakeTransport;
namespace limits = teensy_command_server::support;

class FakeTeardownSink final {
public:
    void requestTeardown(SessionTeardownReason reason) {
        ++request_count_;
        last_reason_ = reason;
    }

    std::size_t requestCount() const {
        return request_count_;
    }

    SessionTeardownReason lastReason() const {
        return last_reason_;
    }

private:
    std::size_t request_count_ = 0;
    SessionTeardownReason last_reason_ = SessionTeardownReason::CriticalTransmitFailure;
};

struct Harness {
    Harness() {
        server.queueInboundConnection();
        handle = server.accept();
        transport = server.fakeTransport(handle);
        assert(transport != nullptr);
    }

    ConstLineView view(const char* value) {
        std::size_t size = 0;
        while (value[size] != '\0') {
            ++size;
        }
        return {value, size};
    }

    FakeClock clock;
    FakeNetworkServer server;
    Counters counters;
    FakeTeardownSink session_driver;
    teensy_command_server::core::ConnectionHandle handle{};
    FakeTransport* transport = nullptr;
};

void partialWritesAreRetriedAndProgressed() {
    Harness harness;
    OutboundWriter writer(
        *harness.transport, harness.server, harness.clock, harness.counters, harness.session_driver);

    harness.transport->scriptWriteAcceptance(2);
    harness.transport->scriptWriteAcceptance(3);
    harness.transport->scriptWriteAcceptance(20);

    assert(writer.sendLine(harness.view("abcdef\n"), MessageClass::Critical) ==
           OutboundSendResult::Sent);
    assert(harness.transport->writtenString() == "abcdef\n");
    assert(harness.server.progressCount() == 2);
    assert(harness.transport->progressCount() == 2);
    assert(harness.transport->flushCount() == 1);
    assert(harness.counters.tx_failures == 0);
    assert(harness.session_driver.requestCount() == 0);
}

void criticalPeerStopsReadingFailsAtDeadline() {
    Harness harness;
    harness.server.advanceClockOnProgress(harness.clock, 25);
    harness.transport->stopAcceptingWrites();
    OutboundWriter writer(
        *harness.transport, harness.server, harness.clock, harness.counters, harness.session_driver);

    assert(writer.sendLine(harness.view("critical\n"), MessageClass::Critical) ==
           OutboundSendResult::DeadlineExpired);
    assert(harness.clock.monotonicMilliseconds() == limits::kTransmitDeadlineMs);
    assert(harness.server.progressCount() == 4);
    assert(harness.transport->flushCount() == 0);
    assert(harness.counters.tx_failures == 1);
    assert(harness.counters.telemetry_dropped == 0);
    assert(harness.session_driver.requestCount() == 1);
    assert(harness.session_driver.lastReason() ==
           SessionTeardownReason::CriticalTransmitFailure);
}

void telemetryPeerStopsReadingDropsWithoutTxFailure() {
    Harness harness;
    harness.server.advanceClockOnProgress(harness.clock, 20);
    harness.transport->stopAcceptingWrites();
    OutboundWriter writer(
        *harness.transport, harness.server, harness.clock, harness.counters, harness.session_driver);

    assert(writer.sendLine(harness.view("telemetry\n"), MessageClass::Telemetry) ==
           OutboundSendResult::DeadlineExpired);
    assert(harness.clock.monotonicMilliseconds() == limits::kTransmitDeadlineMs);
    assert(harness.transport->flushCount() == 0);
    assert(harness.counters.tx_failures == 0);
    assert(harness.counters.telemetry_dropped == 1);
    assert(writer.consecutiveTelemetryFailures() == 1);
    assert(harness.session_driver.requestCount() == 0);
}

void deadlineUsesWrapSafeUnsignedElapsedTime() {
    Harness harness;
    harness.clock.setMonotonicMilliseconds(std::numeric_limits<std::uint64_t>::max() - 49U);
    harness.server.advanceClockOnProgress(harness.clock, 25);
    harness.transport->stopAcceptingWrites();
    OutboundWriter writer(
        *harness.transport, harness.server, harness.clock, harness.counters, harness.session_driver);

    assert(writer.sendLine(harness.view("wrap\n"), MessageClass::Critical) ==
           OutboundSendResult::DeadlineExpired);
    assert(harness.server.progressCount() == 4);
    assert(harness.counters.tx_failures == 1);
    assert(harness.session_driver.requestCount() == 1);
}

void telemetryFailureStreakSignalsAtTenAndResetsOnSuccess() {
    Harness harness;
    harness.server.advanceClockOnProgress(harness.clock, limits::kTransmitDeadlineMs);
    harness.transport->stopAcceptingWrites();
    OutboundWriter writer(
        *harness.transport, harness.server, harness.clock, harness.counters, harness.session_driver);

    for (std::uint8_t i = 1; i <= limits::kTelemetryDeadlineFailuresToTeardown; ++i) {
        assert(writer.sendLine(harness.view("telemetry\n"), MessageClass::Telemetry) ==
               OutboundSendResult::DeadlineExpired);
        assert(writer.consecutiveTelemetryFailures() == i);
    }
    assert(harness.counters.telemetry_dropped == limits::kTelemetryDeadlineFailuresToTeardown);
    assert(harness.counters.tx_failures == 0);
    assert(harness.session_driver.requestCount() == 1);
    assert(harness.session_driver.lastReason() ==
           SessionTeardownReason::TelemetryTransmitFailureStreak);

    harness.transport->resetForConnection();
    assert(writer.sendLine(harness.view("telemetry\n"), MessageClass::Telemetry) ==
           OutboundSendResult::Sent);
    assert(writer.consecutiveTelemetryFailures() == 0);
    assert(harness.transport->flushCount() == 1);

    harness.transport->stopAcceptingWrites();
    assert(writer.sendLine(harness.view("telemetry\n"), MessageClass::Telemetry) ==
           OutboundSendResult::DeadlineExpired);
    assert(writer.consecutiveTelemetryFailures() == 1);

    writer.beginSession();
    assert(writer.consecutiveTelemetryFailures() == 0);
}

void wireLineBoundariesAreEnforced() {
    Harness harness;
    OutboundWriter writer(
        *harness.transport, harness.server, harness.clock, harness.counters, harness.session_driver);

    std::string max_line(limits::kBoardTxMaxLineBytes, 'x');
    max_line.back() = '\n';
    assert(writer.sendLine({max_line.data(), max_line.size()}, MessageClass::Critical) ==
           OutboundSendResult::Sent);
    assert(harness.transport->writtenBytes().size() == limits::kBoardTxMaxLineBytes);
    assert(harness.transport->flushCount() == 1);

    std::string oversized(limits::kBoardTxMaxLineBytes + 1, 'x');
    oversized.back() = '\n';
    assert(writer.sendLine({oversized.data(), oversized.size()}, MessageClass::Critical) ==
           OutboundSendResult::InvalidLine);

    assert(writer.sendLine(harness.view("missing-newline"), MessageClass::Critical) ==
           OutboundSendResult::InvalidLine);
    assert(writer.sendLine(harness.view("two\nnewlines\n"), MessageClass::Critical) ==
           OutboundSendResult::InvalidLine);
    assert(writer.sendLine({nullptr, 0}, MessageClass::Critical) == OutboundSendResult::InvalidLine);
    assert(harness.counters.tx_failures == 0);
}

struct NestedSendContext {
    OutboundWriter* writer = nullptr;
    OutboundSendResult nested_result = OutboundSendResult::Sent;
    bool called = false;
};

void nestedSendObserver(void* raw_context) {
    auto* context = static_cast<NestedSendContext*>(raw_context);
    if (context->called) {
        return;
    }
    context->called = true;
    constexpr char kNested[] = "nested\n";
    context->nested_result = context->writer->sendLine(
        {kNested, sizeof(kNested) - 1U}, MessageClass::Critical);
}

void nestedSendIsRejectedBySerializationGuard() {
    Harness harness;
    OutboundWriter writer(
        *harness.transport, harness.server, harness.clock, harness.counters, harness.session_driver);
    NestedSendContext context{&writer, OutboundSendResult::Sent, false};
    harness.transport->setWriteObserver(nestedSendObserver, &context);

    assert(writer.sendLine(harness.view("outer\n"), MessageClass::Critical) ==
           OutboundSendResult::Sent);
    assert(context.called);
    assert(context.nested_result == OutboundSendResult::Busy);
    assert(harness.transport->writtenString() == "outer\n");
    assert(harness.transport->flushCount() == 1);
    assert(harness.counters.tx_failures == 0);
}

}  // namespace

int main() {
    partialWritesAreRetriedAndProgressed();
    criticalPeerStopsReadingFailsAtDeadline();
    telemetryPeerStopsReadingDropsWithoutTxFailure();
    deadlineUsesWrapSafeUnsignedElapsedTime();
    telemetryFailureStreakSignalsAtTenAndResetsOnSuccess();
    wireLineBoundariesAreEnforced();
    nestedSendIsRejectedBySerializationGuard();
    std::puts("test_outbound_writer: ok");
    return 0;
}
