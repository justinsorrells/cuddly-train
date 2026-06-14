#pragma once

#include <cstddef>
#include <cstdint>

#include <QNEthernet.h>

#include "core/Transport.h"

namespace teensy_command_server::platform::qnethernet {

class QNEthernetTransport final : public core::Transport {
public:
    QNEthernetTransport() = default;

    void reset(qindesign::network::EthernetClient client);
    void close();
    void abort();

    std::size_t readSome(std::uint8_t* buffer, std::size_t capacity) override;
    std::size_t writeSome(const std::uint8_t* data, std::size_t length) override;
    bool flush() override;
    core::TransportConnectionState connectionState() const override;

private:
    qindesign::network::EthernetClient client_;
};

}  // namespace teensy_command_server::platform::qnethernet
