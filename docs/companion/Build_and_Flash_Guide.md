# Build and Flash Guide

This repository builds Teensy 4.1 sketches with `arduino-cli`; no IDE-generated
files or preprocessed `.ino.cpp` outputs are committed.

## Pinned Toolchain

| Component | Pin |
|---|---|
| Board FQBN | `teensy:avr:teensy41` |
| Teensy package index | `https://www.pjrc.com/teensy/package_teensy_index.json` |
| Teensy core / Teensyduino | `teensy:avr@1.59.0` |
| arduino-cli | `1.3.1` |
| QNEthernet | `0.35.0` |
| ArduinoJson | `6.21.5` vendored under `third_party/ArduinoJson`; CI also installs `ArduinoJson@6.21.5` for library-manager parity |
| GitHub Action | `arduino/setup-arduino-cli@v2` |

QNEthernet upgrades require rerunning the hardware/conformance tests before
claiming support for the new version.

## Install

```bash
arduino-cli core update-index \
  --additional-urls https://www.pjrc.com/teensy/package_teensy_index.json
arduino-cli core install teensy:avr@1.59.0 \
  --additional-urls https://www.pjrc.com/teensy/package_teensy_index.json
arduino-cli lib install "QNEthernet@0.35.0"
arduino-cli lib install "ArduinoJson@6.21.5"
```

## Build

From the repository root:

```bash
./tools/build_teensy.sh
```

The script compiles every valid sketch under `sketches/<name>/<name>.ino` for
`teensy:avr:teensy41` and creates a temporary Arduino library wrapper that
points at `src/`. It also validates sketch layout before invoking
`arduino-cli`.

## Sketches

`sketches/ethernet_smoke/ethernet_smoke.ino` is a minimal DHCP bring-up sketch
with one trivial command, one telemetry field, and the required safety
hooks. A commented static IPv4 block is included for bench networks.

`sketches/command_server_conformance/command_server_conformance.ino` is the
dedicated Phase-17 firmware fixture. It uses test-only command names and enables
`get_counters`.
