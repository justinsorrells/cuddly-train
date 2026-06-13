#include "core/LineFramer.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace core = teensy_command_server::core;

static std::string repeat(char ch, std::size_t count) {
    return std::string(count, ch);
}

static std::string acquireString(core::LineFramer& framer) {
    assert(framer.hasLine());
    core::MutableLineView view = framer.acquireLine();
    assert(view.valid());
    assert(view.data[view.size] == '\0');
    std::string out(view.data, view.size);
    framer.releaseLine();
    return out;
}

static void drainAvailable(core::LineFramer& framer, std::vector<std::string>& out) {
    while (framer.hasLine()) {
        out.push_back(acquireString(framer));
    }
}

static void appendAll(core::LineFramer& framer,
                      const std::string& bytes,
                      std::vector<std::string>& out) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t consumed = framer.append(bytes.data() + offset, bytes.size() - offset);
        if (consumed == 0) {
            assert(framer.hasLine());
            drainAvailable(framer, out);
            continue;
        }
        offset += consumed;
        drainAvailable(framer, out);
    }
}

static void exactByteBoundaries() {
    struct Case {
        const char* name;
        std::size_t payload_bytes;
        bool accepted;
        core::Counters::Value oversized_count;
    };

    const Case cases[] = {
        {"0 payload bytes, 1 total wire byte", 0, true, 0},
        {"1022 payload bytes, 1023 total wire bytes", 1022, true, 0},
        {"1023 payload bytes, 1024 total wire bytes", 1023, true, 0},
        {"1024 payload bytes, 1025 total wire bytes", 1024, false, 1},
    };

    for (const Case& test_case : cases) {
        core::Counters counters;
        core::LineFramer framer(counters);
        const std::string payload = repeat('x', test_case.payload_bytes);
        const std::string wire = payload + "\n";

        assert(framer.append(wire.data(), wire.size()) == wire.size());
        assert(counters.oversized_lines == test_case.oversized_count);
        assert(framer.hasLine() == test_case.accepted);
        if (test_case.accepted) {
            assert(acquireString(framer) == payload);
        }
        assert(!framer.hasLine());
        (void)test_case.name;
    }
}

static void fragmentedLineIsReassembled() {
    core::Counters counters;
    core::LineFramer framer(counters);

    assert(framer.append("ab", 2) == 2);
    assert(!framer.hasLine());
    assert(framer.append("c\n", 2) == 2);
    assert(acquireString(framer) == "abc");
    assert(counters.oversized_lines == 0);
}

static void multipleCompleteLinesInOneReadAreIndependent() {
    core::Counters counters;
    core::LineFramer framer(counters);
    std::vector<std::string> lines;

    appendAll(framer, "one\ntwo\nthree\n", lines);

    assert(lines.size() == 3);
    assert(lines[0] == "one");
    assert(lines[1] == "two");
    assert(lines[2] == "three");
    assert(counters.oversized_lines == 0);
}

static void completeThenPartialKeepsPartialForLater() {
    core::Counters counters;
    core::LineFramer framer(counters);

    assert(framer.append("ready\npar", 9) == 9);
    assert(acquireString(framer) == "ready");
    assert(!framer.hasLine());
    assert(framer.append("tial\n", 5) == 5);
    assert(acquireString(framer) == "partial");
}

static void closureDuringPartialDiscardsIt() {
    core::Counters counters;
    core::LineFramer framer(counters);

    assert(framer.append("partial", 7) == 7);
    assert(!framer.hasLine());
    framer.discardPartialLine();
    assert(framer.append("next\n", 5) == 5);
    assert(acquireString(framer) == "next");
}

static void oversizedThenValidInSameChunkRecovers() {
    core::Counters counters;
    core::LineFramer framer(counters);
    std::vector<std::string> lines;

    appendAll(framer, repeat('o', 1024) + "\nvalid\n", lines);

    assert(lines.size() == 1);
    assert(lines[0] == "valid");
    assert(counters.oversized_lines == 1);
}

static void oversizedDiscardSpansChunks() {
    core::Counters counters;
    core::LineFramer framer(counters);

    assert(framer.append(repeat('o', 1024).data(), 1024) == 1024);
    assert(counters.oversized_lines == 1);
    assert(!framer.hasLine());
    assert(framer.append("ignored\nok\n", 11) == 11);
    assert(acquireString(framer) == "ok");
}

static void acquireReleaseLifecycle() {
    core::Counters counters;
    core::LineFramer framer(counters);

    assert(framer.append("first\n", 6) == 6);
    core::MutableLineView first = framer.acquireLine();
    assert(first.valid());
    assert(first.size == 5);
    assert(std::memcmp(first.data, "first", 5) == 0);
    assert(!framer.hasLine());

    core::MutableLineView second_attempt = framer.acquireLine();
    assert(!second_attempt.valid());

    const std::string snapshot(first.data, first.size);
    assert(framer.append("second\nthird\n", 13) == 7);
    assert(std::string(first.data, first.size) == snapshot);
    assert(!framer.hasLine());

    framer.releaseLine();
    assert(framer.hasLine());
    assert(acquireString(framer) == "second");

    assert(framer.append("third\n", 6) == 6);
    assert(acquireString(framer) == "third");
}

static void closeSessionInvalidatesAcquiredAndBufferedState() {
    core::Counters counters;
    core::LineFramer framer(counters);

    assert(framer.append("held\n", 5) == 5);
    core::MutableLineView held = framer.acquireLine();
    assert(held.valid());
    assert(framer.append("pending\n", 8) == 8);

    framer.closeSession();
    assert(!framer.hasLine());
    assert(!framer.acquireLine().valid());
    assert(framer.append("fresh\n", 6) == 6);
    assert(acquireString(framer) == "fresh");
}

static void nullAppendConsumesNothing() {
    core::Counters counters;
    core::LineFramer framer(counters);

    assert(framer.append(static_cast<const char*>(nullptr), 4) == 0);
    assert(!framer.hasLine());
    assert(counters.oversized_lines == 0);
}

int main() {
    static_assert(core::LineFramer::kMaxWireLineBytes == 1024);
    static_assert(core::LineFramer::kMaxPayloadBytes == 1023);

    exactByteBoundaries();
    fragmentedLineIsReassembled();
    multipleCompleteLinesInOneReadAreIndependent();
    completeThenPartialKeepsPartialForLater();
    closureDuringPartialDiscardsIt();
    oversizedThenValidInSameChunkRecovers();
    oversizedDiscardSpansChunks();
    acquireReleaseLifecycle();
    closeSessionInvalidatesAcquiredAndBufferedState();
    nullAppendConsumesNothing();

    std::puts("test_line_framer: ok");
    return 0;
}
