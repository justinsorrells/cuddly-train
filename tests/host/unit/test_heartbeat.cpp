#include "core/HeartbeatHandler.h"
#include "core/ServiceLoop.h"

#include <cassert>
#include <cstring>
#include <string>

namespace {

namespace api = teensy_command_server::api;
namespace core = teensy_command_server::core;

struct FakeSession {
    bool pending_teardown = false;
    bool line_available = false;
    bool applied = false;
    core::MutableLineView line{};
    core::OutboundSendResult send_result = core::OutboundSendResult::Sent;
    core::TeardownReason reason = core::TeardownReason::ExplicitShutdown;
    unsigned sends = 0;
    std::string written;

    void poll() {}

    bool sessionActive() const {
        return !pending_teardown;
    }

    bool teardownPending() const {
        return pending_teardown;
    }

    bool nextLine(core::MutableLineView& out) {
        if (!line_available || pending_teardown) {
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

struct RouteState {
    core::Counters* counters = nullptr;
    const api::BoardIdentity* identity = nullptr;
    core::OutboundScheduler* scheduler = nullptr;
    core::HeartbeatHandler heartbeat;
    unsigned commands = 0;
    unsigned estops = 0;
};

core::MutableLineView line(char* value) {
    return {value, std::strlen(value)};
}

bool routeCommand(const core::ParsedCommand&, void* raw) {
    ++static_cast<RouteState*>(raw)->commands;
    return false;
}

void routeEstop(const core::ParsedEstop&, void* raw) {
    ++static_cast<RouteState*>(raw)->estops;
}

struct LoopHarness {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    RouteState route_state{&counters, &identity, &scheduler, {}, 0, 0};
    FakeSession session;
    core::ServiceLoop loop;

    LoopHarness() : loop(counters, identity, scheduler, routes()) {}

    core::ServiceLoop::Routes routes() {
        core::ServiceLoop::Routes routes;
        routes.command = routeCommand;
        routes.estop = routeEstop;
        routes.heartbeat_with_result = [](const core::ParsedHeartbeat& heartbeat, void* raw) {
            auto* state = static_cast<RouteState*>(raw);
            return state->heartbeat.handleHeartbeat(heartbeat, *state->counters,
                                                    *state->identity, *state->scheduler);
        };
        routes.context = &route_state;
        return routes;
    }
};

void serviceLine(LoopHarness& harness, char* text) {
    harness.session.line = line(text);
    harness.session.line_available = true;
    harness.loop.service(harness.session);
}

void heartbeatAckEchoesSeqAndBoardIdentityFromServiceLoop() {
    LoopHarness harness;
    char heartbeat[] =
        "{\"type\":\"heartbeat\",\"seq\":18446744073709551615,\"source\":\"controller\","
        "\"target\":\"board\"}\n";

    serviceLine(harness, heartbeat);
    assert(harness.counters.heartbeat_received == 1);
    assert(harness.counters.heartbeat_ack_sent == 1);
    assert(harness.session.sends == 1);
    assert(harness.session.written ==
           "{\"type\":\"heartbeat\",\"seq\":18446744073709551615,\"source\":\"board\","
           "\"target\":\"controller\"}\n");
    assert(harness.route_state.commands == 0);
    assert(harness.route_state.estops == 0);
}

void malformedHeartbeatIsRejectedSafely() {
    LoopHarness harness;
    char missing_target[] =
        "{\"type\":\"heartbeat\",\"seq\":7,\"source\":\"controller\"}\n";

    serviceLine(harness, missing_target);
    assert(harness.counters.heartbeat_received == 0);
    assert(harness.counters.heartbeat_ack_sent == 0);
    assert(harness.session.sends == 0);
    assert(harness.route_state.commands == 0);
}

void wrongTargetHeartbeatProducesNoAck() {
    LoopHarness harness;
    char wrong_target[] =
        "{\"type\":\"heartbeat\",\"seq\":7,\"source\":\"controller\",\"target\":\"other\"}\n";

    serviceLine(harness, wrong_target);
    assert(harness.counters.invalid_targets == 1);
    assert(harness.counters.heartbeat_received == 0);
    assert(harness.counters.heartbeat_ack_sent == 0);
    assert(harness.session.sends == 0);
}

void criticalHeartbeatAckFailureRequestsTeardown() {
    LoopHarness harness;
    harness.session.send_result = core::OutboundSendResult::DeadlineExpired;
    char heartbeat[] =
        "{\"type\":\"heartbeat\",\"seq\":7,\"source\":\"controller\",\"target\":\"board\"}\n";

    serviceLine(harness, heartbeat);
    assert(harness.counters.heartbeat_received == 1);
    assert(harness.counters.heartbeat_ack_sent == 0);
    assert(harness.session.applied);
    assert(harness.session.reason == core::TeardownReason::CriticalTransmitFailure);
}

}  // namespace

int main() {
    heartbeatAckEchoesSeqAndBoardIdentityFromServiceLoop();
    malformedHeartbeatIsRejectedSafely();
    wrongTargetHeartbeatProducesNoAck();
    criticalHeartbeatAckFailureRequestsTeardown();
    return 0;
}
