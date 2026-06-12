#!/usr/bin/env bash
# Run the Python conformance suite against a REAL board (hardware required).
# Never run or simulated in hosted CI — see AGENTS.md hardware rules.
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <board-ip:port>" >&2
    exit 2
fi

if [[ ! -f tests/conformance/runner.py ]]; then
    echo "run_conformance: tests/conformance/runner.py not implemented yet (backlog Phase 10)" >&2
    exit 1
fi

exec python3 tests/conformance/runner.py "$1"
