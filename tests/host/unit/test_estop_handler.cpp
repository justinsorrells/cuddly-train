#include "TeensyCommandServer.h"
#include "api/CommandResult.h"
#include "core/EstopHandler.h"
#include "core/ServiceLoop.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeNetworkServer.h"

#include <cassert>
#include <cstring>
#include <string>

namespace {

namespace api = teensy_command_server::api;
namespace core = teensy_command_server::core;
namespace fakes = teensy_command_server::host::fakes;
namespace limits = teensy_command_server::support;
using teensy_command_server::TeensyCommandServer;

struct FakeSession {
    bool active = true;
    bool pending_teardown = false;
    bool applied = false;
    bool line_available = false;
    core::MutableLineView line{};
    core::OutboundSendResult send_result = core::OutboundSendResult::Sent;
    core::TeardownReason reason = core::TeardownReason::ExplicitShutdown;
    unsigned sends = 0;
    unsigned polls = 0;
    std::string written;

    void poll() {
        ++polls;
    }

    bool sessionActive() const {
        return active;
    }

    bool teardownPending() const {
        return pending_teardown;
    }

    bool nextLine(core::MutableLineView& out) {
        if (!active || pending_teardown || !line_available) {
            out = {};
            return false;
        }
        out = line;
        return true;
    }

    void releaseLine() {
        line_available = false;
    }

    void requestTeardown(core::TeardownReason requested) {
        pending_teardown = true;
        reason = requested;
    }

    void applyPendingTeardown() {
        applied = true;
        pending_teardown = false;
    }

    core::OutboundSendResult sendActiveLine(core::ConstLineView outbound,
                                            core::MessageClass) {
        ++sends;
        written.assign(outbound.data, outbound.size);
        return send_result;
    }
};

struct HookState {
    fakes::FakeClock* clock = nullptr;
    unsigned calls = 0;
    bool safe = true;
    std::uint64_t advance_us = 0;
};

struct RouteState {
    core::Counters* counters = nullptr;
    const api::BoardIdentity* identity = nullptr;
    core::OutboundScheduler* scheduler = nullptr;
    fakes::FakeClock* clock = nullptr;
    core::EstopHandler handler;
    HookState hook;
    unsigned commands = 0;
    unsigned telemetry_due = 0;
};

core::MutableLineView line(char* value) {
    return {value, std::strlen(value)};
}

bool safetyHook(void* raw) {
    auto* state = static_cast<HookState*>(raw);
    ++state->calls;
    if (state->clock != nullptr) {
        state->clock->advanceMicroseconds(state->advance_us);
    }
    return state->safe;
}

bool routeCommand(const core::ParsedCommand&, void* raw) {
    ++static_cast<RouteState*>(raw)->commands;
    return false;
}

void routeTelemetry(core::OutboundScheduler&, void* raw) {
    ++static_cast<RouteState*>(raw)->telemetry_due;
}

struct LoopHarness {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    fakes::FakeClock clock;
    RouteState routes_state{&counters, &identity, &scheduler, &clock, {}, {}, 0, 0};
    FakeSession session;
    core::ServiceLoop loop;

    LoopHarness()
        : loop(counters, identity, scheduler, routes(), clock) {
        routes_state.hook.clock = &clock;
    }

