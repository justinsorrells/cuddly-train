---
name: teensy-safety-hooks
description: >
  Read before touching e-stop handling, controller-loss handling, hooks, or
  estop_ack. Bounded synchronous 100 ms hooks, idempotency, truthful ack,
  no board-side latch, and the honesty rules (contract §19–§21).
---

# Teensy Safety Hooks

> **The hardwired interlock and power cut are the safety guarantee. Software
> e-stop is convergence only** (V1 contract 1.13). Never write code or docs
> that imply otherwise. Assume drive may already be physically cut when a
> hook runs.

## The two hooks (board-application-provided, library-invoked)

```text
on_estop_received()   -> apply local safe state
on_controller_lost()  -> apply local safe state
```

Both are **idempotent** (re-invocation is normal: repeated estop, reconnect
during e-stop, supersession) and both carry a **100 ms budget** (§19.1, §21):

* "Apply safe state" = command the safe condition (outputs cut, drive
  disabled), never wait for physics. A ramp-down is commanded, then return.
* The library cannot preempt a hook; it measures duration after return and
  counts over-budget runs (`estop_hook_over_budget`,
  `controller_loss_hook_over_budget` — separate counters).
* Over-budget is a conformance failure surfaced by counters/tests — it never
  changes ack behavior, and teardown/promotion continues after return.

## E-stop sequence (§19)

On a valid `estop` (exact shape: `{"type":"estop","source":"controller",
"target":"<board_id>"}` — no seq, no timestamp):

1. stop dispatching ordinary messages until the hook has run
2. invoke the hook
3. send `estop_ack` **only if the hook reports safe state applied**
4. resume servicing (telemetry included) whether or not it succeeded

## The honesty rules (non-negotiable)

* `estop_ack` is `{"type":"event", ..., "event":"estop_ack",
  "details":{"state":"safe"}}`. `details.state` is **exactly** `"safe"` — the
  controller rejects anything else as malformed.
* Hook failure ⇒ **no ack at all**. Never a qualified ack, never a different
  state value. A missing ack is observable and is the design.
* The ack reports truth, not timing: a slow-but-successful hook still acks.

## No board-side latch (§6.5)

`estop_reset` never reaches boards — a board-side latched software gate could
never clear. E-stop command gating is the controller's job. Board-side
`ESTOP_ACTIVE` is allowed only while the board's own hardware safety
condition is *currently* active.

## Board-originated e-stop (§20)

Local interlock trip ⇒ send `event: estop_triggered` with `details.reason`.
The hardware action never waits on this message.

## Telemetry during e-stop (§18.6)

Telemetry keeps flowing through and after e-stop — it is the liveness signal.
A board that goes quiet after applying safe state looks dead and gets FAULTed
(~250 ms). Tests must assert telemetry resumes within the liveness window.
