#pragma once

#include "api/CommandArgs.h"
#include "core/LineFramer.h"
#include "core/Protocol.h"
#include "support/Limits.h"

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
#include <cstring>

namespace teensy_command_server::core {

enum class ParseOutcomeKind {
    Valid,
    Malformed,
    StructurallyInvalid,
    UnsupportedType,
};

enum class InboundMessageKind {
    None,
    Command,
    Estop,
    Heartbeat,
};

struct ParsedCommand {
    support::Seq seq = 0;
    double controller_ts = 0.0;
    const char* command = nullptr;
    api::CommandArgs args;
};

struct ParsedEstop {
    const char* source = nullptr;
    const char* target = nullptr;
};

struct ParsedHeartbeat {
    support::Seq seq = 0;
    const char* source = nullptr;
    const char* target = nullptr;
};

struct ParseOutcome {
    ParseOutcomeKind kind = ParseOutcomeKind::Malformed;
    InboundMessageKind message_kind = InboundMessageKind::None;
    ParsedCommand command;
    ParsedEstop estop;
    ParsedHeartbeat heartbeat;
    bool has_seq = false;
    support::Seq seq = 0;
    bool has_controller_ts = false;
    double controller_ts = 0.0;
    bool has_suggested_error = false;
    BoardErrorCode suggested_error = BoardErrorCode::InternalError;
};

class InboundParser {
public:
    explicit InboundParser(Counters& counters) : counters_(counters) {
        invalidate();
    }

    ParseOutcome parse(MutableLineView line) {
        invalidate();
        payload_ = line.data;
        payload_size_ = line.size;

        ParseOutcome outcome;
        if (!line.valid() || !isValidUtf8(line.data, line.size)) {
            counters_.increment(&Counters::invalid_json);
            return outcome;
        }

        ArduinoJson::DeserializationError error =
            ArduinoJson::deserializeJson(document_, line.data,
                                         DeserializationOption::NestingLimit(
                                             support::kMaxJsonNestingDepth));
        if (error || !document_.is<ArduinoJson::JsonObject>()) {
            counters_.increment(&Counters::invalid_json);
            return outcome;
        }

        ArduinoJson::JsonObject root = document_.as<ArduinoJson::JsonObject>();
        const char* type = nullptr;
        if (!getString(root[field::kType], type)) {
            markInvalidWithoutResponse(outcome);
            return outcome;
        }

        if (std::strcmp(type, toString(MessageType::Command)) == 0) {
            return parseCommand(root, outcome);
        }
        if (std::strcmp(type, toString(MessageType::Estop)) == 0) {
            return parseEstop(root, outcome);
        }
        if (std::strcmp(type, toString(MessageType::Heartbeat)) == 0) {
            return parseHeartbeat(root, outcome);
        }

        outcome.kind = ParseOutcomeKind::UnsupportedType;
        counters_.increment(&Counters::invalid_json);
        return outcome;
    }

    void invalidate() {
        document_.clear();
        args_backing_.args = ArduinoJson::JsonObject();
        args_backing_.active = false;
        payload_ = nullptr;
        payload_size_ = 0;
    }

private:
    struct ArgsBacking {
        ArduinoJson::JsonObject args;
        bool active = false;
    };

    static bool isFinite(double value) {
        return value <= DBL_MAX && value >= -DBL_MAX;
    }

    static bool getString(ArduinoJson::JsonVariantConst value, const char*& out) {
        if (!value.is<const char*>()) {
            return false;
        }
        out = value.as<const char*>();
        return out != nullptr;
    }

    static bool getSeq(ArduinoJson::JsonVariantConst value, support::Seq& out) {
        if (!value.is<support::Seq>()) {
            return false;
        }
        out = value.as<support::Seq>();
        return true;
    }

    static bool getFiniteDouble(ArduinoJson::JsonVariantConst value, double& out) {
        if (!value.is<double>()) {
            return false;
        }
        out = value.as<double>();
        return isFinite(out);
    }

    static bool backingLive(const ArgsBacking* backing) {
        return backing != nullptr && backing->active && !backing->args.isNull();
    }

    static bool argsHas(const void* backing, const char* name) {
        const ArgsBacking* typed = static_cast<const ArgsBacking*>(backing);
        return name != nullptr && backingLive(typed) && typed->args.containsKey(name);
    }

