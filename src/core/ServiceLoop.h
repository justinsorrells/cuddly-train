#pragma once

#include "core/Clock.h"
#include "core/Counters.h"
#include "core/InboundParser.h"
#include "core/OutboundScheduler.h"
#include "core/Protocol.h"

#ifndef ARDUINOJSON_USE_LONG_LONG
#define ARDUINOJSON_USE_LONG_LONG 1
#endif
#ifndef ARDUINOJSON_ENABLE_STD_STRING
#define ARDUINOJSON_ENABLE_STD_STRING 0
#endif
#include "../../third_party/ArduinoJson/ArduinoJson-v6.21.5.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace teensy_command_server::core {

struct RecoverableCommandError {
    support::Seq seq = 0;
    double controller_ts = 0.0;
    BoardErrorCode code = BoardErrorCode::InternalError;
};

class ServiceLoop {
public:
    struct Routes {
        bool (*command)(const ParsedCommand& command, void* context) = nullptr;
        void (*recoverable_command_error)(const RecoverableCommandError& error,
                                          void* context) = nullptr;
        void (*estop)(const ParsedEstop& estop, void* context) = nullptr;
        void (*heartbeat)(const ParsedHeartbeat& heartbeat, void* context) = nullptr;
        void (*telemetry_due)(OutboundScheduler& scheduler, void* context) = nullptr;
        void (*outbound_outcome)(const OutboundOutcome& outcome, void* context) = nullptr;
        void* context = nullptr;
        bool (*command_with_timing)(const ParsedCommand& command,
                                    std::uint64_t parse_completed_us,
                                    void* context) = nullptr;
        void (*telemetry_inactive)(void* context) = nullptr;
    };

    ServiceLoop(Counters& counters,
                const api::BoardIdentity& identity,
                OutboundScheduler& scheduler)
        : ServiceLoop(counters, identity, scheduler, Routes{}) {}

    ServiceLoop(Counters& counters,
                const api::BoardIdentity& identity,
                OutboundScheduler& scheduler,
                const Clock& clock)
        : ServiceLoop(counters, identity, scheduler, Routes{}, &clock) {}

    ServiceLoop(Counters& counters,
                const api::BoardIdentity& identity,
                OutboundScheduler& scheduler,
                Routes routes)
        : ServiceLoop(counters, identity, scheduler, routes, nullptr) {}

    ServiceLoop(Counters& counters,
                const api::BoardIdentity& identity,
                OutboundScheduler& scheduler,
                Routes routes,
                const Clock& clock)
        : ServiceLoop(counters, identity, scheduler, routes, &clock) {}

    ServiceLoop(Counters& counters,
                const api::BoardIdentity& identity,
                OutboundScheduler& scheduler,
                Routes routes,
                const Clock* clock)
        : counters_(counters),
          identity_(identity),
          scheduler_(scheduler),
          parser_(counters),
          routes_(routes),
          clock_(clock) {}

    template <typename SessionDriverLike>
    void service(SessionDriverLike& session) {
        session.poll();

        MutableLineView line{};
        const bool acquired = session.nextLine(line);
        if (session.teardownPending()) {
            if (acquired) {
                session.releaseLine();
            }
            cancelRouteAndApply(session);
            return;
        }

        runTelemetryDueCheck(session);

        bool drained_outbound = false;
        if (scheduler_.hasPending()) {
            if (drainRouteAndMaybeTeardown(session, acquired)) {
                return;
            }
            drained_outbound = true;
        }

        if (acquired) {
            processLine(line, session);
            session.releaseLine();

            if (session.teardownPending()) {
                cancelRouteAndApply(session);
                return;
            }

            if (!drained_outbound && scheduler_.hasPending()) {
                (void)drainRouteAndMaybeTeardown(session, false);
            }
        }
    }

private:
    struct SourceTarget {
        char source[support::kMaxBoardIdBytes + 1]{};
        char target[support::kMaxBoardIdBytes + 1]{};
        bool valid = false;
    };

