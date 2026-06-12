#include "api/CommandResult.h"
#include "api/SafetyHooks.h"
#include "api/TelemetryProvider.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace api = teensy_command_server::api;

static api::CommandResult sampleHandler(
    const api::CommandContext&,
    api::ObjectWriter& result,
    void* context) {
    assert(context != nullptr);
    assert(result.addBool("accepted", true));
    return api::CommandResult::ok();
}

static bool sampleProvider(api::ObjectWriter& telemetry, void* context) {
    assert(context != nullptr);
    return telemetry.addInt("rpm", 12);
}

static bool sampleHook(void* context) {
    return context != nullptr;
}

static void testFactoriesAndAccessors() {
    const api::CommandResult ok = api::CommandResult::ok();
    assert(ok.isOk());
    assert(std::strcmp(ok.message(), "") == 0);

    const api::CommandResult generic =
        api::CommandResult::error(api::ErrorCode::UnknownCommand, "dispatcher only");
    assert(!generic.isOk());
    assert(generic.errorCode() == api::ErrorCode::UnknownCommand);
    assert(std::strcmp(generic.message(), "dispatcher only") == 0);

    const api::CommandResult missing = api::CommandResult::missingField("missing rpm");
    assert(!missing.isOk());
    assert(missing.errorCode() == api::ErrorCode::MissingField);
    assert(std::strcmp(missing.message(), "missing rpm") == 0);

    const api::CommandResult type = api::CommandResult::invalidType("rpm type");
    assert(type.errorCode() == api::ErrorCode::InvalidType);

    const api::CommandResult arg = api::CommandResult::invalidArgument("rpm range");
    assert(arg.errorCode() == api::ErrorCode::InvalidArgument);

    const api::CommandResult internal = api::CommandResult::internalError("writer full");
    assert(internal.errorCode() == api::ErrorCode::InternalError);

    const api::CommandResult estop = api::CommandResult::estopActive("hardware condition active");
    assert(estop.errorCode() == api::ErrorCode::EstopActive);
}

static void testMessageLifetime() {
    char message[] = "range is invalid";
    api::CommandResult result = api::CommandResult::invalidArgument(message);
    std::memset(message, 'x', sizeof(message) - 1);
    assert(std::strcmp(result.message(), "range is invalid") == 0);
}

static void testCallbackTypes() {
    static_assert(std::is_pointer<api::CommandHandler>::value);
    static_assert(std::is_pointer<api::TelemetryProvider>::value);
    static_assert(std::is_pointer<api::SafetyHook>::value);

    api::CommandHandler handler = sampleHandler;
    api::TelemetryProvider provider = sampleProvider;
    api::SafetyHook hook = sampleHook;

    api::CommandArgs args;
    const api::CommandContext command{7, args};
    api::ObjectWriter result;
    int context = 1;
    assert(handler(command, result, &context).isOk());
    assert(provider(result, &context));
    assert(hook(&context));
}

int main() {
    testFactoriesAndAccessors();
    testMessageLifetime();
    testCallbackTypes();

    std::puts("test_command_result: ok");
    return 0;
}
