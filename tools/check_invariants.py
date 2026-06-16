#!/usr/bin/env python3
"""Static architecture guardrails for the Teensy command-server repo.

Errors fail CI. Warnings are printed for human review (some contract rules,
like deadline-bounded writes, cannot be proven statically).

Run from the repo root: python3 tools/check_invariants.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

CPP_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".ino"}
SKIP_DIRS = {".git", ".pio", "build", "bin", "__pycache__", "node_modules"}
ARTIFACT_SUFFIXES = {".elf", ".hex", ".bin", ".o", ".a", ".d", ".eep", ".lst"}

EXPECTED_SKILLS = (
    "command-server-contract",
    "fixed-capacity-cpp",
    "ndjson-framing",
    "qnethernet-transport",
    "teensy-safety-hooks",
    "host-conformance-testing",
    "arduino-cli-builds",
    "repository-conventions",
)

# QNEthernet / Arduino networking types are confined to the platform adapter
# and sketches (contract §4 of the repo blueprint; core/platform boundary).
PLATFORM_ONLY = re.compile(
    r"#include\s*[<\"](QNEthernet|NativeEthernet|Ethernet)" r'|EthernetClient|EthernetServer'
)
# Exact directories, matched path-aware so a sibling like
# src/platform/qnethernet_bad/ cannot ride the exemption.
PLATFORM_ALLOWED_DIRS = ("src/platform/qnethernet", "sketches")


def in_allowed_platform_dirs(rpath: str) -> bool:
    return any(rpath == d or rpath.startswith(d + "/") for d in PLATFORM_ALLOWED_DIRS)

# Layered include direction (Library_API.md, Dependency Direction):
#   src/support -> std only; src/api -> support; src/core -> api, support;
#   src/platform -> api, core, support. Maps each layer to the path segments
# it must never include. Quoted project includes only; <std> headers pass.
LAYER_FORBIDDEN_INCLUDES = {
    "src/support": ("api/", "core/", "platform/"),
    "src/api": ("core/", "platform/"),
    "src/core": ("platform/",),
}
QUOTED_INCLUDE = re.compile(r'#include\s*"([^"]+)"')

# Board-side forbidden vocabulary (contract §17, §16.4, §30).
CONTROLLER_OWNED_CODES = re.compile(
    r"BOARD_UNAVAILABLE|BOARD_BUSY|COMMAND_TIMEOUT|CONTROLLER_SHUTDOWN"
    r"|PROTOCOL_VERSION_MISMATCH|UNKNOWN_TARGET"
)

ERRORS: list[str] = []
WARNINGS: list[str] = []

FACADE_REQUIRED_ROUTES = (
    "command_with_timing",
    "estop_with_result",
    "heartbeat_with_result",
    "safety_due",
    "outbound_outcome",
    "telemetry_due",
    "telemetry_inactive",
    "context",
)


def rel(path: Path) -> str:
    return str(path.relative_to(REPO_ROOT))


def iter_files() -> list[Path]:
    files = []
    for path in REPO_ROOT.rglob("*"):
        if path.is_dir():
            continue
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        files.append(path)
    return files


def check_file(path: Path) -> None:
    rpath = rel(path)
    if path.suffix in ARTIFACT_SUFFIXES:
        ERRORS.append(f"{rpath}: build artifact in the working tree (must never be committed)")
        return
    if path.suffix not in CPP_SUFFIXES:
        return
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        ERRORS.append(f"{rpath}: C++ source is not valid UTF-8")
        return

    in_src = rpath.startswith("src/")
    in_core_or_api = rpath.startswith(("src/core", "src/api", "src/support"))
    layer = next((d for d in LAYER_FORBIDDEN_INCLUDES if rpath.startswith(d + "/")), None)

    for lineno, line in enumerate(text.splitlines(), 1):
        where = f"{rpath}:{lineno}"
        if layer:
            include = QUOTED_INCLUDE.search(line)
            if include:
                target = include.group(1)
                for segment in LAYER_FORBIDDEN_INCLUDES[layer]:
                    if re.search(r"(?:^|/)" + re.escape(segment), target):
                        ERRORS.append(
                            f"{where}: {layer} must not include {segment[:-1]} headers "
                            "(dependency direction, Library_API.md)"
                        )
        if re.search(r'^#line\s+\d+\s+"', line):
            ERRORS.append(f"{where}: generated Arduino #line directive in committed source")
        if re.search(r'"(/Users/|/home/|[A-Za-z]:\\\\)', line):
            ERRORS.append(f"{where}: developer-specific absolute path in source")
        if PLATFORM_ONLY.search(line) and not in_allowed_platform_dirs(rpath):
            ERRORS.append(f"{where}: QNEthernet/Arduino networking type outside src/platform/qnethernet")
        if in_src and re.search(r"DynamicJsonDocument", line):
            ERRORS.append(f"{where}: DynamicJsonDocument forbidden; use fixed-capacity documents (contract §11)")
        if in_src and re.search(r"\bredis\b", line, re.IGNORECASE):
            ERRORS.append(f"{where}: Redis must never appear board-side (contract §27)")
        if in_src and re.search(r'"get_schema"', line):
            ERRORS.append(f"{where}: no board-level get_schema exists (contract §9.3)")
        if in_core_or_api and re.search(r"\bprintln?\s*\(", line):
            ERRORS.append(f"{where}: print()/println() forbidden in core/api protocol paths (contract §13.1)")
        if in_src and re.search(r"\bwriteFully\s*\(", line):
            WARNINGS.append(f"{where}: raw writeFully() — must be wrapped by the deadline-bounded write helper (contract §13.6)")
        if in_src and CONTROLLER_OWNED_CODES.search(line):
            WARNINGS.append(f"{where}: controller-owned error code in board source — verify it is not emitted (contract §17)")
        if in_src and re.search(r"\b(malloc|new)\b.*\bparse", line):
            WARNINGS.append(f"{where}: possible per-message allocation in a parse path (contract §11)")


def check_skill_consistency() -> None:
    """Process docs are not hash-pinned; keep the skill set self-consistent."""
    agents_md = REPO_ROOT / "AGENTS.md"
    agents_text = agents_md.read_text(encoding="utf-8") if agents_md.exists() else ""
    if not agents_text:
        ERRORS.append("AGENTS.md: missing or empty")
    for name in EXPECTED_SKILLS:
        if not (REPO_ROOT / ".agents" / "skills" / name / "SKILL.md").exists():
            ERRORS.append(f".agents/skills/{name}/SKILL.md: expected skill is missing")
        if agents_text and name not in agents_text:
            ERRORS.append(f"AGENTS.md: does not reference required skill '{name}'")
    skills_root = REPO_ROOT / ".agents" / "skills"
    if skills_root.exists():
        for entry in skills_root.iterdir():
            if entry.is_dir() and entry.name not in EXPECTED_SKILLS:
                WARNINGS.append(
                    f".agents/skills/{entry.name}: skill not in EXPECTED_SKILLS — "
                    "add it there and to AGENTS.md if intentional"
                )


def check_facade_routes() -> None:
    """Guard against public-facade route omissions that unit tests can miss."""
    header = REPO_ROOT / "src" / "TeensyCommandServer.h"
    if not header.exists():
        ERRORS.append("src/TeensyCommandServer.h: missing facade header")
        return
    text = header.read_text(encoding="utf-8")
    match = re.search(
        r"static\s+core::ServiceLoop::Routes\s+facadeRoutes"
        r"\([^)]*\)\s*\{(?P<body>.*?)\n\s*\}",
        text,
        flags=re.DOTALL,
    )
    if not match:
        ERRORS.append("src/TeensyCommandServer.h: facadeRoutes() not found")
        return
    body = match.group("body")
    for route in FACADE_REQUIRED_ROUTES:
        if f"routes.{route}" not in body:
            ERRORS.append(
                f"src/TeensyCommandServer.h: facadeRoutes() does not wire "
                f"ServiceLoop::Routes::{route}"
            )


def main() -> int:
    check_skill_consistency()
    check_facade_routes()
    for path in iter_files():
        check_file(path)

    for warning in WARNINGS:
        print(f"WARN  {warning}")
    for error in ERRORS:
        print(f"ERROR {error}")
    if ERRORS:
        print(f"\ninvariants FAILED: {len(ERRORS)} error(s), {len(WARNINGS)} warning(s)")
        return 1
    print(f"invariants OK ({len(WARNINGS)} warning(s) for human review)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