    template <typename SessionDriverLike>
    void processLine(MutableLineView line, SessionDriverLike& session) {
        const SourceTarget root_gate = extractSourceTarget(line);
        const ParseOutcome outcome = parser_.parse(line);
        const std::uint64_t parse_completed_us =
            clock_ == nullptr ? 0 : clock_->monotonicMicroseconds();
        if (outcome.kind == ParseOutcomeKind::Valid) {
            routeValid(outcome, root_gate, parse_completed_us, session);
            return;
        }

        if (outcome.kind == ParseOutcomeKind::StructurallyInvalid &&
            outcome.message_kind == InboundMessageKind::Command && outcome.has_seq &&
            outcome.has_controller_ts && outcome.has_suggested_error) {
            if (root_gate.valid && !validControllerTarget(root_gate)) {
                counters_.increment(&Counters::invalid_targets);
                return;
            }
            routeRecoverableCommandError({outcome.seq, outcome.controller_ts,
                                          outcome.suggested_error});
        }
    }

    template <typename SessionDriverLike>
    void routeValid(const ParseOutcome& outcome,
                    const SourceTarget& root_gate,
                    std::uint64_t parse_completed_us,
                    SessionDriverLike& session) {
        if (outcome.message_kind == InboundMessageKind::Command) {
            if (!validControllerTarget(root_gate)) {
                counters_.increment(&Counters::invalid_targets);
                return;
            }
            bool teardown_requested = true;
            if (routes_.command_with_timing != nullptr) {
                teardown_requested =
                    routes_.command_with_timing(outcome.command, parse_completed_us,
                                                routes_.context);
            } else if (routes_.command != nullptr) {
                teardown_requested = routes_.command(outcome.command, routes_.context);
            }
            if (teardown_requested) {
                session.requestTeardown(TeardownReason::CriticalTransmitFailure);
            }
            return;
        }

        if (outcome.message_kind == InboundMessageKind::Estop) {
            if (!validControllerTarget(outcome.estop.source, outcome.estop.target)) {
                counters_.increment(&Counters::invalid_targets);
                return;
            }
            if (routes_.estop != nullptr) {
                routes_.estop(outcome.estop, routes_.context);
            } else {
                counters_.increment(&Counters::estop_apply_failed);
            }
            return;
        }

        if (outcome.message_kind == InboundMessageKind::Heartbeat) {
            if (!validControllerTarget(outcome.heartbeat.source, outcome.heartbeat.target)) {
                counters_.increment(&Counters::invalid_targets);
                return;
            }
            if (routes_.heartbeat != nullptr) {
                routes_.heartbeat(outcome.heartbeat, routes_.context);
            }
        }
    }

    template <typename SessionDriverLike>
    void runTelemetryDueCheck(const SessionDriverLike& session) {
        if (telemetrySessionActive(session)) {
            if (routes_.telemetry_due != nullptr) {
                routes_.telemetry_due(scheduler_, routes_.context);
            }
            return;
        }
        if (routes_.telemetry_inactive != nullptr) {
            routes_.telemetry_inactive(routes_.context);
        }
    }

    template <typename SessionDriverLike>
    static auto telemetrySessionActiveImpl(const SessionDriverLike& session, int)
        -> decltype(session.sessionActive(), bool()) {
        return session.sessionActive() && !session.teardownPending();
    }

    template <typename SessionDriverLike>
    static bool telemetrySessionActiveImpl(const SessionDriverLike& session, long) {
        return !session.teardownPending();
    }

    template <typename SessionDriverLike>
    static bool telemetrySessionActive(const SessionDriverLike& session) {
        return telemetrySessionActiveImpl(session, 0);
    }

    void routeRecoverableCommandError(const RecoverableCommandError& error) {
        if (routes_.recoverable_command_error != nullptr) {
            routes_.recoverable_command_error(error, routes_.context);
        }
    }

