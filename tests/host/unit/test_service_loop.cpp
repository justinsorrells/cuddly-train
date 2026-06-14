#include "TeensyCommandServer.h"
#include "api/CommandResult.h"
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
using teensy_command_server::TeensyCommandServer;

api::CommandResult handler(const api::CommandContext&, api::ObjectWriter&, void*) {
    return api::CommandResult::ok();
}

api::CommandResult countingHandler(const api::CommandContext&, api::ObjectWriter&, void* context) {
    if (context != nullptr) {
        ++(*static_cast<unsigned*>(context));
    }
    return api::CommandResult::ok();
}

bool telemetry(api::ObjectWriter&, void*) {
    return true;
}

bool countingTelemetry(api::ObjectWriter&, void* context) {
    if (context != nullptr) {
        ++(*static_cast<unsigned*>(context));
    }
    return true;
}

bool hook(void* context) {
    if (context != nullptr) {
        ++(*static_cast<unsigned*>(context));
    }
    return true;
}

void assertOk(api::Status status) {
    assert(status.code == api::StatusCode::Ok);
}

struct FakeSession {
    bool pending_teardown = false;
    bool line_available = false;
    bool acquired = false;
    bool released = false;
    bool applied = false;
    unsigned polls = 0;
    unsigned next_line_calls = 0;
    unsigned send_calls = 0;
    core::MutableLineView line{};
    core::OutboundSendResult send_result = core::OutboundSendResult::Sent;
    core::TeardownReason reason = core::TeardownReason::ExplicitShutdown;

    void poll() {
        ++polls;
    }

    bool teardownPending() const {
        return pending_teardown;
    }

    bool nextLine(core::MutableLineView& out) {
        ++next_line_calls;
        if (!line_available || pending_teardown) {
            out = {};
            return false;
        }
        acquired = true;
        out = line;
        return true;
    }

    void releaseLine() {
        released = true;
        acquired = false;
    }

    void requestTeardown(core::TeardownReason requested) {
        pending_teardown = true;
        reason = requested;
    }

    void applyPendingTeardown() {
        applied = true;
        pending_teardown = false;
    }

    core::OutboundSendResult sendActiveLine(core::ConstLineView,
                                            core::MessageClass) {
        ++send_calls;
        return send_result;
    }
};

struct RouteState {
    unsigned commands = 0;
    unsigned recoverable = 0;
    unsigned estops = 0;
    unsigned heartbeats = 0;
    unsigned telemetry_due = 0;
    unsigned outcomes = 0;
    unsigned sent_outcomes = 0;
    unsigned canceled_outcomes = 0;
    core::RecoverableCommandError last_error{};
    bool command_requests_teardown = true;
};

bool routeCommand(const core::ParsedCommand&, void* raw) {
    auto* state = static_cast<RouteState*>(raw);
    ++state->commands;
    return state->command_requests_teardown;
}

void routeRecoverable(const core::RecoverableCommandError& error, void* raw) {
    auto* state = static_cast<RouteState*>(raw);
    ++state->recoverable;
    state->last_error = error;
}

void routeEstop(const core::ParsedEstop&, void* raw) {
    ++static_cast<RouteState*>(raw)->estops;
}

void routeHeartbeat(const core::ParsedHeartbeat&, void* raw) {
    ++static_cast<RouteState*>(raw)->heartbeats;
}

void routeTelemetryDue(core::OutboundScheduler&, void* raw) {
    ++static_cast<RouteState*>(raw)->telemetry_due;
}

void routeOutcome(const core::OutboundOutcome& outcome, void* raw) {
    auto* state = static_cast<RouteState*>(raw);
    ++state->outcomes;
    if (outcome.completion == core::OutboundCompletion::Sent) {
        ++state->sent_outcomes;
    }
    if (outcome.completion == core::OutboundCompletion::CanceledBySessionEnd) {
        ++state->canceled_outcomes;
    }
}

core::ServiceLoop::Routes routes(RouteState& state) {
    return {routeCommand, routeRecoverable, routeEstop, routeHeartbeat,
            routeTelemetryDue, routeOutcome, &state};
}

core::MutableLineView line(char* value) {
    return {value, std::strlen(value)};
}

struct FacadeHarness {
    fakes::FakeClock clock;
    fakes::FakeNetworkServer network;
    TeensyCommandServer server{network, clock};
    unsigned command_calls = 0;
    unsigned telemetry_calls = 0;
    unsigned estop_calls = 0;
    unsigned loss_calls = 0;

