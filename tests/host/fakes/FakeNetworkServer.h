#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

#include "core/NetworkServer.h"
#include "fakes/FakeTransport.h"

namespace teensy_command_server::host::fakes {

class FakeNetworkServer final : public core::NetworkServer {
public:
    void scriptAvailability(core::NetworkAvailability availability) {
        availability_script_.push_back(availability);
    }

    void queueInboundConnection() {
        ++pending_connections_;
    }

    void forceNextGeneration(std::uint8_t slot, std::uint32_t generation) {
        if (slot >= slots_.size() || slots_[slot].active) {
            return;
        }
        slots_[slot].generation = generation;
        slots_[slot].retired = (generation == 0U);
    }

    void progress() override {
        ++progress_count_;
        for (Slot& slot : slots_) {
            if (slot.active) {
                slot.transport.progress();
            }
        }
    }

    core::NetworkAvailability availability() const override {
        return availability_;
    }

    bool begin(std::uint16_t port) override {
        listen_port_ = port;
        listening_ = true;
        return true;
    }

    bool isListening() const override {
        return listening_;
    }

    bool hasPendingConnection() const override {
        return pending_connections_ > 0U && firstReusableSlot() < slots_.size();
    }

    core::ConnectionHandle accept() override {
        if (!hasPendingConnection()) {
            return core::kInvalidConnection;
        }

        const std::size_t slot_index = firstReusableSlot();
        Slot& slot = slots_[slot_index];
        if (slot.generation == 0U) {
            slot.generation = 1U;
        }
        slot.transport.resetForConnection();
        slot.active = true;
        --pending_connections_;
        return core::ConnectionHandle{static_cast<std::uint8_t>(slot_index), slot.generation};
    }

    core::Transport* transport(core::ConnectionHandle handle) override {
        Slot* slot = lookup(handle);
        if (slot == nullptr) {
            return nullptr;
        }
        return &slot->transport;
    }

    FakeTransport* fakeTransport(core::ConnectionHandle handle) {
        Slot* slot = lookup(handle);
        if (slot == nullptr) {
            return nullptr;
        }
        return &slot->transport;
    }

    void close(core::ConnectionHandle handle) override {
        Slot* slot = lookup(handle);
        if (slot == nullptr) {
            return;
        }
        slot->transport.close();
        ++close_count_;
        invalidate(*slot);
    }

    void abort(core::ConnectionHandle handle) override {
        Slot* slot = lookup(handle);
        if (slot == nullptr) {
            return;
        }
        slot->transport.abort();
        ++abort_count_;
        invalidate(*slot);
    }

    void advanceAvailabilityScript() {
        if (availability_script_.empty()) {
            return;
        }
        availability_ = availability_script_.front();
        availability_script_.pop_front();
    }

    std::uint16_t listenPort() const {
        return listen_port_;
    }

    std::size_t progressCount() const {
        return progress_count_;
    }

    std::size_t closeCount() const {
        return close_count_;
    }

    std::size_t abortCount() const {
        return abort_count_;
    }

private:
    struct Slot {
        FakeTransport transport;
        std::uint32_t generation = 0U;
        bool active = false;
        bool retired = false;
    };

    std::size_t firstReusableSlot() const {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            if (!slots_[index].active && !slots_[index].retired) {
                return index;
            }
        }
        return slots_.size();
    }

    Slot* lookup(core::ConnectionHandle handle) {
        if (!core::isValidConnectionHandle(handle) || handle.slot >= slots_.size()) {
            return nullptr;
        }
        Slot& slot = slots_[handle.slot];
        if (!slot.active || slot.retired || slot.generation != handle.generation) {
            return nullptr;
        }
        return &slot;
    }

    void invalidate(Slot& slot) {
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

    std::array<Slot, core::NetworkServer::kConnectionSlotCount> slots_{};
    std::deque<core::NetworkAvailability> availability_script_;
    core::NetworkAvailability availability_ = core::NetworkAvailability::Uninitialized;
    std::size_t pending_connections_ = 0U;
    std::size_t progress_count_ = 0U;
    std::size_t close_count_ = 0U;
    std::size_t abort_count_ = 0U;
    std::uint16_t listen_port_ = 0U;
    bool listening_ = false;
};

}  // namespace teensy_command_server::host::fakes
