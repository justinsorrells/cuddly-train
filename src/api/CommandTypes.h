#pragma once

#include <cstddef>
#include <cstdint>

#include "support/Limits.h"

namespace teensy_command_server::api {

enum class ValueType : std::uint8_t {
    Int,
    Float,
    Bool,
    String,
};

// §17: board-emittable command error codes only.
enum class ErrorCode : std::uint8_t {
    MissingField,
    InvalidType,
    UnknownCommand,
    InvalidArgument,
    InternalError,
    EstopActive,
};

struct ArgumentSpec {
    const char* name;
    ValueType type;
};

struct CommandSpec {
    const char* name;
    const ArgumentSpec* args;
    std::size_t arg_count;
    bool blocked_by_estop;
};

struct FieldSpec {
    const char* name;
    ValueType type;
};

constexpr std::size_t kMaxCommandSpecs = support::kMaxRegisteredCommands;
constexpr std::size_t kMaxArgsPerCommand = support::kMaxArgsPerCommand;

}  // namespace teensy_command_server::api
