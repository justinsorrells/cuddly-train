#include <cassert>
#include <cstdio>

#include "core/NetworkServer.h"
#include "fakes/FakeNetworkServer.h"

namespace {

using teensy_command_server::core::ConnectionHandle;
using teensy_command_server::core::NetworkAvailability;
using teensy_command_server::core::NetworkServer;
using teensy_command_server::core::Transport;
using teensy_command_server::core::kInvalidConnection;
using teensy_command_server::core::sameConnection;
using teensy_command_server::host::fakes::FakeNetworkServer;
using teensy_command_server::host::fakes::FakeTransport;

void availabilityTransitionsAreScripted() {
    FakeNetworkServer server;
    server.scriptAvailability(NetworkAvailability::Uninitialized);
    server.scriptAvailability(NetworkAvailability::LinkDown);
    server.scriptAvailability(NetworkAvailability::AddressPending);
    server.scriptAvailability(NetworkAvailability::Ready);

    assert(server.availability() == NetworkAvailability::Uninitialized);
    server.advanceAvailabilityScript();
    assert(server.availability() == NetworkAvailability::Uninitialized);
    server.advanceAvailabilityScript();
    assert(server.availability() == NetworkAvailability::LinkDown);
    server.advanceAvailabilityScript();
    assert(server.availability() == NetworkAvailability::AddressPending);
    server.advanceAvailabilityScript();
    assert(server.availability() == NetworkAvailability::Ready);
}

void beginListenAndProgressAreObservable() {
    FakeNetworkServer server;
    assert(!server.isListening());
    assert(server.begin(4242));
    assert(server.isListening());
    assert(server.listenPort() == 4242);

    server.queueInboundConnection();
    const ConnectionHandle handle = server.accept();
    assert(!sameConnection(handle, kInvalidConnection));
    FakeTransport* transport = server.fakeTransport(handle);
    assert(transport != nullptr);
    assert(server.progressCount() == 0);
    assert(transport->progressCount() == 0);

    server.progress();
    assert(server.progressCount() == 1);
    assert(transport->progressCount() == 1);
}

void simultaneousHandlesResolveIndependently() {
    FakeNetworkServer server;
    server.queueInboundConnection();
    server.queueInboundConnection();
    const ConnectionHandle first = server.accept();
    const ConnectionHandle second = server.accept();

    assert(first.slot != second.slot);
    Transport* first_transport = server.transport(first);
    Transport* second_transport = server.transport(second);
    assert(first_transport != nullptr);
    assert(second_transport != nullptr);
    assert(first_transport != second_transport);

    server.close(first);
    assert(server.closeCount() == 1);
    assert(server.transport(first) == nullptr);
    assert(server.transport(second) == second_transport);

    server.abort(second);
    assert(server.abortCount() == 1);
    assert(server.transport(second) == nullptr);
}

void closeAndAbortInvalidateWithNewGenerations() {
    FakeNetworkServer server;
    server.queueInboundConnection();
    const ConnectionHandle first = server.accept();
    FakeTransport* first_transport = server.fakeTransport(first);
    assert(first_transport != nullptr);

    server.close(first);
    assert(first_transport->wasClosed());
    assert(!first_transport->wasAborted());
    assert(server.transport(first) == nullptr);

    server.queueInboundConnection();
    const ConnectionHandle after_close = server.accept();
    assert(after_close.slot == first.slot);
    assert(after_close.generation != first.generation);
    FakeTransport* after_close_transport = server.fakeTransport(after_close);
    assert(after_close_transport != nullptr);
    assert(!after_close_transport->wasClosed());
    assert(!after_close_transport->wasAborted());
    assert(server.transport(first) == nullptr);

    server.abort(after_close);
    assert(after_close_transport->wasAborted());
    assert(server.transport(after_close) == nullptr);

    server.queueInboundConnection();
    const ConnectionHandle after_abort = server.accept();
    assert(after_abort.slot == after_close.slot);
    assert(after_abort.generation != after_close.generation);
    FakeTransport* after_abort_transport = server.fakeTransport(after_abort);
    assert(after_abort_transport != nullptr);
    assert(!after_abort_transport->wasClosed());
    assert(!after_abort_transport->wasAborted());
    assert(server.transport(after_close) == nullptr);
}

void abortReusesSlotImmediatelyAndStaleHandlesFailClosed() {
    FakeNetworkServer server;
    server.queueInboundConnection();
    const ConnectionHandle old_handle = server.accept();
    assert(server.transport(old_handle) != nullptr);

    server.abort(old_handle);
    assert(server.abortCount() == 1);
    assert(server.transport(old_handle) == nullptr);

    server.queueInboundConnection();
    assert(server.hasPendingConnection());
    const ConnectionHandle new_handle = server.accept();
    assert(new_handle.slot == old_handle.slot);
    assert(new_handle.generation != old_handle.generation);
    assert(server.transport(new_handle) != nullptr);

    server.close(old_handle);
    server.abort(old_handle);
    assert(server.closeCount() == 0);
    assert(server.abortCount() == 1);
    assert(server.transport(new_handle) != nullptr);
}

void invalidAndExhaustedHandlesFailClosed() {
    FakeNetworkServer server;
    assert(server.accept().generation == 0U);
    assert(server.transport(kInvalidConnection) == nullptr);

    server.queueInboundConnection();
    server.queueInboundConnection();
    server.queueInboundConnection();
    const ConnectionHandle first = server.accept();
    const ConnectionHandle second = server.accept();
    assert(server.transport(first) != nullptr);
    assert(server.transport(second) != nullptr);
    assert(!server.hasPendingConnection());
    assert(server.accept().generation == 0U);
}

void generationZeroWrapRetiresSlotUntilReboot() {
    FakeNetworkServer server;
    server.forceNextGeneration(0, UINT32_MAX);
    server.queueInboundConnection();
    const ConnectionHandle handle = server.accept();
    assert(handle.slot == 0U);
    assert(handle.generation == UINT32_MAX);

    server.abort(handle);
    assert(server.transport(handle) == nullptr);

    server.queueInboundConnection();
    const ConnectionHandle replacement = server.accept();
    assert(replacement.slot == 1U);
    assert(replacement.generation == 1U);
}

}  // namespace

int main() {
    availabilityTransitionsAreScripted();
    beginListenAndProgressAreObservable();
    simultaneousHandlesResolveIndependently();
    closeAndAbortInvalidateWithNewGenerations();
    abortReusesSlotImmediatelyAndStaleHandlesFailClosed();
    invalidAndExhaustedHandlesFailClosed();
    generationZeroWrapRetiresSlotUntilReboot();
    std::puts("test_network_server: ok");
    return 0;
}
