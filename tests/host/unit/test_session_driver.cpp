#include "api/BoardIdentity.h"
#include "api/CommandResult.h"
#include "core/SessionDriver.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeNetworkServer.h"
#include "support/Limits.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

namespace api = teensy_command_server::api;
namespace core = teensy_command_server::core;
namespace fakes = teensy_command_server::host::fakes;
namespace limits = teensy_command_server::support;

api::CommandResult handler(const api::CommandContext&, api::ObjectWriter&, void*) {
    return api::CommandResult::ok();
}

bool controllerLossHook(void* raw_context) {
    auto* calls = static_cast<unsigned*>(raw_context);
    ++(*calls);
    return true;
}

struct TimedHookContext {
    fakes::FakeClock* clock = nullptr;
    unsigned calls = 0;
    std::uint64_t advance_us = 0;
};

bool timedControllerLossHook(void* raw_context) {
    auto* context = static_cast<TimedHookContext*>(raw_context);
    ++context->calls;
    context->clock->advanceMicroseconds(context->advance_us);
    return true;
}

void assertOk(api::Status status) {
    assert(status.code == api::StatusCode::Ok);
}

core::CommandRegistry registryWithOneCommand() {
    core::CommandRegistry registry;
    assertOk(registry.registerCommand(
        {"get_status", nullptr, 0, false, true, handler, nullptr}));
    assertOk(registry.registerTelemetryField({"uptime_ms", api::ValueType::Int}));
    assertOk(registry.registerStateField({"ready", api::ValueType::Bool}));
    return registry;
}

struct Harness {
    Harness() : registry(registryWithOneCommand()),
                driver(server,
                       clock,
                       counters,
                       registry,
                       identity,
                       schema_builder,
                       controllerLossHook,
                       &hook_calls,
                       4242) {
        server.scriptAvailability(core::NetworkAvailability::Ready);
        server.advanceAvailabilityScript();
    }

    void startListening() {
        driver.begin();
        driver.poll();
        assert(driver.state() == core::SessionState::LISTENING);
        assert(server.isListening());
        assert(server.listenPort() == 4242);
    }

    core::ConnectionHandle acceptSession() {
        server.queueInboundConnection();
        driver.poll();
        assert(driver.state() == core::SessionState::SESSION_ACTIVE);
        assert(driver.sessionActive());
        return driver.activeHandle();
    }

    fakes::FakeClock clock;
    fakes::FakeNetworkServer server;
    core::Counters counters;
    core::CommandRegistry registry;
    api::BoardIdentity identity{"motor_controller", "1", "0.1.0"};
    core::SchemaBuilder schema_builder;
    unsigned hook_calls = 0;
    core::SessionDriver driver;
};

void startsSafeAndRetriesNetworkInitWithoutCommands() {
    fakes::FakeClock clock;
    fakes::FakeNetworkServer server;
    core::Counters counters;
    core::CommandRegistry registry = registryWithOneCommand();
    core::SchemaBuilder schema_builder;
    unsigned hook_calls = 0;
    core::SessionDriver driver(server,
                               clock,
                               counters,
                               registry,
                               {"motor_controller", "1", "0.1.0"},
                               schema_builder,
                               controllerLossHook,
                               &hook_calls,
                               4242);

    server.scriptAvailability(core::NetworkAvailability::Ready);
    server.advanceAvailabilityScript();
    server.scriptBeginResult(false);

    assert(driver.state() == core::SessionState::BOOT_SAFE);
    driver.begin();
    assert(driver.state() == core::SessionState::NETWORK_STARTING);

    driver.poll();
    assert(driver.state() == core::SessionState::NETWORK_STARTING);
    assert(!server.isListening());
    assert(counters.sessions_accepted == 0);
    assert(hook_calls == 0);

    clock.advanceMilliseconds(limits::kNetworkInitRetryMs - 1U);
    driver.poll();
    assert(driver.state() == core::SessionState::NETWORK_STARTING);

    clock.advanceMilliseconds(1);
    driver.poll();
    assert(driver.state() == core::SessionState::LISTENING);
    assert(server.isListening());
}

