# Phase 19 Audit Verdict — Phase 18 Hardware Validation

Date: 2026-06-17

Verdict: **FAIL**

This audit does not authorize the freeze task. The source logic and several
hardware observations are consistent with the current §31 numbers, but the
Phase-18 evidence package does not independently substantiate every claim in
`SESSION_BRIEF.md`. Under the Phase-19 instruction, any claim that cannot be
independently verified is a finding.

## Scope And Method

Read and cross-checked:

- `SESSION_BRIEF.md`, `README.md`
- `numerics_raw.json`, `transmit_raw.json`, `telemetry_streak_raw.json`
- `conformance_suite.log`, `numerics_confirm.log`, `integration_demo.log`,
  `integration_proxy_events.log`
- `measure_numerics.py`, `measure_transmit.py`
- Phase-18 git diff from `5c1c8f1` to `63b2b9d`
- Current source logic in `src/core/OutboundWriter.h`,
  `src/core/SessionDriver.h`, `src/core/EstopHandler.h`,
  `src/support/Limits.h`, `src/core/TelemetryScheduler.h`,
  `src/core/CommandDispatcher.h`, and `src/core/DiagnosticsCommand.h`

`tools/agent_orchestrator/orchestrate.py` was preflighted with the Phase-19
task file in dry-run mode. The configured Codex, Claude, and Antigravity CLIs
were available; the nested mutating orchestrator was not launched inside this
active audit run.

## Findings

### F1 — Telemetry teardown provenance is not clean enough for a freeze gate

`measure_transmit.py` writes `transmit_raw.json`, but the successful telemetry
teardown evidence is in a separate `telemetry_streak_raw.json`.

Evidence:

- `measure_transmit.py` records numeric 4 into `transmit_raw.json`.
- `transmit_raw.json` numeric 4 shows three runs with `teardown_signal: null`,
  `telemetry_dropped_delta: 0`, and `tx_failures_delta: 0`.
- `telemetry_streak_raw.json` shows two successful runs with `ECONNRESET`,
  `telemetry_dropped_delta` 13 and 12, and `tx_failures_delta: 0`.
- `numerics_confirm.log` also contains two successful telemetry teardown runs,
  but the successful raw JSON is not produced by the visible `measure_transmit.py`
  output path as checked in.

Conclusion: the telemetry-only teardown behavior is plausible and supported by
some artifacts, but the evidence package is not reproducible from the checked-in
rig/output relationship. That is a blocking audit finding.

### F2 — Receive-buffer pinning is asserted but not fully proven

The final telemetry streak artifact reports `pinned_rcvbuf_bytes: 2048`, but
the successful rig reads `SO_RCVBUF` before `connect()`. The earlier numerics
rig reads the effective value after connect and reports macOS clamping to
35040 bytes.

Evidence:

- `measure_transmit.py` sets `SO_RCVBUF` and immediately reads it before
  connecting.
- `numerics_raw.json` records `effective_rcvbuf_bytes: 35040` in the failed
  wedge runs.
- `telemetry_streak_raw.json` records `pinned_rcvbuf_bytes: 2048`, but does not
  prove the effective connected socket buffer stayed pinned.

Conclusion: the successful telemetry teardown data is directionally useful, but
the artifact does not independently prove the macOS autotuning condition the
brief relies on.

### F3 — Critical transmit teardown does not demonstrate the claimed RST probe

The rig includes non-draining reset detection using `SO_ERROR`, `select`, and
`MSG_PEEK`, but the actual critical-path artifacts report `send:BrokenPipeError`
rather than `ECONNRESET` or `SO_ERROR`.

Evidence:

- `measure_transmit.py` has a non-draining `reset_detected()` helper.
- `transmit_raw.json` critical runs report `send:BrokenPipeError`, with
  `tx_failures_delta: 1` and `telemetry_dropped_delta: 0`.
- `numerics_confirm.log` repeats the same `send:BrokenPipeError` signal.

Conclusion: the artifacts support that the critical write path tore the session
down and incremented `tx_failures`; they do not substantiate the specific
RST/`ECONNRESET` detection claim for the critical path.

### F4 — The 100 ms transmit deadline is supported by source, not isolated by the hardware timing

The hardware critical-path latency is about 340 ms and includes command
bursting, peer-buffer fill, board command handling, sleep cadence in the rig,
deadline expiry, teardown, and client-side observation.

Evidence:

- `transmit_raw.json` critical runs report teardown latencies around
  339.6-346.0 ms.