    static bool argsGetInt(const void* backing, const char* name, std::int32_t& out) {
        const ArgsBacking* typed = static_cast<const ArgsBacking*>(backing);
        if (name == nullptr || !backingLive(typed)) {
            return false;
        }
        ArduinoJson::JsonVariantConst value = typed->args[name];
        if (!value.is<support::ContractInt>()) {
            return false;
        }
        out = value.as<support::ContractInt>();
        return true;
    }

    static bool argsGetFloat(const void* backing, const char* name, float& out) {
        const ArgsBacking* typed = static_cast<const ArgsBacking*>(backing);
        if (name == nullptr || !backingLive(typed)) {
            return false;
        }
        ArduinoJson::JsonVariantConst value = typed->args[name];
        if (!value.is<double>()) {
            return false;
        }
        const double parsed = value.as<double>();
        if (!isFinite(parsed)) {
            return false;
        }
        out = static_cast<float>(parsed);
        return true;
    }

    static bool argsGetBool(const void* backing, const char* name, bool& out) {
        const ArgsBacking* typed = static_cast<const ArgsBacking*>(backing);
        if (name == nullptr || !backingLive(typed)) {
            return false;
        }
        ArduinoJson::JsonVariantConst value = typed->args[name];
        if (!value.is<bool>()) {
            return false;
        }
        out = value.as<bool>();
        return true;
    }

    static bool argsGetString(const void* backing,
                              const char* name,
                              const char*& out,
                              std::size_t& length) {
        const ArgsBacking* typed = static_cast<const ArgsBacking*>(backing);
        if (name == nullptr || !backingLive(typed)) {
            return false;
        }
        ArduinoJson::JsonVariantConst value = typed->args[name];
        const char* parsed = nullptr;
        if (!getString(value, parsed)) {
            return false;
        }
        out = parsed;
        length = std::strlen(parsed);
        return true;
    }

    void markInvalidWithoutResponse(ParseOutcome& outcome) {
        outcome.kind = ParseOutcomeKind::Malformed;
        counters_.increment(&Counters::invalid_json);
    }

    ParseOutcome structuralError(ParseOutcome& outcome, BoardErrorCode code) {
        outcome.kind = ParseOutcomeKind::StructurallyInvalid;
        outcome.has_suggested_error = true;
        outcome.suggested_error = code;
        return outcome;
    }

    ParseOutcome parseCommand(ArduinoJson::JsonObject root, ParseOutcome& outcome) {
        outcome.message_kind = InboundMessageKind::Command;

        support::Seq seq = 0;
        if (!getSeq(root[field::kSeq], seq)) {
            markInvalidWithoutResponse(outcome);
            return outcome;
        }
        outcome.has_seq = true;
        outcome.seq = seq;

        double controller_ts = 0.0;
        if (!root.containsKey(field::kControllerTs)) {
            return structuralError(outcome, BoardErrorCode::MissingField);
        }
        if (!getFiniteDouble(root[field::kControllerTs], controller_ts)) {
            return structuralError(outcome, BoardErrorCode::InvalidType);
        }
        outcome.has_controller_ts = true;
        outcome.controller_ts = controller_ts;

        if (!root.containsKey(field::kSource) || !root.containsKey(field::kTarget) ||
            !root.containsKey(field::kCommand) || !root.containsKey(field::kArgs)) {
            return structuralError(outcome, BoardErrorCode::MissingField);
        }

        const char* source = nullptr;
        const char* target = nullptr;
        const char* command = nullptr;
        if (!getString(root[field::kSource], source) || !getString(root[field::kTarget], target) ||
            !getString(root[field::kCommand], command) ||
            !root[field::kArgs].is<ArduinoJson::JsonObject>()) {
            return structuralError(outcome, BoardErrorCode::InvalidType);
        }
        (void)source;
        (void)target;

        args_backing_.args = root[field::kArgs].as<ArduinoJson::JsonObject>();
        args_backing_.active = true;
        outcome.kind = ParseOutcomeKind::Valid;
        outcome.command.seq = seq;
        outcome.command.controller_ts = controller_ts;
        outcome.command.command = command;
        outcome.command.args = api::CommandArgs(&args_backing_, argsHas, argsGetInt, argsGetFloat,
                                                argsGetBool, argsGetString);
        return outcome;
    }