void schemaIsFirstBytesAndReconnectResendsIt() {
    Harness harness;
    harness.startListening();

    const core::ConnectionHandle first = harness.acceptSession();
    fakes::FakeTransport* first_transport = harness.server.fakeTransport(first);
    assert(first_transport != nullptr);
    const std::string first_written = first_transport->writtenString();
    assert(first_written.find("{\"type\":\"schema\"") == 0);
    assert(first_written.find("\"source\":\"motor_controller\"") != std::string::npos);
    assert(first_written.back() == '\n');
    assert(harness.counters.sessions_accepted == 1);
    assert(harness.counters.schemas_sent == 1);

    harness.driver.requestShutdown();
    harness.driver.applyPendingTeardown();
    assert(harness.driver.state() == core::SessionState::LISTENING);

    const core::ConnectionHandle second = harness.acceptSession();
    fakes::FakeTransport* second_transport = harness.server.fakeTransport(second);
    assert(second_transport != nullptr);
    const std::string second_written = second_transport->writtenString();
    assert(second_written.find("{\"type\":\"schema\"") == 0);
    assert(harness.counters.sessions_accepted == 2);
    assert(harness.counters.schemas_sent == 2);
    assert(harness.counters.controller_disconnects == 1);
    assert(harness.hook_calls == 1);
}

void schemaSendFailureClosesToListening() {
    Harness harness;
    harness.startListening();
    harness.server.stopAcceptingWritesOnNextConnection();
    harness.server.advanceClockOnProgress(harness.clock, limits::kTransmitDeadlineMs);

    harness.server.queueInboundConnection();
    harness.driver.poll();

    assert(harness.driver.state() == core::SessionState::LISTENING);
    assert(!harness.driver.sessionActive());
    assert(harness.counters.sessions_accepted == 1);
    assert(harness.counters.schemas_sent == 0);
    assert(harness.counters.tx_failures == 1);
    assert(harness.counters.controller_disconnects == 1);
    assert(harness.hook_calls == 1);
}

void supersessionRunsLossHookClearsFramerAndPromotesReplacement() {
    fakes::FakeClock clock;
    fakes::FakeNetworkServer server;
    core::Counters counters;
    core::CommandRegistry registry = registryWithOneCommand();
    core::SchemaBuilder schema_builder;
    TimedHookContext hook_context{&clock, 0, (limits::kHookBudgetMs + 1ULL) * 1000ULL};
    core::SessionDriver driver(server,
                               clock,
                               counters,
                               registry,
                               {"motor_controller", "1", "0.1.0"},
                               schema_builder,
                               timedControllerLossHook,
                               &hook_context,
                               4242);
    server.scriptAvailability(core::NetworkAvailability::Ready);
    server.advanceAvailabilityScript();
    driver.begin();
    driver.poll();

    server.queueInboundConnection();
    driver.poll();
    const core::ConnectionHandle old_handle = driver.activeHandle();
    fakes::FakeTransport* old_transport = server.fakeTransport(old_handle);
    assert(old_transport != nullptr);
    old_transport->scriptInboundBytes("stale_partial");

    server.queueInboundConnection();
    driver.poll();
    assert(driver.state() == core::SessionState::SESSION_CLOSING);
    assert(driver.teardownPending());
    assert(driver.pendingTeardownReason() == core::TeardownReason::Superseded);

    core::MutableLineView line{};
    assert(!driver.nextLine(line));
    driver.applyPendingTeardown();

    assert(old_transport->wasAborted());
    assert(driver.state() == core::SessionState::SESSION_ACTIVE);
    assert(driver.sessionActive());
    assert(!core::sameConnection(driver.activeHandle(), old_handle));
    fakes::FakeTransport* replacement_transport = server.fakeTransport(driver.activeHandle());
    assert(replacement_transport != nullptr);
    assert(replacement_transport->writtenString().find("{\"type\":\"schema\"") == 0);
    assert(counters.sessions_accepted == 2);
    assert(counters.sessions_superseded == 1);
    assert(counters.schemas_sent == 2);
    assert(counters.controller_disconnects == 1);
    assert(counters.controller_loss_hook_over_budget == 1);
    assert(hook_context.calls == 1);
    assert(!driver.nextLine(line));
}

