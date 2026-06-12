#include "core/Counters.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
#include <type_traits>

namespace counters = teensy_command_server::core;

using CounterField = counters::Counters::Field;

constexpr std::array<CounterField, 24> kAllCounterFields = {
    &counters::Counters::sessions_accepted,
    &counters::Counters::sessions_rejected,
    &counters::Counters::sessions_superseded,
    &counters::Counters::schemas_sent,
    &counters::Counters::commands_received,
    &counters::Counters::commands_ok,
    &counters::Counters::commands_error,
    &counters::Counters::unknown_commands,
    &counters::Counters::invalid_arguments,
    &counters::Counters::invalid_json,
    &counters::Counters::invalid_targets,
    &counters::Counters::oversized_lines,
    &counters::Counters::telemetry_sent,
    &counters::Counters::telemetry_coalesced,
    &counters::Counters::telemetry_dropped,
    &counters::Counters::estop_received,
    &counters::Counters::estop_ack_sent,
    &counters::Counters::estop_apply_failed,
    &counters::Counters::estop_hook_over_budget,
    &counters::Counters::controller_loss_hook_over_budget,
    &counters::Counters::heartbeat_received,
    &counters::Counters::heartbeat_ack_sent,
    &counters::Counters::tx_failures,
    &counters::Counters::controller_disconnects,
};

void assertAllCountersEqual(const counters::Counters& value, counters::Counters::Value expected) {
    for (CounterField field : kAllCounterFields) {
        assert(value.*field == expected);
    }
}

int main() {
    static_assert(kAllCounterFields.size() == 24);
    static_assert(std::is_unsigned<counters::Counters::Value>::value);
    static_assert(std::is_same<counters::Counters::Value, std::uint32_t>::value);

    counters::Counters values;
    assertAllCountersEqual(values.snapshot(), 0);

    for (CounterField field : kAllCounterFields) {
        values.increment(field);
    }

    const counters::Counters& snapshot = values.snapshot();
    assertAllCountersEqual(snapshot, 1);

    values.sessions_accepted = std::numeric_limits<counters::Counters::Value>::max();
    values.increment(&counters::Counters::sessions_accepted);
    assert(values.snapshot().sessions_accepted == 0);

    std::puts("test_counters: ok");
    return 0;
}
