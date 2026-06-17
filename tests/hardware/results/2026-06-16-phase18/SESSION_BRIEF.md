# Phase-18 session brief — hardware validation, findings, and changes

**Date:** 2026-06-16 · **Operator:** Justin (agent-assisted, interactive — not
the autonomous orchestrator) · **Outcome:** all four §31 numerics confirmed on
real hardware, unchanged; contract FREEZE deferred to an orchestrator-run
agentic audit (final backlog task).

This brief is the human-readable record of the session for the agentic audit
that gates the freeze. Pair with `README.md` (results table) and the raw
artifacts in this directory.

## 1. Objective

Backlog Phase 18 (the final V1 gate): confirm the four provisional §31 numeric
defaults against real Teensy 4.1 + QNEthernet, record results, and gate the
contract FREEZE. The standing "agent makes no changes" instruction applies to
the *autonomous orchestrator*; this was an operator-driven interactive run, so
the physical validation could legitimately be performed and recorded.

## 2. Toolchain — restored to the documented pins

The Phase-16 demo had run on arduino-cli 1.5.0 / teensy core 1.60.0; the
Phase-16 audit required re-running on the pinned toolchain before any §31 claim.
Restored before measuring:

| Component | Pin | This run |
|---|---|---|
| arduino-cli | 1.3.1 | 1.3.1 (official macOS ARM64 release, kept **outside** the repo) |
| Teensy core | teensy:avr@1.59.0 | 1.59.0 (installed via arduino-cli, replaced 1.60.0) |
| QNEthernet | 0.35.0 | 0.35.0 |
| ArduinoJson | 6.21.5 | 6.21.5 (vendored `third_party/ArduinoJson`) |

## 3. Board + firmware

- Teensy 4.1, flashed over USB with `sketches/command_server_conformance`
  (board id `command_server_conformance`, fw `0.1.0`, protocol `1`, DHCP).
- Bench: board `192.168.10.2`, host/controller `192.168.10.3` via the `en6`
  USB 10/100/1000 LAN adapter (reversed from Phase-16, where the board was
  static `192.168.10.3`). There is a DHCP server on this link now.
- A temporary serial print of `Ethernet.localIP()` was used to discover the
  DHCP address, then **reverted**; the committed firmware (no serial print) was
  re-flashed and every final result was measured against that binary.

## 4. Findings

### 4.1 Two conformance-harness defects — firmware was conformant

The live §28 suite had never been run clean against hardware before (Phase 17
was fixture-only self-test). The first clean-boot run produced 2 FAILs, both
traced to the *harness*, not the firmware:

- **§28.5 (counter order).** `get_counters` snapshots the counters before
  counting itself, so a freshly booted board has exactly **one** successful
  command (the echo) before the snapshot. The runner asserted `commands_ok >= 2`
  — unsatisfiable on a clean board. Fix: threshold → `>= 1` (`commands_error
  >= 4` unchanged). Verified by probing the board directly: a single get_counters
  reports prior successes excluding itself.
- **§28.7 (oversized telemetry).** The fixture's "oversized" telemetry frame was
  only **7780 wire bytes — under the 8192 limit** — so the board correctly
  serialized and sent it and nothing was dropped. Fix: `kLargePayloadBytes`
  7600 → **8100** so the frame exceeds 8192 and the §13.6 drop path is actually
  exercised.

After the fixes: **11/11 live §28 cases PASS, 0 FAIL** from a clean boot, run
once on the fixed firmware and again on the committed firmware
(`conformance_suite.log`). §29 lines read PARTIAL only because each bundles
hardware-rig-only SKIP rows the wire client cannot cover — no FAILs.

### 4.2 §31 numerics — all four confirmed, no value changes

| # | Numeric | Measured behaviour | Verdict |
|---|---------|--------------------|---------|
| 1 | 100 ms e-stop hook budget (§19.1) | 150 ms hook → `estop_hook_over_budget` +1, honest `{"state":"safe"}` ack at ~150.5–150.9 ms; sub-ms hook → no flag, ~0.3–0.4 ms ack | realistic, unchanged |
| 2 | 100 ms controller-loss budget (§21) | 150 ms loss hook (run at supersession teardown) → `controller_loss_hook_over_budget` +1, replacement **still schema-first** at ~151.4–151.6 ms | realistic, unchanged |
| 3 | 100 ms transmit deadline (§13.6) | wedged Critical write → `tx_failures` +1 + RST teardown after ~1300 unread commands; board never blocks | realistic, unchanged |
| 4 | 10-frame telemetry teardown (§13.6) | wedged peer → 10 consecutive telemetry deadline failures → RST teardown; `telemetry_dropped` Δ 12–16 (10 + a few tail frames) | realistic, unchanged |

**Realism corroboration:** across the entire normal conformance + demo run
(thousands of 50 ms telemetry frames and many command responses)
`tx_failures = 0` and `telemetry_dropped = 0` — the 100 ms deadline never
false-fires in real operation, so it is comfortably above real QNEthernet write
latency, not too tight.

Raw data: `numerics_raw.json`, `transmit_raw.json`,
`telemetry_streak_raw.json`; committed-firmware re-confirmation in
`numerics_confirm.log`. Rigs: `measure_numerics.py`, `measure_transmit.py`.

