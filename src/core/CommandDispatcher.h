#pragma once

#include "api/BoardIdentity.h"
#include "api/CommandResult.h"
#include "core/Clock.h"
#include "core/CommandRegistry.h"
#include "core/Counters.h"
#include "core/ErrorCodeMapping.h"
#include "core/InboundParser.h"
#include "core/OutboundScheduler.h"
#include "core/Protocol.h"
#include "core/ServiceLoop.h"
#include "support/Limits.h"
#include "support/BoundedJsonWriter.h"

#ifndef ARDUINOJSON_USE_LONG_LONG
#define ARDUINOJSON_USE_LONG_LONG 1
#endif
#ifndef ARDUINOJSON_ENABLE_STD_STRING
#define ARDUINOJSON_ENABLE_STD_STRING 0
#endif
#include "../../third_party/ArduinoJson/ArduinoJson-v6.21.5.h"

#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace teensy_command_server::core {

class CommandDispatcher {
public:
    CommandDispatcher(Counters& counters,
                      const CommandRegistry& registry,
                      const api::BoardIdentity& identity,
                      const Clock& clock,
                      OutboundScheduler& scheduler)
        : counters_(counters),
          registry_(registry),
          identity_(identity),
          clock_(clock),
          scheduler_(scheduler) {}

    bool dispatch(const ParsedCommand& command, std::uint64_t parse_completed_us) {
        counters_.increment(&Counters::commands_received);

        const CommandRegistry::StoredCommand* registered =
            registry_.findCommand(command.command);
        if (registered == nullptr) {
            counters_.increment(&Counters::unknown_commands);
            return enqueueError(command.seq, command.controller_ts, BoardErrorCode::UnknownCommand,
                                "unknown command");
        }

        BoardErrorCode validation_error = BoardErrorCode::InternalError;
        const char* validation_message = "";
        if (!validateArgs(*registered, command.args, validation_error, validation_message)) {
            if (validation_error == BoardErrorCode::InvalidArgument) {
                counters_.increment(&Counters::invalid_arguments);
            }
            return enqueueError(command.seq, command.controller_ts, validation_error,
                                validation_message);
        }

        api::ObjectWriter result;
        const api::CommandContext context{command.seq, command.args};
        const api::CommandResult handler_result =
            registered->handler(context, result, registered->context);
        if (!handler_result.isOk()) {
            if (handler_result.errorCode() == api::ErrorCode::InvalidArgument) {
                counters_.increment(&Counters::invalid_arguments);
            }
            return enqueueError(command.seq, command.controller_ts,
                                toBoardErrorCode(handler_result.errorCode()),
                                handler_result.message());
        }

        if (objectWriterFailed(result) || !result.close() || objectWriterFailed(result)) {
            return enqueueFallbackInternalError(command.seq, command.controller_ts);
        }

        const std::uint64_t response_ready_us =
            clock_.monotonicMicroseconds();
        const std::uint64_t board_proc_us = response_ready_us - parse_completed_us;
        if (!buildOkLine(command.seq, command.controller_ts, result, board_proc_us)) {
            return enqueueFallbackInternalError(command.seq, command.controller_ts);
        }
        const OutboundEnqueueResult enqueue_result =
            scheduler_.enqueueCritical(OutboundKind::CommandResponse,
                                       {response_line_, std::strlen(response_line_)});
        if (enqueue_result != OutboundEnqueueResult::Queued) {
            return true;
        }
        counters_.increment(&Counters::commands_ok);
        return false;
    }

    bool recoverableCommandError(const RecoverableCommandError& error) {
        return enqueueError(error.seq, error.controller_ts, error.code, defaultMessage(error.code));
    }

private:
    struct ArgsBackingView {
        ArduinoJson::JsonObject args;
        bool active = false;
    };

    static const ArgsBackingView* recoverArgsBacking(const api::CommandArgs& args) {
        const void* backing = nullptr;
        static_assert(sizeof(backing) <= sizeof(args),
                      "CommandArgs backing pointer must remain first field");
        std::memcpy(&backing, &args, sizeof(backing));
        return static_cast<const ArgsBackingView*>(backing);
    }

    static bool objectWriterFailed(const api::ObjectWriter& result) {
        struct ObjectWriterView {
            char buffer[support::kMaxResultPayloadBytes];
            support::BoundedJsonWriter writer;
            bool closed;
        };
        static_assert(sizeof(ObjectWriterView) == sizeof(api::ObjectWriter),
                      "ObjectWriter layout changed");
        const auto* view = reinterpret_cast<const ObjectWriterView*>(&result);
        return view->writer.failed();
    }

