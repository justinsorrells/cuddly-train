#pragma once

#include "support/Limits.h"

#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace teensy_command_server::support {

class BoundedJsonWriter {
public:
    BoundedJsonWriter(char* buffer, std::size_t capacity)
        : buffer_(buffer), capacity_(capacity) {
        reset();
    }

    void reset() {
        length_ = 0;
        depth_ = 0;
        tail_reserve_ = 0;
        failed_ = false;
        closed_ = false;
        if (capacity_ > 0) {
            buffer_[0] = '\0';
        }
        for (std::size_t i = 0; i < kMaxJsonNestingDepth; ++i) {
            first_[i] = true;
        }
    }

    bool beginObject() {
        if (closed_ || depth_ >= kMaxJsonNestingDepth) {
            failed_ = true;
            return false;
        }
        const Snapshot snapshot = capture();
        if (!appendByte('{')) {
            restore(snapshot);
            return false;
        }
        first_[depth_] = true;
        ++depth_;
        return true;
    }

    bool beginObjectField(const char* name) {
        if (name == nullptr || !inObject() || depth_ >= kMaxJsonNestingDepth) {
            failed_ = true;
            return false;
        }
        const Snapshot snapshot = capture();
        if (!writeFieldPrefix(name) || !appendByte('{')) {
            restore(snapshot);
            return false;
        }
        first_[depth_] = true;
        ++depth_;
        return true;
    }

    bool endObject() {
        if (depth_ == 0) {
            failed_ = true;
            return false;
        }
        const Snapshot snapshot = capture();
        if (!appendByte('}')) {
            restore(snapshot);
            return false;
        }
        --depth_;
        closed_ = (depth_ == 0);
        return true;
    }

    bool addInt(const char* name, std::int32_t value) {
        char literal[kMaxIntLiteralBytes];
        const int written = std::snprintf(literal, sizeof(literal), "%ld",
                                          static_cast<long>(value));
        return written > 0 && addNumberLiteral(name, literal, static_cast<std::size_t>(written));
    }

    bool addUInt64(const char* name, std::uint64_t value) {
        char literal[kMaxUInt64LiteralBytes];
        const int written = std::snprintf(literal, sizeof(literal), "%llu",
                                          static_cast<unsigned long long>(value));
        return written > 0 && addNumberLiteral(name, literal, static_cast<std::size_t>(written));
    }

    bool addDouble(const char* name, double value) {
        if (!isFinite(value)) {
            failed_ = true;
            return false;
        }
        char literal[kMaxDoubleLiteralBytes];
        const int written = std::snprintf(literal, sizeof(literal), "%.*g",
                                          DBL_DECIMAL_DIG, value);
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(literal)) {
            failed_ = true;
            return false;
        }
        return addNumberLiteral(name, literal, static_cast<std::size_t>(written));
    }

    bool addBool(const char* name, bool value) {
        return addLiteral(name, value ? "true" : "false");
    }

    bool addNull(const char* name) {
        return addLiteral(name, "null");
    }

    bool addString(const char* name, const char* value) {
        return value != nullptr && addString(name, value, std::strlen(value));
    }

    bool addString(const char* name, const char* value, std::size_t length) {
        if (name == nullptr || value == nullptr || !inObject()) {
            failed_ = true;
            return false;
        }
        const Snapshot snapshot = capture();
        if (!writeFieldPrefix(name) || !writeEscapedString(value, length)) {
            restore(snapshot);
            return false;
        }
        return true;
    }

    bool appendNewline() {
        return appendByte('\n');
    }

    bool reserveTailBytes(std::size_t bytes) {
        if (bytes > capacity_ || length_ > capacity_ - bytes) {
            failed_ = true;
            return false;
        }
        tail_reserve_ = bytes;
        return true;
    }

    void releaseTailReserve() {
        tail_reserve_ = 0;
    }

    const char* data() const {
        return buffer_;
    }

    std::size_t size() const {
        return length_;
    }

    std::size_t capacity() const {
        return capacity_;
    }

    std::size_t remaining() const {
        if (capacity_ <= tail_reserve_ || length_ >= capacity_ - tail_reserve_) {
            return 0;
        }
        return capacity_ - tail_reserve_ - length_;
    }

    bool failed() const {
        return failed_;
    }