    void configure() {
        network.scriptAvailability(core::NetworkAvailability::Ready);
        network.advanceAvailabilityScript();
        assertOk(server.setIdentity({"board", "1", "0.1.0"}));
        assertOk(server.setNetworkConfig(
            {api::NetworkConfig::Mode::Dhcp, 5050, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}));
        assertOk(server.registerCommand({"set_speed", nullptr, 0, true}, countingHandler,
                                        &command_calls));
        const api::FieldSpec telemetry_fields[] = {{"uptime_ms", api::ValueType::Int}};
        assertOk(server.registerTelemetrySchema(telemetry_fields, 1));
        assertOk(server.setTelemetryProvider(countingTelemetry, &telemetry_calls));
        assertOk(server.setEstopHook(hook, &estop_calls));
        assertOk(server.setControllerLossHook(hook, &loss_calls));
        assertOk(server.start());
    }

    fakes::FakeTransport* acceptFirstSession() {
        server.service();
        assert(network.isListening());
        network.queueInboundConnection();
        server.service();
        fakes::FakeTransport* transport = network.fakeTransport({0, 1});
        assert(transport != nullptr);
        const std::string written = transport->writtenString();
        assert(written.find("{\"type\":\"schema\"") == 0);
        assert(written.find("\"source\":\"board\"") != std::string::npos);
        assert(written.back() == '\n');
        assert(server.counters().sessions_accepted == 1);
        assert(server.counters().schemas_sent == 1);
        return transport;
    }
};

void facadeSealsAndFailedStartLeavesMutable() {
    TeensyCommandServer missing;
    assertOk(missing.setIdentity({"board", "1", "0.1.0"}));
    assert(missing.start().code == api::StatusCode::InvalidConfiguration);
    assertOk(missing.registerCommand({"after_failed", nullptr, 0, false}, handler, nullptr));

    fakes::FakeClock clock;
    fakes::FakeNetworkServer network;
    TeensyCommandServer server(network, clock);
    unsigned estop_calls = 0;
    unsigned loss_calls = 0;
    assertOk(server.setIdentity({"board", "1", "0.1.0"}));
    assertOk(server.setNetworkConfig(
        {api::NetworkConfig::Mode::Dhcp, 5050, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}));
    assertOk(server.registerCommand({"get_status", nullptr, 0, false}, handler, nullptr));
    const api::FieldSpec telemetry_fields[] = {{"uptime_ms", api::ValueType::Int}};
    assertOk(server.registerTelemetrySchema(telemetry_fields, 1));
    assertOk(server.setTelemetryProvider(telemetry, nullptr));
    assertOk(server.setEstopHook(hook, &estop_calls));
    assertOk(server.setControllerLossHook(hook, &loss_calls));
    assertOk(server.start());
    assert(server.isStarted());
    assert(server.registerCommand({"late", nullptr, 0, false}, handler, nullptr).code ==
           api::StatusCode::RegistrationSealed);
    assert(server.setNetworkConfig(
               {api::NetworkConfig::Mode::Dhcp, 5051, {0, 0, 0, 0}, {0, 0, 0, 0},
                {0, 0, 0, 0}})
               .code == api::StatusCode::RegistrationSealed);
}

