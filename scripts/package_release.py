"""Box B's native CLI and produce a checksummed portable target archive."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "darwin-arm64": False,
    "darwin-x86_64": False,
    "linux-arm64": False,
    "linux-x86_64": False,
    "windows-arm64": True,
    "windows-x86_64": True,
}


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--binary", required=True, help="compiled B CLI to package")
    result.add_argument("--target", choices=sorted(TARGETS), required=True)
    result.add_argument("--version", required=True)
    result.add_argument("--output-dir", type=Path, required=True)
    return result


def main():
    args = parser().parse_args()
    binary = Path(args.binary).expanduser()
    if not binary.is_file() and not binary.suffix and Path(str(binary) + ".exe").is_file():
        binary = Path(str(binary) + ".exe")
    if not binary.is_file():
        parser().error(f"compiled B CLI does not exist: {binary}")
    version = args.version.removeprefix("v")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?", version):
        parser().error("version must be semantic version text, optionally prefixed by 'v'")

    windows = TARGETS[args.target]
    core_name = "b-core.exe" if windows else "b-core"
    wrapper_name = "b.cmd" if windows else "b"
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    archive = output_dir / f"b-{args.target}.tar.gz"
    checksum = output_dir / f"b-{args.target}.tar.gz.sha256"
    if archive.exists() or checksum.exists():
        parser().error(f"refusing to overwrite existing release assets in {output_dir}")

    with tempfile.TemporaryDirectory(prefix="b-box-release-") as scratch:
        stage = Path(scratch) / "stage"
        stage.mkdir()
        shutil.copy2(binary, stage / core_name)
        shutil.copy2(ROOT / "LICENSE", stage / "LICENSE")
        shutil.copy2(ROOT / "README.md", stage / "README.md")
        wrapper = stage / wrapper_name
        if windows:
            wrapper.write_text(
                "@echo off\r\n"
                'if not defined B_HOME set "B_HOME=%LOCALAPPDATA%\\b"\r\n'
                '"%~dp0b-core.exe" %*\r\n'
                "exit /b %ERRORLEVEL%\r\n"
            )
        else:
            wrapper.write_text(
                "#!/bin/sh\n"
                "set -eu\n"
                'here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                'if [ -z "${B_HOME:-}" ]; then\n'
                '    case "$(uname -s)" in\n'
                '        Darwin) B_HOME="$HOME/Library/Application Support/b" ;;\n'
                '        *) B_HOME="${XDG_CACHE_HOME:-$HOME/.cache}/b" ;;\n'
                "    esac\n"
                "fi\n"
                'mkdir -p "$B_HOME"\n'
                "export B_HOME\n"
                'exec "$here/b-core" "$@"\n'
            )
            wrapper.chmod(0o755)

        manifest = {
            "schemaVersion": 1,
            "application": {
                "id": "org.vexgraph.b",
                "name": "b",
                "version": version,
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
        package = Path(scratch) / "boxed"
        result = subprocess.run([str(binary), "box", str(manifest_path), str(package)],
                                cwd=ROOT, env=dict(os.environ), capture_output=True,
                                text=True, timeout=120)
        if result.returncode != 0:
            raise SystemExit(result.stdout + result.stderr)

        with tarfile.open(archive, "w:gz", format=tarfile.PAX_FORMAT) as bundle:
            for path in sorted(package.iterdir()):
                bundle.add(path, arcname=path.name, recursive=True)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksum.write_text(f"{digest}  {archive.name}\n")
    print(archive)
    print(checksum)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