    bool validateArgs(const CommandRegistry::StoredCommand& registered,
                      const api::CommandArgs& args,
                      BoardErrorCode& error,
                      const char*& message) const {
        for (std::size_t i = 0; i < registered.arg_count; ++i) {
            const CommandRegistry::StoredArgument& spec = registered.args[i];
            if (!args.has(spec.name)) {
                error = BoardErrorCode::MissingField;
                message = "missing required argument";
                return false;
            }
            if (!argumentTypeMatches(args, spec)) {
                error = BoardErrorCode::InvalidType;
                message = "argument has invalid type";
                return false;
            }
        }

        if (hasExtraArgument(registered, args)) {
            error = BoardErrorCode::InvalidArgument;
            message = "argument is not declared";
            return false;
        }
        return true;
    }

    static bool argumentTypeMatches(const api::CommandArgs& args,
                                    const CommandRegistry::StoredArgument& spec) {
        switch (spec.type) {
            case api::ValueType::Int: {
                std::int32_t value = 0;
                return args.getInt(spec.name, value);
            }
            case api::ValueType::Float: {
                float value = 0.0F;
                return args.getFloat(spec.name, value);
            }
            case api::ValueType::Bool: {
                bool value = false;
                return args.getBool(spec.name, value);
            }
            case api::ValueType::String: {
                const char* value = nullptr;
                std::size_t length = 0;
                return args.getString(spec.name, value, length);
            }
        }
        return false;
    }

    bool hasExtraArgument(const CommandRegistry::StoredCommand& registered,
                          const api::CommandArgs& args) const {
        const ArgsBackingView* backing = recoverArgsBacking(args);
        if (backing == nullptr || !backing->active || backing->args.isNull()) {
            return false;
        }
        for (ArduinoJson::JsonPair pair : backing->args) {
            const char* name = pair.key().c_str();
            if (!declaresArgument(registered, name)) {
                return true;
            }
        }
        return false;
    }

    static bool declaresArgument(const CommandRegistry::StoredCommand& registered,
                                 const char* name) {
        if (name == nullptr) {
            return false;
        }
        for (std::size_t i = 0; i < registered.arg_count; ++i) {
            if (std::strcmp(registered.args[i].name, name) == 0) {
                return true;
            }
        }
        return false;
    }

    bool enqueueError(support::Seq seq,
                      double controller_ts,
                      BoardErrorCode code,
                      const char* message) {
        if (!buildErrorLine(seq, controller_ts, code, message)) {
            return true;
        }
        const OutboundEnqueueResult enqueue_result =
            scheduler_.enqueueCritical(OutboundKind::CommandResponse,
                                       {response_line_, std::strlen(response_line_)});
        if (enqueue_result != OutboundEnqueueResult::Queued) {
            return true;
        }
        counters_.increment(&Counters::commands_error);
        return false;
    }

    bool enqueueFallbackInternalError(support::Seq seq, double controller_ts) {
        return enqueueError(seq, controller_ts, BoardErrorCode::InternalError,
                            "response exceeded fixed capacity");
    }

    bool buildOkLine(support::Seq seq,
                     double controller_ts,
                     const api::ObjectWriter& result,
                     std::uint64_t board_proc_us) {
        resetLine();
        return appendEnvelopePrefix(seq, controller_ts, BoardStatus::Ok) &&
               appendRaw(",\"result\":") &&
               appendResultWithBoardProcUs(result, board_proc_us) &&
               appendRaw(",\"error\":null}\n");
    }

    bool buildErrorLine(support::Seq seq,
                        double controller_ts,
                        BoardErrorCode code,
                        const char* message) {
        resetLine();
        return appendEnvelopePrefix(seq, controller_ts, BoardStatus::Error) &&
               appendRaw(",\"result\":null,\"error\":{\"code\":") &&
               appendEscapedString(toString(code)) &&
               appendRaw(",\"message\":") &&
               appendEscapedString(message == nullptr ? "" : message) &&
               appendRaw("}}\n");
    }

    bool appendEnvelopePrefix(support::Seq seq, double controller_ts, BoardStatus status) {
        return appendRaw("{\"type\":\"response\",\"seq\":") &&
               appendUInt64(seq) &&
               appendRaw(",\"controller_ts\":") &&
               appendDouble(controller_ts) &&
               appendRaw(",\"timestamp\":") &&
               appendUInt64(clock_.monotonicMilliseconds()) &&
               appendRaw(",\"source\":") &&
               appendEscapedString(identity_.board_id == nullptr ? "" : identity_.board_id) &&
               appendRaw(",\"target\":\"controller\",\"status\":") &&
               appendEscapedString(toString(status));
    }