void facadeServicePumpIsExplicitlyFailClosedForPhase7Routes() {
    FacadeHarness harness;
    harness.configure();
    fakes::FakeTransport* transport = harness.acceptFirstSession();

    transport->scriptInboundBytes(
        "{\"type\":\"command\",\"seq\":1,\"controller_ts\":1,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"set_speed\",\"args\":{}}\n");
    harness.server.service();
    assert(harness.command_calls == 0);
    assert(harness.telemetry_calls == 0);
    assert(harness.estop_calls == 0);
    assert(harness.loss_calls == 1);
    assert(harness.network.abortCount() == 1);
    assert(harness.server.counters().controller_disconnects == 1);
    assert(harness.server.counters().commands_ok == 0);
    assert(harness.server.counters().telemetry_sent == 0);
    assert(harness.server.counters().estop_ack_sent == 0);
    assert(harness.server.counters().heartbeat_ack_sent == 0);

    FacadeHarness estop_harness;
    estop_harness.configure();
    fakes::FakeTransport* estop_transport = estop_harness.acceptFirstSession();
    const std::string schema_only = estop_transport->writtenString();
    estop_transport->scriptInboundBytes(
        "{\"type\":\"estop\",\"source\":\"controller\",\"target\":\"board\"}\n");
    estop_harness.server.service();
    assert(estop_harness.estop_calls == 0);
    assert(estop_harness.loss_calls == 0);
    assert(estop_harness.server.counters().estop_apply_failed == 1);
    assert(estop_harness.server.counters().estop_ack_sent == 0);
    assert(estop_transport->writtenString() == schema_only);

    FacadeHarness heartbeat_harness;
    heartbeat_harness.configure();
    fakes::FakeTransport* heartbeat_transport = heartbeat_harness.acceptFirstSession();
    const std::string heartbeat_schema_only = heartbeat_transport->writtenString();
    heartbeat_transport->scriptInboundBytes(
        "{\"type\":\"heartbeat\",\"seq\":7,\"source\":\"controller\",\"target\":\"board\"}\n");
    heartbeat_harness.server.service();
    assert(heartbeat_harness.server.counters().heartbeat_ack_sent == 0);
    assert(heartbeat_transport->writtenString() == heartbeat_schema_only);
}

void routingAndCounterOwnership() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    RouteState route_state;
    core::ServiceLoop loop(counters, identity, scheduler, routes(route_state));
    FakeSession session;

    char malformed[] = "{\"type\":";
    session.line = line(malformed);
    session.line_available = true;
    loop.service(session);
    assert(counters.invalid_json == 1);
    assert(counters.invalid_targets == 0);
    assert(route_state.commands == 0);
    assert(route_state.telemetry_due == 1);
    assert(session.released);

    char wrong_target[] =
        "{\"type\":\"estop\",\"source\":\"controller\",\"target\":\"other\"}\n";
    session = {};
    session.line = line(wrong_target);
    session.line_available = true;
    loop.service(session);
    assert(counters.invalid_json == 1);
    assert(counters.invalid_targets == 1);
    assert(route_state.estops == 0);

    char wrong_source[] =
        "{\"type\":\"heartbeat\",\"seq\":7,\"source\":\"peer\",\"target\":\"board\"}\n";
    session = {};
    session.line = line(wrong_source);
    session.line_available = true;
    loop.service(session);
    assert(counters.invalid_targets == 2);
    assert(route_state.heartbeats == 0);

    char good_estop[] =
        "{\"type\":\"estop\",\"source\":\"controller\",\"target\":\"board\"}\n";
    session = {};
    session.line = line(good_estop);
    session.line_available = true;
    loop.service(session);
    assert(route_state.estops == 1);

    char good_heartbeat[] =
        "{\"type\":\"heartbeat\",\"seq\":7,\"source\":\"controller\",\"target\":\"board\"}\n";
    session = {};
    session.line = line(good_heartbeat);
    session.line_available = true;
    loop.service(session);
    assert(route_state.heartbeats == 1);
}

void recoverableCommandErrorAndFailClosedCommandStub() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    RouteState route_state;
    core::ServiceLoop loop(counters, identity, scheduler, routes(route_state));
    FakeSession session;

    char missing_args[] =
        "{\"type\":\"command\",\"seq\":42,\"controller_ts\":12.5,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"set_speed\"}\n";
    session.line = line(missing_args);
    session.line_available = true;
    loop.service(session);
    assert(route_state.recoverable == 1);
    assert(route_state.last_error.seq == 42);
    assert(route_state.last_error.code == core::BoardErrorCode::MissingField);
    assert(!session.pending_teardown);

    char wrong_target_missing_args[] =
        "{\"type\":\"command\",\"seq\":43,\"controller_ts\":12.5,\"source\":\"controller\","
        "\"target\":\"other\",\"command\":\"set_speed\"}\n";
    session = {};
    session.line = line(wrong_target_missing_args);
    session.line_available = true;
    loop.service(session);
    assert(route_state.recoverable == 1);
    assert(counters.invalid_targets == 1);
    assert(counters.invalid_json == 0);

    char missing_target[] =
        "{\"type\":\"command\",\"seq\":44,\"controller_ts\":12.5,\"source\":\"controller\","
        "\"command\":\"set_speed\",\"args\":{}}\n";
    session = {};
    session.line = line(missing_target);
    session.line_available = true;
    loop.service(session);
    assert(route_state.recoverable == 1);
    assert(counters.invalid_targets == 2);
    assert(counters.invalid_json == 0);

    char wrong_source_missing_args[] =
        "{\"type\":\"command\",\"seq\":45,\"controller_ts\":12.5,\"source\":\"peer\","
        "\"target\":\"board\",\"command\":\"set_speed\"}\n";
    session = {};
    session.line = line(wrong_source_missing_args);
    session.line_available = true;
    loop.service(session);
    assert(route_state.recoverable == 1);
    assert(counters.invalid_targets == 3);
    assert(counters.invalid_json == 0);

    char command[] =
        "{\"type\":\"command\",\"seq\":46,\"controller_ts\":12.5,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"set_speed\",\"args\":{}}\n";
    session = {};
    session.line = line(command);
    session.line_available = true;
    loop.service(session);
    assert(route_state.commands == 1);
    assert(session.applied);
    assert(session.reason == core::TeardownReason::CriticalTransmitFailure);
}

