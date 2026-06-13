#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "core/Transport.h"

namespace teensy_command_server::host::fakes {

class FakeTransport final : public core::Transport {
public:
    using WriteObserver = void (*)(void* context);

    void setWriteObserver(WriteObserver observer, void* context) {
        write_observer_ = observer;
        write_observer_context_ = context;
    }

    void scriptInboundBytes(const std::string& bytes) {
        inbound_script_.push_back(ScriptStep::bytes(bytes));
    }

    void scriptClose() {
        inbound_script_.push_back(ScriptStep::close());
    }

    void scriptLinkDown() {
        inbound_script_.push_back(ScriptStep::linkDown());
    }

    void scriptWriteAcceptance(std::size_t bytes_to_accept) {
        write_acceptance_script_.push_back(bytes_to_accept);
    }

    void stopAcceptingWrites() {
        peer_accepting_writes_ = false;
    }

    void resetForConnection() {
        inbound_script_.clear();
        write_acceptance_script_.clear();
        written_bytes_.clear();
        flush_count_ = 0;
        progress_count_ = 0;
        peer_accepting_writes_ = true;
        closed_ = false;
        link_down_ = false;
        aborted_ = false;
    }

    void progress() {
        ++progress_count_;
    }

    void close() {
        closed_ = true;
    }

    void abort() {
        aborted_ = true;
        closed_ = true;
        inbound_script_.clear();
        write_acceptance_script_.clear();
    }

    bool wasClosed() const {
        return closed_;
    }

    bool wasAborted() const {
        return aborted_;
    }

    std::size_t progressCount() const {
        return progress_count_;
    }

    std::size_t readSome(std::uint8_t* buffer, std::size_t capacity) override {
        if (buffer == nullptr || capacity == 0 || closed_ || link_down_) {
            return 0;
        }

        advanceToReadableOrTerminal();
        if (inbound_script_.empty() || inbound_script_.front().kind != StepKind::Bytes) {
            return 0;
        }

        ScriptStep& step = inbound_script_.front();
        const std::size_t remaining = step.payload.size() - step.offset;
        const std::size_t count = std::min(capacity, remaining);
        std::copy_n(step.payload.data() + step.offset, count, buffer);
        step.offset += count;
        if (step.offset == step.payload.size()) {
            inbound_script_.pop_front();
            advanceToReadableOrTerminal();
        }
        return count;
    }

    std::size_t writeSome(const std::uint8_t* data, std::size_t length) override {
        if (data == nullptr || length == 0 || !peer_accepting_writes_ ||
            connectionState() == core::TransportConnectionState::Closed ||
            connectionState() == core::TransportConnectionState::LinkDown) {
            return 0;
        }

        if (write_observer_ != nullptr) {
            write_observer_(write_observer_context_);
        }

        std::size_t accepted = length;
        if (!write_acceptance_script_.empty()) {
            accepted = write_acceptance_script_.front();
            write_acceptance_script_.pop_front();
        }
        accepted = std::min(accepted, length);

        written_bytes_.insert(written_bytes_.end(), data, data + accepted);
        return accepted;
    }

    bool flush() override {
        if (connectionState() == core::TransportConnectionState::Closed ||
            connectionState() == core::TransportConnectionState::LinkDown) {
            return false;
        }
        ++flush_count_;
        return true;
    }

    core::TransportConnectionState connectionState() const override {
        if (link_down_) {
            return core::TransportConnectionState::LinkDown;
        }
        if (!inbound_script_.empty() && inbound_script_.front().kind == StepKind::LinkDown) {
            return core::TransportConnectionState::LinkDown;
        }
        if (hasReadableBytes()) {
            return core::TransportConnectionState::BytesAvailable;
        }
        if (closed_) {
            return core::TransportConnectionState::Closed;
        }
        if (!inbound_script_.empty() && inbound_script_.front().kind == StepKind::Close) {
            return core::TransportConnectionState::Closed;
        }
        return core::TransportConnectionState::Open;
    }

    const std::vector<std::uint8_t>& writtenBytes() const {
        return written_bytes_;
    }

    std::string writtenString() const {
        return std::string(written_bytes_.begin(), written_bytes_.end());
    }

    std::size_t flushCount() const {
        return flush_count_;
    }

private:
    enum class StepKind {
        Bytes,
        Close,
        LinkDown,
    };

    struct ScriptStep {
        StepKind kind;
        std::string payload;
        std::size_t offset = 0;

        static ScriptStep bytes(const std::string& value) {
            return ScriptStep{StepKind::Bytes, value, 0};
        }

        static ScriptStep close() {
            return ScriptStep{StepKind::Close, "", 0};
        }

        static ScriptStep linkDown() {
            return ScriptStep{StepKind::LinkDown, "", 0};
        }
    };

    bool hasReadableBytes() const {
        return !inbound_script_.empty() && inbound_script_.front().kind == StepKind::Bytes &&
               inbound_script_.front().offset < inbound_script_.front().payload.size();
    }

    void advanceToReadableOrTerminal() {
        while (!inbound_script_.empty() &&
               (inbound_script_.front().kind != StepKind::Bytes ||
                inbound_script_.front().offset == inbound_script_.front().payload.size())) {
            const StepKind kind = inbound_script_.front().kind;
            inbound_script_.pop_front();
            if (kind == StepKind::Bytes) {
                continue;
            }
            if (kind == StepKind::Close) {
                closed_ = true;
                return;
            }
            if (kind == StepKind::LinkDown) {
                link_down_ = true;
                return;
            }
        }
    }

    std::deque<ScriptStep> inbound_script_;
    std::deque<std::size_t> write_acceptance_script_;
    std::vector<std::uint8_t> written_bytes_;
    WriteObserver write_observer_ = nullptr;
    void* write_observer_context_ = nullptr;
    std::size_t flush_count_ = 0;
    std::size_t progress_count_ = 0;
    bool peer_accepting_writes_ = true;
    bool closed_ = false;
    bool link_down_ = false;
    bool aborted_ = false;
};

}  // namespace teensy_command_server::host::fakes