    void routeOutcome(const OutboundOutcome& outcome) {
        if (outcome.completion == OutboundCompletion::Sent) {
            if (outcome.kind == OutboundKind::Telemetry) {
                counters_.increment(&Counters::telemetry_sent);
            } else if (outcome.kind == OutboundKind::EstopAck) {
                counters_.increment(&Counters::estop_ack_sent);
            } else if (outcome.kind == OutboundKind::HeartbeatAck) {
                counters_.increment(&Counters::heartbeat_ack_sent);
            }
        }
        if (routes_.outbound_outcome != nullptr) {
            routes_.outbound_outcome(outcome, routes_.context);
        }
    }

    template <typename SessionDriverLike>
    bool drainRouteAndMaybeTeardown(SessionDriverLike& session, bool line_acquired) {
        OutboundOutcome outcome{};
        if (!scheduler_.drainOne(session, outcome)) {
            return false;
        }
        routeOutcome(outcome);
        if (session.teardownPending() ||
            (outcome.completion == OutboundCompletion::SendFailed &&
             outcome.kind != OutboundKind::Telemetry)) {
            if (!session.teardownPending()) {
                session.requestTeardown(TeardownReason::CriticalTransmitFailure);
            }
            if (line_acquired) {
                session.releaseLine();
            }
            cancelRouteAndApply(session);
            return true;
        }
        return false;
    }

    template <typename SessionDriverLike>
    void cancelRouteAndApply(SessionDriverLike& session) {
        const OutboundCancellationBatch batch = scheduler_.clearForSessionEnd();
        for (std::size_t i = 0; i < batch.count(); ++i) {
            routeOutcome(batch.at(i));
        }
        session.applyPendingTeardown();
    }

    bool validControllerTarget(const SourceTarget& gate) const {
        return gate.valid && identity_.board_id != nullptr &&
               std::strcmp(gate.source, "controller") == 0 &&
               std::strcmp(gate.target, identity_.board_id) == 0;
    }

    bool validControllerTarget(const char* source, const char* target) const {
        return source != nullptr && target != nullptr && identity_.board_id != nullptr &&
               std::strcmp(source, "controller") == 0 &&
               std::strcmp(target, identity_.board_id) == 0;
    }

    static SourceTarget extractSourceTarget(MutableLineView line) {
        SourceTarget result;
        if (!line.valid()) {
            return result;
        }
        ArduinoJson::StaticJsonDocument<support::kCommandJsonDocumentBytes> document;
        if (ArduinoJson::deserializeJson(document, static_cast<const char*>(line.data), line.size,
                                         ArduinoJson::DeserializationOption::NestingLimit(
                                             support::kMaxJsonNestingDepth)) ||
            !document.is<ArduinoJson::JsonObject>()) {
            return result;
        }
        ArduinoJson::JsonObject root = document.as<ArduinoJson::JsonObject>();
        if (!root[field::kSource].is<const char*>() || !root[field::kTarget].is<const char*>()) {
            return result;
        }
        const char* source = root[field::kSource].as<const char*>();
        const char* target = root[field::kTarget].as<const char*>();
        result.valid = copyBounded(source, result.source, support::kMaxBoardIdBytes) &&
                       copyBounded(target, result.target, support::kMaxBoardIdBytes);
        return result;
    }

    static bool copyBounded(const char* source, char* destination, std::size_t capacity) {
        if (source == nullptr || destination == nullptr) {
            return false;
        }
        std::size_t length = 0;
        while (length <= capacity && source[length] != '\0') {
            ++length;
        }
        if (length > capacity) {
            destination[0] = '\0';
            return false;
        }
        std::memcpy(destination, source, length);
        destination[length] = '\0';
        return true;
    }

    Counters& counters_;
    const api::BoardIdentity& identity_;
    OutboundScheduler& scheduler_;
    InboundParser parser_;
    Routes routes_;
    const Clock* clock_;
};

}  // namespace teensy_command_server::core
