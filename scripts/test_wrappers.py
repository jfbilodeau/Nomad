import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import unittest
import zipfile


class QuietRequestHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass


class WrapperTest(unittest.TestCase):
    version = "0.1.0"

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="nomad wrapper ")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.project = self.root / "project"
        self.project.mkdir()
        self.nomad_home = self.root / "nomad home"
        self.platform = "windows-x64" if os.name == "nt" else "linux-x64"
        template = Path(__file__).resolve().parents[1] / "templates" / "projects" / "default"
        for name in ("nomadw", "nomadw.cmd", "nomadw.ps1"):
            shutil.copy2(template / name, self.project / name)
        (self.project / "nomad.toml").write_text(
            f'[nomad]\nversion = "{self.version}"\n', encoding="utf-8"
        )

    def sdk_path(self):
        return self.nomad_home / "sdks" / self.version / self.platform

    def sdk_files(self):
        manifest = json.dumps({"version": self.version, "target": self.platform}).encode()
        if os.name == "nt":
            executable = Path(os.environ["COMSPEC"]).read_bytes()
            return {
                "nomad.exe": executable,
                "nomadc.exe": executable,
                "nomad-runtime.exe": executable,
                "templates/project.txt": b"template",
                "runtime/runtime.json": manifest,
            }
        manager = b"""#!/usr/bin/env bash
printf '%s\n' "$@" > "$NOMAD_TEST_ARGUMENTS"
exit "${NOMAD_TEST_EXIT_CODE:-0}"
"""
        tool = b"#!/usr/bin/env bash\nexit 0\n"
        return {
            "nomad": manager,
            "nomadc": tool,
            "nomad-runtime": tool,
            "templates/project.txt": b"template",
            "runtime/runtime.json": manifest,
        }

    def write_sdk(self, destination):
        for name, content in self.sdk_files().items():
            path = destination / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
            if not name.endswith(".json") and not name.endswith(".txt"):
                path.chmod(0o755)

    def write_release(self):
        release = self.root / "release" / f"v{self.version}"
        release.mkdir(parents=True)
        archive = release / f"nomad-sdk-{self.platform}-{self.version}.zip"
        with zipfile.ZipFile(archive, "w") as output:
            for name, content in self.sdk_files().items():
                entry = zipfile.ZipInfo(name)
                entry.create_system = 3
                mode = 0o100644 if name.endswith(".json") or name.endswith(".txt") else 0o100755
                entry.external_attr = mode << 16
                output.writestr(entry, content)
        checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
        (release / "SHA256SUMS.txt").write_text(
            f"{checksum}  {archive.name}\n", encoding="utf-8"
        )
        return release

    def run_wrapper(self, arguments, extra_environment=None):
        environment = os.environ.copy()
        environment["NOMAD_HOME"] = str(self.nomad_home)
        if extra_environment:
            environment.update(extra_environment)
        if os.name == "nt":
            command = [
                "powershell.exe",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(self.project / "nomadw.ps1"),
                *arguments,
            ]
        else:
            command = ["bash", str(self.project / "nomadw"), *arguments]
        return subprocess.run(
            command, capture_output=True, text=True, timeout=60, env=environment
        )

    def test_uses_installed_exact_sdk_and_forwards_exit_code(self):
        self.write_sdk(self.sdk_path())
        if os.name == "nt":
            result = self.run_wrapper(["/d", "/c", "exit", "23"])
        else:
            arguments_file = self.root / "arguments"
            result = self.run_wrapper(
                ["check", "directory with spaces"],
                {
                    "NOMAD_TEST_ARGUMENTS": str(arguments_file),
                    "NOMAD_TEST_EXIT_CODE": "23",
                },
            )
            self.assertEqual(
                arguments_file.read_text(encoding="utf-8").splitlines(),
                ["check", "directory with spaces"],
            )
        self.assertEqual(result.returncode, 23, result.stdout + result.stderr)

    def test_offline_mode_rejects_missing_sdk(self):
        result = self.run_wrapper(["--version"], {"NOMAD_OFFLINE": "1"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("NOMAD_OFFLINE=1", result.stdout + result.stderr)
        self.assertFalse(self.sdk_path().exists())

    def test_downloads_verifies_and_installs_sdk(self):
        release = self.write_release()
        handler = lambda *args, **kwargs: QuietRequestHandler(
            *args, directory=str(release.parent), **kwargs
        )
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)

        release_url = f"http://127.0.0.1:{server.server_port}"
        if os.name == "nt":
            result = self.run_wrapper(
                ["/d", "/c", "exit", "0"],
                {"NOMAD_RELEASE_BASE_URL": release_url},
            )
        else:
            arguments_file = self.root / "downloaded arguments"
            result = self.run_wrapper(
                ["version"],
                {
                    "NOMAD_RELEASE_BASE_URL": release_url,
                    "NOMAD_TEST_ARGUMENTS": str(arguments_file),
                },
            )
            self.assertEqual(arguments_file.read_text(encoding="utf-8").strip(), "version")

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.sdk_path() / "runtime" / "runtime.json").is_file())
        self.assertEqual(list(self.sdk_path().parent.glob(".nomad-install-*")), [])

    def test_rejects_checksum_mismatch(self):
        release = self.write_release()
        (release / "SHA256SUMS.txt").write_text(
            f"{'0' * 64}  nomad-sdk-{self.platform}-{self.version}.zip\n",
            encoding="utf-8",
        )
        handler = lambda *args, **kwargs: QuietRequestHandler(
            *args, directory=str(release.parent), **kwargs
        )
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)

        result = self.run_wrapper(
            ["--version"],
            {"NOMAD_RELEASE_BASE_URL": f"http://127.0.0.1:{server.server_port}"},
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.sdk_path().exists())
        self.assertEqual(list(self.sdk_path().parent.glob(".nomad-install-*")), [])


if __name__ == "__main__":
    unittest.main()
