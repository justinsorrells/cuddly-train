#pragma once

#include "api/BoardIdentity.h"
#include "api/CommandResult.h"
#include "api/CommandTypes.h"
#include "api/SafetyHooks.h"
#include "api/ServerStatus.h"
#include "api/TelemetryProvider.h"
#include "core/CommandDispatcher.h"
#include "core/CommandRegistry.h"
#include "core/DiagnosticsCommand.h"
#include "core/EstopHandler.h"
#include "core/HeartbeatHandler.h"
#include "core/NetworkServer.h"
#include "core/OutboundScheduler.h"
#include "core/SchemaBuilder.h"
#include "core/ServiceLoop.h"
#include "core/SessionDriver.h"

#include <new>
#include <type_traits>

namespace teensy_command_server {

class TeensyCommandServer {
public:
    TeensyCommandServer()
        : service_loop_(counters_, identity_, scheduler_, facadeRoutes(this),
                        static_cast<const core::Clock*>(nullptr)) {}

    TeensyCommandServer(core::NetworkServer& network, core::Clock& clock)
        : network_(&network),
          clock_(&clock),
          service_loop_(counters_, identity_, scheduler_, facadeRoutes(this), clock) {}

    ~TeensyCommandServer() {
        destroySessionDriver();
    }

    api::Status setIdentity(const api::BoardIdentity& identity) {
        const api::Status result = registry_.setIdentity(identity);
        if (result.ok()) {
            identity_ = registry_.identity();
        }
        return result;
    }

    api::Status setNetworkConfig(const api::NetworkConfig& config) {
        if (registry_.lifecycle() == core::CommandRegistry::Lifecycle::Sealed) {
            return status(api::StatusCode::RegistrationSealed, "registry is sealed");
        }
        if (config.listen_port == 0) {
            return status(api::StatusCode::InvalidConfiguration, "listen port is required");
        }
        if (config.mode != api::NetworkConfig::Mode::Dhcp &&
            config.mode != api::NetworkConfig::Mode::StaticIpv4) {
            return status(api::StatusCode::InvalidConfiguration, "invalid network mode");
        }
        if (config.mode == api::NetworkConfig::Mode::StaticIpv4 && allZero(config.ip)) {
            return status(api::StatusCode::InvalidConfiguration, "static ip is required");
        }
        network_config_ = config;
        network_config_set_ = true;
        return ok();
    }

    api::Status registerCommand(const api::CommandSpec& spec,
                                api::CommandHandler handler,
                                void* context) {
        return registry_.registerCommand(
            {spec.name, spec.args, spec.arg_count, spec.blocked_by_estop, true, handler, context});
    }

    api::Status registerTelemetrySchema(const api::FieldSpec* fields, std::size_t field_count) {
        if (field_count > 0 && fields == nullptr) {
            return status(api::StatusCode::InvalidArgumentSchema, "telemetry fields required");
        }
        for (std::size_t i = 0; i < field_count; ++i) {
            const api::Status result = registry_.registerTelemetryField(fields[i]);
            if (!result.ok()) {
                return result;
            }
        }
        return ok();
    }

    api::Status registerStateSchema(const api::FieldSpec* fields, std::size_t field_count) {
        if (field_count > 0 && fields == nullptr) {
            return status(api::StatusCode::InvalidArgumentSchema, "state fields required");
        }
        for (std::size_t i = 0; i < field_count; ++i) {
            const api::Status result = registry_.registerStateField(fields[i]);
            if (!result.ok()) {
                return result;
            }
        }
        return ok();
    }

    api::Status setTelemetryProvider(api::TelemetryProvider provider, void* context) {
        return registry_.setTelemetryProvider(provider, context);
    }

    api::Status setEstopHook(api::SafetyHook hook, void* context) {
        return registry_.setEstopHook(hook, context);
    }

    api::Status setControllerLossHook(api::SafetyHook hook, void* context) {
        return registry_.setControllerLossHook(hook, context);
    }

    api::Status enableCountersDiagnosticCommand() {
        const api::Status result = core::DiagnosticsCommand::registerCommand(registry_, counters_);
        if (result.ok()) {
            counters_diagnostic_enabled_ = true;
        }
        return result;
    }

    api::Status requestEstopTriggered(const char* reason) {
        return estop_handler_.requestEstopTriggered(reason, clock_, scheduler_);
    }