void linkLossCasesReturnToListeningAndRestorationRelistens() {
    Harness harness;
    harness.startListening();

    harness.server.scriptAvailability(core::NetworkAvailability::LinkDown);
    harness.server.advanceAvailabilityScript();
    harness.driver.poll();
    assert(harness.driver.state() == core::SessionState::LISTENING);
    assert(harness.hook_calls == 0);

    harness.server.scriptAvailability(core::NetworkAvailability::Ready);
    harness.server.advanceAvailabilityScript();
    harness.driver.poll();
    assert(harness.driver.state() == core::SessionState::LISTENING);
    assert(harness.server.isListening());

    harness.server.linkDownOnNextConnection();
    harness.server.advanceClockOnProgress(harness.clock, limits::kTransmitDeadlineMs);
    harness.server.queueInboundConnection();
    harness.driver.poll();
    assert(harness.driver.state() == core::SessionState::LISTENING);
    assert(harness.counters.controller_disconnects == 1);
    assert(harness.hook_calls == 1);

    const core::ConnectionHandle active = harness.acceptSession();
    fakes::FakeTransport* transport = harness.server.fakeTransport(active);
    assert(transport != nullptr);
    transport->scriptLinkDown();
    harness.driver.poll();
    assert(harness.driver.state() == core::SessionState::SESSION_CLOSING);
    harness.driver.applyPendingTeardown();
    assert(harness.driver.state() == core::SessionState::LISTENING);
    assert(harness.counters.controller_disconnects == 2);
    assert(harness.hook_calls == 2);
}

void connectionLossRunsHookAndNoLineAfterLoss() {
    Harness harness;
    harness.startListening();
    const core::ConnectionHandle active = harness.acceptSession();
    fakes::FakeTransport* transport = harness.server.fakeTransport(active);
    assert(transport != nullptr);

    transport->scriptInboundBytes("partial");
    transport->scriptClose();
    harness.driver.poll();
    assert(harness.driver.state() == core::SessionState::SESSION_CLOSING);

    core::MutableLineView line{};
    assert(!harness.driver.nextLine(line));
    harness.driver.applyPendingTeardown();
    assert(harness.driver.state() == core::SessionState::LISTENING);
    assert(harness.counters.controller_disconnects == 1);
    assert(harness.hook_calls == 1);
}

void releaseLineRemainsValidAfterClosing() {
    Harness harness;
    harness.startListening();
    const core::ConnectionHandle active = harness.acceptSession();
    fakes::FakeTransport* transport = harness.server.fakeTransport(active);
    assert(transport != nullptr);

    transport->scriptInboundBytes("complete\n");
    core::MutableLineView line{};
    assert(harness.driver.nextLine(line));
    assert(line.valid());
    assert(line.size == std::strlen("complete"));
    harness.driver.requestShutdown();
    assert(harness.driver.state() == core::SessionState::SESSION_CLOSING);
    harness.driver.releaseLine();
    harness.driver.applyPendingTeardown();
    assert(harness.driver.state() == core::SessionState::LISTENING);
}

}  // namespace

int main() {
    startsSafeAndRetriesNetworkInitWithoutCommands();
    schemaIsFirstBytesAndReconnectResendsIt();
    schemaSendFailureClosesToListening();
    supersessionRunsLossHookClearsFramerAndPromotesReplacement();
    linkLossCasesReturnToListeningAndRestorationRelistens();
    connectionLossRunsHookAndNoLineAfterLoss();
    releaseLineRemainsValidAfterClosing();
    std::puts("test_session_driver: ok");
    return 0;
}