    core::ServiceLoop::Routes routes() {
        core::ServiceLoop::Routes routes;
        routes.command = routeCommand;
        routes.telemetry_due = routeTelemetry;
        routes.estop_with_result = [](const core::ParsedEstop& estop, void* raw) {
            auto* state = static_cast<RouteState*>(raw);
            return state->handler.handleEstop(estop, *state->counters, *state->identity,
                                              state->clock, safetyHook, &state->hook,
                                              *state->scheduler);
        };
        routes.safety_due = [](core::OutboundScheduler& scheduler, void* raw) {
            auto* state = static_cast<RouteState*>(raw);
            return state->handler.service(scheduler, *state->identity);
        };
        routes.outbound_outcome = [](const core::OutboundOutcome& outcome, void* raw) {
            auto* state = static_cast<RouteState*>(raw);
            state->handler.onOutboundOutcome(outcome, *state->scheduler);
        };
        routes.context = &routes_state;
        return routes;
    }
};

void serviceLine(LoopHarness& harness, char* text) {
    harness.session.line = line(text);
    harness.session.line_available = true;
    harness.loop.service(harness.session);
}

void safeEstopAcksOnlyAfterWireSendAndRepeatedEstopIsIdempotent() {
    LoopHarness harness;
    char estop[] = "{\"type\":\"estop\",\"source\":\"controller\",\"target\":\"board\"}\n";

    serviceLine(harness, estop);
    assert(harness.routes_state.hook.calls == 1);
    assert(harness.counters.estop_received == 1);
    assert(harness.counters.estop_ack_sent == 1);
    assert(harness.session.written.find("\"event\":\"estop_ack\"") != std::string::npos);
    assert(harness.session.written.find("\"state\":\"safe\"") != std::string::npos);
    assert(harness.session.written.find("\"state\":\"") ==
           harness.session.written.find("\"state\":\"safe\""));

    char repeated[] = "{\"type\":\"estop\",\"source\":\"controller\",\"target\":\"board\"}\n";
    serviceLine(harness, repeated);
    assert(harness.routes_state.hook.calls == 2);
    assert(harness.counters.estop_received == 2);
    assert(harness.counters.estop_ack_sent == 2);
}

void failedHookWithholdsAckAndServiceContinues() {
    LoopHarness harness;
    harness.routes_state.hook.safe = false;
    char estop[] = "{\"type\":\"estop\",\"source\":\"controller\",\"target\":\"board\"}\n";

    serviceLine(harness, estop);
    assert(harness.routes_state.hook.calls == 1);
    assert(harness.counters.estop_received == 1);
    assert(harness.counters.estop_apply_failed == 1);
    assert(harness.counters.estop_ack_sent == 0);
    assert(harness.session.sends == 0);
    assert(harness.routes_state.telemetry_due == 1);
    assert(!harness.session.applied);
}

void overBudgetButSafeStillAcksAndCounts() {
    LoopHarness harness;
    harness.routes_state.hook.advance_us =
        static_cast<std::uint64_t>(limits::kHookBudgetMs + 1) * 1000ULL;
    char estop[] = "{\"type\":\"estop\",\"source\":\"controller\",\"target\":\"board\"}\n";

    serviceLine(harness, estop);
    assert(harness.counters.estop_hook_over_budget == 1);
    assert(harness.counters.estop_ack_sent == 1);
    assert(harness.counters.estop_apply_failed == 0);
}

void wrongTargetDoesNotRunHookOrAckAndEstopDoesNotLatchCommandGate() {
    LoopHarness harness;
    char wrong[] = "{\"type\":\"estop\",\"source\":\"controller\",\"target\":\"other\"}\n";
    serviceLine(harness, wrong);
    assert(harness.routes_state.hook.calls == 0);
    assert(harness.counters.invalid_targets == 1);
    assert(harness.counters.estop_ack_sent == 0);

    char estop[] = "{\"type\":\"estop\",\"source\":\"controller\",\"target\":\"board\"}\n";
    serviceLine(harness, estop);
    char command[] =
        "{\"type\":\"command\",\"seq\":1,\"controller_ts\":1,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"diagnostic\",\"args\":{}}\n";
    serviceLine(harness, command);
    assert(harness.routes_state.commands == 1);
}

void estopTriggeredIsQueuedFromServiceAndClearsOnlyOnSuccess() {
    LoopHarness harness;
    harness.clock.setMonotonicMilliseconds(42);
    assert(harness.routes_state.handler
               .requestEstopTriggered("interlock_gpio", &harness.clock, harness.scheduler)
               .ok());
    assert(harness.routes_state.handler.pendingState() ==
           core::EstopHandler::PendingState::PendingNotQueued);
    assert(harness.routes_state.handler.pendingTimestampForTest() == 42);

    harness.session.active = false;
    harness.loop.service(harness.session);
    assert(harness.session.sends == 0);

    harness.session.active = true;
    harness.loop.service(harness.session);
    assert(harness.session.sends == 1);
    assert(harness.session.written.find("\"event\":\"estop_triggered\"") != std::string::npos);
    assert(harness.session.written.find("\"timestamp\":42") != std::string::npos);
    assert(harness.session.written.find("\"reason\":\"interlock_gpio\"") != std::string::npos);
    assert(harness.routes_state.handler.pendingState() ==
           core::EstopHandler::PendingState::Empty);
}

void estopTriggeredSlotIsLosslessAndBusy() {
    LoopHarness harness;
    assert(harness.routes_state.handler.requestEstopTriggered("first", &harness.clock,
                                                              harness.scheduler)
               .ok());
    const api::Status busy = harness.routes_state.handler.requestEstopTriggered(
        "second", &harness.clock, harness.scheduler);
    assert(busy.code == api::StatusCode::CapacityExceeded);
    assert(std::strcmp(harness.routes_state.handler.pendingReasonForTest(), "first") == 0);

    char too_long[limits::kEstopTriggeredReasonMaxBytes + 2]{};
    std::memset(too_long, 'x', sizeof(too_long) - 1);
    LoopHarness other;
    assert(other.routes_state.handler
               .requestEstopTriggered(too_long, &other.clock, other.scheduler)
               .code == api::StatusCode::CapacityExceeded);
}

void estopTriggeredSurvivesCriticalSendFailureAndRetries() {
    LoopHarness harness;
    assert(harness.routes_state.handler.requestEstopTriggered("limit", &harness.clock,
                                                              harness.scheduler)
               .ok());
    harness.session.send_result = core::OutboundSendResult::DeadlineExpired;
    harness.loop.service(harness.session);
    assert(harness.session.applied);
    assert(harness.session.reason == core::TeardownReason::CriticalTransmitFailure);
    assert(harness.routes_state.handler.pendingState() ==
           core::EstopHandler::PendingState::PendingNotQueued);
    assert(std::strcmp(harness.routes_state.handler.pendingReasonForTest(), "limit") == 0);

    harness.session.send_result = core::OutboundSendResult::Sent;
    harness.loop.service(harness.session);
    assert(harness.routes_state.handler.pendingState() ==
           core::EstopHandler::PendingState::Empty);
}

api::CommandResult commandHandler(const api::CommandContext&, api::ObjectWriter&, void*) {
    return api::CommandResult::ok();
}

bool telemetry(api::ObjectWriter&, void*) {
    return true;
}

bool facadeHook(void*) {
    return true;
}

void assertOk(api::Status status) {
    assert(status.code == api::StatusCode::Ok);
}

void facadeRequestEstopTriggeredPersistsUntilSessionService() {
    fakes::FakeClock clock;
    fakes::FakeNetworkServer network;
    TeensyCommandServer server(network, clock);
    network.scriptAvailability(core::NetworkAvailability::Ready);
    network.advanceAvailabilityScript();
    assertOk(server.setIdentity({"board", "1", "0.1.0"}));
    assertOk(server.setNetworkConfig(
        {api::NetworkConfig::Mode::Dhcp, 5050, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}));
    assertOk(server.registerCommand({"diagnostic", nullptr, 0, false}, commandHandler, nullptr));
    assertOk(server.setTelemetryProvider(telemetry, nullptr));
    assertOk(server.setEstopHook(facadeHook, nullptr));
    assertOk(server.setControllerLossHook(facadeHook, nullptr));
    assertOk(server.start());

    clock.setMonotonicMilliseconds(99);
    assertOk(server.requestEstopTriggered("panel"));
    server.service();
    network.queueInboundConnection();
    server.service();
    fakes::FakeTransport* transport = network.fakeTransport({0, 1});
    assert(transport != nullptr);
    const std::string written = transport->writtenString();
    const std::size_t schema_at = written.find("{\"type\":\"schema\"");
    const std::size_t event_at = written.find("\"event\":\"estop_triggered\"");
    assert(schema_at == 0);
    assert(event_at != std::string::npos);
    assert(event_at > schema_at);
    assert(written.find("\"event\":\"estop_triggered\"") != std::string::npos);
    assert(written.find("\"timestamp\":99") != std::string::npos);
    assert(written.find("\"reason\":\"panel\"") != std::string::npos);
}

}  // namespace

int main() {
    static_assert(limits::kEstopTriggeredReasonMaxBytes == 160);
    safeEstopAcksOnlyAfterWireSendAndRepeatedEstopIsIdempotent();
    failedHookWithholdsAckAndServiceContinues();
    overBudgetButSafeStillAcksAndCounts();
    wrongTargetDoesNotRunHookOrAckAndEstopDoesNotLatchCommandGate();
    estopTriggeredIsQueuedFromServiceAndClearsOnlyOnSuccess();
    estopTriggeredSlotIsLosslessAndBusy();
    estopTriggeredSurvivesCriticalSendFailureAndRetries();
    facadeRequestEstopTriggeredPersistsUntilSessionService();
    return 0;
}
