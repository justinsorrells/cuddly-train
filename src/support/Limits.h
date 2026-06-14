#pragma once

#include <cstddef>
#include <cstdint>

namespace teensy_command_server::support {

// §10.1-§10.2: controller-to-board line limit, including the terminating '\n'.
constexpr std::size_t kBoardRxMaxLineBytes = 1024;

// §10.1, §10.3: board-to-controller line limit, including the terminating '\n'.
constexpr std::size_t kBoardTxMaxLineBytes = 8192;

// §11.1: inbound accumulator capacity includes implementation terminator slack.
constexpr std::size_t kInboundLineBufferBytes = kBoardRxMaxLineBytes + 1;

// §11.1: outbound serializers are sized against the board-to-controller limit.
constexpr std::size_t kSchemaJsonBufferBytes = kBoardTxMaxLineBytes;
constexpr std::size_t kResponseJsonBufferBytes = kBoardTxMaxLineBytes;
constexpr std::size_t kTelemetryJsonBufferBytes = kBoardTxMaxLineBytes;
constexpr std::size_t kEventJsonBufferBytes = kBoardTxMaxLineBytes;
constexpr std::size_t kHeartbeatJsonBufferBytes = kBoardTxMaxLineBytes;

// §13.4 outbound scheduler: four queued critical messages plus one replaceable
// telemetry slot. Each critical entry owns a full board-to-controller line.
constexpr std::size_t kOutboundCriticalQueueCapacity = 4;
constexpr std::size_t kOutboundCancellationBatchCapacity =
    kOutboundCriticalQueueCapacity + 1;
constexpr std::size_t kOutboundCriticalEntryMaxBytes = kBoardTxMaxLineBytes;
constexpr std::size_t kOutboundSchedulerMaxBytes = 48 * 1024;

// ObjectWriter payload is intentionally smaller than the full response line so
// §16 envelope fields and library-owned board_proc_us always fit.
constexpr std::size_t kResponseEnvelopeReserveBytes = 512;
constexpr std::size_t kMaxResultPayloadBytes =
    kBoardTxMaxLineBytes - kResponseEnvelopeReserveBytes;
constexpr std::size_t kMaxErrorMessageBytes = 160;
constexpr std::size_t kMaxDoubleLiteralBytes = 32;
constexpr std::size_t kMaxIntLiteralBytes = 16;
constexpr std::size_t kMaxUInt64LiteralBytes = 24;
constexpr std::size_t kMaxJsonNestingDepth = 8;

// §18: telemetry is pushed at a nominal 50 ms period.
constexpr std::uint32_t kTelemetryPeriodMs = 50;

// §18.6: controller-side liveness fact documented here for deterministic tests.
constexpr std::uint32_t kLivenessWindowMs = 250;

// §13.6, §31: provisional until hardware validation; architecture is fixed.
constexpr std::uint32_t kTransmitDeadlineMs = 100;

// §13.6, §31: provisional until hardware validation; architecture is fixed.
constexpr std::uint8_t kTelemetryDeadlineFailuresToTeardown = 10;

// §19.1, §21, §31: provisional hook budget for e-stop and controller loss.
constexpr std::uint32_t kHookBudgetMs = 100;

// §8.4: controller registration window; informational for schema-first tests.
constexpr std::uint32_t kRegistrationWindowMs = 2000;

// §26.1: local network initialization retry interval; internal, not API.
constexpr std::uint32_t kNetworkInitRetryMs = 1000;

// §5.1, §14: registry is immutable after startup; full schema must fit 8192 bytes.
constexpr std::size_t kMaxRegisteredCommands = 16;

// §6.2, §14: V1 args are small and flat; full schema must fit 8192 bytes.
constexpr std::size_t kMaxArgsPerCommand = 8;

// §11.1: parser capacity is sized from the permitted inbound message shapes,
// not from the wire byte limit: command root object slots plus one args object
// and kMaxArgsPerCommand flat argument slots; estop/heartbeat are smaller.
constexpr std::size_t kCommandJsonRootSlots = 7;
constexpr std::size_t kCommandJsonArgsObjectSlots = 1;
constexpr std::size_t kJsonDocumentSlotBytes = 64;
constexpr std::size_t kJsonDocumentBaseBytes = 64;
constexpr std::size_t kCommandJsonDocumentBytes =
    kJsonDocumentBaseBytes +
    (kCommandJsonRootSlots + kCommandJsonArgsObjectSlots + kMaxArgsPerCommand) *
        kJsonDocumentSlotBytes;

// §3, §6.1, §14: registration metadata is copied into fixed-capacity storage.
constexpr std::size_t kMaxBoardIdBytes = 48;
constexpr std::size_t kMaxProtocolVersionBytes = 8;
constexpr std::size_t kMaxFirmwareVersionBytes = 32;
constexpr std::size_t kMaxCommandNameBytes = 48;
constexpr std::size_t kMaxArgNameBytes = 48;
constexpr std::size_t kMaxFieldNameBytes = 48;
constexpr std::size_t kMaxTelemetryFields = 24;
constexpr std::size_t kMaxStateFields = 24;

// §15, §16.1, §18.3: all protocol seq values use uint64_t.
using Seq = std::uint64_t;

// §6.2: contract "int" arguments are signed 32-bit values.
using ContractInt = std::int32_t;

}  // namespace teensy_command_server::support
