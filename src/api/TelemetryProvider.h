#pragma once

#include "api/ObjectWriter.h"

namespace teensy_command_server::api {

// §18.2: fast latest-snapshot copy only; no network work or sensor waits.
using TelemetryProvider = bool (*)(ObjectWriter& telemetry, void* context);

}  // namespace teensy_command_server::api
