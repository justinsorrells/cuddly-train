# Phase 18 — hardware validation of the §31 numerics (2026-06-16)

Operator-run hardware validation of the four provisional contract §31 numeric
defaults against a real Teensy 4.1 + QNEthernet, gating the contract FREEZE.
Every number below was measured on the board; nothing here is simulated.

## Verdict

**All four §31 numerics behave correctly on real hardware. No numeric requires
change.** The freeze keeps `kTransmitDeadlineMs = 100`, `kHookBudgetMs = 100`,
and `kTelemetryDeadlineFailuresToTeardown = 10` unchanged; only the contract
Status line advances to FROZEN.

## Setup

| Item | Value |
|---|---|
| Firmware sketch | `sketches/command_server_conformance` |
| Firmware version | `0.1.0` (board id `command_server_conformance`) |
| Repo commit | `5c1c8f1` + the Phase-18 fixture/runner fixes in this change |
| Board | Teensy 4.1, listen port 5051, DHCP `192.168.10.2` |
| Controller host | macOS, `192.168.10.3` via en6 USB 10/100/1000 LAN adapter |
| arduino-cli | **1.3.1** (pinned) |
| Teensy core | **teensy:avr@1.59.0** (pinned) |
| QNEthernet | **0.35.0** (pinned) |
| ArduinoJson | **6.21.5** (vendored, `third_party/ArduinoJson`) |

The toolchain was explicitly downgraded to the documented Build/Flash pins
(1.3.1 / 1.59.0) before any measurement, per the Phase-16 audit requirement to
re-run on the pinned toolchain before §31 claims (the demo had used 1.5.0 /
1.60.0).

## §28 / §29 conformance (clean boot, committed firmware)

`tools/run_conformance.sh 192.168.10.2:5051` — **11/11 live cases PASS, 0 FAIL**
(full output in `conformance_suite.log`). The remaining §28 rows are the
hardware-rig-only items the wire client legitimately marks `SKIP`; the §29
acceptance lines are therefore `PARTIAL` (PASS + SKIP, never FAIL).

Two conformance-harness defects were found and fixed (firmware was conformant
in both cases; the live suite had never been run clean against hardware before,
only fixture self-tested in Phase 17):

1. **Sketch fixture** `kLargePayloadBytes` 7600 → **8100**: the "oversized"
   telemetry frame was only 7780 wire bytes — under the 8192 limit — so the
   board correctly sent it and nothing was dropped. 8100 pushes the frame over
   8192 so the §28.7 oversized-telemetry-drop path is actually exercised.
2. **Runner threshold** `commands_ok >= 2` → **`>= 1`** in the §28.5 case:
   `get_counters` snapshots counters before counting itself, so a freshly
   booted board has exactly one successful command (the echo) before the
   snapshot. The old threshold was unsatisfiable on a clean board.

## §31 measurements

Raw data: `numerics_raw.json`, `transmit_raw.json`, `telemetry_streak_raw.json`;
committed-firmware re-confirmation in `numerics_confirm.log`. Rigs:
`measure_numerics.py`, `measure_transmit.py`. The conformance fixture exercises
each path with a deliberate 150 ms (`kOverBudgetDelayMs`) over-budget hook.

### 1. E-stop hook budget = 100 ms (§19.1) — CONFIRMED

| Case | ack latency | `estop_hook_over_budget` Δ | ack details |
|---|---|---|---|
| in-budget hook (`test_estop_success`) | ~0.3–0.4 ms | **0** | `{"state":"safe"}` |
| over-budget hook (`test_estop_over_budget`, 150 ms) | ~150.5–150.9 ms | **+1** | `{"state":"safe"}` |

The 100 ms budget classifies a 150 ms hook as over-budget and a sub-ms hook as
in-budget. The ack remains honest (`safe`) and is gated on the hook's reported
truth, not its timing — the over-budget hook still acks because it returned
safe. `estop_ack_sent` moved by 2 (both acks delivered).

