#include "api/BoardIdentity.h"
#include "api/CommandTypes.h"
#include "api/ServerStatus.h"
#include "core/ErrorCodeMapping.h"
#include "core/Protocol.h"
#include "support/Limits.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <type_traits>

namespace api = teensy_command_server::api;
namespace core = teensy_command_server::core;
namespace limits = teensy_command_server::support;

constexpr std::array<api::ErrorCode, 6> kApiErrors = {
    api::ErrorCode::MissingField,
    api::ErrorCode::InvalidType,
    api::ErrorCode::UnknownCommand,
    api::ErrorCode::InvalidArgument,
    api::ErrorCode::InternalError,
    api::ErrorCode::EstopActive,
};

constexpr std::array<core::BoardErrorCode, 6> kCoreErrors = {
    core::BoardErrorCode::MissingField,
    core::BoardErrorCode::InvalidType,
    core::BoardErrorCode::UnknownCommand,
    core::BoardErrorCode::InvalidArgument,
    core::BoardErrorCode::InternalError,
    core::BoardErrorCode::EstopActive,
};

constexpr std::array<api::StatusCode, 11> kStatusCodes = {
    api::StatusCode::Ok,
    api::StatusCode::DuplicateRegistration,
    api::StatusCode::RegistrationSealed,
    api::StatusCode::CapacityExceeded,
    api::StatusCode::InvalidName,
    api::StatusCode::InvalidArgumentSchema,
    api::StatusCode::InvalidConfiguration,
    api::StatusCode::NetworkStartFailed,
    api::StatusCode::NoTelemetryProvider,
    api::StatusCode::NoEstopHook,
    api::StatusCode::NoControllerLossHook,
};

constexpr api::ArgumentSpec kCommandArgs[] = {
    {"rpm", api::ValueType::Int},
    {"enabled", api::ValueType::Bool},
};

constexpr api::CommandSpec kCommandSpec{"set_speed", kCommandArgs, 2, true};
constexpr api::FieldSpec kTelemetryField{"temperature_c", api::ValueType::Float};

int main() {
    static_assert(std::is_enum<api::ValueType>::value);
    static_assert(std::is_enum<api::ErrorCode>::value);
    static_assert(std::is_enum<api::StatusCode>::value);
    static_assert(sizeof(api::NetworkConfig::ip) == 4);
    static_assert(sizeof(api::NetworkConfig::gateway) == 4);
    static_assert(sizeof(api::NetworkConfig::subnet) == 4);

    static_assert(api::kMaxCommandSpecs == limits::kMaxRegisteredCommands);
    static_assert(api::kMaxArgsPerCommand == limits::kMaxArgsPerCommand);
    static_assert(api::kMaxCommandSpecs > 0);
    static_assert(api::kMaxArgsPerCommand > 0);

    static_assert(core::toBoardErrorCode(api::ErrorCode::MissingField) ==
                  core::BoardErrorCode::MissingField);
    static_assert(core::toBoardErrorCode(api::ErrorCode::InvalidType) ==
                  core::BoardErrorCode::InvalidType);
    static_assert(core::toBoardErrorCode(api::ErrorCode::UnknownCommand) ==
                  core::BoardErrorCode::UnknownCommand);
    static_assert(core::toBoardErrorCode(api::ErrorCode::InvalidArgument) ==
                  core::BoardErrorCode::InvalidArgument);
    static_assert(core::toBoardErrorCode(api::ErrorCode::InternalError) ==
                  core::BoardErrorCode::InternalError);
    static_assert(core::toBoardErrorCode(api::ErrorCode::EstopActive) ==
                  core::BoardErrorCode::EstopActive);

    static_assert(core::toApiErrorCode(core::BoardErrorCode::MissingField) ==
                  api::ErrorCode::MissingField);
    static_assert(core::toApiErrorCode(core::BoardErrorCode::InvalidType) ==
                  api::ErrorCode::InvalidType);
    static_assert(core::toApiErrorCode(core::BoardErrorCode::UnknownCommand) ==
                  api::ErrorCode::UnknownCommand);
    static_assert(core::toApiErrorCode(core::BoardErrorCode::InvalidArgument) ==
                  api::ErrorCode::InvalidArgument);
    static_assert(core::toApiErrorCode(core::BoardErrorCode::InternalError) ==
                  api::ErrorCode::InternalError);
    static_assert(core::toApiErrorCode(core::BoardErrorCode::EstopActive) ==
                  api::ErrorCode::EstopActive);

    for (std::size_t i = 0; i < kApiErrors.size(); ++i) {
        assert(core::toBoardErrorCode(kApiErrors[i]) == kCoreErrors[i]);
        assert(core::toApiErrorCode(kCoreErrors[i]) == kApiErrors[i]);
        assert(core::toApiErrorCode(core::toBoardErrorCode(kApiErrors[i])) == kApiErrors[i]);
    }

    for (api::StatusCode code : kStatusCodes) {
        const api::Status status{code, ""};
        assert(status.ok() == (code == api::StatusCode::Ok));
    }

    const api::BoardIdentity identity{"motor_controller", "1", "0.1.0"};
    const api::NetworkConfig network{
        api::NetworkConfig::Mode::StaticIpv4,
        4242,
        {192, 168, 1, 42},
        {192, 168, 1, 1},
        {255, 255, 255, 0},
    };
    const api::ServerConfig server{identity, network};
    assert(server.identity.board_id == identity.board_id);
    assert(server.network.listen_port == 4242);
    assert(server.network.ip[3] == 42);

    static_assert(kCommandSpec.arg_count == 2);
    static_assert(kCommandSpec.blocked_by_estop);
    static_assert(kTelemetryField.type == api::ValueType::Float);

    std::puts("test_api_types: ok");
    return 0;
}
