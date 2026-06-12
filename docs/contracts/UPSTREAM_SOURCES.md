# Contract provenance and sync rules

Three contract documents live in `docs/contracts/`. They have different
canonical homes. Never edit a contract here without an explicit task that
authorizes it; `tools/check_contract_sync.py` fails CI if a contract file
changes without its pinned hash being deliberately regenerated.

| Document | Canonical home | Status here |
|---|---|---|
| `Teensy_Command_Server_Contract.md` | **this repo** | Authoritative original. Moved here from the `special-lamp` working tree on 2026-06-11 (authored there, never committed there). |
| `V1_Networking_Decisions.md` | `special-lamp` repo (controller) | Pinned read-only snapshot of special-lamp commit `cf347cc`. |
| `Board_Developer_Guide.md` | `special-lamp` repo (controller) | Pinned read-only snapshot of special-lamp commit `cf347cc`. |

## Rules

* The V1 networking contract and the Board Developer Guide are owned by the
  controller repo. If the controller repo updates them, re-snapshot here,
  regenerate the hash manifest, and record the new source commit in this
  table. Never edit the snapshots directly.
* The Teensy command-server contract is owned here. Changing it is a contract
  change: it requires owner (Justin) approval, rationale recorded in the
  contract itself (changelog or §31), and a manifest regeneration in the same
  commit. Contract changes do **not** get `docs/decisions/` entries — the
  decision log is for implementation-level judgment calls only (see
  `docs/decisions/README.md`).
* The authority order in `AGENTS.md` applies on any conflict between these
  documents.

## Hash manifest

`docs/contracts/contracts.sha256` pins the current bytes of all three files.

* Verify: `python3 tools/check_contract_sync.py`
* Regenerate after an authorized change: `python3 tools/check_contract_sync.py --update`

**Only these three files are hash-pinned.** Process docs (`AGENTS.md`,
`CLAUDE.md`, this file, the skills) are protected by review, not hashes;
`tools/check_invariants.py` performs a lightweight consistency check that the
expected skill set exists and is referenced from `AGENTS.md`.

## Vendored third-party source

Operator-ingested only (backlog OPERATOR GATE entries are the records). The
orchestrator hard-stops any agent change under `third_party/`; no task
keyword can grant it. The path is excluded from project formatting/lint
rewrites but remains included in secret scanning and the invariant sweep.

| Component | Detail |
|---|---|
| Library | ArduinoJson (single-header amalgamation) |
| Upstream URL | https://github.com/bblanchon/ArduinoJson |
| Version / tag | `v6.21.5` |
| Release commit | `40ee05c065ce248192c47eb37af7a70a6935dfa6` |
| Release asset | `ArduinoJson-v6.21.5.h` (downloaded from the v6.21.5 GitHub release) |
| Vendored path | `third_party/ArduinoJson/ArduinoJson-v6.21.5.h` |
| sha256 | `47eca7985cf619dfa1a60d5cc1a6b3b7213a233bd45119eb4af56dd4f12e1962` |
| License | MIT, preserved at `third_party/ArduinoJson/LICENSE.txt` |
| Ingested | 2026-06-12 by the operator (Justin), conveyed in session |