    ParseOutcome parseEstop(ArduinoJson::JsonObject root, ParseOutcome& outcome) {
        outcome.message_kind = InboundMessageKind::Estop;
        if (!root.containsKey(field::kSource) || !root.containsKey(field::kTarget)) {
            markInvalidWithoutResponse(outcome);
            return outcome;
        }
        const char* source = nullptr;
        const char* target = nullptr;
        if (!getString(root[field::kSource], source) || !getString(root[field::kTarget], target)) {
            markInvalidWithoutResponse(outcome);
            return outcome;
        }
        outcome.kind = ParseOutcomeKind::Valid;
        outcome.estop.source = source;
        outcome.estop.target = target;
        return outcome;
    }

    ParseOutcome parseHeartbeat(ArduinoJson::JsonObject root, ParseOutcome& outcome) {
        outcome.message_kind = InboundMessageKind::Heartbeat;

        support::Seq seq = 0;
        if (!getSeq(root[field::kSeq], seq)) {
            markInvalidWithoutResponse(outcome);
            return outcome;
        }
        outcome.has_seq = true;
        outcome.seq = seq;

        if (!root.containsKey(field::kSource) || !root.containsKey(field::kTarget)) {
            return structuralError(outcome, BoardErrorCode::MissingField);
        }

        const char* source = nullptr;
        const char* target = nullptr;
        if (!getString(root[field::kSource], source) || !getString(root[field::kTarget], target)) {
            return structuralError(outcome, BoardErrorCode::InvalidType);
        }

        outcome.kind = ParseOutcomeKind::Valid;
        outcome.heartbeat.seq = seq;
        outcome.heartbeat.source = source;
        outcome.heartbeat.target = target;
        return outcome;
    }

    static bool isValidUtf8(const char* data, std::size_t size) {
        if (data == nullptr) {
            return false;
        }

        std::size_t i = 0;
        while (i < size) {
            const std::uint8_t c = static_cast<std::uint8_t>(data[i]);
            if (c <= 0x7F) {
                ++i;
                continue;
            }

            if (c >= 0xC2 && c <= 0xDF) {
                if (i + 1 >= size || !isContinuation(data[i + 1])) {
                    return false;
                }
                i += 2;
                continue;
            }

            if (c == 0xE0) {
                if (i + 2 >= size || !inRange(data[i + 1], 0xA0, 0xBF) ||
                    !isContinuation(data[i + 2])) {
                    return false;
                }
                i += 3;
                continue;
            }

            if ((c >= 0xE1 && c <= 0xEC) || (c >= 0xEE && c <= 0xEF)) {
                if (i + 2 >= size || !isContinuation(data[i + 1]) ||
                    !isContinuation(data[i + 2])) {
                    return false;
                }
                i += 3;
                continue;
            }

            if (c == 0xED) {
                if (i + 2 >= size || !inRange(data[i + 1], 0x80, 0x9F) ||
                    !isContinuation(data[i + 2])) {
                    return false;
                }
                i += 3;
                continue;
            }

            if (c == 0xF0) {
                if (i + 3 >= size || !inRange(data[i + 1], 0x90, 0xBF) ||
                    !isContinuation(data[i + 2]) || !isContinuation(data[i + 3])) {
                    return false;
                }
                i += 4;
                continue;
            }

            if (c >= 0xF1 && c <= 0xF3) {
                if (i + 3 >= size || !isContinuation(data[i + 1]) ||
                    !isContinuation(data[i + 2]) || !isContinuation(data[i + 3])) {
                    return false;
                }
                i += 4;
                continue;
            }

            if (c == 0xF4) {
                if (i + 3 >= size || !inRange(data[i + 1], 0x80, 0x8F) ||
                    !isContinuation(data[i + 2]) || !isContinuation(data[i + 3])) {
                    return false;
                }
                i += 4;
                continue;
            }

            return false;
        }
        return true;
    }

    static bool isContinuation(char byte) {
        return inRange(byte, 0x80, 0xBF);
    }

    static bool inRange(char byte, std::uint8_t low, std::uint8_t high) {
        const std::uint8_t value = static_cast<std::uint8_t>(byte);
        return value >= low && value <= high;
    }

    Counters& counters_;
    ArduinoJson::StaticJsonDocument<support::kCommandJsonDocumentBytes> document_;
    ArgsBacking args_backing_;
    char* payload_ = nullptr;
    std::size_t payload_size_ = 0;
};

}  // namespace teensy_command_server::core