    api::Status start() {
        if (!network_config_set_) {
            return status(api::StatusCode::InvalidConfiguration, "network config is required");
        }

        api::Status result = registry_.validateMetadataForSeal();
        if (!result.ok()) {
            return result;
        }
        result = core::SchemaBuilder::validateMaximumSchemaSize(registry_, registry_.identity());
        if (!result.ok()) {
            return result;
        }
        result = registry_.commitSeal();
        if (!result.ok()) {
            return result;
        }

        identity_ = registry_.identity();
        started_ = true;
        if (network_ != nullptr && clock_ != nullptr) {
            const core::CommandRegistry::StoredHook hook = registry_.controllerLossHook();
            destroySessionDriver();
            session_driver_ = new (&session_storage_) core::SessionDriver(*network_,
                                                                          *clock_,
                                                                          counters_,
                                                                          registry_,
                                                                          identity_,
                                                                          schema_builder_,
                                                                          hook.fn,
                                                                          hook.context,
                                                                          network_config_.listen_port);
            session_driver_->begin();
        }
        return ok();
    }

    void service() {
        if (!started_ || session_driver_ == nullptr) {
            return;
        }
        service_loop_.service(*session_driver_);
    }

    bool isStarted() const {
        return started_;
    }

    const core::Counters& counters() const {
        return counters_.snapshot();
    }

private:
    static api::Status ok() {
        return status(api::StatusCode::Ok, "ok");
    }

    static api::Status status(api::StatusCode code, const char* message) {
        return {code, message};
    }

    static bool allZero(const std::uint8_t (&bytes)[4]) {
        return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0;
    }

    void destroySessionDriver() {
        if (session_driver_ != nullptr) {
            session_driver_->~SessionDriver();
            session_driver_ = nullptr;
        }
    }

    static bool routeCommand(const core::ParsedCommand& command,
                             std::uint64_t parse_completed_us,
                             void* context) {
        auto* self = static_cast<TeensyCommandServer*>(context);
        if (self == nullptr || !self->counters_diagnostic_enabled_ ||
            !core::DiagnosticsCommand::isDiagnosticsCommand(command.command) ||
            self->clock_ == nullptr) {
            return true;
        }
        core::CommandDispatcher dispatcher(
            self->counters_, self->registry_, self->identity_, *self->clock_, self->scheduler_);
        return dispatcher.dispatch(command, parse_completed_us);
    }

    static bool routeEstop(const core::ParsedEstop& estop, void* context) {
        auto* self = static_cast<TeensyCommandServer*>(context);
        if (self == nullptr) {
            return false;
        }
        const core::CommandRegistry::StoredHook hook = self->registry_.estopHook();
        return self->estop_handler_.handleEstop(estop, self->counters_, self->identity_,
                                                self->clock_, hook.fn, hook.context,
                                                self->scheduler_);
    }

    static bool routeHeartbeat(const core::ParsedHeartbeat& heartbeat, void* context) {
        auto* self = static_cast<TeensyCommandServer*>(context);
        if (self == nullptr) {
            return false;
        }
        return self->heartbeat_handler_.handleHeartbeat(heartbeat, self->counters_,
                                                        self->identity_, self->scheduler_);
    }

    static bool routeSafetyDue(core::OutboundScheduler& scheduler, void* context) {
        auto* self = static_cast<TeensyCommandServer*>(context);
        if (self == nullptr) {
            return false;
        }
        return self->estop_handler_.service(scheduler, self->identity_);
    }

    static void routeOutboundOutcome(const core::OutboundOutcome& outcome, void* context) {
        auto* self = static_cast<TeensyCommandServer*>(context);
        if (self == nullptr) {
            return;
        }
        self->estop_handler_.onOutboundOutcome(outcome, self->scheduler_);
    }

    static core::ServiceLoop::Routes facadeRoutes(TeensyCommandServer* self) {
        core::ServiceLoop::Routes routes;
        routes.command_with_timing = routeCommand;
        routes.estop_with_result = routeEstop;
        routes.heartbeat_with_result = routeHeartbeat;
        routes.safety_due = routeSafetyDue;
        routes.outbound_outcome = routeOutboundOutcome;
        routes.context = self;
        return routes;
    }

    core::NetworkServer* network_ = nullptr;
    core::Clock* clock_ = nullptr;
    core::Counters counters_;
    core::CommandRegistry registry_;
    core::SchemaBuilder schema_builder_;
    core::OutboundScheduler scheduler_;
    core::EstopHandler estop_handler_;
    core::HeartbeatHandler heartbeat_handler_;
    api::BoardIdentity identity_{nullptr, nullptr, nullptr};
    api::NetworkConfig network_config_{api::NetworkConfig::Mode::Dhcp, 0, {0, 0, 0, 0},
                                       {0, 0, 0, 0}, {0, 0, 0, 0}};
    bool network_config_set_ = false;
    bool started_ = false;
    bool counters_diagnostic_enabled_ = false;
    typename std::aligned_storage<sizeof(core::SessionDriver), alignof(core::SessionDriver)>::type
        session_storage_;
    core::SessionDriver* session_driver_ = nullptr;
    core::ServiceLoop service_loop_;
};

}  // namespace teensy_command_server
