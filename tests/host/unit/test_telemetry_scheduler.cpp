#include "api/BoardIdentity.h"
#include "api/ObjectWriter.h"
#include "core/Counters.h"
#include "core/OutboundScheduler.h"
#include "core/ServiceLoop.h"
#include "core/TelemetryScheduler.h"
#include "fakes/FakeClock.h"

#include <cassert>
#include <cstring>
#include <string>

namespace {

namespace api = teensy_command_server::api;
namespace core = teensy_command_server::core;
namespace fakes = teensy_command_server::host::fakes;
namespace limits = teensy_command_server::support;

struct ProviderState {
    bool ok = true;
    bool overflow = false;
    std::int32_t value = 10;
    unsigned calls = 0;
};

bool telemetryProvider(api::ObjectWriter& telemetry, void* raw) {
    auto* state = static_cast<ProviderState*>(raw);
    ++state->calls;
    if (!state->ok) {
        return false;
    }
    if (state->overflow) {
        char large[limits::kMaxResultPayloadBytes]{};
        for (std::size_t i = 0; i + 1 < sizeof(large); ++i) {
            large[i] = 'x';
        }
        return telemetry.addString("oversized", large) &&
               telemetry.addString("second", "does not fit");
    }
    return telemetry.addInt("value", state->value);
}

struct FakeSession {
    bool active = true;
    bool pending_teardown = false;
    bool applied = false;
    unsigned polls = 0;
    unsigned sends = 0;
    core::OutboundSendResult send_result = core::OutboundSendResult::Sent;
    core::TeardownReason requested_reason = core::TeardownReason::ExplicitShutdown;
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
        out = {};
        return false;
    }

    void releaseLine() {}

    void requestTeardown(core::TeardownReason reason) {
        pending_teardown = true;
        requested_reason = reason;
    }

    void applyPendingTeardown() {
        applied = true;
        pending_teardown = false;
        active = false;
    }

    core::OutboundSendResult sendActiveLine(core::ConstLineView line,
                                            core::MessageClass message_class) {
        assert(message_class == core::MessageClass::Telemetry);
        ++sends;
        written.assign(line.data, line.size);
        return send_result;
    }
};

struct RouteState {
    core::TelemetryScheduler* telemetry = nullptr;
    unsigned due_calls = 0;
    unsigned inactive_calls = 0;
};

void routeTelemetryDue(core::OutboundScheduler& scheduler, void* raw) {
    auto* state = static_cast<RouteState*>(raw);
    ++state->due_calls;
    state->telemetry->service(scheduler);
}

void routeTelemetryInactive(void* raw) {
    auto* state = static_cast<RouteState*>(raw);
    ++state->inactive_calls;
    state->telemetry->onSessionInactive();
}

core::ServiceLoop::Routes routes(RouteState& state) {
    core::ServiceLoop::Routes r;
    r.telemetry_due = routeTelemetryDue;
    r.context = &state;
    r.telemetry_inactive = routeTelemetryInactive;
    return r;
}

