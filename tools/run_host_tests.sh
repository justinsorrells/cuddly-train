#!/usr/bin/env bash
# Compile and run every host C++ test (tests/host/**/test_*.cpp).
# Platform-free: system toolchain only, no Arduino/QNEthernet headers.
set -euo pipefail

cd "$(dirname "$0")/.."
BIN_DIR="tests/host/bin"
mkdir -p "$BIN_DIR"

CXX="${CXX:-c++}"
CXXFLAGS="-std=c++17 -Wall -Wextra -Werror -I src -I tests/host"

test_files=()
while IFS= read -r -d '' f; do
    test_files+=("$f")
done < <(find tests/host/unit tests/host/integration -name 'test_*.cpp' -print0 2>/dev/null | sort -z)

if [[ ${#test_files[@]} -eq 0 ]]; then
    echo "run_host_tests: no test files found"
    exit 1
fi

failures=0
count=${#test_files[@]}
for test_src in "${test_files[@]}"; do
    bin="$BIN_DIR/$(basename "${test_src%.cpp}")"
    if ! "$CXX" $CXXFLAGS "$test_src" -o "$bin"; then
        echo "FAIL (compile) $test_src"
        failures=$((failures + 1))
        continue
    fi
    if ! "$bin"; then
        echo "FAIL (run) $test_src"
        failures=$((failures + 1))
    fi
done

echo "host tests: $((count - failures))/$count passed"
[[ "$failures" -eq 0 ]]
