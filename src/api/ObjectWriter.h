#pragma once

#include "support/BoundedJsonWriter.h"
#include "support/Limits.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace teensy_command_server::api {

class ObjectWriter {
public:
    ObjectWriter() : writer_(buffer_, sizeof(buffer_)), closed_(false) {
        writer_.beginObject();
        writer_.reserveTailBytes(1);
    }

    bool addInt(const char* name, std::int32_t value) {
        return allowed(name) && writer_.addInt(name, value);
    }

    bool addUInt64(const char* name, std::uint64_t value) {
        return allowed(name) && writer_.addUInt64(name, value);
    }

    bool addFloat(const char* name, float value) {
        return allowed(name) && writer_.addDouble(name, static_cast<double>(value));
    }

    bool addDouble(const char* name, double value) {
        return allowed(name) && writer_.addDouble(name, value);
    }

    bool addBool(const char* name, bool value) {
        return allowed(name) && writer_.addBool(name, value);
    }

    bool addString(const char* name, const char* value) {
        return allowed(name) && writer_.addString(name, value);
    }

    bool addString(const char* name, const char* value, std::size_t length) {
        return allowed(name) && writer_.addString(name, value, length);
    }

    bool close() {
        if (closed_) {
            return true;
        }
        writer_.releaseTailReserve();
        closed_ = writer_.endObject();
        return closed_;
    }

    const char* data() const {
        return writer_.data();
    }

    std::size_t size() const {
        return writer_.size();
    }

    std::size_t capacity() const {
        return writer_.capacity();
    }

private:
    static bool isBoardProcUs(const char* name) {
        return name != nullptr && std::strcmp(name, "board_proc_us") == 0;
    }

    bool allowed(const char* name) const {
        return !closed_ && name != nullptr && !isBoardProcUs(name);
    }

    char buffer_[support::kMaxResultPayloadBytes];
    support::BoundedJsonWriter writer_;
    bool closed_;
};

}  // namespace teensy_command_server::api
