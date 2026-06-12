#pragma once

#include <cstdint>

namespace teensy_command_server::api {

// Local registration/startup outcome only; not a protocol response status.
enum class StatusCode : std::uint8_t {
    Ok,
    DuplicateRegistration,
    RegistrationSealed,
    CapacityExceeded,
    InvalidName,
    InvalidArgumentSchema,
    InvalidConfiguration,
    NetworkStartFailed,
    NoTelemetryProvider,
    NoEstopHook,
    NoControllerLossHook,
};

struct Status {
    StatusCode code;
    const char* message;

    constexpr bool ok() const {
        return code == StatusCode::Ok;
    }
};

}  // namespace teensy_command_server::api
