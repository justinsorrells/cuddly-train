#pragma once

#include "api/BoardIdentity.h"
#include "api/CommandResult.h"
#include "api/CommandTypes.h"
#include "api/SafetyHooks.h"
#include "api/ServerStatus.h"
#include "api/TelemetryProvider.h"
#include "support/Limits.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace teensy_command_server::core {

class CommandRegistry {
public:
    enum class Lifecycle : std::uint8_t {
        Mutable,
        Sealed,
    };

    struct CommandRegistration {
        const char* name;
        const api::ArgumentSpec* args;
        std::size_t arg_count;
        bool blocked_by_estop;
        bool blocked_by_estop_specified;
        api::CommandHandler handler;
        void* context;
    };

    struct StoredArgument {
        char name[support::kMaxArgNameBytes + 1];
        api::ValueType type;
    };

    struct StoredCommand {
        char name[support::kMaxCommandNameBytes + 1];
        StoredArgument args[support::kMaxArgsPerCommand];
        std::size_t arg_count;
        bool blocked_by_estop;
        api::CommandHandler handler;
        void* context;
    };

    struct StoredField {
        char name[support::kMaxFieldNameBytes + 1];
        api::ValueType type;
    };

    struct StoredProvider {
        api::TelemetryProvider fn;
        void* context;
    };

    struct StoredHook {
        api::SafetyHook fn;
        void* context;
    };

    CommandRegistry()
        : lifecycle_(Lifecycle::Mutable),
          identity_set_(false),
          command_count_(0),
          telemetry_field_count_(0),
          state_field_count_(0),
          telemetry_provider_{nullptr, nullptr},
          estop_hook_{nullptr, nullptr},
          controller_loss_hook_{nullptr, nullptr} {
        identity_.board_id[0] = '\0';
        identity_.protocol_version[0] = '\0';
        identity_.firmware_version[0] = '\0';
    }

    api::Status setIdentity(const api::BoardIdentity& identity) {
        if (lifecycle_ == Lifecycle::Sealed) {
            return status(api::StatusCode::RegistrationSealed, "registry is sealed");
        }
        const CopyResult board_id = copyRequired(identity.board_id, identity_.board_id,
                                                 support::kMaxBoardIdBytes);
        if (board_id != CopyResult::Ok) {
            return copyStatus(board_id, api::StatusCode::InvalidName, "invalid board id");
        }
        const CopyResult protocol_version =
            copyRequired(identity.protocol_version, identity_.protocol_version,
                         support::kMaxProtocolVersionBytes);
        if (protocol_version != CopyResult::Ok) {
            return copyStatus(protocol_version, api::StatusCode::InvalidConfiguration,
                              "invalid protocol version");
        }
        const CopyResult firmware_version =
            copyRequired(identity.firmware_version, identity_.firmware_version,
                         support::kMaxFirmwareVersionBytes);
        if (firmware_version != CopyResult::Ok) {
            return copyStatus(firmware_version, api::StatusCode::InvalidConfiguration,
                              "invalid firmware version");
        }
        identity_set_ = true;
        return ok();
    }

    api::Status registerCommand(const CommandRegistration& registration) {
        if (lifecycle_ == Lifecycle::Sealed) {
            return status(api::StatusCode::RegistrationSealed, "registry is sealed");
        }
        if (!registration.blocked_by_estop_specified) {
            return status(api::StatusCode::InvalidArgumentSchema,
                          "blocked_by_estop must be explicit");
        }
        if (registration.handler == nullptr) {
            return status(api::StatusCode::InvalidConfiguration, "command handler is required");
        }
        if (registration.arg_count > support::kMaxArgsPerCommand) {
            return status(api::StatusCode::CapacityExceeded, "too many command arguments");
        }
        if (registration.arg_count > 0 && registration.args == nullptr) {
            return status(api::StatusCode::InvalidArgumentSchema, "argument specs are required");
        }
        if (command_count_ >= support::kMaxRegisteredCommands) {
            return status(api::StatusCode::CapacityExceeded, "too many commands");
        }
        if (!validRequiredString(registration.name, support::kMaxCommandNameBytes)) {
            return status(stringFailureCode(registration.name, support::kMaxCommandNameBytes,
                                            api::StatusCode::InvalidName),
                          "invalid command name");
        }
        if (findCommand(registration.name) != nullptr) {
            return status(api::StatusCode::DuplicateRegistration, "duplicate command");
        }

        StoredArgument pending_args[support::kMaxArgsPerCommand]{};
        for (std::size_t i = 0; i < registration.arg_count; ++i) {
            if (!validValueType(registration.args[i].type)) {
                return status(api::StatusCode::InvalidArgumentSchema, "invalid argument type");
            }
            const CopyResult copied =
                copyRequired(registration.args[i].name, pending_args[i].name,
                             support::kMaxArgNameBytes);
            if (copied != CopyResult::Ok) {
                return copyStatus(copied, api::StatusCode::InvalidArgumentSchema,
                                  "invalid argument name");
            }
            if (argumentNameExists(pending_args, i, pending_args[i].name)) {
                return status(api::StatusCode::DuplicateRegistration, "duplicate argument");
            }
            pending_args[i].type = registration.args[i].type;
        }

        StoredCommand& command = commands_[command_count_];
        copyRequired(registration.name, command.name, support::kMaxCommandNameBytes);
        for (std::size_t i = 0; i < registration.arg_count; ++i) {
            command.args[i] = pending_args[i];
        }
        command.arg_count = registration.arg_count;
        command.blocked_by_estop = registration.blocked_by_estop;
        command.handler = registration.handler;
        command.context = registration.context;
        ++command_count_;
        return ok();
    }