std::string drainOne(core::OutboundScheduler& scheduler,
                     core::Counters& counters,
                     core::OutboundCompletion* completion = nullptr) {
    FakeSession session;
    core::OutboundOutcome outcome{};
    assert(scheduler.drainOne(session, outcome));
    assert(outcome.kind == core::OutboundKind::Telemetry);
    if (completion != nullptr) {
        *completion = outcome.completion;
    }
    if (outcome.completion == core::OutboundCompletion::Sent) {
        counters.increment(&core::Counters::telemetry_sent);
    }
    return session.written;
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

void firstFrameThenFiftyMillisecondCadence() {
    core::Counters counters;
    fakes::FakeClock clock;
    ProviderState provider;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    core::OutboundScheduler outbound;
    core::TelemetryScheduler telemetry(counters, identity, clock, telemetryProvider, &provider);

    telemetry.service(outbound);
    assert(provider.calls == 1);
    assert(outbound.telemetryPending());
    std::string written = drainOne(outbound, counters);
    assert(contains(written, "\"type\":\"telemetry\""));
    assert(contains(written, "\"seq\":1"));
    assert(contains(written, "\"timestamp\":0"));
    assert(contains(written, "\"source\":\"board\""));
    assert(contains(written, "\"telemetry\":{\"value\":10}"));
    assert(counters.telemetry_sent == 1);

    telemetry.service(outbound);
    assert(provider.calls == 1);
    assert(!outbound.telemetryPending());

    clock.advanceMilliseconds(limits::kTelemetryPeriodMs - 1);
    telemetry.service(outbound);
    assert(provider.calls == 1);

    clock.advanceMilliseconds(1);
    provider.value = 11;
    telemetry.service(outbound);
    assert(provider.calls == 2);
    written = drainOne(outbound, counters);
    assert(contains(written, "\"seq\":2"));
    assert(contains(written, "\"timestamp\":50"));
    assert(contains(written, "\"telemetry\":{\"value\":11}"));
    assert(counters.telemetry_sent == 2);
}

void missedPeriodsCoalesceAndReplacementCreatesSequenceGap() {
    core::Counters counters;
    fakes::FakeClock clock;
    ProviderState provider;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    core::OutboundScheduler outbound;
    core::TelemetryScheduler telemetry(counters, identity, clock, telemetryProvider, &provider);

    telemetry.service(outbound);
    assert(telemetry.nextSeqForTest() == 2);
    clock.advanceMilliseconds(250);
    provider.value = 20;
    telemetry.service(outbound);
    assert(telemetry.nextSeqForTest() == 3);
    assert(counters.telemetry_coalesced == 1);
    assert(provider.calls == 2);

    const std::string written = drainOne(outbound, counters);
    assert(contains(written, "\"seq\":2"));
    assert(contains(written, "\"value\":20"));
    assert(!contains(written, "\"seq\":1"));
    assert(counters.telemetry_sent == 1);
}

void providerAndSerializationFailureDropWithoutSequenceOrDeadlineStreak() {
    core::Counters counters;
    fakes::FakeClock clock;
    ProviderState provider;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    core::OutboundScheduler outbound;
    core::TelemetryScheduler telemetry(counters, identity, clock, telemetryProvider, &provider);

    provider.ok = false;
    telemetry.service(outbound);
    assert(!outbound.telemetryPending());
    assert(counters.telemetry_dropped == 1);
    assert(telemetry.nextSeqForTest() == 1);

    provider.ok = true;
    provider.overflow = true;
    clock.advanceMilliseconds(limits::kTelemetryPeriodMs);
    telemetry.service(outbound);
    assert(!outbound.telemetryPending());
    assert(counters.telemetry_dropped == 2);
    assert(telemetry.nextSeqForTest() == 1);

    provider.overflow = false;
    clock.advanceMilliseconds(limits::kTelemetryPeriodMs);
    telemetry.service(outbound);
    assert(outbound.telemetryPending());
    assert(telemetry.nextSeqForTest() == 2);
    const std::string written = drainOne(outbound, counters);
    assert(contains(written, "\"seq\":1"));
    assert(counters.telemetry_sent == 1);
}

void serviceLoopGatesBeforeSchemaAndResetsFirstFrameOnReconnect() {
    core::Counters counters;
    fakes::FakeClock clock;
    ProviderState provider;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    core::OutboundScheduler outbound;
    core::TelemetryScheduler telemetry(counters, identity, clock, telemetryProvider, &provider);
    RouteState route_state{&telemetry};
    core::ServiceLoop loop(counters, identity, outbound, routes(route_state), clock);
    FakeSession session;
    session.active = false;

    loop.service(session);
    assert(route_state.inactive_calls == 1);
    assert(route_state.due_calls == 0);
    assert(provider.calls == 0);
    assert(!outbound.telemetryPending());

    session.active = true;
    loop.service(session);
    assert(route_state.due_calls == 1);
    assert(provider.calls == 1);
    assert(session.sends == 1);
    assert(contains(session.written, "\"seq\":1"));
    assert(counters.telemetry_sent == 1);

    session.active = false;
    loop.service(session);
    assert(route_state.inactive_calls == 2);

    clock.advanceMilliseconds(1);
    session.active = true;
    session.written.clear();
    loop.service(session);
    assert(provider.calls == 2);
    assert(session.sends == 2);
    assert(contains(session.written, "\"seq\":2"));
    assert(counters.telemetry_sent == 2);
}

void telemetryContinuesDuringEstopAndAfterHookReturnsWithinLivenessWindow() {
    core::Counters counters;
    fakes::FakeClock clock;
    ProviderState provider;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    core::OutboundScheduler outbound;
    core::TelemetryScheduler telemetry(counters, identity, clock, telemetryProvider, &provider);
    RouteState route_state{&telemetry};
    core::ServiceLoop loop(counters, identity, outbound, routes(route_state), clock);
    FakeSession session;

    loop.service(session);
    assert(session.sends == 1);
    clock.advanceMilliseconds(limits::kHookBudgetMs);
    provider.value = 12;
    loop.service(session);
    assert(session.sends == 2);
    assert(contains(session.written, "\"seq\":2"));
    assert(contains(session.written, "\"value\":12"));
    assert(clock.monotonicMilliseconds() < limits::kLivenessWindowMs);
}

void continuousCommandInputDoesNotStarveTelemetry() {
    core::Counters counters;
    fakes::FakeClock clock;
    ProviderState provider;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    core::OutboundScheduler outbound;
    core::TelemetryScheduler telemetry(counters, identity, clock, telemetryProvider, &provider);
    RouteState route_state{&telemetry};
    core::ServiceLoop loop(counters, identity, outbound, routes(route_state), clock);
    FakeSession session;

    for (unsigned i = 0; i < 5; ++i) {
        provider.value = static_cast<std::int32_t>(30 + i);
        loop.service(session);
        clock.advanceMilliseconds(limits::kTelemetryPeriodMs);
    }

    assert(session.sends == 5);
    assert(provider.calls == 5);
    assert(counters.telemetry_sent == 5);
    assert(clock.monotonicMilliseconds() == limits::kLivenessWindowMs);
}

void telemetryDeadlineDropDoesNotTeardownUntilWriterSignals() {
    core::Counters counters;
    fakes::FakeClock clock;
    ProviderState provider;
    api::BoardIdentity identity{"board", "1", "0.1.0"};
    core::OutboundScheduler outbound;
    core::TelemetryScheduler telemetry(counters, identity, clock, telemetryProvider, &provider);
    RouteState route_state{&telemetry};
    core::ServiceLoop loop(counters, identity, outbound, routes(route_state), clock);
    FakeSession session;
    session.send_result = core::OutboundSendResult::DeadlineExpired;

    loop.service(session);
    assert(session.sends == 1);
    assert(!session.applied);
    assert(!session.pending_teardown);
    assert(counters.telemetry_sent == 0);

    clock.advanceMilliseconds(limits::kTelemetryPeriodMs);
    session.pending_teardown = true;
    session.requested_reason = core::TeardownReason::TelemetryTransmitFailureStreak;
    loop.service(session);
    assert(session.applied);
    assert(session.requested_reason == core::TeardownReason::TelemetryTransmitFailureStreak);
}

}  // namespace

int main() {
    firstFrameThenFiftyMillisecondCadence();
    missedPeriodsCoalesceAndReplacementCreatesSequenceGap();
    providerAndSerializationFailureDropWithoutSequenceOrDeadlineStreak();
    serviceLoopGatesBeforeSchemaAndResetsFirstFrameOnReconnect();
    telemetryContinuesDuringEstopAndAfterHookReturnsWithinLivenessWindow();
    continuousCommandInputDoesNotStarveTelemetry();
    telemetryDeadlineDropDoesNotTeardownUntilWriterSignals();
    return 0;
}
