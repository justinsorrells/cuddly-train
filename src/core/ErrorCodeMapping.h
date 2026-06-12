#pragma once

#include "api/CommandTypes.h"
#include "core/Protocol.h"

namespace teensy_command_server::core {

constexpr BoardErrorCode toBoardErrorCode(api::ErrorCode value) {
    switch (value) {
        case api::ErrorCode::MissingField:
            return BoardErrorCode::MissingField;
        case api::ErrorCode::InvalidType:
            return BoardErrorCode::InvalidType;
        case api::ErrorCode::UnknownCommand:
            return BoardErrorCode::UnknownCommand;
        case api::ErrorCode::InvalidArgument:
            return BoardErrorCode::InvalidArgument;
        case api::ErrorCode::InternalError:
            return BoardErrorCode::InternalError;
        case api::ErrorCode::EstopActive:
            return BoardErrorCode::EstopActive;
    }
#if defined(__GNUC__) || defined(__clang__)
    __builtin_unreachable();
#endif
}

constexpr api::ErrorCode toApiErrorCode(BoardErrorCode value) {
    switch (value) {
        case BoardErrorCode::MissingField:
            return api::ErrorCode::MissingField;
        case BoardErrorCode::InvalidType:
            return api::ErrorCode::InvalidType;
        case BoardErrorCode::UnknownCommand:
            return api::ErrorCode::UnknownCommand;
        case BoardErrorCode::InvalidArgument:
            return api::ErrorCode::InvalidArgument;
        case BoardErrorCode::InternalError:
            return api::ErrorCode::InternalError;
        case BoardErrorCode::EstopActive:
            return api::ErrorCode::EstopActive;
    }
#if defined(__GNUC__) || defined(__clang__)
    __builtin_unreachable();
#endif
}

}  // namespace teensy_command_server::core
