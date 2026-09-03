#!/usr/bin/env bash
# Compile one named sketch, or every sketch, for Teensy 4.1 with arduino-cli.
# Exits 0 with a notice while no sketches exist (bootstrap state).
set -euo pipefail

cd "$(dirname "$0")/.."
FQBN="teensy:avr:teensy41"

usage() {
    echo "usage: ./tools/build_teensy.sh [sketch-name]"
}

requested_sketch=""
case "$#" in
    0)
        ;;
    1)
        case "$1" in
            -h|--help)
                usage
                exit 0
                ;;
            -*|*/*|"")
                usage >&2
                exit 2
                ;;
            *)
                requested_sketch="$1"
                ;;
        esac
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

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
    elif [[ "$(dirname "$sketch_dir")" == "tests/hardware/fixtures" \
          && "$(basename "$ino")" == "$(basename "$sketch_dir").ino" ]]; then
        valid_dirs+=("$sketch_dir")
    else
        echo "build_teensy: $ino violates required layout sketches/<name>/<name>.ino or tests/hardware/fixtures/<name>/<name>.ino" >&2
        invalid=$((invalid + 1))
    fi
done < <(find sketches tests/hardware/fixtures -name '*.ino' -print0 2>/dev/null | sort -z)

if [[ "$invalid" -gt 0 ]]; then
    echo "build_teensy: $invalid misplaced .ino file(s); fix the layout before building" >&2
    exit 1
fi

if [[ ${#valid_dirs[@]} -eq 0 && -z "$requested_sketch" ]]; then
    echo "build_teensy: no sketches or compile fixtures yet (bootstrap state) — nothing to build"
    exit 0
fi

if [[ -n "$requested_sketch" ]]; then
    matching_dirs=()
    for sketch_dir in "${valid_dirs[@]}"; do
        if [[ "$(basename "$sketch_dir")" == "$requested_sketch" ]]; then
            matching_dirs+=("$sketch_dir")
        fi
    done

    if [[ ${#matching_dirs[@]} -eq 0 ]]; then
        echo "build_teensy: no sketch or compile fixture named '$requested_sketch'" >&2
        exit 1
    fi
    if [[ ${#matching_dirs[@]} -gt 1 ]]; then
        echo "build_teensy: sketch name '$requested_sketch' is ambiguous:" >&2
        printf '  %s\n' "${matching_dirs[@]}" >&2
        exit 1
    fi
    valid_dirs=("${matching_dirs[0]}")
fi

if ! command -v arduino-cli >/dev/null 2>&1; then
    echo "build_teensy: arduino-cli not found; see the arduino-cli-builds skill" >&2
    exit 1
fi

build_root=$(mktemp -d "${TMPDIR:-/tmp}/teensy-build.XXXXXX")
library_root="$build_root/libraries/TeensyCommandServer"
mkdir -p "$library_root"
ln -s "$PWD/src" "$library_root/src"
cat >"$library_root/library.properties" <<'PROPERTIES'
name=TeensyCommandServer
version=0.0.0
author=Teensy Command Server
maintainer=Teensy Command Server
sentence=Teensy 4.1 command-server library.
paragraph=Compile-only staged library for repository validation.
category=Communication
architectures=*
includes=TeensyCommandServer.h
PROPERTIES
cleanup() {
    rm -rf "$build_root"
}
trap cleanup EXIT

failures=0
for sketch_dir in "${valid_dirs[@]}"; do
    echo "compiling $sketch_dir"
    sketch_name=$(basename "$sketch_dir")
    if ! arduino-cli compile --fqbn "$FQBN" --library "$library_root" --warnings all \
        --build-path "$build_root/$sketch_name" \
        "$sketch_dir"; then
        failures=$((failures + 1))
    fi
done

[[ "$failures" -eq 0 ]] || { echo "build_teensy: $failures sketch(es) failed"; exit 1; }
if [[ -n "$requested_sketch" ]]; then
    echo "build_teensy: $requested_sketch compiled"
else
    echo "build_teensy: all sketches compiled"
fi
