---
name: arduino-cli-builds
description: >
  Read before touching .ino files, build scripts, or CI compile jobs. Teensy
  4.1 FQBN, required libraries, portable-source rules, and clean-build
  commands (contract §4).
---

# Arduino CLI Builds

## Toolchain

* Build tool: `arduino-cli` (no IDE-generated output is ever committed).
* Board: Teensy 4.1 — FQBN `teensy:avr:teensy41`.
* Core index: `https://www.pjrc.com/teensy/package_teensy_index.json`
  (`arduino-cli core install teensy:avr --additional-urls <index>`).
* Libraries: QNEthernet, ArduinoJson. **Pin the tested versions in
  `docs/companion/Build_and_Flash_Guide.md` when first validated** — a
  QNEthernet upgrade requires rerunning conformance tests (contract §4).

## Commands

```bash
./tools/build_teensy.sh            # compiles every sketch under sketches/
arduino-cli compile --fqbn teensy:avr:teensy41 --libraries src sketches/<name>
```

The script validates layout first and is strict about it: every `.ino` at any
depth under `sketches/` must be exactly `sketches/<name>/<name>.ino` — one
primary `.ino` per sketch folder, no secondary `.ino` files, no nesting.
Precedence: layout errors (exit 1, reported even without `arduino-cli`
installed) > bootstrap no-op (exit 0 only when zero `.ino` files exist
anywhere under `sketches/`) > missing-`arduino-cli` error > per-sketch
compile failures.

## Portable source rules (contract §4 — invariant-checked)

* Commit only original `.ino` / `.cpp` / `.h` files.
* Never commit generated preprocessor output: no `#line N "/abs/path"`
  directives, no `*.ino.cpp`, no build/cache directories (see `.gitignore`).
* No developer-specific absolute paths anywhere in committed source.
* Sketches stay thin: identity + registration + hooks wiring, then
  `start`/`service`. All behavior lives in `src/`; a sketch is a consumer of
  the library exactly like a board developer's firmware would be.

## CI

The Teensy compile job in `.github/workflows/ci.yml` runs
`./tools/build_teensy.sh`. It currently passes trivially (no sketches). When
the first sketch lands (Phase 9), add the arduino-cli setup steps that are
stubbed in the workflow comments and make the job required.
