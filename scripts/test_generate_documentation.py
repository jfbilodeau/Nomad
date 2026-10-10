import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from generate_documentation import generate_documentation


class DocumentationTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="nomad docs ")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.sdk = self.root / "SDK with spaces"
        self.sdk.mkdir()
        suffix = ".exe" if os.name == "nt" else ""
        for name in ("nomad", "nomadc"):
            (self.sdk / f"{name}{suffix}").touch()
        (self.root / "docs").mkdir()
        (self.root / "docs" / "index.md").write_text("# Guide", encoding="utf-8")
        (self.root / "docs" / "releases.md").write_text("# Releases", encoding="utf-8")
        self.output = self.root / "out" / "documentation" / "source"
        self.output.mkdir(parents=True)
        (self.output / "stale.md").touch()

    def run_tool(self, command, **options):
        self.assertTrue(options["check"])
        self.assertEqual(options["timeout"], 60)
        if command[1] == "init":
            Path(command[2]).mkdir()
        else:
            Path(command[-1]).write_text("# Engine API", encoding="utf-8")

    def test_generates_pages_and_removes_stale_files(self):
        with patch("generate_documentation.subprocess.run", side_effect=self.run_tool):
            generate_documentation(self.sdk, self.root)
        self.assertEqual(
            sorted(page.name for page in self.output.iterdir()),
            ["engine-api.md", "index.md"],
        )
        self.assertEqual(list(self.output.parent.iterdir()), [self.output])

    def test_missing_tool_fails(self):
        with self.assertRaises(FileNotFoundError):
            generate_documentation(self.sdk / "missing", self.root)

    def test_tool_failure_preserves_previous_sources_and_cleans_project(self):
        with patch(
            "generate_documentation.subprocess.run",
            side_effect=subprocess.CalledProcessError(1, ["nomad"]),
        ):
            with self.assertRaises(subprocess.CalledProcessError):
                generate_documentation(self.sdk, self.root)
        self.assertTrue((self.output / "stale.md").exists())
        self.assertEqual(list(self.output.parent.iterdir()), [self.output])

    def test_empty_api_fails(self):
        def empty_reference(command, **options):
            self.run_tool(command, **options)
            if command[1] == "docs":
                Path(command[-1]).write_text("", encoding="utf-8")

        with patch("generate_documentation.subprocess.run", side_effect=empty_reference):
            with self.assertRaisesRegex(ValueError, "empty"):
                generate_documentation(self.sdk, self.root)
        self.assertTrue((self.output / "stale.md").exists())


if __name__ == "__main__":
    unittest.main()
