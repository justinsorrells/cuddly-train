#pragma once

#include "api/CommandArgs.h"
#include "api/CommandTypes.h"
#include "api/ObjectWriter.h"
#include "support/Limits.h"

#include <cstring>

namespace teensy_command_server::api {

class CommandResult {
public:
    static CommandResult ok() {
        return CommandResult(true, ErrorCode::InternalError, "");
    }

    static CommandResult error(ErrorCode code, const char* message) {
        return CommandResult(false, code, message);
    }

    static CommandResult missingField(const char* message) {
        return error(ErrorCode::MissingField, message);
    }

    static CommandResult invalidType(const char* message) {
        return error(ErrorCode::InvalidType, message);
    }

    static CommandResult invalidArgument(const char* message) {
        return error(ErrorCode::InvalidArgument, message);
    }

    static CommandResult internalError(const char* message) {
        return error(ErrorCode::InternalError, message);
    }

    static CommandResult estopActive(const char* message) {
        return error(ErrorCode::EstopActive, message);
    }

    bool isOk() const {
        return ok_;
    }

    ErrorCode errorCode() const {
        return error_code_;
    }

    const char* message() const {
        return message_;
    }

private:
    CommandResult(bool ok, ErrorCode error_code, const char* message)
        : ok_(ok), error_code_(error_code), message_{} {
        copyMessage(message);
    }

    void copyMessage(const char* message) {
        if (message == nullptr) {
            message_[0] = '\0';
            return;
        }
        const std::size_t length = std::strlen(message);
        if (length > support::kMaxErrorMessageBytes) {
            static constexpr const char* kTooLong = "error message exceeds fixed capacity";
            const std::size_t fallback_length = std::strlen(kTooLong);
            std::memcpy(message_, kTooLong, fallback_length);
            message_[fallback_length] = '\0';
            return;
        }
        std::memcpy(message_, message, length);
        message_[length] = '\0';
    }

    bool ok_;
    ErrorCode error_code_;
    char message_[support::kMaxErrorMessageBytes + 1];
};

using CommandHandler = CommandResult (*)(
    const CommandContext& command,
    ObjectWriter& result,
    void* context);

}  // namespace teensy_command_server::api
