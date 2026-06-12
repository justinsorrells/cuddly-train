#include "support/Limits.h"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace limits = teensy_command_server::support;

int main() {
    static_assert(limits::kBoardRxMaxLineBytes == 1024);
    static_assert(limits::kBoardTxMaxLineBytes == 8192);
    static_assert(limits::kTelemetryPeriodMs == 50);
    static_assert(limits::kLivenessWindowMs == 250);
    static_assert(limits::kTransmitDeadlineMs == 100);
    static_assert(limits::kTelemetryDeadlineFailuresToTeardown == 10);
    static_assert(limits::kHookBudgetMs == 100);
    static_assert(limits::kRegistrationWindowMs == 2000);

    static_assert(limits::kInboundLineBufferBytes >= limits::kBoardRxMaxLineBytes);
    static_assert(limits::kCommandJsonDocumentBytes >= limits::kBoardRxMaxLineBytes);
    static_assert(limits::kSchemaJsonBufferBytes >= limits::kBoardTxMaxLineBytes);
    static_assert(limits::kResponseJsonBufferBytes >= limits::kBoardTxMaxLineBytes);
    static_assert(limits::kTelemetryJsonBufferBytes >= limits::kBoardTxMaxLineBytes);
    static_assert(limits::kEventJsonBufferBytes >= limits::kBoardTxMaxLineBytes);
    static_assert(limits::kHeartbeatJsonBufferBytes >= limits::kBoardTxMaxLineBytes);
    static_assert(limits::kResponseEnvelopeReserveBytes > 0);
    static_assert(limits::kMaxResultPayloadBytes ==
                  limits::kBoardTxMaxLineBytes - limits::kResponseEnvelopeReserveBytes);
    static_assert(limits::kMaxResultPayloadBytes < limits::kBoardTxMaxLineBytes);
    static_assert(limits::kMaxErrorMessageBytes > 0);
    static_assert(limits::kMaxDoubleLiteralBytes >= 32);
    static_assert(limits::kMaxIntLiteralBytes >= 16);
    static_assert(limits::kMaxUInt64LiteralBytes >= 21);
    static_assert(limits::kMaxJsonNestingDepth > 0);

    static_assert(limits::kMaxRegisteredCommands > 0);
    static_assert(limits::kMaxArgsPerCommand > 0);

    static_assert(std::is_same<limits::Seq, std::uint64_t>::value);
    static_assert(std::numeric_limits<limits::Seq>::digits == 64);
    static_assert(std::is_same<limits::ContractInt, std::int32_t>::value);
    static_assert(std::numeric_limits<limits::ContractInt>::digits == 31);
    static_assert(std::numeric_limits<limits::ContractInt>::is_signed);

    std::puts("test_limits: ok");
    return 0;
}