void teardownBarrierWaitsForReleaseAndCancelsBeforeAbort() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    RouteState route_state;
    route_state.command_requests_teardown = true;
    core::ServiceLoop loop(counters, identity, scheduler, routes(route_state));
    FakeSession session;
    char command[] =
        "{\"type\":\"command\",\"seq\":1,\"controller_ts\":1,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"x\",\"args\":{}}\n";
    assert(scheduler.enqueueCritical(core::OutboundKind::EstopAck, {"{\"ack\":1}\n", 10}) ==
           core::OutboundEnqueueResult::Queued);
    session.line = line(command);
    session.line_available = true;

    loop.service(session);
    assert(session.released);
    assert(session.applied);
    assert(route_state.canceled_outcomes == 1);
    assert(counters.estop_ack_sent == 0);
}

void noSecondLineAndAtMostOneSendPerService() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    RouteState route_state;
    route_state.command_requests_teardown = false;
    core::ServiceLoop loop(counters, identity, scheduler, routes(route_state));
    FakeSession session;
    char command[] =
        "{\"type\":\"command\",\"seq\":1,\"controller_ts\":1,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"x\",\"args\":{}}\n";
    session.line = line(command);
    session.line_available = true;
    assert(scheduler.enqueueCritical(core::OutboundKind::HeartbeatAck, {"{\"h\":1}\n", 8}) ==
           core::OutboundEnqueueResult::Queued);
    assert(scheduler.enqueueCritical(core::OutboundKind::CommandResponse, {"{\"r\":1}\n", 8}) ==
           core::OutboundEnqueueResult::Queued);

    loop.service(session);
    assert(session.next_line_calls == 1);
    assert(session.send_calls == 1);
    assert(route_state.telemetry_due == 1);
    assert(route_state.commands == 1);
    assert(counters.heartbeat_ack_sent == 0);
}

void sendFailureTeardownBeforeReturnAndDefaultStubsFailClosed() {
    core::Counters counters;
    core::OutboundScheduler scheduler;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    core::ServiceLoop loop(counters, identity, scheduler);
    FakeSession session;
    assert(scheduler.enqueueCritical(core::OutboundKind::EstopAck, {"{\"ack\":1}\n", 10}) ==
           core::OutboundEnqueueResult::Queued);
    session.send_result = core::OutboundSendResult::DeadlineExpired;
    loop.service(session);
    assert(session.applied);
    assert(counters.estop_ack_sent == 0);

    char estop[] = "{\"type\":\"estop\",\"source\":\"controller\",\"target\":\"board\"}\n";
    session = {};
    session.line = line(estop);
    session.line_available = true;
    loop.service(session);
    assert(counters.estop_apply_failed == 1);

    char heartbeat[] =
        "{\"type\":\"heartbeat\",\"seq\":1,\"source\":\"controller\",\"target\":\"board\"}\n";
    session = {};
    session.line = line(heartbeat);
    session.line_available = true;
    loop.service(session);
    assert(counters.heartbeat_ack_sent == 0);
}

}  // namespace

int main() {
    facadeSealsAndFailedStartLeavesMutable();
    facadeServicePumpIsExplicitlyFailClosedForPhase7Routes();
    routingAndCounterOwnership();
    recoverableCommandErrorAndFailClosedCommandStub();
    teardownBarrierWaitsForReleaseAndCancelsBeforeAbort();
    noSecondLineAndAtMostOneSendPerService();
    sendFailureTeardownBeforeReturnAndDefaultStubsFailClosed();
    return 0;
}
