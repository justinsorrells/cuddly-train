---
description: Bring up (or manage) the full board demo stack — discover boards, controller, and the web dashboard GUI
argument-hint: "[up|down|status|discover]"
allowed-tools: Bash(python3 tools/demo_stack.py:*), Bash(./tools/build_teensy.sh:*), Bash(arduino-cli:*), Read
---

Run the Teensy command-server demo stack via `tools/demo_stack.py`. The
subcommand is `$ARGUMENTS` (default `up` when empty).

Do this:

1. From the repo root, run `python3 tools/demo_stack.py $ARGUMENTS`
   (treat empty `$ARGUMENTS` as `up`). The script:
   - `discover` — scans the host's active /24 subnets for boards that answer
     TCP :5051 with a schema-first frame;
   - `up` — discovers boards, starts Redis + the special-lamp controller
     (`run_controller_redis.py`) + the web dashboard, and health-checks until the
     boards reach `REGISTERED`;
   - `status` — shows what's running and each board's registration;
   - `down` — stops the controller/dashboard/proxy and removes the socket.

2. If `up`/`discover` finds **no boards**, tell the user and offer to flash the
   conformance firmware, then retry:
   `./tools/build_teensy.sh` then
   `arduino-cli upload --fqbn teensy:avr:teensy41 -p <usb-port> --input-dir <build> sketches/command_server_conformance`.
   (For a freeze-grade run, use the pinned toolchain — see
   `tests/hardware/results/2026-06-16-phase18/SESSION_BRIEF.md` §2.)

3. Report exactly what the script prints: the **dashboard URL**
   (http://127.0.0.1:8000/) and the per-board registration status. Remind the
   user they can stop everything with `/demo down`.

Report board/registration status **only** from the script's actual output —
never fabricate it. Overrides: `DEMO_CONTROLLER_DIR` (special-lamp repo, default
`~/special-lamp`), `DEMO_SUBNETS`, `DEMO_BOARDS`, `DEMO_BOARD_PORT`.
