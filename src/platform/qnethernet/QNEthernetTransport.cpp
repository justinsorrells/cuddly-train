#include "platform/qnethernet/QNEthernetTransport.h"

#include <algorithm>

namespace teensy_command_server::platform::qnethernet {
namespace {

bool linkIsDown() {
    return qindesign::network::Ethernet.linkStatus() == qindesign::network::LinkOFF;
}

}  // namespace

void QNEthernetTransport::reset(qindesign::network::EthernetClient client) {
    client_ = client;
    client_.setConnectionTimeoutEnabled(false);
}

void QNEthernetTransport::close() {
    client_.setConnectionTimeoutEnabled(false);
    client_.close();
}

void QNEthernetTransport::abort() {
    client_.abort();
}

std::size_t QNEthernetTransport::readSome(std::uint8_t* buffer, std::size_t capacity) {
    if (buffer == nullptr || capacity == 0 || linkIsDown()) {
        return 0;
    }

    const int available = client_.available();
    if (available <= 0) {
        return 0;
    }

    const std::size_t requested =
        std::min(capacity, static_cast<std::size_t>(available));
    const int read = client_.read(buffer, requested);
    return read > 0 ? static_cast<std::size_t>(read) : 0U;
}

std::size_t QNEthernetTransport::writeSome(const std::uint8_t* data, std::size_t length) {
    if (data == nullptr || length == 0 || linkIsDown() || !static_cast<bool>(client_)) {
        return 0;
    }
    return client_.write(data, length);
}

bool QNEthernetTransport::flush() {
    if (linkIsDown() || !static_cast<bool>(client_)) {
        return false;
    }
    client_.flush();
    return connectionState() != core::TransportConnectionState::Closed &&
           connectionState() != core::TransportConnectionState::LinkDown;
}

core::TransportConnectionState QNEthernetTransport::connectionState() const {
    if (linkIsDown()) {
        return core::TransportConnectionState::LinkDown;
    }
    if (const_cast<qindesign::network::EthernetClient&>(client_).available() > 0) {
        return core::TransportConnectionState::BytesAvailable;
    }
    if (static_cast<bool>(const_cast<qindesign::network::EthernetClient&>(client_))) {
        return core::TransportConnectionState::Open;
    }
    return core::TransportConnectionState::Closed;
}

}  // namespace teensy_command_server::platform::qnethernet
