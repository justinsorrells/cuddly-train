#pragma once

#include "api/BoardIdentity.h"
#include "api/SafetyHooks.h"
#include "api/ServerStatus.h"
#include "core/CommandRegistry.h"
#include "core/LineFramer.h"
#include "core/OutboundWriter.h"
#include "core/SchemaBuilder.h"
#include "core/SessionState.h"

namespace teensy_command_server::core {

class SessionDriver {
public:
    SessionDriver() : framer_(dummyCounters()) {}

    SessionDriver(NetworkServer& network,
                  Clock& clock,
                  Counters& counters,
                  const CommandRegistry& registry,
                  const api::BoardIdentity& identity,
                  SchemaBuilder& schema_builder,
                  api::SafetyHook controller_loss_hook,
                  void* controller_loss_context,
                  std::uint16_t listen_port)
        : network_(&network),
          clock_(&clock),
          counters_(&counters),
          registry_(&registry),
          identity_(identity),
          schema_builder_(&schema_builder),
          controller_loss_hook_(controller_loss_hook),
          controller_loss_context_(controller_loss_context),
          listen_port_(listen_port),
          framer_ready_(true),
          framer_(counters),
          writer_(network, clock, counters, *this) {}

    virtual ~SessionDriver() = default;

    SessionState state() const {
        return state_;
    }

    void begin() {
        if (state_ != SessionState::BOOT_SAFE) {
            return;
        }
        transitionTo(SessionState::NETWORK_STARTING);
        next_network_init_ms_ = nowMs();
    }

    void poll() {
        if (!configured()) {
            return;
        }

        network_->progress();

        if (state_ == SessionState::NETWORK_STARTING) {
            pumpNetworkStarting();
            return;
        }

        if (state_ == SessionState::LISTENING) {
            pumpListening();
            return;
        }

        if (state_ == SessionState::SESSION_CONNECTED ||
            state_ == SessionState::SESSION_ACTIVE) {
            if (network_->hasPendingConnection()) {
                acceptReplacement();
                return;
            }
            detectActiveLoss();
            if (state_ == SessionState::SESSION_ACTIVE && !teardown_pending_) {
                readAvailable();
            }
        }
    }

    OutboundSendResult sendActiveLine(ConstLineView line, MessageClass message_class) {
        if (!sessionActive() || teardown_pending_) {
            return OutboundSendResult::NoActiveTransport;
        }
        return writer_.sendLine(line, message_class);
    }

    virtual void requestTeardown(SessionTeardownReason reason) {
        if (teardown_pending_) {
            return;
        }
        teardown_pending_ = true;
        pending_teardown_reason_ = reason;
        if (state_ == SessionState::SESSION_CONNECTED || state_ == SessionState::SESSION_ACTIVE) {
            transitionTo(SessionState::SESSION_CLOSING);
        }
    }

    bool teardownPending() const {
        return teardown_pending_;
    }

    TeardownReason pendingTeardownReason() const {
        return pending_teardown_reason_;
    }

    void applyPendingTeardown() {
        if (!teardown_pending_ || !configured()) {
            return;
        }

        const bool had_active = isValidConnectionHandle(active_handle_);
        if (had_active) {
            runControllerLossHook();
        }
        framer_.closeSession();
        writer_.clearTransport();
        if (had_active) {
            network_->abort(active_handle_);
            active_handle_ = kInvalidConnection;
            counters_->increment(&Counters::controller_disconnects);
        }
        teardown_pending_ = false;
        pending_teardown_reason_ = TeardownReason::ExplicitShutdown;
        transitionTo(SessionState::LISTENING);

        if (isValidConnectionHandle(replacement_handle_)) {
            const ConnectionHandle replacement = replacement_handle_;
            replacement_handle_ = kInvalidConnection;
            promoteAcceptedConnection(replacement);
        }
    }

    bool sessionActive() const {
        return state_ == SessionState::SESSION_ACTIVE && isValidConnectionHandle(active_handle_);
    }

    bool nextLine(MutableLineView& out) {
        out = {};
        if (!sessionActive() || teardown_pending_) {
            return false;
        }
        if (!framer_.hasLine()) {
            readAvailable();
        }
        if (!framer_.hasLine()) {
            return false;
        }
        out = framer_.acquireLine();
        return out.valid();
    }

    void releaseLine() {
        if (framer_ready_) {
            framer_.releaseLine();
        }
    }

    void requestShutdown() {
        requestTeardown(TeardownReason::ExplicitShutdown);
    }

    ConnectionHandle activeHandle() const {
        return active_handle_;
    }

    OutboundWriter& outboundWriter() {
        return writer_;
    }

private:
    bool configured() const {
        return network_ != nullptr && clock_ != nullptr && counters_ != nullptr &&
               registry_ != nullptr && schema_builder_ != nullptr &&
               framer_ready_;
    }

    std::uint64_t nowMs() const {
        return clock_ == nullptr ? 0 : clock_->monotonicMilliseconds();
    }

    void transitionTo(SessionState next) {
        if (state_ == next) {
            return;
        }
        if (isValidTransition(state_, next)) {
            state_ = next;
        }
    }

    void pumpNetworkStarting() {
        if (network_->availability() != NetworkAvailability::Ready) {
            return;
        }
        if (network_init_attempted_ &&
            static_cast<std::uint64_t>(nowMs() - next_network_init_ms_) <
                support::kNetworkInitRetryMs) {
            return;
        }
        network_init_attempted_ = true;
        if (!network_->begin(listen_port_)) {
            next_network_init_ms_ = nowMs();
            return;
        }
        transitionTo(SessionState::LISTENING);
    }