    api::Status registerTelemetryField(const api::FieldSpec& field) {
        return registerField(field, telemetry_fields_, telemetry_field_count_,
                             support::kMaxTelemetryFields);
    }

    api::Status registerStateField(const api::FieldSpec& field) {
        return registerField(field, state_fields_, state_field_count_, support::kMaxStateFields);
    }

    api::Status setTelemetryProvider(api::TelemetryProvider provider, void* context) {
        if (lifecycle_ == Lifecycle::Sealed) {
            return status(api::StatusCode::RegistrationSealed, "registry is sealed");
        }
        if (provider == nullptr) {
            return status(api::StatusCode::NoTelemetryProvider, "telemetry provider is required");
        }
        telemetry_provider_ = {provider, context};
        return ok();
    }

    api::Status setEstopHook(api::SafetyHook hook, void* context) {
        if (lifecycle_ == Lifecycle::Sealed) {
            return status(api::StatusCode::RegistrationSealed, "registry is sealed");
        }
        if (hook == nullptr) {
            return status(api::StatusCode::NoEstopHook, "e-stop hook is required");
        }
        estop_hook_ = {hook, context};
        return ok();
    }

    api::Status setControllerLossHook(api::SafetyHook hook, void* context) {
        if (lifecycle_ == Lifecycle::Sealed) {
            return status(api::StatusCode::RegistrationSealed, "registry is sealed");
        }
        if (hook == nullptr) {
            return status(api::StatusCode::NoControllerLossHook,
                          "controller-loss hook is required");
        }
        controller_loss_hook_ = {hook, context};
        return ok();
    }

    api::Status validateMetadataForSeal() const {
        if (!identity_set_) {
            return status(api::StatusCode::InvalidConfiguration, "identity is required");
        }
        if (telemetry_provider_.fn == nullptr) {
            return status(api::StatusCode::NoTelemetryProvider, "telemetry provider is required");
        }
        if (estop_hook_.fn == nullptr) {
            return status(api::StatusCode::NoEstopHook, "e-stop hook is required");
        }
        if (controller_loss_hook_.fn == nullptr) {
            return status(api::StatusCode::NoControllerLossHook,
                          "controller-loss hook is required");
        }
        return ok();
    }

    api::Status commitSeal() {
        if (lifecycle_ == Lifecycle::Sealed) {
            return status(api::StatusCode::RegistrationSealed, "registry is sealed");
        }
        lifecycle_ = Lifecycle::Sealed;
        return ok();
    }

    const StoredCommand* findCommand(const char* name) const {
        if (name == nullptr) {
            return nullptr;
        }
        for (std::size_t i = 0; i < command_count_; ++i) {
            if (std::strcmp(commands_[i].name, name) == 0) {
                return &commands_[i];
            }
        }
        return nullptr;
    }

    Lifecycle lifecycle() const {
        return lifecycle_;
    }

    const api::BoardIdentity identity() const {
        return {identity_.board_id, identity_.protocol_version, identity_.firmware_version};
    }

    std::size_t commandCount() const {
        return command_count_;
    }

    const StoredCommand& commandAt(std::size_t index) const {
        return commands_[index];
    }

    std::size_t telemetryFieldCount() const {
        return telemetry_field_count_;
    }

    const StoredField& telemetryFieldAt(std::size_t index) const {
        return telemetry_fields_[index];
    }

    std::size_t stateFieldCount() const {
        return state_field_count_;
    }

    const StoredField& stateFieldAt(std::size_t index) const {
        return state_fields_[index];
    }

