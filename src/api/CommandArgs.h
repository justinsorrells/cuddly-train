#pragma once

#include "support/Limits.h"

#include <cstddef>
#include <cstdint>

namespace teensy_command_server::api {

class CommandArgs {
public:
    using HasFn = bool (*)(const void* backing, const char* name);
    using GetIntFn = bool (*)(const void* backing, const char* name, std::int32_t& out);
    using GetFloatFn = bool (*)(const void* backing, const char* name, float& out);
    using GetBoolFn = bool (*)(const void* backing, const char* name, bool& out);
    using GetStringFn = bool (*)(const void* backing, const char* name, const char*& out,
                                 std::size_t& length);

    constexpr CommandArgs()
        : backing_(nullptr),
          has_fn_(nullptr),
          get_int_fn_(nullptr),
          get_float_fn_(nullptr),
          get_bool_fn_(nullptr),
          get_string_fn_(nullptr) {}

    constexpr CommandArgs(const void* backing,
                          HasFn has_fn,
                          GetIntFn get_int_fn,
                          GetFloatFn get_float_fn,
                          GetBoolFn get_bool_fn,
                          GetStringFn get_string_fn)
        : backing_(backing),
          has_fn_(has_fn),
          get_int_fn_(get_int_fn),
          get_float_fn_(get_float_fn),
          get_bool_fn_(get_bool_fn),
          get_string_fn_(get_string_fn) {}

    bool has(const char* name) const {
        return has_fn_ != nullptr && has_fn_(backing_, name);
    }

    bool getInt(const char* name, std::int32_t& out) const {
        return get_int_fn_ != nullptr && get_int_fn_(backing_, name, out);
    }

    bool getFloat(const char* name, float& out) const {
        return get_float_fn_ != nullptr && get_float_fn_(backing_, name, out);
    }

    bool getBool(const char* name, bool& out) const {
        return get_bool_fn_ != nullptr && get_bool_fn_(backing_, name, out);
    }

    bool getString(const char* name, const char*& out, std::size_t& length) const {
        return get_string_fn_ != nullptr && get_string_fn_(backing_, name, out, length);
    }

private:
    const void* backing_;
    HasFn has_fn_;
    GetIntFn get_int_fn_;
    GetFloatFn get_float_fn_;
    GetBoolFn get_bool_fn_;
    GetStringFn get_string_fn_;
};

struct CommandContext {
    support::Seq seq;
    const CommandArgs& args;
};

}  // namespace teensy_command_server::api
