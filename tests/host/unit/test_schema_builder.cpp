#include "core/SchemaBuilder.h"
#include "fakes/FakeClock.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

namespace api = teensy_command_server::api;
namespace core = teensy_command_server::core;
namespace fakes = teensy_command_server::host::fakes;
namespace limits = teensy_command_server::support;

api::CommandResult handler(const api::CommandContext&, api::ObjectWriter&, void*) {
    return api::CommandResult::ok();
}

core::CommandRegistry::CommandRegistration commandRegistration(
    const char* name,
    const api::ArgumentSpec* args = nullptr,
    std::size_t arg_count = 0,
    bool blocked_by_estop = true) {
    return {name, args, arg_count, blocked_by_estop, true, handler, nullptr};
}

void assertOk(api::Status status) {
    assert(status.code == api::StatusCode::Ok);
}

void assertBufferEquals(const char* buffer, std::size_t length, const char* expected) {
    assert(length == std::strlen(expected));
    assert(std::memcmp(buffer, expected, length) == 0);
}

core::CommandRegistry smallRegistry() {
    core::CommandRegistry registry;
    const api::ArgumentSpec speed_args[] = {
        {"rpm", api::ValueType::Int},
        {"enabled", api::ValueType::Bool},
    };

    assertOk(registry.registerCommand(commandRegistration("set_speed", speed_args, 2, true)));
    assertOk(registry.registerCommand(commandRegistration("get_status", nullptr, 0, false)));
    assertOk(registry.registerTelemetryField({"rpm", api::ValueType::Int}));
    assertOk(registry.registerTelemetryField({"temperature_c", api::ValueType::Float}));
    assertOk(registry.registerStateField({"mode", api::ValueType::String}));
    assertOk(registry.registerStateField({"faulted", api::ValueType::Bool}));
    return registry;
}

void goldenSchemaUsesContractShapeAndOrder() {
    core::CommandRegistry registry = smallRegistry();
    fakes::FakeClock clock;
    clock.advanceMilliseconds(1234);
    char buffer[limits::kSchemaJsonBufferBytes]{};

    assertOk(core::SchemaBuilder::buildSchemaLine(
        registry, {"motor_controller", "1", "0.1.0"}, clock, buffer, sizeof(buffer)));

    const char* expected =
        "{\"type\":\"schema\",\"seq\":1,\"timestamp\":1234,\"source\":\"motor_controller\","
        "\"target\":\"controller\",\"protocol_version\":\"1\",\"schema\":{\"commands\":{"
        "\"set_speed\":{\"args\":{\"rpm\":\"int\",\"enabled\":\"bool\"},"
        "\"blocked_by_estop\":true},\"get_status\":{\"args\":{},"
        "\"blocked_by_estop\":false}},\"telemetry\":{\"rpm\":\"int\","
        "\"temperature_c\":\"float\"},\"state\":{\"mode\":\"string\",\"faulted\":\"bool\"},"
        "\"firmware_version\":\"0.1.0\"}}\n";
    assertBufferEquals(buffer, std::strlen(buffer), expected);
    assert(std::strstr(buffer, "\"source\":\"motor_controller\"") != nullptr);
    assert(std::strstr(buffer, "\"schema\":{\"commands\"") != nullptr);
    assert(std::strstr(buffer, "\"firmware_version\":\"0.1.0\"") != nullptr);
    assert(std::strstr(buffer, "\"blocked_by_estop\":true") != nullptr);
    assert(std::strstr(buffer, "\"blocked_by_estop\":false") != nullptr);
}

void exactRuntimeCapacityIsAcceptedAndOneByteShortIsRejected() {
    core::CommandRegistry registry = smallRegistry();
    fakes::FakeClock clock;
    clock.advanceMilliseconds(1);
    char full[limits::kSchemaJsonBufferBytes]{};
    assertOk(core::SchemaBuilder::buildSchemaLine(
        registry, {"board", "1", "fw"}, clock, full, sizeof(full)));

    const std::size_t exact_size = std::strlen(full);
    char exact[limits::kSchemaJsonBufferBytes]{};
    assertOk(core::SchemaBuilder::buildSchemaLine(
        registry, {"board", "1", "fw"}, clock, exact, exact_size));
    assert(std::memcmp(exact, full, exact_size) == 0);

    char short_buffer[limits::kSchemaJsonBufferBytes]{};
    assert(core::SchemaBuilder::buildSchemaLine(
               registry, {"board", "1", "fw"}, clock, short_buffer, exact_size - 1)
               .code == api::StatusCode::CapacityExceeded);
}

void constructedOversizedSchemaIsRejected() {
    core::CommandRegistry registry = smallRegistry();
    char huge_firmware[limits::kBoardTxMaxLineBytes]{};
    for (std::size_t i = 0; i < sizeof(huge_firmware) - 1; ++i) {
        huge_firmware[i] = 'f';
    }
    huge_firmware[sizeof(huge_firmware) - 1] = '\0';

    assert(core::SchemaBuilder::validateMaximumSchemaSize(
               registry, {"board", "1", huge_firmware})
               .code == api::StatusCode::CapacityExceeded);
}

class MaxClock final : public core::Clock {
public:
    std::uint64_t monotonicMilliseconds() const override {
        return std::numeric_limits<std::uint64_t>::max();
    }

    std::uint64_t monotonicMicroseconds() const override {
        return std::numeric_limits<std::uint64_t>::max();
    }
};

void sealTimeMaximumTimestampReservationCoversRuntimeWidth() {
    core::CommandRegistry registry = smallRegistry();
    assertOk(core::SchemaBuilder::validateMaximumSchemaSize(
        registry, {"motor_controller", "1", "0.1.0"}));

    MaxClock clock;
    char buffer[limits::kSchemaJsonBufferBytes]{};
    assertOk(core::SchemaBuilder::buildSchemaLine(
        registry, {"motor_controller", "1", "0.1.0"}, clock, buffer, sizeof(buffer)));
    assert(std::strlen(buffer) <= limits::kBoardTxMaxLineBytes);
    assert(std::strstr(buffer, "\"timestamp\":18446744073709551615") != nullptr);
}

void validationDoesNotSealRegistry() {
    core::CommandRegistry failed;
    char huge_firmware[limits::kBoardTxMaxLineBytes]{};
    for (std::size_t i = 0; i < sizeof(huge_firmware) - 1; ++i) {
        huge_firmware[i] = 'x';
    }
    huge_firmware[sizeof(huge_firmware) - 1] = '\0';

    assert(core::SchemaBuilder::validateMaximumSchemaSize(failed, {"board", "1", huge_firmware})
               .code == api::StatusCode::CapacityExceeded);
    assert(failed.lifecycle() == core::CommandRegistry::Lifecycle::Mutable);
    assertOk(failed.registerCommand(commandRegistration("after_failed_validation")));

    core::CommandRegistry passed = smallRegistry();
    assertOk(core::SchemaBuilder::validateMaximumSchemaSize(passed, {"board", "1", "fw"}));
    assert(passed.lifecycle() == core::CommandRegistry::Lifecycle::Mutable);
    assertOk(passed.commitSeal());
    assert(passed.registerCommand(commandRegistration("after_seal")).code ==
           api::StatusCode::RegistrationSealed);
}

int main() {
    goldenSchemaUsesContractShapeAndOrder();
    exactRuntimeCapacityIsAcceptedAndOneByteShortIsRejected();
    constructedOversizedSchemaIsRejected();
    sealTimeMaximumTimestampReservationCoversRuntimeWidth();
    validationDoesNotSealRegistry();

    std::puts("test_schema_builder: ok");
    return 0;
}
