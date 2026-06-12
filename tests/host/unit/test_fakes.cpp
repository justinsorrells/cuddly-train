#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

#include "fakes/FakeClock.h"
#include "fakes/FakeTransport.h"

namespace {

using teensy_command_server::core::Clock;
using teensy_command_server::core::Transport;
using teensy_command_server::core::TransportConnectionState;
using teensy_command_server::host::fakes::FakeClock;
using teensy_command_server::host::fakes::FakeTransport;

std::string readChunk(Transport& transport, std::size_t capacity) {
    std::array<std::uint8_t, 16> buffer{};
    assert(capacity <= buffer.size());
    const std::size_t count = transport.readSome(buffer.data(), capacity);
    return std::string(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count));
}

void splitDeliveryAcrossReads() {
    FakeTransport transport;
    transport.scriptInboundBytes("abc");
    transport.scriptInboundBytes("def\n");

    assert(transport.connectionState() == TransportConnectionState::BytesAvailable);
    assert(readChunk(transport, 2) == "ab");
    assert(transport.connectionState() == TransportConnectionState::BytesAvailable);
    assert(readChunk(transport, 2) == "c");
    assert(readChunk(transport, 3) == "def");
    assert(readChunk(transport, 8) == "\n");
    assert(transport.connectionState() == TransportConnectionState::Open);
    assert(readChunk(transport, 8).empty());
}

void partialWritesAndStoppedPeer() {
    FakeTransport transport;
    const std::array<std::uint8_t, 6> bytes{{'a', 'b', 'c', 'd', 'e', 'f'}};
    transport.scriptWriteAcceptance(2);
    transport.scriptWriteAcceptance(10);
    transport.scriptWriteAcceptance(0);

    assert(transport.writeSome(bytes.data(), bytes.size()) == 2);
    assert(transport.writtenString() == "ab");
    assert(transport.writeSome(bytes.data() + 2, bytes.size() - 2) == 4);
    assert(transport.writtenString() == "abcdef");
    assert(transport.writeSome(bytes.data(), bytes.size()) == 0);
    assert(transport.writtenString() == "abcdef");

    transport.stopAcceptingWrites();
    assert(transport.writeSome(bytes.data(), bytes.size()) == 0);
    assert(transport.flush());
    assert(transport.flushCount() == 1);
}

void closureAndLinkLossReporting() {
    FakeTransport closed_transport;
    closed_transport.scriptInboundBytes("xy");
    closed_transport.scriptClose();
    assert(closed_transport.connectionState() == TransportConnectionState::BytesAvailable);
    assert(readChunk(closed_transport, 4) == "xy");
    assert(closed_transport.connectionState() == TransportConnectionState::Closed);
    assert(readChunk(closed_transport, 4).empty());
    assert(!closed_transport.flush());

    FakeTransport link_down_transport;
    link_down_transport.scriptLinkDown();
    assert(link_down_transport.connectionState() == TransportConnectionState::LinkDown);
    assert(readChunk(link_down_transport, 4).empty());
    assert(link_down_transport.connectionState() == TransportConnectionState::LinkDown);
    assert(link_down_transport.writeSome(reinterpret_cast<const std::uint8_t*>("z"), 1) == 0);
}

void clockAdvanceThroughInterface() {
    FakeClock fake;
    Clock& clock = fake;

    assert(clock.monotonicMilliseconds() == 0);
    assert(clock.monotonicMicroseconds() == 0);

    fake.advanceMicroseconds(999);
    assert(clock.monotonicMilliseconds() == 0);
    assert(clock.monotonicMicroseconds() == 999);

    fake.advanceMicroseconds(1);
    assert(clock.monotonicMilliseconds() == 1);
    assert(clock.monotonicMicroseconds() == 1000);

    fake.advanceMilliseconds(42);
    assert(clock.monotonicMilliseconds() == 43);
    assert(clock.monotonicMicroseconds() == 43000);
}

}  // namespace

int main() {
    splitDeliveryAcrossReads();
    partialWritesAndStoppedPeer();
    closureAndLinkLossReporting();
    clockAdvanceThroughInterface();
    std::puts("test_fakes: ok");
    return 0;
}