- `measure_transmit.py` sends 50-command batches and sleeps 10 ms per loop.
- `OutboundWriter.h` enforces `kTransmitDeadlineMs` in source and increments
  `tx_failures` on critical deadline expiry.

Conclusion: the source enforces the 100 ms deadline, and the hardware artifact
shows bounded teardown under backpressure. The artifact does not isolate the
100 ms interval as a hardware measurement.

### F5 — Normal-operation false-fire evidence is not isolated

The brief claims normal conformance/demo operation had `tx_failures = 0` and
`telemetry_dropped = 0`, but the visible integration counter snapshots contain
nonzero accumulated values.

Evidence:

- `integration_proxy_events.log` repeatedly reports `telemetry_dropped: 37` and
  `tx_failures: 3`.
- These are likely carryover from earlier rig activity, but the artifact does
  not present a clean normal-operation baseline/delta showing zero false fires.

Conclusion: this is not proof of a firmware false fire, but it fails the audit
standard for independently verifying the no-false-fire claim.

### F6 — Oversized telemetry test exercises drop/count, but not the exact serializer boundary claimed

The Phase-18 `kLargePayloadBytes = 8100` fix is directionally correct: a compact
telemetry line with that payload would exceed 8192 bytes. However, the current
fixture is dropped before final telemetry-line serialization because telemetry
uses `ObjectWriter`, whose payload capacity is `kMaxResultPayloadBytes`
(`8192 - 512`).

Evidence:

- `kLargePayloadBytes = 8100` is used by `test_telemetry_mode` mode 1.
- Re-derived compact telemetry line length is about 8273 bytes, greater than
  the 8192-byte board-to-controller limit.
- `TelemetryScheduler` counts a provider/ObjectWriter failure as
  `telemetry_dropped` before the final `buildLine()` path.

Conclusion: §28.7 does now exercise the oversized telemetry drop/count behavior.
It does not isolate the final `buildLine()` over-8192 rejection path. This is a
test precision finding, not evidence that firmware sends an over-limit frame.

## Checks That Passed This Audit

### Harness fix: `commands_ok >= 1`

Pass. The runner sends one successful `test_echo`, then four error-producing
commands, then `get_counters`. `get_counters` snapshots counters inside the
handler before the dispatcher increments the current command's `commands_ok`.
Therefore a clean board should show one prior successful command at snapshot
time. `commands_error >= 4` remains correct because unknown, missing, wrong-type,
and extra-argument cases all route through `enqueueError()`.

### Harness fix: `kLargePayloadBytes = 8100`

Pass with the precision caveat in F6. The current payload is large enough that a
fully wrapped compact telemetry line exceeds 8192 bytes. The sketch remains
DHCP by default, has no committed serial/debug print, and the canonical schema
is metadata-only, so the payload size does not bloat schema output.

### Hook budgets

Pass. Source uses `kHookBudgetMs = 100`. `EstopHandler` measures hook duration
and increments `estop_hook_over_budget` on over-budget success; `SessionDriver`
does the same for controller-loss hooks. `numerics_raw.json` and
`numerics_confirm.log` show 150 ms hooks incrementing the correct counters while
truthful `{"state":"safe"}` acks are still emitted for successful e-stop hooks.

### Critical-vs-telemetry source distinction

Pass. `OutboundWriter` maps critical deadline expiry to `tx_failures` and
`CriticalTransmitFailure`; telemetry deadline expiry increments
`telemetry_dropped` and requests teardown only after
`kTelemetryDeadlineFailuresToTeardown`. The artifacts align directionally:
critical runs show `tx_failures_delta: 1`, `telemetry_dropped_delta: 0`; telemetry
streak runs show `tx_failures_delta: 0`, `telemetry_dropped_delta` above 10.

### Phase-18 regression scope

Pass. `git diff 5c1c8f1..63b2b9d -- src docs/contracts third_party` is empty.
The Phase-18 source-bearing changes are the conformance sketch fixture and
`tests/conformance/runner.py`, plus result artifacts/backlog/gitignore cleanup.
No build products or tracked swap files remain.

## Validation

Validation was run after writing this audit.

```text
python3 tools/check_invariants.py
python3 tools/check_contract_sync.py
./tools/run_host_tests.sh
./tools/build_teensy.sh
```

The command summaries are recorded in the task completion report for this
audit. Hardware/conformance tests were not run or simulated by this host-side
audit.

## Decision

FAIL. Leave the Phase-19 backlog task unchecked. Do not advance to the freeze
task until the evidence package is repaired or a fresh Phase-20 operator run
records clean, provenance-clear measurements that resolve the findings above.