    bool appendResultWithBoardProcUs(const api::ObjectWriter& result,
                                     std::uint64_t board_proc_us) {
        const char* data = result.data();
        const std::size_t size = result.size();
        if (data == nullptr || size < 2 || data[0] != '{' || data[size - 1] != '}') {
            return false;
        }
        if (size == 2) {
            return appendRaw("{\"board_proc_us\":") &&
                   appendUInt64(board_proc_us) &&
                   appendRaw("}");
        }
        return appendBytes(data, size - 1) &&
               appendRaw(",\"board_proc_us\":") &&
               appendUInt64(board_proc_us) &&
               appendRaw("}");
    }

    void resetLine() {
        response_size_ = 0;
        response_line_[0] = '\0';
    }

    bool appendRaw(const char* value) {
        return value != nullptr && appendBytes(value, std::strlen(value));
    }

    bool appendBytes(const char* value, std::size_t length) {
        if (value == nullptr || length >= sizeof(response_line_) ||
            response_size_ > sizeof(response_line_) - 1 - length) {
            return false;
        }
        std::memcpy(response_line_ + response_size_, value, length);
        response_size_ += length;
        response_line_[response_size_] = '\0';
        return true;
    }

    bool appendEscapedString(const char* value) {
        if (!appendRaw("\"")) {
            return false;
        }
        const char* text = value == nullptr ? "" : value;
        for (std::size_t i = 0; text[i] != '\0'; ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            switch (c) {
                case '"':
                    if (!appendRaw("\\\"")) {
                        return false;
                    }
                    break;
                case '\\':
                    if (!appendRaw("\\\\")) {
                        return false;
                    }
                    break;
                case '\b':
                    if (!appendRaw("\\b")) {
                        return false;
                    }
                    break;
                case '\f':
                    if (!appendRaw("\\f")) {
                        return false;
                    }
                    break;
                case '\n':
                    if (!appendRaw("\\n")) {
                        return false;
                    }
                    break;
                case '\r':
                    if (!appendRaw("\\r")) {
                        return false;
                    }
                    break;
                case '\t':
                    if (!appendRaw("\\t")) {
                        return false;
                    }
                    break;
                default:
                    if (c < 0x20) {
                        char escaped[7]{};
                        const int written = std::snprintf(escaped, sizeof(escaped),
                                                          "\\u%04x", c);
                        if (written != 6 || !appendBytes(escaped, 6)) {
                            return false;
                        }
                    } else if (!appendBytes(reinterpret_cast<const char*>(&text[i]), 1)) {
                        return false;
                    }
                    break;
            }
        }
        return appendRaw("\"");
    }

    bool appendUInt64(std::uint64_t value) {
        char literal[support::kMaxUInt64LiteralBytes]{};
        const int written = std::snprintf(literal, sizeof(literal), "%llu",
                                          static_cast<unsigned long long>(value));
        return written > 0 && static_cast<std::size_t>(written) < sizeof(literal) &&
               appendBytes(literal, static_cast<std::size_t>(written));
    }

    bool appendDouble(double value) {
        if (!(value <= DBL_MAX && value >= -DBL_MAX)) {
            return false;
        }
        char literal[support::kMaxDoubleLiteralBytes]{};
        const int written = std::snprintf(literal, sizeof(literal), "%.*g",
                                          DBL_DECIMAL_DIG, value);
        return written > 0 && static_cast<std::size_t>(written) < sizeof(literal) &&
               appendBytes(literal, static_cast<std::size_t>(written));
    }

    static const char* defaultMessage(BoardErrorCode code) {
        switch (code) {
            case BoardErrorCode::MissingField:
                return "missing required field";
            case BoardErrorCode::InvalidType:
                return "field has invalid type";
            case BoardErrorCode::UnknownCommand:
                return "unknown command";
            case BoardErrorCode::InvalidArgument:
                return "invalid argument";
            case BoardErrorCode::InternalError:
                return "internal error";
            case BoardErrorCode::EstopActive:
                return "hardware safety condition is active";
        }
        return "internal error";
    }

    Counters& counters_;
    const CommandRegistry& registry_;
    const api::BoardIdentity identity_;
    const Clock& clock_;
    OutboundScheduler& scheduler_;
    char response_line_[support::kResponseJsonBufferBytes]{};
    std::size_t response_size_ = 0;
};

}  // namespace teensy_command_server::core
