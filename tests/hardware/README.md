# tests/hardware — manual hardware validation

Manual procedures and recorded results for real Teensy 4.1 + QNEthernet
validation. This is the only place the four provisional numeric defaults are
confirmed (contract §31):

```text
100 ms e-stop hook budget
100 ms controller-loss hook budget
100 ms transmit deadline
10 consecutive telemetry deadline failures before teardown
```

If hardware shows a value is unrealistic, adjust the value and its tests —
never the architecture.

Record each run as `results/<date>_<firmware_version>.md` with: hardware
setup, QNEthernet/ArduinoJson versions, measurements, pass/fail per contract
§28 hardware-relevant case, and anomalies. The contract is stamped FROZEN
only after owner review of these results (backlog Phase 10).
