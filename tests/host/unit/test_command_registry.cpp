#include "core/CommandRegistry.h"
#include "support/Limits.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace api = teensy_command_server::api;
namespace core = teensy_command_server::core;
namespace limits = teensy_command_server::support;

api::CommandResult handler(const api::CommandContext&, api::ObjectWriter&, void*) {
    return api::CommandResult::ok();
}

bool telemetry(api::ObjectWriter&, void*) {
    return true;
}

bool hook(void*) {
    return true;
}

core::CommandRegistry::CommandRegistration commandRegistration(
    const char* name,
    const api::ArgumentSpec* args = nullptr,
    std::size_t arg_count = 0,
    bool blocked_by_estop = true,
    bool blocked_by_estop_specified = true,
    api::CommandHandler command_handler = handler,
    void* context = nullptr) {
    return {name, args, arg_count, blocked_by_estop, blocked_by_estop_specified,
            command_handler, context};
}

void fillString(char* out, std::size_t length, char ch) {
    for (std::size_t i = 0; i < length; ++i) {
        out[i] = ch;
    }
    out[length] = '\0';
}

void assertOk(api::Status status) {
    assert(status.code == api::StatusCode::Ok);
}

void completesAndSeals() {
    core::CommandRegistry registry;
    int command_context = 7;
    int telemetry_context = 8;
    int estop_context = 9;
    int loss_context = 10;

    assert(registry.lifecycle() == core::CommandRegistry::Lifecycle::Mutable);
    assert(registry.validateMetadataForSeal().code == api::StatusCode::InvalidConfiguration);

    assertOk(registry.setIdentity({"motor_controller", "1", "0.1.0"}));
    assert(registry.validateMetadataForSeal().code == api::StatusCode::NoTelemetryProvider);
    assertOk(registry.setTelemetryProvider(telemetry, &telemetry_context));
    assert(registry.validateMetadataForSeal().code == api::StatusCode::NoEstopHook);
    assertOk(registry.setEstopHook(hook, &estop_context));
    assert(registry.validateMetadataForSeal().code == api::StatusCode::NoControllerLossHook);

    const api::ArgumentSpec args[] = {
        {"rpm", api::ValueType::Int},
        {"enabled", api::ValueType::Bool},
    };
    assertOk(registry.registerCommand(commandRegistration("set_speed", args, 2, true, true,
                                                          handler, &command_context)));
    assertOk(registry.registerTelemetryField({"temperature_c", api::ValueType::Float}));
    assertOk(registry.registerStateField({"mode", api::ValueType::String}));
    assertOk(registry.setControllerLossHook(hook, &loss_context));

    const api::Status validation = registry.validateMetadataForSeal();
    assertOk(validation);
    assert(registry.lifecycle() == core::CommandRegistry::Lifecycle::Mutable);
    assert(registry.commandCount() == 1);

    assertOk(registry.commitSeal());
    assert(registry.lifecycle() == core::CommandRegistry::Lifecycle::Sealed);
    assert(registry.registerCommand(commandRegistration("after_seal")).code ==
           api::StatusCode::RegistrationSealed);
    assert(registry.registerTelemetryField({"humidity", api::ValueType::Float}).code ==
           api::StatusCode::RegistrationSealed);
    assert(registry.setIdentity({"other", "1", "0.1.0"}).code ==
           api::StatusCode::RegistrationSealed);

    const core::CommandRegistry::StoredCommand* command = registry.findCommand("set_speed");
    assert(command != nullptr);
    assert(std::strcmp(command->name, "set_speed") == 0);
    assert(command->arg_count == 2);
    assert(std::strcmp(command->args[0].name, "rpm") == 0);
    assert(command->args[0].type == api::ValueType::Int);
    assert(command->args[1].type == api::ValueType::Bool);
    assert(command->blocked_by_estop);
    assert(command->handler == handler);
    assert(command->context == &command_context);
    assert(registry.findCommand("missing") == nullptr);
    assert(registry.findCommand(nullptr) == nullptr);

    assert(std::strcmp(registry.identity().board_id, "motor_controller") == 0);
    assert(std::strcmp(registry.identity().protocol_version, "1") == 0);
    assert(std::strcmp(registry.identity().firmware_version, "0.1.0") == 0);
    assert(registry.telemetryFieldCount() == 1);
    assert(std::strcmp(registry.telemetryFieldAt(0).name, "temperature_c") == 0);
    assert(registry.stateFieldCount() == 1);
    assert(std::strcmp(registry.stateFieldAt(0).name, "mode") == 0);
    assert(registry.telemetryProvider().fn == telemetry);
    assert(registry.telemetryProvider().context == &telemetry_context);
    assert(registry.estopHook().fn == hook);
    assert(registry.estopHook().context == &estop_context);
    assert(registry.controllerLossHook().fn == hook);
    assert(registry.controllerLossHook().context == &loss_context);
}

