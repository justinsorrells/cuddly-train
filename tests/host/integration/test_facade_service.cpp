#include "TeensyCommandServer.h"
#include "api/CommandResult.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeNetworkServer.h"
#include "support/Limits.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

namespace api = teensy_command_server::api;
namespace core = teensy_command_server::core;
namespace fakes = teensy_command_server::host::fakes;
namespace limits = teensy_command_server::support;
using teensy_command_server::TeensyCommandServer;

struct HarnessState {
    unsigned set_speed_calls = 0;
    unsigned get_status_calls = 0;
    unsigned telemetry_calls = 0;
    unsigned estop_calls = 0;
    unsigned loss_calls = 0;
    bool telemetry_ok = true;
    bool telemetry_oversized = false;
    bool response_oversized = false;
    std::int32_t last_rpm = 0;
};

void assertOk(api::Status status) {
    assert(status.code == api::StatusCode::Ok);
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

std::size_t countOccurrences(const std::string& haystack, const char* needle) {
    std::size_t count = 0;
    std::size_t offset = 0;
    const std::size_t needle_size = std::strlen(needle);
    while ((offset = haystack.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += needle_size;
    }
    return count;
}

api::CommandResult setSpeed(const api::CommandContext& command,
                            api::ObjectWriter& result,
                            void* raw) {
    auto* state = static_cast<HarnessState*>(raw);
    ++state->set_speed_calls;
    std::int32_t rpm = 0;
    if (!command.args.getInt("rpm", rpm)) {
        return api::CommandResult::invalidType("rpm must be an int");
    }
    state->last_rpm = rpm;
    return result.addInt("rpm", rpm) ? api::CommandResult::ok()
                                     : api::CommandResult::internalError("result overflow");
}

api::CommandResult getStatus(const api::CommandContext&,
                             api::ObjectWriter& result,
                             void* raw) {
    auto* state = static_cast<HarnessState*>(raw);
    ++state->get_status_calls;
    if (state->response_oversized) {
        static char payload[limits::kMaxResultPayloadBytes + 1]{};
        std::memset(payload, 'x', sizeof(payload) - 1);
        payload[sizeof(payload) - 1] = '\0';
        (void)result.addString("payload", payload);
        return api::CommandResult::ok();
    }
    return result.addBool("ready", true) ? api::CommandResult::ok()
                                         : api::CommandResult::internalError("result overflow");
}

bool telemetryProvider(api::ObjectWriter& telemetry, void* raw) {
    auto* state = static_cast<HarnessState*>(raw);
    ++state->telemetry_calls;
    if (!state->telemetry_ok) {
        return false;
    }
    if (!telemetry.addInt("rpm", state->last_rpm)) {
        return false;
    }
    if (state->telemetry_oversized) {
        static char payload[limits::kMaxResultPayloadBytes + 1]{};
        std::memset(payload, 'y', sizeof(payload) - 1);
        payload[sizeof(payload) - 1] = '\0';
        return telemetry.addString("overflow", payload);
    }
    return true;
}

bool estopHook(void* raw) {
    ++static_cast<HarnessState*>(raw)->estop_calls;
    return true;
}

bool lossHook(void* raw) {
    ++static_cast<HarnessState*>(raw)->loss_calls;
    return true;
}

struct FacadeHarness {
    fakes::FakeClock clock;
    fakes::FakeNetworkServer network;
    HarnessState state;
    TeensyCommandServer server{network, clock};

    void configure(bool diagnostics = true) {
        network.scriptAvailability(core::NetworkAvailability::Ready);
        network.advanceAvailabilityScript();
        assertOk(server.setIdentity({"board", "1", "0.1.0"}));
        assertOk(server.setNetworkConfig(
            {api::NetworkConfig::Mode::Dhcp, 5050, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}));

        const api::ArgumentSpec set_speed_args[]{{"rpm", api::ValueType::Int}};
        assertOk(server.registerCommand({"set_speed", set_speed_args, 1, true}, setSpeed,
                                        &state));
        assertOk(server.registerCommand({"get_status", nullptr, 0, false}, getStatus, &state));

        const api::FieldSpec telemetry_fields[]{{"rpm", api::ValueType::Int},
                                                {"overflow", api::ValueType::String}};
        assertOk(server.registerTelemetrySchema(telemetry_fields, 2));
        assertOk(server.setTelemetryProvider(telemetryProvider, &state));
        assertOk(server.setEstopHook(estopHook, &state));
        assertOk(server.setControllerLossHook(lossHook, &state));
        if (diagnostics) {
            assertOk(server.enableCountersDiagnosticCommand());
        }
        assertOk(server.start());
    }

    fakes::FakeTransport* acceptSession() {
        server.service();
        assert(network.isListening());
        network.queueInboundConnection();
        server.service();
        fakes::FakeTransport* transport = network.fakeTransport({0, 1});
        assert(transport != nullptr);
        const std::string written = transport->writtenString();
        assert(written.find("{\"type\":\"schema\"") == 0);
        assert(contains(written, "\"type\":\"telemetry\""));
        assert(server.counters().sessions_accepted == 1);
        assert(server.counters().schemas_sent == 1);
        assert(server.counters().telemetry_sent == 1);
        return transport;
    }
};

void registeredCommandThroughFacadeProducesCorrelatedResponseWithoutTeardown() {
    FacadeHarness harness;
    harness.configure();
    fakes::FakeTransport* transport = harness.acceptSession();
    const std::string before = transport->writtenString();

    transport->scriptInboundBytes(
        "{\"type\":\"command\",\"seq\":42,\"controller_ts\":12.5,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"set_speed\",\"args\":{\"rpm\":1200}}\n");
    harness.clock.advanceMicroseconds(250);
    harness.server.service();

    const std::string after = transport->writtenString();
    assert(after.size() > before.size());
    assert(harness.state.set_speed_calls == 1);
    assert(harness.state.last_rpm == 1200);
    assert(harness.state.loss_calls == 0);
    assert(harness.network.abortCount() == 0);
    assert(harness.server.counters().commands_received == 1);
    assert(harness.server.counters().commands_ok == 1);
    assert(harness.server.counters().controller_disconnects == 0);
    assert(contains(after, "\"type\":\"response\""));
    assert(contains(after, "\"seq\":42"));
    assert(contains(after, "\"controller_ts\":12.5"));
    assert(contains(after, "\"status\":\"ok\""));
    assert(contains(after, "\"rpm\":1200"));
    assert(contains(after, "\"board_proc_us\""));
}

void telemetryUsesFiftyMillisecondScheduleAndResumesAfterReplacement() {
    FacadeHarness harness;
    harness.configure();
    fakes::FakeTransport* first = harness.acceptSession();
    assert(harness.state.telemetry_calls == 1);

    harness.clock.advanceMilliseconds(limits::kTelemetryPeriodMs - 1);
    harness.server.service();
    assert(harness.state.telemetry_calls == 1);
    assert(countOccurrences(first->writtenString(), "\"type\":\"telemetry\"") == 1);

    harness.clock.advanceMilliseconds(1);
    harness.server.service();
    assert(harness.state.telemetry_calls == 2);
    assert(countOccurrences(first->writtenString(), "\"type\":\"telemetry\"") == 2);

    harness.network.queueInboundConnection();
    harness.server.service();
    assert(harness.state.loss_calls == 1);
    assert(harness.network.abortCount() == 1);
    assert(harness.server.counters().sessions_accepted == 2);
    assert(harness.server.counters().sessions_superseded == 1);
    assert(harness.server.counters().schemas_sent == 2);
    assert(harness.server.counters().controller_disconnects == 1);

    fakes::FakeTransport* second = harness.network.fakeTransport({1, 1});
    assert(second != nullptr);
    const std::string replacement_schema = second->writtenString();
    assert(replacement_schema.find("{\"type\":\"schema\"") == 0);
    assert(!contains(replacement_schema, "\"type\":\"telemetry\""));
    harness.server.service();
    const std::string replacement = second->writtenString();
    assert(contains(replacement, "\"type\":\"telemetry\""));
    assert(harness.state.telemetry_calls == 3);
}

void estopAckAndGetCountersBothRouteThroughFacade() {
    FacadeHarness harness;
    harness.configure();
    fakes::FakeTransport* transport = harness.acceptSession();

    transport->scriptInboundBytes(
        "{\"type\":\"estop\",\"source\":\"controller\",\"target\":\"board\"}\n");
    harness.server.service();
    assert(harness.state.estop_calls == 1);
    assert(harness.server.counters().estop_received == 1);
    assert(harness.server.counters().estop_ack_sent == 1);
    assert(contains(transport->writtenString(), "\"event\":\"estop_ack\""));
    assert(contains(transport->writtenString(), "\"details\":{\"state\":\"safe\"}"));

    transport->scriptInboundBytes(
        "{\"type\":\"command\",\"seq\":43,\"controller_ts\":13,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"get_counters\",\"args\":{}}\n");
    harness.server.service();
    const std::string written = transport->writtenString();
    assert(contains(written, "\"seq\":43"));
    assert(contains(written, "\"status\":\"ok\""));
    assert(contains(written, "\"estop_received\":1"));
    assert(harness.state.loss_calls == 0);
    assert(harness.network.abortCount() == 0);
}

void unknownAndBadArgsReturnErrorsWithoutTeardown() {
    FacadeHarness harness;
    harness.configure();
    fakes::FakeTransport* transport = harness.acceptSession();

    transport->scriptInboundBytes(
        "{\"type\":\"command\",\"seq\":44,\"controller_ts\":14,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"missing\",\"args\":{}}\n");
    harness.server.service();
    assert(harness.state.set_speed_calls == 0);
    assert(harness.server.counters().unknown_commands == 1);
    assert(contains(transport->writtenString(), "UNKNOWN_COMMAND"));

    transport->scriptInboundBytes(
        "{\"type\":\"command\",\"seq\":45,\"controller_ts\":15,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"set_speed\",\"args\":{\"rpm\":\"fast\"}}\n");
    harness.server.service();
    const std::string written = transport->writtenString();
    assert(harness.state.set_speed_calls == 0);
    assert(harness.server.counters().commands_error == 2);
    assert(contains(written, "\"seq\":45"));
    assert(contains(written, "INVALID_TYPE"));
    assert(harness.state.loss_calls == 0);
    assert(harness.network.abortCount() == 0);
    assert(harness.server.counters().controller_disconnects == 0);
}

void oversizedResponseFallsBackAndTelemetryProviderFailuresDropFrames() {
    FacadeHarness harness;
    harness.configure();
    fakes::FakeTransport* transport = harness.acceptSession();

    harness.state.response_oversized = true;
    transport->scriptInboundBytes(
        "{\"type\":\"command\",\"seq\":46,\"controller_ts\":16,\"source\":\"controller\","
        "\"target\":\"board\",\"command\":\"get_status\",\"args\":{}}\n");
    harness.server.service();
    assert(harness.state.get_status_calls == 1);
    assert(contains(transport->writtenString(), "\"seq\":46"));
    assert(contains(transport->writtenString(), "INTERNAL_ERROR"));
    assert(harness.server.counters().commands_error == 1);
    assert(harness.network.abortCount() == 0);

    const std::size_t before_size = transport->writtenString().size();
    harness.state.telemetry_ok = false;
    harness.clock.advanceMilliseconds(limits::kTelemetryPeriodMs);
    harness.server.service();
    assert(harness.server.counters().telemetry_dropped == 1);
    assert(transport->writtenString().size() == before_size);

    harness.state.telemetry_ok = true;
    harness.state.telemetry_oversized = true;
    harness.clock.advanceMilliseconds(limits::kTelemetryPeriodMs);
    harness.server.service();
    assert(harness.server.counters().telemetry_dropped == 2);
    assert(transport->writtenString().size() == before_size);
}

void stalePeerThatStopsReadingRecoversThroughTelemetryDeadlineStreak() {
    FacadeHarness harness;
    harness.configure();
    fakes::FakeTransport* transport = harness.acceptSession();
    transport->stopAcceptingWrites();
    harness.network.advanceClockOnProgress(harness.clock, limits::kTransmitDeadlineMs);

    for (std::uint8_t i = 0; i < limits::kTelemetryDeadlineFailuresToTeardown; ++i) {
        harness.clock.advanceMilliseconds(limits::kTelemetryPeriodMs);
        harness.server.service();
    }

    assert(harness.server.counters().telemetry_dropped ==
           limits::kTelemetryDeadlineFailuresToTeardown);
    assert(harness.server.counters().controller_disconnects == 1);
    assert(harness.state.loss_calls == 1);
    assert(harness.network.abortCount() == 1);
}

}  // namespace

int main() {
    registeredCommandThroughFacadeProducesCorrelatedResponseWithoutTeardown();
    telemetryUsesFiftyMillisecondScheduleAndResumesAfterReplacement();
    estopAckAndGetCountersBothRouteThroughFacade();
    unknownAndBadArgsReturnErrorsWithoutTeardown();
    oversizedResponseFallsBackAndTelemetryProviderFailuresDropFrames();
    stalePeerThatStopsReadingRecoversThroughTelemetryDeadlineStreak();
    std::puts("test_facade_service: ok");
    return 0;
}
