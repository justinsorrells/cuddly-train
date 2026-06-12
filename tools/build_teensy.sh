#!/usr/bin/env bash
# Compile every sketch under sketches/ for Teensy 4.1 with arduino-cli.
# Exits 0 with a notice while no sketches exist (bootstrap state).
set -euo pipefail

cd "$(dirname "$0")/.."
FQBN="teensy:avr:teensy41"

# Classify every .ino under sketches/ at any depth BEFORE the toolchain
# check, so layout errors are loud and toolchain-independent. Valid layout is
# exactly sketches/<name>/<name>.ino (Arduino requires the primary .ino to
# match its folder); anything else fails the build, never a silent no-op.
valid_dirs=()
invalid=0
while IFS= read -r -d '' ino; do
    sketch_dir=$(dirname "$ino")
    if [[ "$(dirname "$sketch_dir")" == "sketches" \
          && "$(basename "$ino")" == "$(basename "$sketch_dir").ino" ]]; then
        valid_dirs+=("$sketch_dir")
    else
        echo "build_teensy: $ino violates required layout sketches/<name>/<name>.ino" >&2
        invalid=$((invalid + 1))
    fi
done < <(find sketches -name '*.ino' -print0 2>/dev/null | sort -z)

if [[ "$invalid" -gt 0 ]]; then
    echo "build_teensy: $invalid misplaced .ino file(s); fix the layout before building" >&2
    exit 1
fi

if [[ ${#valid_dirs[@]} -eq 0 ]]; then
    echo "build_teensy: no sketches yet (bootstrap state) — nothing to build"
    exit 0
fi

if ! command -v arduino-cli >/dev/null 2>&1; then
    echo "build_teensy: arduino-cli not found; see the arduino-cli-builds skill" >&2
    exit 1
fi

failures=0
for sketch_dir in "${valid_dirs[@]}"; do
    echo "compiling $sketch_dir"
    if ! arduino-cli compile --fqbn "$FQBN" --libraries src --warnings all "$sketch_dir"; then
        failures=$((failures + 1))
    fi
done

[[ "$failures" -eq 0 ]] || { echo "build_teensy: $failures sketch(es) failed"; exit 1; }
echo "build_teensy: all sketches compiled"
