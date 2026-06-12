#!/usr/bin/env python3
"""Fail if a contract file changed without its pinned hash being regenerated.

Verify:     python3 tools/check_contract_sync.py
Regenerate: python3 tools/check_contract_sync.py --update   (authorized changes only)
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CONTRACTS_DIR = REPO_ROOT / "docs" / "contracts"
MANIFEST = CONTRACTS_DIR / "contracts.sha256"

PINNED_FILES = (
    "Teensy_Command_Server_Contract.md",
    "V1_Networking_Decisions.md",
    "Board_Developer_Guide.md",
)


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def update() -> int:
    lines = [f"{file_hash(CONTRACTS_DIR / name)}  {name}" for name in PINNED_FILES]
    MANIFEST.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {MANIFEST.relative_to(REPO_ROOT)}")
    return 0


def verify() -> int:
    if not MANIFEST.exists():
        print("ERROR: docs/contracts/contracts.sha256 is missing; run --update once")
        return 1
    pinned: dict[str, str] = {}
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if line.strip():
            digest, name = line.split(None, 1)
            pinned[name.strip()] = digest
    failures = 0
    for name in PINNED_FILES:
        path = CONTRACTS_DIR / name
        if not path.exists():
            print(f"ERROR: contract file missing: {name}")
            failures += 1
            continue
        if name not in pinned:
            print(f"ERROR: {name} is not pinned in the manifest")
            failures += 1
            continue
        if file_hash(path) != pinned[name]:
            print(
                f"ERROR: {name} differs from its pinned hash. Contract edits "
                "require explicit authorization (see docs/contracts/UPSTREAM_SOURCES.md); "
                "if this change is authorized, run check_contract_sync.py --update "
                "in the same commit."
            )
            failures += 1
    if failures:
        return 1
    print(f"contract sync OK ({len(PINNED_FILES)} files match pinned hashes)")
    return 0


if __name__ == "__main__":
    sys.exit(update() if "--update" in sys.argv[1:] else verify())
