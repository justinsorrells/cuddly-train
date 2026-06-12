#pragma once

#include <array>

namespace teensy_command_server::core {

// V1 contract §2: connection lifecycle and safety state are orthogonal axes.
// This enum intentionally does not encode e-stop or any safety latch state.
enum class SessionState {
    BOOT_SAFE,
    NETWORK_STARTING,
    LISTENING,
    SESSION_CONNECTED,
    SESSION_ACTIVE,
    SESSION_CLOSING,
};

constexpr std::array<SessionState, 6> kSessionStates = {
    SessionState::BOOT_SAFE,
    SessionState::NETWORK_STARTING,
    SessionState::LISTENING,
    SessionState::SESSION_CONNECTED,
    SessionState::SESSION_ACTIVE,
    SessionState::SESSION_CLOSING,
};

constexpr const char* toString(SessionState value) {
    switch (value) {
        case SessionState::BOOT_SAFE:
            return "BOOT_SAFE";
        case SessionState::NETWORK_STARTING:
            return "NETWORK_STARTING";
        case SessionState::LISTENING:
            return "LISTENING";
        case SessionState::SESSION_CONNECTED:
            return "SESSION_CONNECTED";
        case SessionState::SESSION_ACTIVE:
            return "SESSION_ACTIVE";
        case SessionState::SESSION_CLOSING:
            return "SESSION_CLOSING";
    }
    return "";
}

constexpr bool isValidTransition(SessionState from, SessionState to) {
    switch (from) {
        case SessionState::BOOT_SAFE:
            return to == SessionState::NETWORK_STARTING;
        case SessionState::NETWORK_STARTING:
            return to == SessionState::LISTENING;
        case SessionState::LISTENING:
            return to == SessionState::SESSION_CONNECTED;
        case SessionState::SESSION_CONNECTED:
            return to == SessionState::SESSION_ACTIVE || to == SessionState::SESSION_CLOSING;
        case SessionState::SESSION_ACTIVE:
            return to == SessionState::SESSION_CLOSING;
        case SessionState::SESSION_CLOSING:
            return to == SessionState::LISTENING;
    }
    return false;
}

}  // namespace teensy_command_server::core
