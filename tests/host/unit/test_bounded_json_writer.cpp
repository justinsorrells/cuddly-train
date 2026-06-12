#include "support/BoundedJsonWriter.h"
#include "support/Limits.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace support = teensy_command_server::support;

static void assertBufferEquals(const support::BoundedJsonWriter& writer, const char* expected) {
    assert(writer.size() == std::strlen(expected));
    assert(std::memcmp(writer.data(), expected, writer.size()) == 0);
}

static void testCompactEscapingAndByteAccounting() {
    char buffer[256]{};
    support::BoundedJsonWriter writer(buffer, sizeof(buffer));

    assert(writer.beginObject());
    assert(writer.addInt("rpm", -1200));
    assert(writer.addBool("enabled", true));
    assert(writer.addString("escaped", "quote\" slash\\\n\001"));
    assert(writer.endObject());

    const char* expected =
        "{\"rpm\":-1200,\"enabled\":true,\"escaped\":\"quote\\\" slash\\\\\\n\\u0001\"}";
    assertBufferEquals(writer, expected);
    assert(std::strchr(writer.data(), ' ') != nullptr);
    assert(std::strstr(writer.data(), ": ") == nullptr);
    assert(std::strstr(writer.data(), ", ") == nullptr);
}

static void testOverflowLeavesPriorContentValid() {
    char buffer[18]{};
    support::BoundedJsonWriter writer(buffer, sizeof(buffer));

    assert(writer.beginObject());
    assert(writer.addInt("a", 1));
    assertBufferEquals(writer, "{\"a\":1");
    assert(!writer.addString("b", "too large"));
    assertBufferEquals(writer, "{\"a\":1");
    assert(writer.endObject());
    assertBufferEquals(writer, "{\"a\":1}");
}

static void assertRoundTrips(double value) {
    char buffer[128]{};
    support::BoundedJsonWriter writer(buffer, sizeof(buffer));

    assert(writer.beginObject());
    assert(writer.addDouble("v", value));
    assert(writer.endObject());

    const char* prefix = "{\"v\":";
    assert(std::memcmp(writer.data(), prefix, std::strlen(prefix)) == 0);
    const char* literal = writer.data() + std::strlen(prefix);
    char* end = nullptr;
    const double parsed = std::strtod(literal, &end);
    assert(end != literal);
    assert(*end == '}');
    assert(parsed == value);
}

static void testDoublePolicy() {
    assert(DBL_DECIMAL_DIG == 17);
    assertRoundTrips(1718210123.4567891);
    assertRoundTrips(1.0);
    assertRoundTrips(-12345.678901234567);
    assertRoundTrips(std::numeric_limits<double>::denorm_min());
    assertRoundTrips(std::numeric_limits<double>::max());

    char buffer[128]{};
    support::BoundedJsonWriter writer(buffer, sizeof(buffer));
    assert(writer.beginObject());
    assert(!writer.addDouble("nan", std::numeric_limits<double>::quiet_NaN()));
    assert(!writer.addDouble("inf", std::numeric_limits<double>::infinity()));
    assert(!writer.addDouble("ninf", -std::numeric_limits<double>::infinity()));
    assertBufferEquals(writer, "{");
}

int main() {
    testCompactEscapingAndByteAccounting();
    testOverflowLeavesPriorContentValid();
    testDoublePolicy();

    std::puts("test_bounded_json_writer: ok");
    return 0;
}
