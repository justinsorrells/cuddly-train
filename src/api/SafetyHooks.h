#pragma once

namespace teensy_command_server::api {

// §19.1, §21, §31: idempotent hook, provisional 100 ms budget. Returns true
// only when board-local safe state was actually applied; false withholds ack.
using SafetyHook = bool (*)(void* context);

}  // namespace teensy_command_server::api
