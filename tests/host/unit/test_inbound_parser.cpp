#include "core/InboundParser.h"
#include "support/BoundedJsonWriter.h"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>

namespace core = teensy_command_server::core;
namespace support = teensy_command_server::support;

static std::size_t g_allocations = 0;

void* operator new(std::size_t size) {
    ++g_allocations;
    void* ptr = std::malloc(size);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void* operator new[](std::size_t size) {
    ++g_allocations;
    void* ptr = std::malloc(size);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

static core::MutableLineView lineView(char* buffer) {
    return {buffer, std::strlen(buffer)};
}

static core::ParseOutcome parseLiteral(core::InboundParser& parser, const char* literal) {
    char buffer[support::kInboundLineBufferBytes]{};
    assert(std::strlen(literal) < sizeof(buffer));
    std::memcpy(buffer, literal, std::strlen(literal) + 1);
    return parser.parse(lineView(buffer));
}

static const char* commandJson(const char* arg_literal) {
    static char buffer[512]{};
    const int written = std::snprintf(
        buffer,
        sizeof(buffer),
        "{\"type\":\"command\",\"seq\":42,\"controller_ts\":1718210123.4567891,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set\","
        "\"args\":{\"v\":%s}}",
        arg_literal);
    assert(written > 0 && static_cast<std::size_t>(written) < sizeof(buffer));
    return buffer;
}

static void validMessageClassification() {
    core::Counters counters;
    core::InboundParser parser(counters);

    core::ParseOutcome command = parseLiteral(
        parser,
        "{\"type\":\"command\",\"seq\":18446744073709551615,\"controller_ts\":1,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set\","
        "\"args\":{\"rpm\":1,\"enabled\":true,\"name\":\"axis\"}}");
    assert(command.kind == core::ParseOutcomeKind::Valid);
    assert(command.message_kind == core::InboundMessageKind::Command);
    assert(command.has_seq);
    assert(command.seq == std::numeric_limits<support::Seq>::max());
    assert(command.command.seq == std::numeric_limits<support::Seq>::max());
    assert(command.has_controller_ts);
    assert(command.controller_ts == 1.0);
    assert(command.command.controller_ts == 1.0);
    assert(std::strcmp(command.command.command, "set") == 0);
    assert(command.command.args.has("rpm"));
    assert(command.command.args.has("enabled"));
    assert(command.command.args.has("name"));

    core::ParseOutcome estop = parseLiteral(
        parser, "{\"type\":\"estop\",\"source\":\"controller\",\"target\":\"motor\"}");
    assert(estop.kind == core::ParseOutcomeKind::Valid);
    assert(estop.message_kind == core::InboundMessageKind::Estop);
    assert(std::strcmp(estop.estop.source, "controller") == 0);
    assert(std::strcmp(estop.estop.target, "motor") == 0);
    assert(!estop.has_suggested_error);

    core::ParseOutcome heartbeat = parseLiteral(
        parser, "{\"type\":\"heartbeat\",\"seq\":7,\"source\":\"controller\",\"target\":\"motor\"}");
    assert(heartbeat.kind == core::ParseOutcomeKind::Valid);
    assert(heartbeat.message_kind == core::InboundMessageKind::Heartbeat);
    assert(heartbeat.has_seq);
    assert(heartbeat.seq == 7);
    assert(heartbeat.heartbeat.seq == 7);
    assert(std::strcmp(heartbeat.heartbeat.source, "controller") == 0);
    assert(std::strcmp(heartbeat.heartbeat.target, "motor") == 0);

    assert(counters.invalid_json == 0);
}

static void unsupportedTypesHaveNoSuggestedError() {
    const char* unsupported[] = {
        "schema",
        "response",
        "telemetry",
        "event",
        "estop_reset",
        "bogus",
    };

    core::Counters counters;
    core::InboundParser parser(counters);
    for (const char* type : unsupported) {
        char buffer[256]{};
        const int written = std::snprintf(buffer,
                                          sizeof(buffer),
                                          "{\"type\":\"%s\",\"seq\":99,"
                                          "\"source\":\"controller\",\"target\":\"motor\"}",
                                          type);
        assert(written > 0 && static_cast<std::size_t>(written) < sizeof(buffer));
        core::ParseOutcome outcome = parser.parse(lineView(buffer));
        assert(outcome.kind == core::ParseOutcomeKind::UnsupportedType);
        assert(outcome.message_kind == core::InboundMessageKind::None);
        assert(!outcome.has_suggested_error);
    }
    assert(counters.invalid_json == 6);
}

static void malformedInputHasNoTrustworthySeq() {
    core::Counters counters;
    core::InboundParser parser(counters);

    char malformed[] = "{\"type\":\"command\",\"seq\":1";
    core::ParseOutcome bad_json = parser.parse(lineView(malformed));
    assert(bad_json.kind == core::ParseOutcomeKind::Malformed);
    assert(!bad_json.has_seq);
    assert(!bad_json.has_suggested_error);

    char bare_continuation[] = {
        '{', '"', 't', 'y', 'p', 'e', '"', ':', '"', 'c', 'o', 'm', 'm',
        'a', 'n', 'd', '"', ',', '"', 'x', '"', ':', '"', static_cast<char>(0x80),
        '"', '}', '\0'};
    core::ParseOutcome continuation = parser.parse(lineView(bare_continuation));
    assert(continuation.kind == core::ParseOutcomeKind::Malformed);
    assert(!continuation.has_seq);

    char overlong[] = {
        '{', '"', 't', 'y', 'p', 'e', '"', ':', '"', 'c', 'o', 'm', 'm',
        'a', 'n', 'd', '"', ',', '"', 'x', '"', ':', '"', static_cast<char>(0xC0),
        static_cast<char>(0xAF), '"', '}', '\0'};
    core::ParseOutcome overlong_result = parser.parse(lineView(overlong));
    assert(overlong_result.kind == core::ParseOutcomeKind::Malformed);
    assert(!overlong_result.has_seq);

    char truncated[] = {
        '{', '"', 't', 'y', 'p', 'e', '"', ':', '"', 'c', 'o', 'm', 'm',
        'a', 'n', 'd', '"', ',', '"', 'x', '"', ':', '"', static_cast<char>(0xE2),
        static_cast<char>(0x82), '\0'};
    core::ParseOutcome truncated_result = parser.parse(lineView(truncated));
    assert(truncated_result.kind == core::ParseOutcomeKind::Malformed);
    assert(!truncated_result.has_seq);

    assert(counters.invalid_json == 4);
}

static void structuralInvalidPreservesRecoverableFields() {
    core::Counters counters;
    core::InboundParser parser(counters);

    core::ParseOutcome missing_command = parseLiteral(
        parser,
        "{\"type\":\"command\",\"seq\":44,\"controller_ts\":2.5,"
        "\"source\":\"controller\",\"target\":\"motor\",\"args\":{}}");
    assert(missing_command.kind == core::ParseOutcomeKind::StructurallyInvalid);
    assert(missing_command.message_kind == core::InboundMessageKind::Command);
    assert(missing_command.has_seq);
    assert(missing_command.seq == 44);
    assert(missing_command.has_controller_ts);
    assert(missing_command.controller_ts == 2.5);
    assert(missing_command.has_suggested_error);
    assert(missing_command.suggested_error == core::BoardErrorCode::MissingField);

    core::ParseOutcome bad_controller_ts = parseLiteral(
        parser,
        "{\"type\":\"command\",\"seq\":45,\"controller_ts\":\"2.5\","
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set\",\"args\":{}}");
    assert(bad_controller_ts.kind == core::ParseOutcomeKind::StructurallyInvalid);
    assert(bad_controller_ts.has_seq);
    assert(bad_controller_ts.seq == 45);
    assert(!bad_controller_ts.has_controller_ts);
    assert(bad_controller_ts.suggested_error == core::BoardErrorCode::InvalidType);

    core::ParseOutcome non_finite_controller_ts = parseLiteral(
        parser,
        "{\"type\":\"command\",\"seq\":47,\"controller_ts\":1e999,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set\",\"args\":{}}");
    assert(non_finite_controller_ts.kind == core::ParseOutcomeKind::StructurallyInvalid);
    assert(non_finite_controller_ts.has_seq);
    assert(non_finite_controller_ts.seq == 47);
    assert(!non_finite_controller_ts.has_controller_ts);
    assert(non_finite_controller_ts.suggested_error == core::BoardErrorCode::InvalidType);

    core::ParseOutcome missing_heartbeat_target = parseLiteral(
        parser, "{\"type\":\"heartbeat\",\"seq\":46,\"source\":\"controller\"}");
    assert(missing_heartbeat_target.kind == core::ParseOutcomeKind::StructurallyInvalid);
    assert(missing_heartbeat_target.message_kind == core::InboundMessageKind::Heartbeat);
    assert(missing_heartbeat_target.has_seq);
    assert(missing_heartbeat_target.seq == 46);
    assert(missing_heartbeat_target.suggested_error == core::BoardErrorCode::MissingField);

    assert(counters.invalid_json == 0);
}

static void integerArgumentDiscipline() {
    struct Case {
        const char* literal;
        bool accepted;
        std::int32_t expected;
    };

    const Case cases[] = {
        {"1", true, 1},
        {"1.0", false, 0},
        {"1e0", false, 0},
        {"1.5", false, 0},
        {"-2147483648", true, std::numeric_limits<std::int32_t>::min()},
        {"2147483647", true, std::numeric_limits<std::int32_t>::max()},
        {"-2147483649", false, 0},
        {"2147483648", false, 0},
    };

    core::Counters counters;
    core::InboundParser parser(counters);
    for (const Case& test_case : cases) {
        core::ParseOutcome outcome = parseLiteral(parser, commandJson(test_case.literal));
        assert(outcome.kind == core::ParseOutcomeKind::Valid);
        std::int32_t value = 0;
        assert(outcome.command.args.getInt("v", value) == test_case.accepted);
        if (test_case.accepted) {
            assert(value == test_case.expected);
        }
    }
}

static void commandArgsTypeDiscipline() {
    core::Counters counters;
    core::InboundParser parser(counters);
    core::ParseOutcome outcome = parseLiteral(
        parser,
        "{\"type\":\"command\",\"seq\":1,\"controller_ts\":1,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set\","
        "\"args\":{\"i\":1,\"f\":1.25,\"b\":true,\"s\":\"name\",\"inf\":1e999}}");
    assert(outcome.kind == core::ParseOutcomeKind::Valid);

    std::int32_t int_value = 0;
    float float_value = 0.0f;
    bool bool_value = false;
    const char* string_value = nullptr;
    std::size_t string_length = 0;

    assert(outcome.command.args.getInt("i", int_value));
    assert(int_value == 1);
    assert(!outcome.command.args.getInt("f", int_value));
    assert(outcome.command.args.getFloat("f", float_value));
    assert(float_value == 1.25f);
    assert(!outcome.command.args.getFloat("s", float_value));
    assert(!outcome.command.args.getFloat("inf", float_value));
    assert(outcome.command.args.getBool("b", bool_value));
    assert(bool_value);
    assert(!outcome.command.args.getBool("i", bool_value));
    assert(outcome.command.args.getString("s", string_value, string_length));
    assert(std::strcmp(string_value, "name") == 0);
    assert(string_length == 4);
    assert(!outcome.command.args.getString("b", string_value, string_length));
    assert(!outcome.command.args.has("missing"));
}

static void controllerTsRoundTripsThroughPinnedWriterPolicy() {
    const char* rows[] = {"1718210123.4567891", "1", "1.0", "1e0"};

    core::Counters counters;
    core::InboundParser parser(counters);
    for (const char* literal : rows) {
        char input[384]{};
        const int input_size = std::snprintf(
            input,
            sizeof(input),
            "{\"type\":\"command\",\"seq\":1,\"controller_ts\":%s,"
            "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set\",\"args\":{}}",
            literal);
        assert(input_size > 0 && static_cast<std::size_t>(input_size) < sizeof(input));
        core::ParseOutcome outcome = parser.parse(lineView(input));
        assert(outcome.kind == core::ParseOutcomeKind::Valid);

        char output[128]{};
        support::BoundedJsonWriter writer(output, sizeof(output));
        assert(writer.beginObject());
        assert(writer.addDouble("controller_ts", outcome.command.controller_ts));
        assert(writer.endObject());

        ArduinoJson::StaticJsonDocument<128> reparsed;
        ArduinoJson::DeserializationError error = ArduinoJson::deserializeJson(reparsed, output);
        assert(!error);
        assert(reparsed["controller_ts"].as<double>() == outcome.command.controller_ts);
    }
}

static void zeroCopyViewsFollowAcquiredLineLifetime() {
    core::Counters counters;
    core::LineFramer framer(counters);
    core::InboundParser parser(counters);

    const char* wire =
        "{\"type\":\"command\",\"seq\":1,\"controller_ts\":1,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set\","
        "\"args\":{\"s\":\"axis\"}}\n";
    assert(framer.append(wire, std::strlen(wire)) == std::strlen(wire));
    assert(framer.hasLine());

    core::MutableLineView view = framer.acquireLine();
    assert(view.valid());
    core::ParseOutcome outcome = parser.parse(view);
    assert(outcome.kind == core::ParseOutcomeKind::Valid);

    const char* value = nullptr;
    std::size_t length = 0;
    assert(outcome.command.args.getString("s", value, length));
    assert(length == 4);
    assert(value >= view.data);
    assert(value < view.data + view.size);
    assert(std::strcmp(value, "axis") == 0);

    framer.releaseLine();
    parser.invalidate();
    assert(!outcome.command.args.getString("s", value, length));
}

static void steadyStateParsingAllocatesNothing() {
    core::Counters counters;
    core::InboundParser parser(counters);
    const char* literal =
        "{\"type\":\"command\",\"seq\":1,\"controller_ts\":1,"
        "\"source\":\"controller\",\"target\":\"motor\",\"command\":\"set\","
        "\"args\":{\"rpm\":1,\"enabled\":true,\"name\":\"axis\"}}";

    char warmup[support::kInboundLineBufferBytes]{};
    std::memcpy(warmup, literal, std::strlen(literal) + 1);
    core::ParseOutcome warmup_outcome = parser.parse(lineView(warmup));
    assert(warmup_outcome.kind == core::ParseOutcomeKind::Valid);

    g_allocations = 0;
    for (int i = 0; i < 200; ++i) {
        char buffer[support::kInboundLineBufferBytes]{};
        std::memcpy(buffer, literal, std::strlen(literal) + 1);
        core::ParseOutcome outcome = parser.parse(lineView(buffer));
        assert(outcome.kind == core::ParseOutcomeKind::Valid);
        std::int32_t rpm = 0;
        assert(outcome.command.args.getInt("rpm", rpm));
        assert(rpm == 1);
    }
    assert(g_allocations == 0);
}

int main() {
    validMessageClassification();
    unsupportedTypesHaveNoSuggestedError();
    malformedInputHasNoTrustworthySeq();
    structuralInvalidPreservesRecoverableFields();
    integerArgumentDiscipline();
    commandArgsTypeDiscipline();
    controllerTsRoundTripsThroughPinnedWriterPolicy();
    zeroCopyViewsFollowAcquiredLineLifetime();
    steadyStateParsingAllocatesNothing();

    std::puts("test_inbound_parser: ok");
    return 0;
}
