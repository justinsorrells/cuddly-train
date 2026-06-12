#pragma once

#include <cstdint>

namespace teensy_command_server::core {

struct Counters {
    using Value = std::uint32_t;
    using Field = Value Counters::*;

    Value sessions_accepted = 0;
    Value sessions_rejected = 0;
    Value sessions_superseded = 0;
    Value schemas_sent = 0;
    Value commands_received = 0;
    Value commands_ok = 0;
    Value commands_error = 0;
    Value unknown_commands = 0;
    Value invalid_arguments = 0;
    Value invalid_json = 0;
    Value invalid_targets = 0;
    Value oversized_lines = 0;
    Value telemetry_sent = 0;
    Value telemetry_coalesced = 0;
    Value telemetry_dropped = 0;
    Value estop_received = 0;
    Value estop_ack_sent = 0;
    Value estop_apply_failed = 0;
    Value estop_hook_over_budget = 0;
    Value controller_loss_hook_over_budget = 0;
    Value heartbeat_received = 0;
    Value heartbeat_ack_sent = 0;
    Value tx_failures = 0;
    Value controller_disconnects = 0;

    // §25: service loop is single-threaded; unsigned wraparound is acceptable.
    void increment(Field field) {
        ++(this->*field);
    }

    const Counters& snapshot() const {
        return *this;
    }
};

}  // namespace teensy_command_server::core
