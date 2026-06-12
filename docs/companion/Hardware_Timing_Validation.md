# Hardware Timing Validation (companion — to be written in Phase 10)

Procedure for validating the four provisional numeric defaults on real
hardware (contract §31):

```text
100 ms e-stop hook budget
100 ms controller-loss hook budget
100 ms transmit deadline
10 consecutive telemetry deadline failures before teardown
```

Will document: measurement method per value, QNEthernet behaviors that bear
on them (send-buffer size, `Ethernet.loop()` cadence, writeFully semantics),
acceptance thresholds, and the rule that hardware findings adjust values and
their tests only — never architecture. Results go to
`tests/hardware/results/`; the contract is FROZEN only after owner review.
