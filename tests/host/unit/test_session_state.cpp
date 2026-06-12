#include "core/SessionState.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace session = teensy_command_server::core;

struct ExpectedTransition {
    session::SessionState from;
    session::SessionState to;
    bool valid;
};

void assertUniqueStateMappings() {
    for (std::size_t i = 0; i < session::kSessionStates.size(); ++i) {
        const char* mapped = session::toString(session::kSessionStates[i]);
        assert(mapped != nullptr);
        assert(std::strlen(mapped) > 0);
        for (std::size_t j = i + 1; j < session::kSessionStates.size(); ++j) {
            assert(std::strcmp(mapped, session::toString(session::kSessionStates[j])) != 0);
        }
    }
}

int main() {
    using State = session::SessionState;

    static_assert(std::is_enum<State>::value);
    static_assert(session::kSessionStates.size() == 6);

    assert(std::strcmp(session::toString(State::BOOT_SAFE), "BOOT_SAFE") == 0);
    assert(std::strcmp(session::toString(State::NETWORK_STARTING), "NETWORK_STARTING") == 0);
    assert(std::strcmp(session::toString(State::LISTENING), "LISTENING") == 0);
    assert(std::strcmp(session::toString(State::SESSION_CONNECTED), "SESSION_CONNECTED") == 0);
    assert(std::strcmp(session::toString(State::SESSION_ACTIVE), "SESSION_ACTIVE") == 0);
    assert(std::strcmp(session::toString(State::SESSION_CLOSING), "SESSION_CLOSING") == 0);
    assertUniqueStateMappings();

    constexpr std::array<ExpectedTransition, 36> kExpectedTransitions = {
        ExpectedTransition{State::BOOT_SAFE, State::BOOT_SAFE, false},
        ExpectedTransition{State::BOOT_SAFE, State::NETWORK_STARTING, true},
        ExpectedTransition{State::BOOT_SAFE, State::LISTENING, false},
        ExpectedTransition{State::BOOT_SAFE, State::SESSION_CONNECTED, false},
        ExpectedTransition{State::BOOT_SAFE, State::SESSION_ACTIVE, false},
        ExpectedTransition{State::BOOT_SAFE, State::SESSION_CLOSING, false},

        ExpectedTransition{State::NETWORK_STARTING, State::BOOT_SAFE, false},
        ExpectedTransition{State::NETWORK_STARTING, State::NETWORK_STARTING, false},
        ExpectedTransition{State::NETWORK_STARTING, State::LISTENING, true},
        ExpectedTransition{State::NETWORK_STARTING, State::SESSION_CONNECTED, false},
        ExpectedTransition{State::NETWORK_STARTING, State::SESSION_ACTIVE, false},
        ExpectedTransition{State::NETWORK_STARTING, State::SESSION_CLOSING, false},

        ExpectedTransition{State::LISTENING, State::BOOT_SAFE, false},
        ExpectedTransition{State::LISTENING, State::NETWORK_STARTING, false},
        ExpectedTransition{State::LISTENING, State::LISTENING, false},
        ExpectedTransition{State::LISTENING, State::SESSION_CONNECTED, true},
        ExpectedTransition{State::LISTENING, State::SESSION_ACTIVE, false},
        ExpectedTransition{State::LISTENING, State::SESSION_CLOSING, false},

        ExpectedTransition{State::SESSION_CONNECTED, State::BOOT_SAFE, false},
        ExpectedTransition{State::SESSION_CONNECTED, State::NETWORK_STARTING, false},
        ExpectedTransition{State::SESSION_CONNECTED, State::LISTENING, false},
        ExpectedTransition{State::SESSION_CONNECTED, State::SESSION_CONNECTED, false},
        ExpectedTransition{State::SESSION_CONNECTED, State::SESSION_ACTIVE, true},
        ExpectedTransition{State::SESSION_CONNECTED, State::SESSION_CLOSING, true},

        ExpectedTransition{State::SESSION_ACTIVE, State::BOOT_SAFE, false},
        ExpectedTransition{State::SESSION_ACTIVE, State::NETWORK_STARTING, false},
        ExpectedTransition{State::SESSION_ACTIVE, State::LISTENING, false},
        ExpectedTransition{State::SESSION_ACTIVE, State::SESSION_CONNECTED, false},
        ExpectedTransition{State::SESSION_ACTIVE, State::SESSION_ACTIVE, false},
        ExpectedTransition{State::SESSION_ACTIVE, State::SESSION_CLOSING, true},

        ExpectedTransition{State::SESSION_CLOSING, State::BOOT_SAFE, false},
        ExpectedTransition{State::SESSION_CLOSING, State::NETWORK_STARTING, false},
        ExpectedTransition{State::SESSION_CLOSING, State::LISTENING, true},
        ExpectedTransition{State::SESSION_CLOSING, State::SESSION_CONNECTED, false},
        ExpectedTransition{State::SESSION_CLOSING, State::SESSION_ACTIVE, false},
        ExpectedTransition{State::SESSION_CLOSING, State::SESSION_CLOSING, false},
    };

    for (const ExpectedTransition& expected : kExpectedTransitions) {
        assert(session::isValidTransition(expected.from, expected.to) == expected.valid);
    }

    std::puts("test_session_state: ok");
    return 0;
}
