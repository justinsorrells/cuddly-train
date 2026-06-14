#pragma once

#include "api/BoardIdentity.h"
#include "api/CommandResult.h"
#include "api/CommandTypes.h"
#include "api/SafetyHooks.h"
#include "api/ServerStatus.h"
#include "api/TelemetryProvider.h"
#include "core/CommandRegistry.h"
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
    TeensyCommandServer() : service_loop_(counters_, identity_, scheduler_, phase7Routes()) {}

    TeensyCommandServer(core::NetworkServer& network, core::Clock& clock)
        : network_(&network),
          clock_(&clock),
          service_loop_(counters_, identity_, scheduler_, phase7Routes()) {}

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

    static core::ServiceLoop::Routes phase7Routes() {
        // Phase 7 owns the facade pump and scheduler only. Command response
        // bytes, telemetry production, e-stop ack, and heartbeat ack are wired
        // in later phases; empty routes are the explicit fail-closed stubs.
        return {};
    }

    core::NetworkServer* network_ = nullptr;
    core::Clock* clock_ = nullptr;
    core::Counters counters_;
    core::CommandRegistry registry_;
    core::SchemaBuilder schema_builder_;
    core::OutboundScheduler scheduler_;
    api::BoardIdentity identity_{nullptr, nullptr, nullptr};
    api::NetworkConfig network_config_{api::NetworkConfig::Mode::Dhcp, 0, {0, 0, 0, 0},
                                       {0, 0, 0, 0}, {0, 0, 0, 0}};
    bool network_config_set_ = false;
    bool started_ = false;
    typename std::aligned_storage<sizeof(core::SessionDriver), alignof(core::SessionDriver)>::type
        session_storage_;
    core::SessionDriver* session_driver_ = nullptr;
    core::ServiceLoop service_loop_;
};

}  // namespace teensy_command_server
