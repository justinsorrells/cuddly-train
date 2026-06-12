# Teensy 4.1 Command Server - Agent Orchestrator

The **Agent Orchestrator** is a repo-local utility that automates the
development, review, auditing, and commit cycles for the Teensy command-server
library. Ported from the `special-lamp` controller repo and adapted to this
repo's verification suite and firmware invariants. It organizes agents into a
structured pipeline:

```
[Task] ──> Codex (Implementation) ──> Verification Checks ──> Claude CLI (Adversarial Review) ──> Antigravity (Independent Audit) ──> [Auto-Commit or Stop]
```

---

## Agent Workflow

- **Antigravity (Gemini 3.5 Flash)**: project manager, task sequencer, and final auditor. Invoked non-interactively via `agy --print`.
- **Codex (GPT-5.5)**: primary implementation agent; writes production code and host tests.
- **Claude CLI (Opus 4.8)**: adversarial reviewer; verifies the git diff against the contracts and invariants. Invoked via `claude -p` (print mode).
- **Human**: exception handler, resolver of ambiguity, and final merge authority.

No human gating is required when all checks, reviews, and audits pass: the
orchestrator commits to an agent branch (it **never pushes or merges**). An
Antigravity FAIL routes through the bounded override policy; verification
failures and high-risk modifications always interrupt the operator.

**Precondition:** the repo must have commits and a clean worktree — the
orchestrator branches off `main` and refuses to start dirty (unless
`--allow-dirty`).

---

## Setup & Configuration

```bash
cp tools/agent_orchestrator/config.example.toml tools/agent_orchestrator/config.toml
```

Key config (see `config.example.toml` for the full file):

```toml
[checks]
contract_sync = true   # tools/check_contract_sync.py — hash-pinned contracts
invariants = true      # tools/check_invariants.py — architecture guardrails
host_tests = true      # ./tools/run_host_tests.sh — host C++ tests
teensy_build = true    # ./tools/build_teensy.sh — no-ops until sketches exist
compileall = true      # changed .py files only (orchestrator maintenance)
```

---

## CLI Usage

```bash
# Dry run: verifies config + agent CLIs, prints planned commands, changes nothing
python3 tools/agent_orchestrator/orchestrate.py --task-file tools/agent_orchestrator/examples/task.md --dry-run

# Single task
python3 tools/agent_orchestrator/orchestrate.py --task-file path/to/task.md

# Backlog mode: runs the first unchecked `- [ ]` item, checks it off on success
python3 tools/agent_orchestrator/orchestrate.py --backlog backlog.md
```

Note on backlog mode: `backlog.md` Phase 0 contains owner-only items; point the
orchestrator at a curated task file or a phase-scoped backlog slice rather than
the raw Phase 0 section.

---

## Stop Conditions & Safety Gates

The orchestrator halts for manual intervention when:

1. **Checks fail**: contract sync, invariants, host tests, Teensy build, or
   compileall. Failure logs are structured and fed back to Codex for bounded
   retry cycles (`max_task_cycles`).
2. **Review fails**: Claude verdict `FAIL` or items under "Must fix before
   commit:" (negations like "None"/"n/a" ignored); Antigravity verdict `FAIL`.
   Verdict parsing is anchored, markdown-tolerant, last-match-wins, and
   ignores diff-prefixed lines (anti-spoofing).
3. **Forbidden changes** (content scans on added lines, scoped per file,
   failing closed to the global diff when per-file parsing misses):
   - `docs/contracts/`, `AGENTS.md`, or `.agents/skills/` modified without
     explicit task permission.
   - QNEthernet/Arduino networking types added outside
     `src/platform/qnethernet` and `sketches/` (exact-directory matching —
     a sibling like `src/platform/qnethernet_bad/` is not exempt).
   - A board-side `status` value other than `ok`/`error` (the board never
     generates `timeout`). Scans quoted protocol writes only
     (`doc["status"] = "x"`, `"status":"x"`, `doc["status"].set("x")`,
     including `F("status")` forms); local lifecycle members named `status`
     are not flagged.
   - `DynamicJsonDocument`, Redis references, or board-level `get_schema`
     added under `src/`.
4. **Security / risk triggers**: newly created files staged with `git add -N .`
   so they are scanned; diff size limits; hardcoded secrets; dependency or
   CI/deployment file modifications; dirty worktree.

## Bounded Antigravity Override Policy

When Antigravity FAILs, Claude may adjudicate an override only for safe
non-critical changes. Deterministic hard stops (no override possible):

- any check failed (tests, builds, invariants, contract sync)
- `docs/contracts/` modified
- command-path files changed (`CommandServerCore`, `SessionState`,
  `OutboundScheduler`, `LineFramer`, `MessageParser`, `Protocol`,
  `QNEthernetTransport`, `QNEthernetServer`)
- diff touches safety/e-stop keywords, seq/`controller_ts` logic, or the
  transmit path (`writeFully`, deadlines, `flush`, `setNoDelay`)

Adjudication requires the exact `ANTIGRAVITY_ADJUDICATION:` and `confidence:`
markers; low confidence or `HARD_STOP` stops for human review.

---

## Run Artifacts

`final_report.md` verification outcomes are recorded run facts, never inferred
from the terminal classification: a dry run reports `NOT_RUN`, a disabled
check reports `DISABLED`, a check with nothing to do reports `SKIPPED` (e.g.
compileall with no Python changes), and an Antigravity FAIL that stops as
`STOP_HUMAN_REVIEW_REQUIRED` still reports `FAIL`.

Every cycle saves diagnostics under `.agent_runs/<timestamp>/` (gitignored):
task, prompts, agent stdout/stderr, git status/diff/diff-stat, per-check logs
(`contract_sync.txt`, `check_invariants.txt`, `host_tests.txt`,
`teensy_build.txt`, `compileall.txt`), review/audit/adjudication texts, and
`final_report.md` with the terminal classification.

## Result Classifications

`AUTO_COMMITTED`, `DRY_RUN_OK`, `STOP_BACKLOG_EMPTY`, `STOP_TESTS_FAILED`,
`STOP_COMPILE_FAILED`, `STOP_INVARIANTS_FAILED`, `STOP_CONTRACT_CHANGE`,
`STOP_CLAUDE_REVIEW_FAILED`, `STOP_ANTIGRAVITY_AUDIT_FAILED`,
`STOP_HUMAN_REVIEW_REQUIRED`, `STOP_ARCHITECTURE_RISK`,
`STOP_HIGH_RISK_CHANGE`, `STOP_MODEL_UNVERIFIED`, `STOP_DIRTY_WORKTREE`,
`STOP_TOOL_ERROR`.

## Self-Modification Limitations & Maintenance Mode

Modifications to `tools/agent_orchestrator/*`, `tools/check_invariants.py`,
or `tools/check_contract_sync.py` are allowed for orchestrator-maintenance
tasks but bypass parts of the pipeline's own gates — they require close
reviewer attention. When changing a checker, add or update tests proving it
still catches representative violations:

```bash
python3 -m unittest tools.agent_orchestrator.test_orchestrate -v
```
