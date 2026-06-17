#!/usr/bin/env bash
# Run the Python conformance suite against a REAL board (hardware required).
# Never run or simulate hardware conformance in hosted CI; see AGENTS.md.
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <board-ip:port> [runner args]" >&2
    echo "       $0 --self-test" >&2
    exit 2
fi

exec python3 tests/conformance/runner.py "$@"
