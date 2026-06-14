#pragma once

#include "api/CommandResult.h"
#include "api/CommandTypes.h"
#include "core/CommandRegistry.h"
#include "core/Counters.h"

#include <cstring>

namespace teensy_command_server::core {

class DiagnosticsCommand {
public:
    static constexpr const char* kName = "get_counters";

    static api::Status registerCommand(CommandRegistry& registry, Counters& counters) {
        return registry.registerCommand(
            {kName, nullptr, 0, false, true, handle, &counters});
    }

    static bool isDiagnosticsCommand(const char* name) {
        return name != nullptr && std::strcmp(name, kName) == 0;
    }

private:
    static api::CommandResult handle(const api::CommandContext&,
                                     api::ObjectWriter& result,
                                     void* context) {
        if (context == nullptr) {
            return api::CommandResult::internalError("counters context is required");
        }
        const Counters& snapshot = static_cast<const Counters*>(context)->snapshot();
        if (!writeSnapshot(snapshot, result)) {
            return api::CommandResult::internalError("counter snapshot exceeds fixed capacity");
        }
        return api::CommandResult::ok();
    }

    static bool writeSnapshot(const Counters& counters, api::ObjectWriter& result) {
        return result.addUInt64("sessions_accepted", counters.sessions_accepted) &&
               result.addUInt64("sessions_rejected", counters.sessions_rejected) &&
               result.addUInt64("sessions_superseded", counters.sessions_superseded) &&
               result.addUInt64("schemas_sent", counters.schemas_sent) &&
               result.addUInt64("commands_received", counters.commands_received) &&
               result.addUInt64("commands_ok", counters.commands_ok) &&
               result.addUInt64("commands_error", counters.commands_error) &&
               result.addUInt64("unknown_commands", counters.unknown_commands) &&
               result.addUInt64("invalid_arguments", counters.invalid_arguments) &&
               result.addUInt64("invalid_json", counters.invalid_json) &&
               result.addUInt64("invalid_targets", counters.invalid_targets) &&
               result.addUInt64("oversized_lines", counters.oversized_lines) &&
               result.addUInt64("telemetry_sent", counters.telemetry_sent) &&
               result.addUInt64("telemetry_coalesced", counters.telemetry_coalesced) &&
               result.addUInt64("telemetry_dropped", counters.telemetry_dropped) &&
               result.addUInt64("estop_received", counters.estop_received) &&
               result.addUInt64("estop_ack_sent", counters.estop_ack_sent) &&
               result.addUInt64("estop_apply_failed", counters.estop_apply_failed) &&
               result.addUInt64("estop_hook_over_budget", counters.estop_hook_over_budget) &&
               result.addUInt64("controller_loss_hook_over_budget",
                                counters.controller_loss_hook_over_budget) &&
               result.addUInt64("heartbeat_received", counters.heartbeat_received) &&
               result.addUInt64("heartbeat_ack_sent", counters.heartbeat_ack_sent) &&
               result.addUInt64("tx_failures", counters.tx_failures) &&
               result.addUInt64("controller_disconnects", counters.controller_disconnects);
    }
};

}  // namespace teensy_command_server::core
