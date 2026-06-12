#include "api/ObjectWriter.h"
#include "support/Limits.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace api = teensy_command_server::api;
namespace support = teensy_command_server::support;

static void testObjectWriterBasicShape() {
    api::ObjectWriter writer;
    assert(writer.addInt("rpm", 1200));
    assert(writer.addFloat("temperature_c", 41.25F));
    assert(writer.addBool("enabled", true));
    assert(writer.addString("mode", "run"));
    assert(writer.close());

    assert(std::strstr(writer.data(), "\"rpm\":1200") != nullptr);
    assert(std::strstr(writer.data(), "\"temperature_c\":41.25") != nullptr);
    assert(std::strstr(writer.data(), "\"enabled\":true") != nullptr);
    assert(std::strstr(writer.data(), "\"mode\":\"run\"") != nullptr);
    assert(std::strstr(writer.data(), ": ") == nullptr);
    assert(std::strstr(writer.data(), ", ") == nullptr);
}

static void testBoardProcUsRejected() {
    api::ObjectWriter writer;
    assert(!writer.addInt("board_proc_us", 42));
    assert(writer.addInt("ok", 1));
    assert(writer.close());
    assert(std::strcmp(writer.data(), "{\"ok\":1}") == 0);
}

static void testCapacityIsEnvelopeReserved() {
    static_assert(support::kMaxResultPayloadBytes <
                  support::kBoardTxMaxLineBytes);
    static_assert(support::kMaxResultPayloadBytes ==
                  support::kBoardTxMaxLineBytes - support::kResponseEnvelopeReserveBytes);

    api::ObjectWriter writer;
    assert(writer.capacity() == support::kMaxResultPayloadBytes);

    const std::size_t prefix = std::strlen("{\"payload\":\"");
    const std::size_t suffix = std::strlen("\"}");
    const std::size_t accepted_length = support::kMaxResultPayloadBytes - prefix - suffix;
    static char accepted[support::kMaxResultPayloadBytes]{};
    std::memset(accepted, 'a', accepted_length);
    accepted[accepted_length] = '\0';

    assert(writer.addString("payload", accepted, accepted_length));
    assert(writer.close());
    assert(writer.size() == support::kMaxResultPayloadBytes);

    api::ObjectWriter overflow;
    static char rejected[support::kMaxResultPayloadBytes]{};
    std::memset(rejected, 'b', accepted_length + 1);
    rejected[accepted_length + 1] = '\0';
    assert(!overflow.addString("payload", rejected, accepted_length + 1));
    assert(std::strcmp(overflow.data(), "{") == 0);
}

int main() {
    testObjectWriterBasicShape();
    testBoardProcUsRejected();
    testCapacityIsEnvelopeReserved();

    std::puts("test_object_writer: ok");
    return 0;
}
