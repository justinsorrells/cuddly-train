#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <QNEthernet.h>

#include "api/BoardIdentity.h"
#include "core/NetworkServer.h"
#include "platform/qnethernet/QNEthernetTransport.h"

namespace teensy_command_server::platform::qnethernet {

class QNEthernetNetworkServer final : public core::NetworkServer {
public:
    QNEthernetNetworkServer() = default;
    explicit QNEthernetNetworkServer(const api::NetworkConfig& config);

    void configure(const api::NetworkConfig& config);

    void progress() override;
    core::NetworkAvailability availability() const override;
    bool begin(std::uint16_t port) override;
    bool isListening() const override;
    bool hasPendingConnection() const override;
    core::ConnectionHandle accept() override;
    core::Transport* transport(core::ConnectionHandle handle) override;
    void close(core::ConnectionHandle handle) override;
    void abort(core::ConnectionHandle handle) override;

private:
    struct Slot {
        QNEthernetTransport transport;
        std::uint32_t generation = 0U;
        bool active = false;
        bool retired = false;
    };

    static IPAddress ipAddress(const std::uint8_t (&bytes)[4]);
    static bool hasAddress(IPAddress address);
    static void logNoDelayFailure();

    void beginInterfaceIfNeeded();
    std::size_t firstReusableSlot() const;
    Slot* lookup(core::ConnectionHandle handle);
    const Slot* lookup(core::ConnectionHandle handle) const;
    void invalidate(Slot& slot);
    bool cachePendingConnection() const;

    std::array<Slot, core::NetworkServer::kConnectionSlotCount> slots_{};
    api::NetworkConfig config_{api::NetworkConfig::Mode::Dhcp, 0, {0, 0, 0, 0},
                               {0, 0, 0, 0}, {0, 0, 0, 0}};
    qindesign::network::EthernetServer server_;
    mutable qindesign::network::EthernetClient pending_client_;
    bool configured_ = false;
    bool interface_begin_attempted_ = false;
    bool interface_started_ = false;
    bool listening_ = false;
};

}  // namespace teensy_command_server::platform::qnethernet
