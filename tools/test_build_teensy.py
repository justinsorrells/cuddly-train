#!/usr/bin/env python3

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
BUILD_SCRIPT = REPO_ROOT / "tools" / "build_teensy.sh"


class BuildTeensyTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        (self.root / "tools").mkdir()
        (self.root / "src").mkdir()
        (self.root / "sketches").mkdir()
        (self.root / "tests" / "hardware" / "fixtures").mkdir(parents=True)
        shutil.copy2(BUILD_SCRIPT, self.root / "tools" / "build_teensy.sh")

        self.log_path = self.root / "arduino-cli.jsonl"
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        fake_cli = bin_dir / "arduino-cli"
        fake_cli.write_text(
            """#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

with Path(os.environ["ARDUINO_CLI_LOG"]).open("a", encoding="utf-8") as log:
    log.write(json.dumps(sys.argv[1:]) + "\\n")
""",
            encoding="utf-8",
        )
        fake_cli.chmod(0o755)

        self.env = os.environ.copy()
        self.env["PATH"] = f"{bin_dir}{os.pathsep}{self.env['PATH']}"
        self.env["TMPDIR"] = str(self.root)
        self.env["ARDUINO_CLI_LOG"] = str(self.log_path)

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def add_sketch(self, relative_dir: str) -> None:
        sketch_dir = self.root / relative_dir
        sketch_dir.mkdir(parents=True)
        (sketch_dir / f"{sketch_dir.name}.ino").write_text(
            "void setup() {}\nvoid loop() {}\n", encoding="utf-8"
        )

    def run_script(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(self.root / "tools" / "build_teensy.sh"), *args],
            cwd=self.root,
            env=self.env,
            check=False,
            capture_output=True,
            text=True,
        )

    def calls(self) -> list[list[str]]:
        if not self.log_path.exists():
            return []
        return [json.loads(line) for line in self.log_path.read_text().splitlines()]

    def test_no_argument_compiles_all_sketches_and_fixtures(self) -> None:
        self.add_sketch("sketches/alpha")
        self.add_sketch("sketches/beta")
        self.add_sketch("tests/hardware/fixtures/platform_compile")

        result = self.run_script()

        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(
            {
                "sketches/alpha",
                "sketches/beta",
                "tests/hardware/fixtures/platform_compile",
            },
            {call[-1] for call in self.calls()},
        )
        self.assertIn("build_teensy: all sketches compiled", result.stdout)

    def test_name_compiles_only_matching_sketch(self) -> None:
        self.add_sketch("sketches/alpha")
        self.add_sketch("sketches/beta")

        result = self.run_script("beta")

        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["sketches/beta"], [call[-1] for call in self.calls()])
        self.assertIn("build_teensy: beta compiled", result.stdout)
        self.assertNotIn("compiling sketches/alpha", result.stdout)

    def test_unknown_name_fails_without_compiling(self) -> None:
        self.add_sketch("sketches/alpha")

        result = self.run_script("missing")

        self.assertEqual(1, result.returncode)
        self.assertIn("no sketch or compile fixture named 'missing'", result.stderr)
        self.assertEqual([], self.calls())

    def test_duplicate_name_is_rejected_as_ambiguous(self) -> None:
        self.add_sketch("sketches/duplicate")
        self.add_sketch("tests/hardware/fixtures/duplicate")

        result = self.run_script("duplicate")

        self.assertEqual(1, result.returncode)
        self.assertIn("sketch name 'duplicate' is ambiguous", result.stderr)
        self.assertIn("sketches/duplicate", result.stderr)
        self.assertIn("tests/hardware/fixtures/duplicate", result.stderr)
        self.assertEqual([], self.calls())

    def test_path_argument_is_rejected(self) -> None:
        self.add_sketch("sketches/alpha")

        result = self.run_script("sketches/alpha")

        self.assertEqual(2, result.returncode)
        self.assertIn("usage: ./tools/build_teensy.sh [sketch-name]", result.stderr)
        self.assertEqual([], self.calls())

    def test_layout_error_precedes_name_lookup(self) -> None:
        invalid_dir = self.root / "sketches" / "invalid"
        invalid_dir.mkdir()
        (invalid_dir / "wrong_name.ino").write_text("void setup() {}\n", encoding="utf-8")

        result = self.run_script("missing")

        self.assertEqual(1, result.returncode)
        self.assertIn("violates required layout", result.stderr)
        self.assertNotIn("no sketch or compile fixture named", result.stderr)
        self.assertEqual([], self.calls())


if __name__ == "__main__":
    unittest.main()
