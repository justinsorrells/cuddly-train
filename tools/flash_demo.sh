#!/usr/bin/env bash
# Flash one sketch to a connected Teensy 4.1 — a live-demo convenience wrapper
# around `arduino-cli compile -u`. It puts the pinned toolchain on PATH, stages
# this repo's src/ as an Arduino library, auto-detects the Teensy USB port, then
# compiles + uploads. NOT part of the firmware library or its verification.
#
#   ./tools/flash_demo.sh                 # flash sketches/demo_board (default)
#   ./tools/flash_demo.sh ethernet_smoke  # flash a different sketch
#
# Env overrides:
#   TEENSY_TOOLCHAIN_BIN  dir holding the pinned arduino-cli
#                         (default: $HOME/cuddly-train-toolchain/bin)
#   TEENSY_PORT           upload port (default: auto-detected from board list)
set -euo pipefail

cd "$(dirname "$0")/.."
FQBN="teensy:avr:teensy41"
SKETCH="${1:-demo_board}"
SKETCH_DIR="sketches/$SKETCH"

# 1) Toolchain: use arduino-cli already on PATH, else fall back to the pinned dir.
TOOLCHAIN_BIN="${TEENSY_TOOLCHAIN_BIN:-$HOME/cuddly-train-toolchain/bin}"
if ! command -v arduino-cli >/dev/null 2>&1 && [ -x "$TOOLCHAIN_BIN/arduino-cli" ]; then
    PATH="$TOOLCHAIN_BIN:$PATH"
fi
command -v arduino-cli >/dev/null 2>&1 \
    || { echo "flash_demo: arduino-cli not found (set TEENSY_TOOLCHAIN_BIN)" >&2; exit 1; }
[ -d "$SKETCH_DIR" ] || { echo "flash_demo: no sketch at $SKETCH_DIR" >&2; exit 1; }

# 2) Stage src/ as an Arduino library the sketch can #include.
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/teensy-flash.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
LIB="$STAGE/libraries/TeensyCommandServer"
mkdir -p "$LIB"
ln -s "$PWD/src" "$LIB/src"
cat > "$LIB/library.properties" <<'PROPS'
name=TeensyCommandServer
version=0.0.0
author=Teensy Command Server
maintainer=Teensy Command Server
sentence=Teensy 4.1 command-server library.
paragraph=demo flash staging.
category=Communication
architectures=*
includes=TeensyCommandServer.h
PROPS

# 3) Find the Teensy port (the row whose protocol is "teensy").
PORT="${TEENSY_PORT:-$(arduino-cli board list | awk '$2=="teensy"{print $1; exit}')}"
[ -n "$PORT" ] || { echo "flash_demo: no Teensy found on USB — is it plugged in?" >&2; exit 1; }

echo "flash_demo: flashing $SKETCH_DIR -> $PORT"
arduino-cli compile --fqbn "$FQBN" --library "$LIB" -u -p "$PORT" "$SKETCH_DIR"
echo "flash_demo: done — the board reboots and the controller reconnects in a few seconds"
