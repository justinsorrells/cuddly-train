# src/support — neutral dependency layer

Fixed low-level utilities shared by `src/api` and `src/core`. This layer may
depend only on the C++ standard library — never on `src/api`, `src/core`, or
`src/platform` (invariant-checked include direction).

Full dependency matrix (enforced by `tools/check_invariants.py`):

```text
src/support   -> std only
src/api       -> src/support
src/core      -> src/api, src/support
src/platform  -> src/api, src/core, src/support
```

Files (created by their backlog tasks — see `backlog.md`):

```text
Limits.h               all compile-time capacities and timing defaults
BoundedJsonWriter.h    the single bounded compact-JSON writer (no second
                       serializer anywhere in the library)
```