private:
    struct Snapshot {
        std::size_t length;
        std::size_t depth;
        std::size_t tail_reserve;
        bool closed;
        bool first[kMaxJsonNestingDepth];
    };

    static bool isFinite(double value) {
        return value <= DBL_MAX && value >= -DBL_MAX;
    }

    bool inObject() const {
        return depth_ > 0 && !closed_;
    }

    Snapshot capture() const {
        Snapshot snapshot{length_, depth_, tail_reserve_, closed_, {}};
        for (std::size_t i = 0; i < kMaxJsonNestingDepth; ++i) {
            snapshot.first[i] = first_[i];
        }
        return snapshot;
    }

    void restore(const Snapshot& snapshot) {
        length_ = snapshot.length;
        depth_ = snapshot.depth;
        tail_reserve_ = snapshot.tail_reserve;
        closed_ = snapshot.closed;
        for (std::size_t i = 0; i < kMaxJsonNestingDepth; ++i) {
            first_[i] = snapshot.first[i];
        }
        terminate();
        failed_ = true;
    }

    bool addLiteral(const char* name, const char* literal) {
        return addNumberLiteral(name, literal, std::strlen(literal));
    }

    bool addNumberLiteral(const char* name, const char* literal, std::size_t length) {
        if (name == nullptr || literal == nullptr || !inObject()) {
            failed_ = true;
            return false;
        }
        const Snapshot snapshot = capture();
        if (!writeFieldPrefix(name) || !appendBytes(literal, length)) {
            restore(snapshot);
            return false;
        }
        return true;
    }

    bool writeFieldPrefix(const char* name) {
        if (!first_[depth_ - 1] && !appendByte(',')) {
            return false;
        }
        if (!writeEscapedString(name, std::strlen(name)) || !appendByte(':')) {
            return false;
        }
        first_[depth_ - 1] = false;
        return true;
    }

    bool writeEscapedString(const char* value, std::size_t length) {
        if (!appendByte('"')) {
            return false;
        }
        for (std::size_t i = 0; i < length; ++i) {
            const unsigned char c = static_cast<unsigned char>(value[i]);
            switch (c) {
                case '"':
                    if (!appendBytes("\\\"", 2)) {
                        return false;
                    }
                    break;
                case '\\':
                    if (!appendBytes("\\\\", 2)) {
                        return false;
                    }
                    break;
                case '\b':
                    if (!appendBytes("\\b", 2)) {
                        return false;
                    }
                    break;
                case '\f':
                    if (!appendBytes("\\f", 2)) {
                        return false;
                    }
                    break;
                case '\n':
                    if (!appendBytes("\\n", 2)) {
                        return false;
                    }
                    break;
                case '\r':
                    if (!appendBytes("\\r", 2)) {
                        return false;
                    }
                    break;
                case '\t':
                    if (!appendBytes("\\t", 2)) {
                        return false;
                    }
                    break;
                default:
                    if (c < 0x20) {
                        char escaped[7] = {'\\', 'u', '0', '0', hex(c >> 4), hex(c & 0x0F), '\0'};
                        if (!appendBytes(escaped, 6)) {
                            return false;
                        }
                    } else if (!appendByte(static_cast<char>(c))) {
                        return false;
                    }
                    break;
            }
        }
        return appendByte('"');
    }

    static char hex(unsigned char value) {
        return value < 10 ? static_cast<char>('0' + value)
                          : static_cast<char>('A' + (value - 10));
    }

    bool appendByte(char value) {
        return appendBytes(&value, 1);
    }

    bool appendBytes(const char* value, std::size_t length) {
        if (value == nullptr || length > remaining()) {
            failed_ = true;
            return false;
        }
        std::memcpy(buffer_ + length_, value, length);
        length_ += length;
        terminate();
        return true;
    }

    void terminate() {
        if (capacity_ > 0 && length_ < capacity_) {
            buffer_[length_] = '\0';
        }
    }

    char* buffer_;
    std::size_t capacity_;
    std::size_t length_;
    std::size_t depth_;
    std::size_t tail_reserve_;
    bool failed_;
    bool closed_;
    bool first_[kMaxJsonNestingDepth];
};

}  // namespace teensy_command_server::support