    StoredProvider telemetryProvider() const {
        return telemetry_provider_;
    }

    StoredHook estopHook() const {
        return estop_hook_;
    }

    StoredHook controllerLossHook() const {
        return controller_loss_hook_;
    }

private:
    struct StoredIdentity {
        char board_id[support::kMaxBoardIdBytes + 1];
        char protocol_version[support::kMaxProtocolVersionBytes + 1];
        char firmware_version[support::kMaxFirmwareVersionBytes + 1];
    };

    enum class CopyResult : std::uint8_t {
        Ok,
        Missing,
        TooLong,
    };

    static api::Status ok() {
        return status(api::StatusCode::Ok, "ok");
    }

    static api::Status status(api::StatusCode code, const char* message) {
        return {code, message};
    }

    static bool validValueType(api::ValueType type) {
        switch (type) {
            case api::ValueType::Int:
            case api::ValueType::Float:
            case api::ValueType::Bool:
            case api::ValueType::String:
                return true;
        }
        return false;
    }

    static CopyResult copyRequired(const char* source, char* destination, std::size_t capacity) {
        if (source == nullptr || source[0] == '\0') {
            destination[0] = '\0';
            return CopyResult::Missing;
        }
        const std::size_t length = boundedLength(source, capacity);
        if (length > capacity) {
            destination[0] = '\0';
            return CopyResult::TooLong;
        }
        std::memcpy(destination, source, length);
        destination[length] = '\0';
        return CopyResult::Ok;
    }

    static std::size_t boundedLength(const char* source, std::size_t capacity) {
        std::size_t length = 0;
        while (length <= capacity && source[length] != '\0') {
            ++length;
        }
        return length;
    }

    static bool validRequiredString(const char* source, std::size_t capacity) {
        return source != nullptr && source[0] != '\0' && boundedLength(source, capacity) <= capacity;
    }

    static api::StatusCode stringFailureCode(const char* source,
                                             std::size_t capacity,
                                             api::StatusCode default_code) {
        if (source != nullptr && source[0] != '\0' && boundedLength(source, capacity) > capacity) {
            return api::StatusCode::CapacityExceeded;
        }
        return default_code;
    }

    static api::Status copyStatus(CopyResult result,
                                  api::StatusCode missing_code,
                                  const char* message) {
        if (result == CopyResult::TooLong) {
            return status(api::StatusCode::CapacityExceeded, message);
        }
        return status(missing_code, message);
    }

    static bool argumentNameExists(const StoredArgument* args,
                                   std::size_t count,
                                   const char* name) {
        for (std::size_t i = 0; i < count; ++i) {
            if (std::strcmp(args[i].name, name) == 0) {
                return true;
            }
        }
        return false;
    }

    static bool fieldNameExists(const StoredField* fields, std::size_t count, const char* name) {
        for (std::size_t i = 0; i < count; ++i) {
            if (std::strcmp(fields[i].name, name) == 0) {
                return true;
            }
        }
        return false;
    }

    api::Status registerField(const api::FieldSpec& field,
                              StoredField* fields,
                              std::size_t& count,
                              std::size_t capacity) {
        if (lifecycle_ == Lifecycle::Sealed) {
            return status(api::StatusCode::RegistrationSealed, "registry is sealed");
        }
        if (count >= capacity) {
            return status(api::StatusCode::CapacityExceeded, "too many fields");
        }
        if (!validValueType(field.type)) {
            return status(api::StatusCode::InvalidArgumentSchema, "invalid field type");
        }
        const CopyResult copied = copyRequired(field.name, fields[count].name,
                                               support::kMaxFieldNameBytes);
        if (copied != CopyResult::Ok) {
            return copyStatus(copied, api::StatusCode::InvalidName, "invalid field name");
        }
        if (fieldNameExists(fields, count, fields[count].name)) {
            fields[count].name[0] = '\0';
            return status(api::StatusCode::DuplicateRegistration, "duplicate field");
        }
        fields[count].type = field.type;
        ++count;
        return ok();
    }

    Lifecycle lifecycle_;
    bool identity_set_;
    StoredIdentity identity_;
    StoredCommand commands_[support::kMaxRegisteredCommands]{};
    std::size_t command_count_;
    StoredField telemetry_fields_[support::kMaxTelemetryFields]{};
    std::size_t telemetry_field_count_;
    StoredField state_fields_[support::kMaxStateFields]{};
    std::size_t state_field_count_;
    StoredProvider telemetry_provider_;
    StoredHook estop_hook_;
    StoredHook controller_loss_hook_;
};

}  // namespace teensy_command_server::core