### 4.3 Measurement methodology notes (for the audit)

- Teardown is `NetworkServer::abort()` = **TCP RST** (`SessionDriver::
  applyPendingTeardown`), so a non-reading peer detects teardown via
  `ECONNRESET` / `SO_ERROR` without draining (draining would re-open the window).
- `applyPendingTeardown()` runs the controller-loss hook on **any** active-
  session teardown, including supersession — so superseding an active session
  with the loss-over-budget flag set exercises the controller-loss hook.
- macOS **receive autotuning** grows a non-reading socket's buffer without
  bound; the telemetry-streak rig must pin `SO_RCVBUF` to force the window
  closed (without pinning, 40 s of telemetry produced 0 drops). The Critical
  100 ms transmit deadline was instead exercised with a burst of unread command
  responses that fills the buffer directly.

### 4.4 Real-controller integration demo (incl. web GUI)

Full stack brought up: Redis → logging proxy (`127.0.0.1:5052` → board) →
special-lamp controller (`run_controller_redis.py`, commit `0064056`,
observability + 0.25 s liveness) → web dashboard (`http://127.0.0.1:8000/`) →
5-bullet driver. **All five Phase-16 bullets exercised** (`integration_demo.log`,
`integration_proxy_events.log`):

1. schema-first registration (board `REGISTERED`, available, fw 0.1.0)
2. `test_echo value=42` → `result.value=42`, latency **0.76 ms**
3. 50 ms telemetry held liveness (never dropped under the 0.25 s window)
4. intruder bumped the controller → board re-registered
5. software e-stop → `estop_ack` → `estop_reset` → `estop_active:false`

Dashboard telemetry **jitter** (`|interval − previous_interval|`, nominal 50 ms,
`state.py:TelemetryRateObservation`) was **sub-millisecond** — the telemetry
cadence is rock-steady, corroborating the §18 50 ms cadence and 0.25 s liveness
window on real hardware.

## 5. Changes this session

**Keepers (to be committed):**
- `sketches/command_server_conformance/command_server_conformance.ino`:
  `kLargePayloadBytes` 7600 → 8100 (fixture fix; §28.7).
- `tests/conformance/runner.py`: §28.5 threshold `commands_ok >= 2` → `>= 1`.
- `tests/hardware/results/2026-06-16-phase18/` (this dir): README, this brief,
  rigs, raw JSON, conformance + numerics + integration logs.
- `backlog.md`: Phase-18 hardware-validation gate checked off with a completion
  record; new **final agentic-audit + FREEZE** task added.
- Removed a stray tracked `*.swp` vim swap file; added `*.swp` to `.gitignore`.

**Temporary / not committed (reverted or kept out of the repo):**
- temporary serial IP print in the sketch (reverted before the final flash).
- pinned arduino-cli 1.3.1 binary and all build artifacts live **outside** the
  repo (`/Users/justinsorrells/cuddly-train-toolchain`).
- `/tmp/phase18_demo/` runtime (proxy, demo driver, controller TOML).

## 6. NOT done (deliberately deferred)

- **Contract FREEZE** — no contract bytes were changed this session. The freeze
  is gated behind two final backlog tasks: **Phase 19** is a harsh, independent
  `orchestrate.py`-run agentic audit of this entire body of work (this brief is
  the audit input — treated as claims to disprove); **Phase 20** then requires a
  FRESH operator-run end-to-end re-test on real hardware (conformance + §31 rigs
  + integration demo, recorded under `retest/`) and only then stamps the Status
  line FROZEN and regenerates the hash manifest. The freeze happens only if both
  the audit PASSES and the fresh e2e re-test is green.
- **`/demo` command** — requested; proposed design in §7, not yet built.

## 7. `/demo` command (built)

Implemented: `tools/demo_stack.py` + `.claude/commands/demo.md`. A reusable
"encantation" to bring the whole demo up for boards on the network: discover →
controller + dashboard, with `up` / `down` / `status` / `discover` subcommands.
Design:

- `tools/demo_stack.py` orchestrator with subcommands `up` / `down` / `status` /
  `discover`:
  - **discover**: scan the host's active /24 subnets for TCP `:5051`, connect,
    read the first line, accept any peer whose first frame is a `schema` with a
    `source` board id; collect `{ip, board_id, fw}`.
  - **up**: start Redis; generate a controller TOML listing every discovered
    board (direct `ip:5051`, observability + liveness on); start the
    special-lamp controller + web dashboard; optional logging proxy + 5-bullet
    driver; health-check until boards are `REGISTERED`; print the dashboard URL.
  - **down**: stop the started processes and remove the socket.
- `.claude/commands/demo.md` thin command that runs the orchestrator, falls back
  to flashing `command_server_conformance` if no boards are found, and reports
  the dashboard URL + per-board status.
- Configurable via env: controller repo dir (default `~/special-lamp`), subnet
  override, board port (default 5051).

## 8. Verdict

All four §31 numerics behave correctly and are realistic at their current
values; the firmware is conformant on real Teensy 4.1 + QNEthernet; the
real-controller integration demo (incl. GUI) passed end to end. Recommendation
for the audit: confirm the recorded evidence and the two harness fixes, verify
the standard four validations, and stamp the contract **FROZEN with no numeric
or architecture change**.
