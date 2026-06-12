#include "core/Protocol.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace protocol = teensy_command_server::core;

template <typename Enum, std::size_t N>
void assertUniqueMappings(const std::array<Enum, N>& values) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        const char* mapped = protocol::toString(values[i]);
        assert(mapped != nullptr);
        assert(std::strlen(mapped) > 0);
        for (std::size_t j = i + 1; j < values.size(); ++j) {
            assert(std::strcmp(mapped, protocol::toString(values[j])) != 0);
        }
    }
}

bool contains(const char* candidate, const std::array<const char*, 24>& values) {
    for (const char* value : values) {
        if (std::strcmp(candidate, value) == 0) {
            return true;
        }
    }
    return false;
}

template <typename Enum, std::size_t N>
bool containsMapped(const char* candidate, const std::array<Enum, N>& values) {
    for (Enum value : values) {
        if (std::strcmp(candidate, protocol::toString(value)) == 0) {
            return true;
        }
    }
    return false;
}

int main() {
    static_assert(std::is_enum<protocol::MessageType>::value);
    static_assert(std::is_enum<protocol::BoardStatus>::value);
    static_assert(std::is_enum<protocol::BoardErrorCode>::value);
    static_assert(std::is_enum<protocol::EventName>::value);
    static_assert(std::is_enum<protocol::ArgumentType>::value);

    static_assert(protocol::kMessageTypes.size() == 7);
    assert(std::strcmp(protocol::toString(protocol::MessageType::Schema), "schema") == 0);
    assert(std::strcmp(protocol::toString(protocol::MessageType::Command), "command") == 0);
    assert(std::strcmp(protocol::toString(protocol::MessageType::Response), "response") == 0);
    assert(std::strcmp(protocol::toString(protocol::MessageType::Telemetry), "telemetry") == 0);
    assert(std::strcmp(protocol::toString(protocol::MessageType::Event), "event") == 0);
    assert(std::strcmp(protocol::toString(protocol::MessageType::Estop), "estop") == 0);
    assert(std::strcmp(protocol::toString(protocol::MessageType::Heartbeat), "heartbeat") == 0);
    assertUniqueMappings(protocol::kMessageTypes);

    static_assert(protocol::kBoardStatuses.size() == 2);
    assert(std::strcmp(protocol::toString(protocol::BoardStatus::Ok), "ok") == 0);
    assert(std::strcmp(protocol::toString(protocol::BoardStatus::Error), "error") == 0);
    assert(!containsMapped("timeout", protocol::kBoardStatuses));
    assertUniqueMappings(protocol::kBoardStatuses);

    static_assert(protocol::kBoardErrorCodes.size() == 6);
    assert(std::strcmp(protocol::toString(protocol::BoardErrorCode::MissingField), "MISSING_FIELD") == 0);
    assert(std::strcmp(protocol::toString(protocol::BoardErrorCode::InvalidType), "INVALID_TYPE") == 0);
    assert(std::strcmp(protocol::toString(protocol::BoardErrorCode::UnknownCommand), "UNKNOWN_COMMAND") == 0);
    assert(std::strcmp(protocol::toString(protocol::BoardErrorCode::InvalidArgument), "INVALID_ARGUMENT") == 0);
    assert(std::strcmp(protocol::toString(protocol::BoardErrorCode::InternalError), "INTERNAL_ERROR") == 0);
    assert(std::strcmp(protocol::toString(protocol::BoardErrorCode::EstopActive), "ESTOP_ACTIVE") == 0);
    assert(!containsMapped("BOARD_UNAVAILABLE", protocol::kBoardErrorCodes));
    assert(!containsMapped("BOARD_BUSY", protocol::kBoardErrorCodes));
    assert(!containsMapped("COMMAND_TIMEOUT", protocol::kBoardErrorCodes));
    assert(!containsMapped("CONTROLLER_SHUTDOWN", protocol::kBoardErrorCodes));
    assert(!containsMapped("UNKNOWN_TARGET", protocol::kBoardErrorCodes));
    assert(!containsMapped("PROTOCOL_VERSION_MISMATCH", protocol::kBoardErrorCodes));
    assertUniqueMappings(protocol::kBoardErrorCodes);

    static_assert(protocol::kEventNames.size() == 2);
    assert(std::strcmp(protocol::toString(protocol::EventName::EstopAck), "estop_ack") == 0);
    assert(std::strcmp(protocol::toString(protocol::EventName::EstopTriggered), "estop_triggered") == 0);
    assert(std::strcmp(protocol::kEstopAckSafeState, "safe") == 0);
    assertUniqueMappings(protocol::kEventNames);

    static_assert(protocol::kArgumentTypes.size() == 4);
    assert(std::strcmp(protocol::toString(protocol::ArgumentType::Int), "int") == 0);
    assert(std::strcmp(protocol::toString(protocol::ArgumentType::Float), "float") == 0);
    assert(std::strcmp(protocol::toString(protocol::ArgumentType::Bool), "bool") == 0);
    assert(std::strcmp(protocol::toString(protocol::ArgumentType::String), "string") == 0);
    assertUniqueMappings(protocol::kArgumentTypes);

    assert(std::strcmp(protocol::kProtocolVersion, "1") == 0);

    static_assert(protocol::field::kAll.size() == 24);
    assert(contains("type", protocol::field::kAll));
    assert(contains("seq", protocol::field::kAll));
    assert(contains("timestamp", protocol::field::kAll));
    assert(contains("controller_ts", protocol::field::kAll));
    assert(contains("source", protocol::field::kAll));
    assert(contains("target", protocol::field::kAll));
    assert(contains("command", protocol::field::kAll));
    assert(contains("args", protocol::field::kAll));
    assert(contains("status", protocol::field::kAll));
    assert(contains("result", protocol::field::kAll));
    assert(contains("error", protocol::field::kAll));
    assert(contains("code", protocol::field::kAll));
    assert(contains("message", protocol::field::kAll));
    assert(contains("telemetry", protocol::field::kAll));
    assert(contains("schema", protocol::field::kAll));
    assert(contains("event", protocol::field::kAll));
    assert(contains("details", protocol::field::kAll));
    assert(contains("reason", protocol::field::kAll));
    assert(contains("protocol_version", protocol::field::kAll));
    assert(contains("firmware_version", protocol::field::kAll));
    assert(contains("blocked_by_estop", protocol::field::kAll));
    assert(contains("board_proc_us", protocol::field::kAll));
    assert(contains("commands", protocol::field::kAll));
    assert(contains("state", protocol::field::kAll));

    for (std::size_t i = 0; i < protocol::field::kAll.size(); ++i) {
        assert(protocol::field::kAll[i] != nullptr);
        assert(std::strlen(protocol::field::kAll[i]) > 0);
        for (std::size_t j = i + 1; j < protocol::field::kAll.size(); ++j) {
            assert(std::strcmp(protocol::field::kAll[i], protocol::field::kAll[j]) != 0);
        }
    }

    std::puts("test_protocol_vocab: ok");
    return 0;
}