    void pumpListening() {
        if (network_->availability() == NetworkAvailability::Ready && !network_->isListening()) {
            (void)network_->begin(listen_port_);
        }
        if (!network_->hasPendingConnection()) {
            return;
        }
        const ConnectionHandle accepted = network_->accept();
        if (!isValidConnectionHandle(accepted) || network_->transport(accepted) == nullptr) {
            counters_->increment(&Counters::sessions_rejected);
            return;
        }
        counters_->increment(&Counters::sessions_accepted);
        promoteAcceptedConnection(accepted);
    }

    void acceptReplacement() {
        const ConnectionHandle accepted = network_->accept();
        if (!isValidConnectionHandle(accepted) || network_->transport(accepted) == nullptr) {
            counters_->increment(&Counters::sessions_rejected);
            return;
        }
        counters_->increment(&Counters::sessions_accepted);
        counters_->increment(&Counters::sessions_superseded);
        replacement_handle_ = accepted;
        requestTeardown(TeardownReason::Superseded);
    }

    void promoteAcceptedConnection(ConnectionHandle handle) {
        Transport* transport = network_->transport(handle);
        if (transport == nullptr) {
            counters_->increment(&Counters::sessions_rejected);
            return;
        }
        active_handle_ = handle;
        writer_.bindTransport(*transport);
        framer_.closeSession();
        transitionTo(SessionState::SESSION_CONNECTED);

        char schema_line[support::kSchemaJsonBufferBytes]{};
        const api::Status schema_status = SchemaBuilder::buildSchemaLine(
            *registry_, identity_, *clock_, schema_line, sizeof(schema_line));
        if (!schema_status.ok()) {
            requestTeardown(TeardownReason::SchemaSendFailure);
            applyPendingTeardown();
            return;
        }

        const std::size_t schema_size = boundedCStringLength(schema_line, sizeof(schema_line));
        if (writer_.sendLine({schema_line, schema_size}, MessageClass::Critical) !=
            OutboundSendResult::Sent) {
            replacePendingTeardownReason(TeardownReason::SchemaSendFailure);
            applyPendingTeardown();
            return;
        }
        counters_->increment(&Counters::schemas_sent);
        transitionTo(SessionState::SESSION_ACTIVE);
    }

    static std::size_t boundedCStringLength(const char* value, std::size_t capacity) {
        std::size_t length = 0;
        while (length < capacity && value[length] != '\0') {
            ++length;
        }
        return length;
    }

    void replacePendingTeardownReason(TeardownReason reason) {
        if (!teardown_pending_) {
            requestTeardown(reason);
            return;
        }
        pending_teardown_reason_ = reason;
    }

    void detectActiveLoss() {
        Transport* transport = network_->transport(active_handle_);
        if (transport == nullptr) {
            requestTeardown(TeardownReason::ConnectionLost);
            return;
        }
        const TransportConnectionState state = transport->connectionState();
        if (state == TransportConnectionState::Closed) {
            requestTeardown(TeardownReason::ConnectionLost);
            return;
        }
        if (state == TransportConnectionState::LinkDown) {
            requestTeardown(TeardownReason::LinkLost);
        }
    }

    void readAvailable() {
        Transport* transport = network_->transport(active_handle_);
        if (transport == nullptr) {
            requestTeardown(TeardownReason::ConnectionLost);
            return;
        }
        std::uint8_t buffer[64]{};
        while (!framer_.hasLine()) {
            const TransportConnectionState state = transport->connectionState();
            if (state == TransportConnectionState::Closed) {
                requestTeardown(TeardownReason::ConnectionLost);
                return;
            }
            if (state == TransportConnectionState::LinkDown) {
                requestTeardown(TeardownReason::LinkLost);
                return;
            }
            if (state != TransportConnectionState::BytesAvailable) {
                return;
            }
            const std::size_t read = transport->readSome(buffer, sizeof(buffer));
            if (read == 0) {
                detectActiveLoss();
                return;
            }
            framer_.append(buffer, read);
        }
    }

    void runControllerLossHook() {
        if (controller_loss_hook_ == nullptr || clock_ == nullptr || counters_ == nullptr) {
            return;
        }
        const std::uint64_t start_us = clock_->monotonicMicroseconds();
        (void)controller_loss_hook_(controller_loss_context_);
        const std::uint64_t elapsed_us =
            static_cast<std::uint64_t>(clock_->monotonicMicroseconds() - start_us);
        if (elapsed_us > static_cast<std::uint64_t>(support::kHookBudgetMs) * 1000ULL) {
            counters_->increment(&Counters::controller_loss_hook_over_budget);
        }
    }

    NetworkServer* network_ = nullptr;
    Clock* clock_ = nullptr;
    Counters* counters_ = nullptr;
    const CommandRegistry* registry_ = nullptr;
    api::BoardIdentity identity_{};
    SchemaBuilder* schema_builder_ = nullptr;
    api::SafetyHook controller_loss_hook_ = nullptr;
    void* controller_loss_context_ = nullptr;
    std::uint16_t listen_port_ = 0;
    SessionState state_ = SessionState::BOOT_SAFE;
    ConnectionHandle active_handle_ = kInvalidConnection;
    ConnectionHandle replacement_handle_ = kInvalidConnection;
    TeardownReason pending_teardown_reason_ = TeardownReason::ExplicitShutdown;
    bool teardown_pending_ = false;
    bool network_init_attempted_ = false;
    bool framer_ready_ = false;
    std::uint64_t next_network_init_ms_ = 0;
    LineFramer framer_;
    OutboundWriter writer_;

    static Counters& dummyCounters() {
        static Counters counters;
        return counters;
    }
};

}  // namespace teensy_command_server::core
