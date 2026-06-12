#pragma once

#include "api/BoardIdentity.h"
#include "api/CommandTypes.h"
#include "api/ServerStatus.h"
#include "core/Clock.h"
#include "core/CommandRegistry.h"
#include "core/Protocol.h"
#include "support/BoundedJsonWriter.h"
#include "support/Limits.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace teensy_command_server::core {

class SchemaBuilder {
public:
    static api::Status buildSchemaLine(const CommandRegistry& registry,
                                       const api::BoardIdentity& identity,
                                       const Clock& clock,
                                       char* buffer,
                                       std::size_t capacity) {
        return buildSchemaLineWithTimestamp(registry, identity, clock.monotonicMilliseconds(),
                                            buffer, capacity);
    }

    static api::Status validateMaximumSchemaSize(const CommandRegistry& registry,
                                                 const api::BoardIdentity& identity) {
        char buffer[support::kSchemaJsonBufferBytes]{};
        return buildSchemaLineWithTimestamp(registry, identity,
                                            std::numeric_limits<std::uint64_t>::max(), buffer,
                                            sizeof(buffer));
    }

private:
    static api::Status buildSchemaLineWithTimestamp(const CommandRegistry& registry,
                                                    const api::BoardIdentity& identity,
                                                    std::uint64_t timestamp,
                                                    char* buffer,
                                                    std::size_t capacity) {
        if (buffer == nullptr) {
            return status(api::StatusCode::InvalidConfiguration, "schema buffer is required");
        }

        support::BoundedJsonWriter writer(buffer, capacity);
        writer.reserveTailBytes(1);

        if (!writer.beginObject() ||
            !writer.addString(field::kType, toString(MessageType::Schema)) ||
            !writer.addUInt64(field::kSeq, 1) ||
            !writer.addUInt64(field::kTimestamp, timestamp) ||
            !writer.addString(field::kSource, identity.board_id) ||
            !writer.addString(field::kTarget, "controller") ||
            !writer.addString(field::kProtocolVersion, identity.protocol_version) ||
            !writeSchemaObject(writer, registry, identity) ||
            !writer.endObject()) {
            return status(api::StatusCode::CapacityExceeded, "schema exceeds transmit limit");
        }

        writer.releaseTailReserve();
        if (!writer.appendNewline() || writer.size() > support::kBoardTxMaxLineBytes) {
            return status(api::StatusCode::CapacityExceeded, "schema exceeds transmit limit");
        }
        return ok();
    }

    static bool writeSchemaObject(support::BoundedJsonWriter& writer,
                                  const CommandRegistry& registry,
                                  const api::BoardIdentity& identity) {
        if (!writer.beginObjectField(field::kSchema) ||
            !writeCommands(writer, registry) ||
            !writeFields(writer, field::kTelemetry, registry.telemetryFieldCount(),
                         FieldSelector::Telemetry, registry) ||
            !writeFields(writer, field::kState, registry.stateFieldCount(), FieldSelector::State,
                         registry) ||
            !writer.addString(field::kFirmwareVersion, identity.firmware_version) ||
            !writer.endObject()) {
            return false;
        }
        return true;
    }

    static bool writeCommands(support::BoundedJsonWriter& writer,
                              const CommandRegistry& registry) {
        if (!writer.beginObjectField(field::kCommands)) {
            return false;
        }
        for (std::size_t i = 0; i < registry.commandCount(); ++i) {
            const CommandRegistry::StoredCommand& command = registry.commandAt(i);
            if (!writer.beginObjectField(command.name) ||
                !writer.beginObjectField(field::kArgs)) {
                return false;
            }
            for (std::size_t arg = 0; arg < command.arg_count; ++arg) {
                if (!writer.addString(command.args[arg].name,
                                      valueTypeString(command.args[arg].type))) {
                    return false;
                }
            }
            if (!writer.endObject() ||
                !writer.addBool(field::kBlockedByEstop, command.blocked_by_estop) ||
                !writer.endObject()) {
                return false;
            }
        }
        return writer.endObject();
    }

    enum class FieldSelector : std::uint8_t {
        Telemetry,
        State,
    };

    static bool writeFields(support::BoundedJsonWriter& writer,
                            const char* object_name,
                            std::size_t count,
                            FieldSelector selector,
                            const CommandRegistry& registry) {
        if (!writer.beginObjectField(object_name)) {
            return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const CommandRegistry::StoredField& field =
                selector == FieldSelector::Telemetry ? registry.telemetryFieldAt(i)
                                                     : registry.stateFieldAt(i);
            if (!writer.addString(field.name, valueTypeString(field.type))) {
                return false;
            }
        }
        return writer.endObject();
    }

    static const char* valueTypeString(api::ValueType type) {
        switch (type) {
            case api::ValueType::Int:
                return toString(ArgumentType::Int);
            case api::ValueType::Float:
                return toString(ArgumentType::Float);
            case api::ValueType::Bool:
                return toString(ArgumentType::Bool);
            case api::ValueType::String:
                return toString(ArgumentType::String);
        }
        return "";
    }

    static api::Status ok() {
        return status(api::StatusCode::Ok, "ok");
    }

    static api::Status status(api::StatusCode code, const char* message) {
        return {code, message};
    }
};

}  // namespace teensy_command_server::core
