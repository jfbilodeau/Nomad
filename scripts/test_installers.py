import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile


class InstallerTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="nomad installer ")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.scripts = Path(__file__).resolve().parent
        self.platform = "windows-x64" if os.name == "nt" else "linux-x64"
        self.fixture = self.root / "downloads"
        self.fixture.mkdir()
        self.destination = self.root / "installation with spaces"
        self.archive = self.fixture / f"nomad-sdk-{self.platform}-0.1.0.zip"
        self.write_archive()
        self.write_checksums()
        (self.fixture / "release.json").write_text(
            json.dumps({"tag_name": "v0.1.0", "draft": False, "prerelease": False}),
            encoding="utf-8",
        )

    def write_archive(self, extra=None, version="0.1.0"):
        suffix = ".exe" if os.name == "nt" else ""
        files = {
            f"{name}{suffix}": b"fixture"
            for name in ("nomad", "nomadc", "nomad-runtime")
        }
        files["templates/project.txt"] = b"template"
        files["runtime/runtime.json"] = json.dumps(
            {"version": version, "target": self.platform}
        ).encode()
        if extra:
            files.update(extra)
        with zipfile.ZipFile(self.archive, "w") as archive:
            for name, content in files.items():
                entry = zipfile.ZipInfo(name)
                entry.create_system = 3
                entry.external_attr = (0o100755 if name.startswith("nomad") else 0o100644) << 16
                archive.writestr(entry, content)

    def write_checksums(self):
        checksum = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        (self.fixture / "SHA256SUMS.txt").write_text(
            f"{checksum}  {self.archive.name}\n", encoding="utf-8"
        )

    def install(self, version=None):
        if os.name == "nt":
            wrapper = self.root / "run.ps1"
            wrapper.write_text(
                """
param([string]$Fixture, [string]$Installer, [string]$Destination, [string]$Version)
$ErrorActionPreference = 'Stop'
Import-Module Microsoft.PowerShell.Utility
function Get-FileHash {
    throw 'The installer must not depend on Get-FileHash being available.'
}
function Invoke-RestMethod {
    param($Uri, $TimeoutSec)
    Get-Content -LiteralPath (Join-Path $Fixture 'release.json') -Raw | ConvertFrom-Json
}
function Invoke-WebRequest {
    param($Uri, $OutFile, $TimeoutSec, [switch]$UseBasicParsing)
    Copy-Item -LiteralPath (Join-Path $Fixture ($Uri.Split('/')[-1])) -Destination $OutFile
}
if ($Version) {
    & $Installer -Destination $Destination -Version $Version
} else {
    & $Installer -Destination $Destination
}
""",
                encoding="utf-8",
            )
            command = [
                "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                "-File", str(wrapper), "-Fixture", str(self.fixture),
                "-Installer", str(self.scripts / "install.ps1"),
                "-Destination", str(self.destination),
            ]
            if version is not None:
                command.extend(["-Version", version])
        else:
            wrapper = """
set -euo pipefail
export fixture="$1"
installer="$2"
destination="$3"
shift 3
curl() {
    local url="" output=""
    while (( $# )); do
        case "$1" in
            -o) output="$2"; shift 2 ;;
            https:*) url="$1"; shift ;;
            *) shift ;;
        esac
    done
    if [[ "$url" == */releases/latest ]]; then
        cat "$fixture/release.json"
    else
        cp "$fixture/${url##*/}" "$output"
    fi
}
export -f curl
bash "$installer" "${1:-latest}" "$destination"
"""
            command = [
                "bash", "-c", wrapper, "installer-test", str(self.fixture),
                str(self.scripts / "install.sh"), str(self.destination),
            ]
            if version is not None:
                command.append(version)
        environment = os.environ.copy()
        if os.name == "nt":
            environment.pop("PSModulePath", None)
        return subprocess.run(command, capture_output=True, text=True, timeout=60, env=environment)

    def assert_no_staging(self):
        self.assertEqual(list(self.root.glob(".nomad-install-*")), [])

    def test_optional_latest_and_overwrite_protection(self):
        result = self.install()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.destination / "runtime" / "runtime.json").is_file())
        self.assertIn("PATH", result.stdout)
        self.assert_no_staging()
        result = self.install("latest")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("already exists", result.stdout + result.stderr)

    def test_explicit_version(self):
        (self.fixture / "release.json").unlink()
        result = self.install("v0.1.0")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_no_staging()

    def test_download_failure(self):
        self.archive.unlink()
        result = self.install("0.1.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.destination.exists())
        self.assert_no_staging()

    def test_checksum_mismatch(self):
        self.archive.write_bytes(b"corrupt")
        result = self.install("0.1.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.destination.exists())
        self.assert_no_staging()

    def test_missing_checksum(self):
        (self.fixture / "SHA256SUMS.txt").write_text("", encoding="utf-8")
        result = self.install("0.1.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exactly one checksum", result.stdout + result.stderr)
        self.assertFalse(self.destination.exists())
        self.assert_no_staging()

    def test_symbolic_link_archive(self):
        with zipfile.ZipFile(self.archive, "a") as archive:
            entry = zipfile.ZipInfo("link")
            entry.create_system = 3
            entry.external_attr = 0o120777 << 16
            archive.writestr(entry, "../outside")
        self.write_checksums()
        result = self.install("0.1.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.destination.exists())
        self.assert_no_staging()

    def test_unsafe_archive(self):
        self.write_archive({"../escaped.txt": b"must not escape"})
        self.write_checksums()
        result = self.install("0.1.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unsafe archive", result.stdout + result.stderr)
        self.assertFalse(self.destination.exists())
        self.assertFalse((self.root / "escaped.txt").exists())
        self.assert_no_staging()

    def test_wrong_manifest_version(self):
        self.write_archive(version="0.2.0")
        self.write_checksums()
        result = self.install("0.1.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.destination.exists())
        self.assert_no_staging()

    def test_latest_unavailable(self):
        (self.fixture / "release.json").unlink()
        result = self.install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Specify", result.stdout + result.stderr)
        self.assertFalse(self.destination.exists())

    def test_latest_rejects_prerelease(self):
        (self.fixture / "release.json").write_text(
            '{"tag_name":"v0.1.0","draft":false,"prerelease":true}', encoding="utf-8"
        )
        result = self.install()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.destination.exists())

    def test_invalid_version(self):
        result = self.install("../invalid")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.destination.exists())

    @unittest.skipUnless(os.environ.get("NOMAD_INSTALLER_DISTRIBUTION_ROOT"), "CI SDK not supplied")
    def test_tested_ci_sdk(self):
        root = Path(os.environ["NOMAD_INSTALLER_DISTRIBUTION_ROOT"])
        archives = list(root.rglob(f"nomad-sdk-{self.platform}-*.zip"))
        self.assertEqual(len(archives), 1)
        archive = archives[0]
        version = archive.name.removeprefix(f"nomad-sdk-{self.platform}-").removesuffix(".zip")
        shutil.copy2(archive, self.fixture / archive.name)
        shutil.copy2(archive.parent / "SHA256SUMS.txt", self.fixture)
        result = self.install(version)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        suffix = ".exe" if os.name == "nt" else ""
        for name in ("nomad", "nomadc"):
            subprocess.run([str(self.destination / f"{name}{suffix}"), "--version"], check=True, timeout=30)
        project = self.root / "game"
        manager = str(self.destination / f"nomad{suffix}")
        for command in ("init", "check", "package"):
            subprocess.run([manager, command, str(project)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