void duplicateAndCapacityRules() {
    core::CommandRegistry duplicate_registry;
    assertOk(duplicate_registry.registerCommand(commandRegistration("cmd")));
    assert(duplicate_registry.registerCommand(commandRegistration("cmd")).code ==
           api::StatusCode::DuplicateRegistration);

    const api::ArgumentSpec duplicate_args[] = {
        {"rpm", api::ValueType::Int},
        {"rpm", api::ValueType::Float},
    };
    assert(duplicate_registry.registerCommand(
               commandRegistration("bad_args", duplicate_args, 2)).code ==
           api::StatusCode::DuplicateRegistration);

    core::CommandRegistry command_capacity;
    char names[limits::kMaxRegisteredCommands + 1][limits::kMaxCommandNameBytes + 1]{};
    for (std::size_t i = 0; i < limits::kMaxRegisteredCommands; ++i) {
        std::snprintf(names[i], sizeof(names[i]), "cmd_%02zu", i);
        assertOk(command_capacity.registerCommand(commandRegistration(names[i])));
    }
    std::snprintf(names[limits::kMaxRegisteredCommands],
                  sizeof(names[limits::kMaxRegisteredCommands]), "cmd_over");
    assert(command_capacity
               .registerCommand(commandRegistration(names[limits::kMaxRegisteredCommands]))
               .code == api::StatusCode::CapacityExceeded);

    api::ArgumentSpec args[limits::kMaxArgsPerCommand + 1]{};
    char arg_names[limits::kMaxArgsPerCommand + 1][limits::kMaxArgNameBytes + 1]{};
    for (std::size_t i = 0; i <= limits::kMaxArgsPerCommand; ++i) {
        std::snprintf(arg_names[i], sizeof(arg_names[i]), "arg_%02zu", i);
        args[i] = {arg_names[i], api::ValueType::Int};
    }
    core::CommandRegistry arg_capacity;
    assert(arg_capacity
               .registerCommand(commandRegistration("too_many_args", args,
                                                    limits::kMaxArgsPerCommand + 1))
               .code == api::StatusCode::CapacityExceeded);

    core::CommandRegistry telemetry_capacity;
    char field_names[limits::kMaxTelemetryFields + 1][limits::kMaxFieldNameBytes + 1]{};
    for (std::size_t i = 0; i < limits::kMaxTelemetryFields; ++i) {
        std::snprintf(field_names[i], sizeof(field_names[i]), "tele_%02zu", i);
        assertOk(telemetry_capacity.registerTelemetryField({field_names[i], api::ValueType::Int}));
    }
    std::snprintf(field_names[limits::kMaxTelemetryFields],
                  sizeof(field_names[limits::kMaxTelemetryFields]), "tele_over");
    assert(telemetry_capacity
               .registerTelemetryField(
                   {field_names[limits::kMaxTelemetryFields], api::ValueType::Int})
               .code == api::StatusCode::CapacityExceeded);

    core::CommandRegistry state_capacity;
    char state_names[limits::kMaxStateFields + 1][limits::kMaxFieldNameBytes + 1]{};
    for (std::size_t i = 0; i < limits::kMaxStateFields; ++i) {
        std::snprintf(state_names[i], sizeof(state_names[i]), "state_%02zu", i);
        assertOk(state_capacity.registerStateField({state_names[i], api::ValueType::Bool}));
    }
    std::snprintf(state_names[limits::kMaxStateFields],
                  sizeof(state_names[limits::kMaxStateFields]), "state_over");
    assert(state_capacity
               .registerStateField({state_names[limits::kMaxStateFields], api::ValueType::Bool})
               .code == api::StatusCode::CapacityExceeded);
}

void explicitBlockedByEstopAndTypeRules() {
    core::CommandRegistry registry;
    assert(registry.registerCommand(commandRegistration("implicit", nullptr, 0, true, false)).code ==
           api::StatusCode::InvalidArgumentSchema);

    const api::ArgumentSpec invalid_type_arg[] = {
        {"rpm", static_cast<api::ValueType>(99)},
    };
    assert(registry
               .registerCommand(commandRegistration("bad_type", invalid_type_arg, 1, true, true))
               .code == api::StatusCode::InvalidArgumentSchema);

    assert(registry.registerTelemetryField({"bad", static_cast<api::ValueType>(99)}).code ==
           api::StatusCode::InvalidArgumentSchema);
    assert(registry.registerCommand(commandRegistration("no_handler", nullptr, 0, true, true,
                                                        nullptr)).code ==
           api::StatusCode::InvalidConfiguration);
}

