#pragma once

#include "api/BoardIdentity.h"
#include "core/Counters.h"
#include "core/InboundParser.h"
#include "core/OutboundScheduler.h"
#include "core/Protocol.h"
#include "support/BoundedJsonWriter.h"
#include "support/Limits.h"

#include <cstddef>

namespace teensy_command_server::core {

class HeartbeatHandler {
public:
    bool handleHeartbeat(const ParsedHeartbeat& heartbeat,
                         Counters& counters,
                         const api::BoardIdentity& identity,
                         OutboundScheduler& scheduler) const {
        counters.increment(&Counters::heartbeat_received);
        char line[support::kHeartbeatJsonBufferBytes]{};
        std::size_t size = 0;
        if (!buildHeartbeatAckLine(heartbeat, identity, line, sizeof(line), size)) {
            return true;
        }
        const OutboundEnqueueResult result =
            scheduler.enqueueCritical(OutboundKind::HeartbeatAck, {line, size});
        return result != OutboundEnqueueResult::Queued;
    }

private:
    static bool buildHeartbeatAckLine(const ParsedHeartbeat& heartbeat,
                                      const api::BoardIdentity& identity,
                                      char* buffer,
                                      std::size_t capacity,
                                      std::size_t& size) {
        support::BoundedJsonWriter writer(buffer, capacity);
        writer.reserveTailBytes(1);
        if (!writer.beginObject() ||
            !writer.addString(field::kType, toString(MessageType::Heartbeat)) ||
            !writer.addUInt64(field::kSeq, heartbeat.seq) ||
            !writer.addString(field::kSource, identity.board_id) ||
            !writer.addString(field::kTarget, "controller") ||
            !writer.endObject()) {
            return false;
        }
        writer.releaseTailReserve();
        if (!writer.appendNewline() || writer.size() > support::kBoardTxMaxLineBytes) {
            return false;
        }
        size = writer.size();
        return true;
    }
};

}  // namespace teensy_command_server::core
