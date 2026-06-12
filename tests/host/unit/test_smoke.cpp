// Proves the host test harness compiles and runs (backlog Phase 0).
// Replaced by real component tests from Phase 2 onward.

#include <cassert>
#include <cstdio>

int main() {
    assert(1 + 1 == 2);
    std::puts("test_smoke: ok");
    return 0;
}