void failedValidationLeavesRegistryMutable() {
    core::CommandRegistry registry;
    assertOk(registry.setIdentity({"board", "1", "0.1.0"}));
    assert(registry.validateMetadataForSeal().code == api::StatusCode::NoTelemetryProvider);
    assert(registry.lifecycle() == core::CommandRegistry::Lifecycle::Mutable);
    assertOk(registry.registerCommand(commandRegistration("after_failed_validation")));
    assert(registry.commandCount() == 1);
}

void copiedStringOwnership() {
    core::CommandRegistry registry;
    char board_id[] = "board_a";
    char protocol[] = "1";
    char firmware[] = "0.1.0";
    char command_name[16] = "set_speed";
    char arg_name[] = "rpm";
    char telemetry_name[] = "temperature_c";
    char state_name[8] = "mode";
    api::ArgumentSpec args[] = {
        {arg_name, api::ValueType::Int},
    };

    assertOk(registry.setIdentity({board_id, protocol, firmware}));
    assertOk(registry.registerCommand(commandRegistration(command_name, args, 1)));
    assertOk(registry.registerTelemetryField({telemetry_name, api::ValueType::Float}));
    assertOk(registry.registerStateField({state_name, api::ValueType::String}));

    std::strcpy(board_id, "xxxxxxx");
    std::strcpy(protocol, "9");
    std::strcpy(firmware, "9.9.9");
    std::strcpy(command_name, "overwritten");
    std::strcpy(arg_name, "zzz");
    std::strcpy(telemetry_name, "other");
    std::strcpy(state_name, "fault");

    assert(std::strcmp(registry.identity().board_id, "board_a") == 0);
    assert(std::strcmp(registry.identity().protocol_version, "1") == 0);
    assert(std::strcmp(registry.identity().firmware_version, "0.1.0") == 0);
    const core::CommandRegistry::StoredCommand* command = registry.findCommand("set_speed");
    assert(command != nullptr);
    assert(std::strcmp(command->args[0].name, "rpm") == 0);
    assert(std::strcmp(registry.telemetryFieldAt(0).name, "temperature_c") == 0);
    assert(std::strcmp(registry.stateFieldAt(0).name, "mode") == 0);
}

void overLengthStringsFail() {
    char board_id[limits::kMaxBoardIdBytes + 2];
    char firmware[limits::kMaxFirmwareVersionBytes + 2];
    char command_name[limits::kMaxCommandNameBytes + 2];
    char arg_name[limits::kMaxArgNameBytes + 2];
    char field_name[limits::kMaxFieldNameBytes + 2];
    fillString(board_id, limits::kMaxBoardIdBytes + 1, 'b');
    fillString(firmware, limits::kMaxFirmwareVersionBytes + 1, 'f');
    fillString(command_name, limits::kMaxCommandNameBytes + 1, 'c');
    fillString(arg_name, limits::kMaxArgNameBytes + 1, 'a');
    fillString(field_name, limits::kMaxFieldNameBytes + 1, 't');

    core::CommandRegistry identity_registry;
    assert(identity_registry.setIdentity({board_id, "1", "0.1.0"}).code ==
           api::StatusCode::CapacityExceeded);
    assert(identity_registry.setIdentity({"board", "1", firmware}).code ==
           api::StatusCode::CapacityExceeded);

    core::CommandRegistry command_registry;
    assert(command_registry.registerCommand(commandRegistration(command_name)).code ==
           api::StatusCode::CapacityExceeded);

    const api::ArgumentSpec args[] = {
        {arg_name, api::ValueType::Int},
    };
    assert(command_registry.registerCommand(commandRegistration("cmd", args, 1)).code ==
           api::StatusCode::CapacityExceeded);

    core::CommandRegistry field_registry;
    assert(field_registry.registerTelemetryField({field_name, api::ValueType::Float}).code ==
           api::StatusCode::CapacityExceeded);

    assert(command_registry.registerCommand(commandRegistration("")).code ==
           api::StatusCode::InvalidName);
    const api::ArgumentSpec empty_arg[] = {
        {"", api::ValueType::Int},
    };
    assert(command_registry.registerCommand(commandRegistration("empty_arg", empty_arg, 1)).code ==
           api::StatusCode::InvalidArgumentSchema);
}

int main() {
    completesAndSeals();
    duplicateAndCapacityRules();
    explicitBlockedByEstopAndTypeRules();
    failedValidationLeavesRegistryMutable();
    copiedStringOwnership();
    overLengthStringsFail();

    std::puts("test_command_registry: ok");
    return 0;
}
