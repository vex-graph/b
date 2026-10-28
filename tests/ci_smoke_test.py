"""Cross-platform CLI smoke and self-box proof for release builds."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CiSmokeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix="b-ci-smoke-")
        cls.addClassCleanup(cls.scratch.cleanup)
        cls.home = Path(os.environ.get("B_HOME", Path(cls.scratch.name) / "state"))
        cls.environment = dict(os.environ, B_HOME=str(cls.home))
        binary = os.environ.get("B_BINARY")
        if binary is None:
            candidates = (cls.home / "cli", cls.home / "cli.exe")
            binary = next((str(path) for path in candidates if path.is_file()), None)
        if binary is None:
            raise RuntimeError("set B_BINARY to the compiled B CLI path")
        cls.binary = Path(binary)
        if not cls.binary.is_file():
            raise RuntimeError(f"compiled B CLI is missing: {cls.binary}")

    def invoke(self, *arguments, expected=0, cwd=None):
        result = subprocess.run([str(self.binary), *map(str, arguments)],
                                cwd=cwd or ROOT, env=self.environment,
                                capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result.stdout.strip()

    def test_project_run_build_and_self_box(self):
        with tempfile.TemporaryDirectory(prefix="b-ci-project-") as scratch:
            temp = Path(scratch)
            project = temp / "project with spaces"
            project.mkdir()
            (project / "main.c").write_text(
                '#include <stdio.h>\n'
                'int main(int argc, char **argv) {\n'
                '    printf("project:");\n'
                '    for (int index = 1; index < argc; ++index) printf("%s%s", index == 1 ? "" : " ", argv[index]);\n'
                '    putchar(\'\\n\');\n'
                '    return 0;\n'
                '}\n'
            )

            help_text = self.invoke("--help")
            self.assertIn("b breeze exec", help_text)
            self.assertIn("b run <exec|instance>", help_text)
            self.invoke("project", "ci-smoke", project)
            refresh = self.invoke("refresh", "ci-smoke")
            self.assertIn("1 adapter files indexed", refresh)
            index = self.home / "projects" / "ci-smoke" / "index.tsv"
            self.assertIn("main.c", index.read_text())

            self.assertEqual(self.invoke("breeze", "exec", "main.c", "--in", "ci-smoke",
                                         "--", "one", "two"), "project:one two")
            self.assertEqual(self.invoke("run", "exec", "main.c", "--in", "ci-smoke",
                                         "--", "alias"), "project:alias")

            standalone = temp / "standalone.c"
            standalone.write_text('int main(void) { return 0; }\n')
            self.assertEqual(self.invoke("breeze", "exec", standalone), "")

            stage = temp / "b-stage"
            stage.mkdir()
            core_name = "b-core.exe" if os.name == "nt" else "b-core"
            shutil.copy2(self.binary, stage / core_name)
            shutil.copy2(ROOT / "LICENSE", stage / "LICENSE")
            shutil.copy2(ROOT / "README.md", stage / "README.md")
            wrapper_name = "b.cmd" if os.name == "nt" else "b"
            wrapper = stage / wrapper_name
            if os.name == "nt":
                wrapper.write_text(
                    '@echo off\r\n'
                    'if not defined B_HOME set "B_HOME=%LOCALAPPDATA%\\b"\r\n'
                    '"%~dp0b-core.exe" %*\r\n'
                    'exit /b %ERRORLEVEL%\r\n'
                )
            else:
                wrapper.write_text(
                    '#!/bin/sh\n'
                    'set -eu\n'
                    'here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                    'if [ -z "${B_HOME:-}" ]; then\n'
                    '    case "$(uname -s)" in\n'
                    '        Darwin) B_HOME="$HOME/Library/Application Support/b" ;;\n'
                    '        *) B_HOME="${XDG_CACHE_HOME:-$HOME/.cache}/b" ;;\n'
                    '    esac\n'
                    'fi\n'
                    'mkdir -p "$B_HOME"\n'
                    'export B_HOME\n'
                    'exec "$here/b-core" "$@"\n'
                )
                wrapper.chmod(0o755)

            manifest = {
                "schemaVersion": 1,
                "application": {
                    "id": "org.vexgraph.b",
                    "name": "b",
                    "version": "0.1.0",
                    "authors": ["Vexgraph"],
                    "publisher": "Vexgraph",
                    "description": "Build, breeze, box.",
                    "license": "MIT",
                },
                "payload": {
                    "entry": core_name,
                    "files": [
                        {"source": wrapper_name, "destination": wrapper_name},
                        {"source": "LICENSE", "destination": "LICENSE"},
                        {"source": "README.md", "destination": "README.md"},
                    ],
                },
                "targets": [],
                "formats": ["portable"],
                "statePolicy": "clean",
            }
            manifest_path = stage / "manifest.json"
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
            package = temp / "boxed-b"
            self.invoke("box", manifest_path, package)
            for name in (core_name, wrapper_name, "LICENSE", "README.md"):
                self.assertTrue((package / name).is_file(), f"self-box omitted {name}")
            self.assertEqual(
                (package / core_name).read_bytes(), self.binary.read_bytes(),
                "self-box changed executable bytes",
            )
            self.assertEqual(self.invoke("--help"), help_text)

            target = os.environ.get("B_TARGET", "local")
            assets = temp / "assets"
            subprocess.run(
                [sys.executable, str(ROOT / "scripts" / "package_release.py"),
                 "--binary", str(self.binary), "--target", target,
                 "--version", "0.1.0", "--output-dir", str(assets)],
                cwd=ROOT, env=self.environment, check=True, timeout=120,
            )
            archive = assets / f"b-{target}.tar.gz"
            checksum = assets / f"b-{target}.tar.gz.sha256"
            self.assertTrue(archive.is_file())
            self.assertTrue(checksum.is_file())
            with tarfile.open(archive, "r:gz") as bundle:
                names = {Path(name).name for name in bundle.getnames()}
            self.assertTrue({core_name, wrapper_name, "LICENSE", "README.md"}.issubset(names))
            unpacked = temp / "unpacked-release"
            unpacked.mkdir()
            with tarfile.open(archive, "r:gz") as bundle:
                bundle.extractall(unpacked)
            self.assertEqual(
                (unpacked / core_name).read_bytes(), self.binary.read_bytes(),
                "release archive changed executable bytes",
            )
            if os.name == "nt":
                packaged_binary = unpacked / core_name
                result = subprocess.run([str(packaged_binary), "--help"],
                                        env=self.environment, capture_output=True,
                                        text=True, timeout=30)
            else:
                result = subprocess.run([str(unpacked / wrapper_name), "--help"],
                                        env=self.environment, capture_output=True,
                                        text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("b breeze exec", result.stdout)

            install_dir = temp / "installed-bin"
            install_state = temp / "installed-state"
            install_environment = dict(
                self.environment,
                B_VERSION="0.1.0",
                B_DOWNLOAD_BASE_URL=assets.resolve().as_uri(),
                B_INSTALL_DIR=str(install_dir),
                B_INSTALL_STATE_DIR=str(install_state),
            )
            for script_name in ("install.sh", "install.ps1"):
                installer = (ROOT / script_name).read_text()
                self.assertIn(
                    "https://vex-graph.github.io/b/downloads/latest",
                    installer,
                )
                self.assertIn(
                    "https://github.com/vex-graph/b/releases/download/",
                    installer,
                )

            if os.name == "nt":
                script = subprocess.run(
                    ["cygpath", "-w", str(ROOT / "install.ps1")],
                    check=True, capture_output=True, text=True, timeout=15,
                ).stdout.strip()
                install = subprocess.run(
                    ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                     "-File", script],
                    cwd=ROOT, env=dict(install_environment, B_INSTALL_NO_PATH="1"),
                    capture_output=True, text=True, timeout=90,
                )
                installed_files = ("b.cmd", "b-core.exe")
                packaged_launcher = ["cmd.exe", "/c", str(install_dir / "b.cmd"), "--help"]
            else:
                install = subprocess.run(
                    ["sh", str(ROOT / "install.sh")], cwd=ROOT,
                    env=install_environment, capture_output=True, text=True, timeout=90,
                )
                installed_files = ("b", "b-core")
                packaged_launcher = [str(install_dir / "b"), "--help"]
            self.assertEqual(install.returncode, 0, install.stdout + install.stderr)
            for name in installed_files:
                self.assertTrue((install_dir / name).is_file(), f"installer omitted {name}")
            record = install_state / "org.vexgraph.b.record"
            self.assertIn("application=org.vexgraph.b", record.read_text())
            installed_help = subprocess.run(
                packaged_launcher, cwd=ROOT, env=install_environment,
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(installed_help.returncode, 0,
                             installed_help.stdout + installed_help.stderr)
            self.assertIn("b breeze exec", installed_help.stdout)


if __name__ == "__main__":
    unittest.main()
