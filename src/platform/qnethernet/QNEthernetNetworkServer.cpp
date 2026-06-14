#include "platform/qnethernet/QNEthernetNetworkServer.h"

#include <Arduino.h>

namespace teensy_command_server::platform::qnethernet {
namespace {

constexpr char kNoDelayFailureMessage[] = "command-server: setNoDelay failed\n";

}  // namespace

QNEthernetNetworkServer::QNEthernetNetworkServer(const api::NetworkConfig& config) {
    configure(config);
}

void QNEthernetNetworkServer::configure(const api::NetworkConfig& config) {
    config_ = config;
    configured_ = true;
    interface_begin_attempted_ = false;
    interface_started_ = false;
    listening_ = false;
}

void QNEthernetNetworkServer::progress() {
    beginInterfaceIfNeeded();
    qindesign::network::Ethernet.loop();
    yield();
}

core::NetworkAvailability QNEthernetNetworkServer::availability() const {
    if (!configured_ || !interface_started_) {
        return core::NetworkAvailability::Uninitialized;
    }
    if (qindesign::network::Ethernet.linkStatus() == qindesign::network::LinkOFF) {
        return core::NetworkAvailability::LinkDown;
    }
    if (!hasAddress(qindesign::network::Ethernet.localIP())) {
        return core::NetworkAvailability::AddressPending;
    }
    return core::NetworkAvailability::Ready;
}

bool QNEthernetNetworkServer::begin(std::uint16_t port) {
    if (availability() != core::NetworkAvailability::Ready || port == 0U) {
        return false;
    }
    listening_ = server_.beginWithReuse(port);
    return listening_;
}

bool QNEthernetNetworkServer::isListening() const {
    return listening_ && static_cast<bool>(server_);
}

bool QNEthernetNetworkServer::hasPendingConnection() const {
    if (!isListening() || firstReusableSlot() >= slots_.size()) {
        return false;
    }
    return cachePendingConnection();
}

core::ConnectionHandle QNEthernetNetworkServer::accept() {
    if (!hasPendingConnection()) {
        return core::kInvalidConnection;
    }

    const std::size_t slot_index = firstReusableSlot();
    if (slot_index >= slots_.size()) {
        return core::kInvalidConnection;
    }

    Slot& slot = slots_[slot_index];
    if (slot.generation == 0U) {
        slot.generation = 1U;
    }
    pending_client_.setConnectionTimeoutEnabled(false);
    if (!pending_client_.setNoDelay(true)) {
        logNoDelayFailure();
    }
    slot.transport.reset(pending_client_);
    slot.active = true;
    pending_client_ = qindesign::network::EthernetClient{};
    return core::ConnectionHandle{static_cast<std::uint8_t>(slot_index), slot.generation};
}

core::Transport* QNEthernetNetworkServer::transport(core::ConnectionHandle handle) {
    Slot* slot = lookup(handle);
    return slot == nullptr ? nullptr : &slot->transport;
}

void QNEthernetNetworkServer::close(core::ConnectionHandle handle) {
    Slot* slot = lookup(handle);
    if (slot == nullptr) {
        return;
    }
    slot->transport.close();
    invalidate(*slot);
}

void QNEthernetNetworkServer::abort(core::ConnectionHandle handle) {
    Slot* slot = lookup(handle);
    if (slot == nullptr) {
        return;
    }
    slot->transport.abort();
    invalidate(*slot);
}

IPAddress QNEthernetNetworkServer::ipAddress(const std::uint8_t (&bytes)[4]) {
    return IPAddress(bytes[0], bytes[1], bytes[2], bytes[3]);
}

bool QNEthernetNetworkServer::hasAddress(IPAddress address) {
    const std::uint32_t raw = static_cast<std::uint32_t>(address);
    return raw != 0U && raw != INADDR_NONE;
}

void QNEthernetNetworkServer::logNoDelayFailure() {
    if (Serial) {
        Serial.write(reinterpret_cast<const std::uint8_t*>(kNoDelayFailureMessage),
                     sizeof(kNoDelayFailureMessage) - 1U);
    }
}

void QNEthernetNetworkServer::beginInterfaceIfNeeded() {
    if (!configured_ || interface_started_ || interface_begin_attempted_) {
        return;
    }

    interface_begin_attempted_ = true;
    if (config_.mode == api::NetworkConfig::Mode::StaticIpv4) {
        interface_started_ = qindesign::network::Ethernet.begin(
            ipAddress(config_.ip), ipAddress(config_.subnet), ipAddress(config_.gateway));
        return;
    }

    interface_started_ = qindesign::network::Ethernet.begin();
}

std::size_t QNEthernetNetworkServer::firstReusableSlot() const {
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (!slots_[index].active && !slots_[index].retired) {
            return index;
        }
    }
    return slots_.size();
}

QNEthernetNetworkServer::Slot* QNEthernetNetworkServer::lookup(
    core::ConnectionHandle handle) {
    if (!core::isValidConnectionHandle(handle) || handle.slot >= slots_.size()) {
        return nullptr;
    }
    Slot& slot = slots_[handle.slot];
    if (!slot.active || slot.retired || slot.generation != handle.generation) {
        return nullptr;
    }
    return &slot;
}

const QNEthernetNetworkServer::Slot* QNEthernetNetworkServer::lookup(
    core::ConnectionHandle handle) const {
    return const_cast<QNEthernetNetworkServer*>(this)->lookup(handle);
}

void QNEthernetNetworkServer::invalidate(Slot& slot) {
    slot.active = false;
    if (slot.generation == UINT32_MAX) {
        slot.generation = 0U;
        slot.retired = true;
        return;
    }
    ++slot.generation;
    if (slot.generation == 0U) {
        slot.retired = true;
    }
}

bool QNEthernetNetworkServer::cachePendingConnection() const {
    if (static_cast<bool>(pending_client_)) {
        return true;
    }
    pending_client_ = server_.accept();
    return static_cast<bool>(pending_client_);
}

}  // namespace teensy_command_server::platform::qnethernet