### 2. Controller-loss hook budget = 100 ms (§21) — CONFIRMED

| Metric | Result |
|---|---|
| replacement got schema-first | **true** |
| replacement schema latency | ~151.4–151.6 ms |
| `controller_loss_hook_over_budget` Δ | **+1** |
| `sessions_superseded` Δ | +1 |

A 150 ms over-budget controller-loss hook (run during supersession teardown,
`applyPendingTeardown` → `runControllerLossHook`) is flagged over-budget, and
the replacement session is **still promoted and served schema-first** — the
specified "over-budget hook still promotes" behavior. The ~151 ms replacement
latency is the over-budget hook running before the new session is promoted.

### 3. Transmit deadline = 100 ms (§13.6) — CONFIRMED

Critical path: a peer that stops reading, fed a burst of ~1300 unread commands,
fills the buffer; the next Critical response write cannot complete and is
bounded by the transmit deadline:

| Metric | Result |
|---|---|
| `tx_failures` Δ | **+1** |
| teardown | RST (`CriticalTransmitFailure`), board never blocks |
| `telemetry_dropped` Δ | 0 (critical teardown fired first) |

The board gives up the wedged write and tears the session down rather than
hanging. **Realism check:** across the entire normal conformance run (thousands
of 50 ms telemetry frames and many command responses) `tx_failures = 0` and
`telemetry_dropped = 0` — the 100 ms deadline never false-fires in real
operation, so it is comfortably above real QNEthernet write latency.

### 4. Telemetry teardown = 10 consecutive failures (§13.6) — CONFIRMED

Telemetry path: a peer with a pinned 2 KB receive buffer (autotuning disabled)
that stops reading:

| Metric | Result |
|---|---|
| teardown | RST (`TelemetryTransmitFailureStreak`) |
| teardown latency | ~13.0–13.5 s |
| `telemetry_dropped` Δ | 12–16 |

Once real backpressure sets in, 10 consecutive telemetry deadline failures tear
the session down (RST), consistent with `kTelemetryDeadlineFailuresToTeardown`.
The dropped count slightly exceeds 10 (a few tail frames drop between the
threshold being hit and `applyPendingTeardown` running). The ~13 s is lwIP/
QNEthernet send-buffer headroom filling before genuine backpressure — it is not
the contract numeric, which is a count, not a time.

## Notes / anomalies

- macOS receive autotuning grows a non-reading socket's buffer without bound;
  the telemetry-streak rig must pin `SO_RCVBUF` to force the window closed.
  Without pinning, 40 s of telemetry produced zero drops.
- A temporary serial print of `Ethernet.localIP()` was used to discover the
  DHCP address during bring-up and then removed; the committed firmware (no
  serial print) was re-flashed and is the binary all final results above were
  measured against.

## Real-controller integration demo (incl. web GUI)

Full stack — Redis + logging proxy + special-lamp controller
(`run_controller_redis.py`, commit `0064056`, observability + 0.25 s liveness) +
web dashboard (`http://127.0.0.1:8000/`) + 5-bullet driver — exercised all five
Phase-16 bullets end to end (`integration_demo.log`,
`integration_proxy_events.log`): schema-first registration; `test_echo value=42`
→ `result.value=42` (0.76 ms latency); 50 ms telemetry held liveness;
disconnect/reconnect re-registration; software e-stop → `estop_ack` →
`estop_reset`. Dashboard telemetry **jitter sub-millisecond** on the 50 ms
period — the cadence is rock-steady. See `SESSION_BRIEF.md` §4.4.

## Standard validation (pinned toolchain)

```
python3 tools/check_invariants.py     # OK
python3 tools/check_contract_sync.py  # OK (pre-freeze)
./tools/run_host_tests.sh             # 25/25 passed
./tools/build_teensy.sh               # all sketches compiled (arduino-cli 1.3.1 / core 1.59.0)
```
