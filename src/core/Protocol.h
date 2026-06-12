#pragma once

#include <array>
#include <cstddef>

namespace teensy_command_server::core {

// §3.2: coordinated board/controller protocol version for this contract.
constexpr const char* kProtocolVersion = "1";

enum class MessageType {
    Schema,     // §14: board schema, first application message on session.
    Command,    // §15: controller-owned board_seq command.
    Response,   // §16: board command response.
    Telemetry,  // §18: one-way pushed liveness telemetry.
    Event,      // §19.2, §20: board-originated protocol event.
    Estop,      // §19: controller-to-board software e-stop.
    Heartbeat,  // §22: controller receive-path insurance acknowledgement.
};

constexpr std::array<MessageType, 7> kMessageTypes = {
    MessageType::Schema,
    MessageType::Command,
    MessageType::Response,
    MessageType::Telemetry,
    MessageType::Event,
    MessageType::Estop,
    MessageType::Heartbeat,
};

constexpr const char* toString(MessageType value) {
    switch (value) {
        case MessageType::Schema:
            return "schema";
        case MessageType::Command:
            return "command";
        case MessageType::Response:
            return "response";
        case MessageType::Telemetry:
            return "telemetry";
        case MessageType::Event:
            return "event";
        case MessageType::Estop:
            return "estop";
        case MessageType::Heartbeat:
            return "heartbeat";
    }
    return "";
}

enum class BoardStatus {
    Ok,     // §16.4: board-generated terminal success.
    Error,  // §16.4: board-generated terminal failure.
};

constexpr std::array<BoardStatus, 2> kBoardStatuses = {
    BoardStatus::Ok,
    BoardStatus::Error,
};

constexpr const char* toString(BoardStatus value) {
    switch (value) {
        case BoardStatus::Ok:
            return "ok";
        case BoardStatus::Error:
            return "error";
    }
    return "";
}

enum class BoardErrorCode {
    MissingField,     // §6.3, §17: required argument or field missing.
    InvalidType,      // §6.3, §17: JSON value has the wrong V1 type.
    UnknownCommand,   // §15, §17: command is not registered.
    InvalidArgument,  // §6.3, §17: argument value is outside board constraints.
    InternalError,    // §17, §26.6: compact response for internal dispatch failure.
    EstopActive,      // §6.5, §17: condition-based only, never a board-side latch.
};

constexpr std::array<BoardErrorCode, 6> kBoardErrorCodes = {
    BoardErrorCode::MissingField,
    BoardErrorCode::InvalidType,
    BoardErrorCode::UnknownCommand,
    BoardErrorCode::InvalidArgument,
    BoardErrorCode::InternalError,
    BoardErrorCode::EstopActive,
};

constexpr const char* toString(BoardErrorCode value) {
    switch (value) {
        case BoardErrorCode::MissingField:
            return "MISSING_FIELD";
        case BoardErrorCode::InvalidType:
            return "INVALID_TYPE";
        case BoardErrorCode::UnknownCommand:
            return "UNKNOWN_COMMAND";
        case BoardErrorCode::InvalidArgument:
            return "INVALID_ARGUMENT";
        case BoardErrorCode::InternalError:
            return "INTERNAL_ERROR";
        case BoardErrorCode::EstopActive:
            return "ESTOP_ACTIVE";
    }
    return "";
}

enum class EventName {
    EstopAck,        // §19.2: safe-state acknowledgement event.
    EstopTriggered,  // §20: board-local hardware e-stop condition event.
};

constexpr std::array<EventName, 2> kEventNames = {
    EventName::EstopAck,
    EventName::EstopTriggered,
};

constexpr const char* toString(EventName value) {
    switch (value) {
        case EventName::EstopAck:
            return "estop_ack";
        case EventName::EstopTriggered:
            return "estop_triggered";
    }
    return "";
}

// §19.2: the only valid estop_ack details.state value.
constexpr const char* kEstopAckSafeState = "safe";

enum class ArgumentType {
    Int,     // §6.2: signed 32-bit integer.
    Float,   // §6.2: finite JSON number representable by the board.
    Bool,    // §6.2: JSON Boolean.
    String,  // §6.2: JSON string within bounded line/parser capacity.
};

constexpr std::array<ArgumentType, 4> kArgumentTypes = {
    ArgumentType::Int,
    ArgumentType::Float,
    ArgumentType::Bool,
    ArgumentType::String,
};

constexpr const char* toString(ArgumentType value) {
    switch (value) {
        case ArgumentType::Int:
            return "int";
        case ArgumentType::Float:
            return "float";
        case ArgumentType::Bool:
            return "bool";
        case ArgumentType::String:
            return "string";
    }
    return "";
}

namespace field {

constexpr const char* kType = "type";                            // §14-§23
constexpr const char* kSeq = "seq";                              // §14-§18, §22
constexpr const char* kTimestamp = "timestamp";                  // §14, §16, §18-§20
constexpr const char* kControllerTs = "controller_ts";           // §15-§16.2
constexpr const char* kSource = "source";                        // §14-§23
constexpr const char* kTarget = "target";                        // §14-§23
constexpr const char* kCommand = "command";                      // §15
constexpr const char* kArgs = "args";                            // §14-§15
constexpr const char* kStatus = "status";                        // §16
constexpr const char* kResult = "result";                        // §16
constexpr const char* kError = "error";                          // §16
constexpr const char* kCode = "code";                            // §16-§17
constexpr const char* kMessage = "message";                      // §16-§17
constexpr const char* kTelemetry = "telemetry";                  // §14, §18.3
constexpr const char* kSchema = "schema";                        // §14
constexpr const char* kEvent = "event";                          // §19.2, §20
constexpr const char* kDetails = "details";                      // §19.2, §20
constexpr const char* kReason = "reason";                        // §20
constexpr const char* kProtocolVersion = "protocol_version";     // §14
constexpr const char* kFirmwareVersion = "firmware_version";     // §14
constexpr const char* kBlockedByEstop = "blocked_by_estop";      // §6.4, §14
constexpr const char* kBoardProcUs = "board_proc_us";            // §16.3
constexpr const char* kCommands = "commands";                    // §14
constexpr const char* kState = "state";                          // §14

constexpr std::array<const char*, 24> kAll = {
    kType,
    kSeq,
    kTimestamp,
    kControllerTs,
    kSource,
    kTarget,
    kCommand,
    kArgs,
    kStatus,
    kResult,
    kError,
    kCode,
    kMessage,
    kTelemetry,
    kSchema,
    kEvent,
    kDetails,
    kReason,
    kProtocolVersion,
    kFirmwareVersion,
    kBlockedByEstop,
    kBoardProcUs,
    kCommands,
    kState,
};

}  // namespace field

}  // namespace teensy_command_server::core
