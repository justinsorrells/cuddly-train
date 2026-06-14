#pragma once

#include "api/BoardIdentity.h"
#include "api/ObjectWriter.h"
#include "api/TelemetryProvider.h"
#include "core/Clock.h"
#include "core/Counters.h"
#include "core/OutboundScheduler.h"
#include "support/Limits.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace teensy_command_server::core {

class TelemetryScheduler {
public:
    TelemetryScheduler(Counters& counters,
                       const api::BoardIdentity& identity,
                       const Clock& clock,
                       api::TelemetryProvider provider,
                       void* provider_context)
        : counters_(counters),
          identity_(identity),
          clock_(clock),
          provider_(provider),
          provider_context_(provider_context) {}

    void service(OutboundScheduler& outbound) {
        if (!session_active_) {
            session_active_ = true;
            first_frame_due_ = true;
        }

        const std::uint64_t now_ms = clock_.monotonicMilliseconds();
        if (!first_frame_due_ &&
            static_cast<std::uint64_t>(now_ms - last_due_ms_) <
                support::kTelemetryPeriodMs) {
            return;
        }

        first_frame_due_ = false;
        last_due_ms_ = now_ms;
        buildAndQueue(outbound);
    }

    void onSessionInactive() {
        session_active_ = false;
        first_frame_due_ = true;
    }

    support::Seq nextSeqForTest() const {
        return next_seq_;
    }

private:
    bool buildAndQueue(OutboundScheduler& outbound) {
        api::ObjectWriter telemetry;
        if (provider_ == nullptr || !provider_(telemetry, provider_context_) ||
            !telemetry.close()) {
            counters_.increment(&Counters::telemetry_dropped);
            return false;
        }

        if (!buildLine(next_seq_, telemetry)) {
            counters_.increment(&Counters::telemetry_dropped);
            return false;
        }

        const OutboundEnqueueResult result =
            outbound.replaceTelemetry({line_, line_size_}, counters_);
        if (result != OutboundEnqueueResult::Queued) {
            counters_.increment(&Counters::telemetry_dropped);
            return false;
        }

        ++next_seq_;
        return true;
    }

    bool buildLine(support::Seq seq, const api::ObjectWriter& telemetry) {
        resetLine();
        const char* telemetry_data = telemetry.data();
        const std::size_t telemetry_size = telemetry.size();
        if (telemetry_data == nullptr || telemetry_size < 2 || telemetry_data[0] != '{' ||
            telemetry_data[telemetry_size - 1] != '}') {
            return false;
        }

        return appendRaw("{\"type\":\"telemetry\",\"seq\":") &&
               appendUInt64(seq) &&
               appendRaw(",\"timestamp\":") &&
               appendUInt64(clock_.monotonicMilliseconds()) &&
               appendRaw(",\"source\":") &&
               appendEscapedString(identity_.board_id == nullptr ? "" : identity_.board_id) &&
               appendRaw(",\"target\":\"controller\",\"telemetry\":") &&
               appendBytes(telemetry_data, telemetry_size) &&
               appendRaw("}\n");
    }

    void resetLine() {
        line_size_ = 0;
        line_[0] = '\0';
    }

    bool appendRaw(const char* value) {
        return value != nullptr && appendBytes(value, std::strlen(value));
    }

    bool appendBytes(const char* value, std::size_t length) {
        if (value == nullptr || length >= sizeof(line_) ||
            line_size_ > sizeof(line_) - 1 - length) {
            return false;
        }
        std::memcpy(line_ + line_size_, value, length);
        line_size_ += length;
        line_[line_size_] = '\0';
        return true;
    }

    bool appendUInt64(std::uint64_t value) {
        char literal[support::kMaxUInt64LiteralBytes]{};
        const int written = std::snprintf(literal, sizeof(literal), "%llu",
                                          static_cast<unsigned long long>(value));
        return written > 0 && static_cast<std::size_t>(written) < sizeof(literal) &&
               appendBytes(literal, static_cast<std::size_t>(written));
    }

    bool appendEscapedString(const char* value) {
        if (!appendRaw("\"")) {
            return false;
        }
        const char* text = value == nullptr ? "" : value;
        for (std::size_t i = 0; text[i] != '\0'; ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            switch (c) {
                case '"':
                    if (!appendRaw("\\\"")) {
                        return false;
                    }
                    break;
                case '\\':
                    if (!appendRaw("\\\\")) {
                        return false;
                    }
                    break;
                case '\b':
                    if (!appendRaw("\\b")) {
                        return false;
                    }
                    break;
                case '\f':
                    if (!appendRaw("\\f")) {
                        return false;
                    }
                    break;
                case '\n':
                    if (!appendRaw("\\n")) {
                        return false;
                    }
                    break;
                case '\r':
                    if (!appendRaw("\\r")) {
                        return false;
                    }
                    break;
                case '\t':
                    if (!appendRaw("\\t")) {
                        return false;
                    }
                    break;
                default:
                    if (c < 0x20) {
                        char escaped[7] = {'\\', 'u', '0', '0', hex(c >> 4),
                                           hex(c & 0x0F), '\0'};
                        if (!appendBytes(escaped, 6)) {
                            return false;
                        }
                    } else if (!appendBytes(reinterpret_cast<const char*>(&text[i]), 1)) {
                        return false;
                    }
                    break;
            }
        }
        return appendRaw("\"");
    }

    static char hex(unsigned char value) {
        return value < 10 ? static_cast<char>('0' + value)
                          : static_cast<char>('A' + (value - 10));
    }

    Counters& counters_;
    const api::BoardIdentity identity_;
    const Clock& clock_;
    api::TelemetryProvider provider_ = nullptr;
    void* provider_context_ = nullptr;
    support::Seq next_seq_ = 1;
    bool session_active_ = false;
    bool first_frame_due_ = true;
    std::uint64_t last_due_ms_ = 0;
    char line_[support::kTelemetryJsonBufferBytes]{};
    std::size_t line_size_ = 0;
};

}  // namespace teensy_command_server::core
