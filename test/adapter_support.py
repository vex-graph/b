"""Shared isolated-project fixtures; no language scenarios live in this helper."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

_ROOT_CANDIDATES = [
    Path(__file__).resolve().parents[3] / "vexgraph-hobby-projects/b",
    Path(__file__).resolve().parents[2] / "suite/personal/b",
    Path(__file__).resolve().parents[2] / "personal/b",
]
ROOT = next((p for p in _ROOT_CANDIDATES if p.exists()), _ROOT_CANDIDATES[0])


class AdapterCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix="b-adapters-")
        cls.addClassCleanup(cls.scratch.cleanup)
        cls.home = Path(cls.scratch.name)
        cls.environment = dict(os.environ, B_HOME=str(cls.home / "state"),
                               npm_config_loglevel="silent", npm_config_update_notifier="false",
                               npm_config_cache=str(cls.home / "npm-cache"))
        b_cmd = ["bash", str(ROOT / "b")] if os.name == "nt" else [str(ROOT / "b")]
        subprocess.run([*b_cmd, "--help"], env=cls.environment,
                       capture_output=True, check=True, timeout=120)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="project with spaces ", dir=self.home)
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name)

    def invoke(self, *args, expected=0, environment=None):
        b_cmd = ["bash", str(ROOT / "b")] if os.name == "nt" else [str(ROOT / "b")]
        result = subprocess.run([*b_cmd, *map(str, args)], cwd=self.project,
                                env=environment or self.environment, capture_output=True,
                                text=True, timeout=120)
        if expected is None:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def source(self, name, text):
        path = self.project / name
        path.write_text(text)
        return path

    def require_tool(self, name, *args):
        if shutil.which(name) is None:
            self.skipTest(f"{name} not installed")
        result = subprocess.run([name, *args], capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            self.skipTest(f"{name} toolchain unavailable")
        return result.stdout

    def missing_tool_environment(self):
        # Keep launcher utilities reachable but exclude language toolchains.
        tools = self.project / "launcher-tools"
        tools.mkdir(exist_ok=True)
        for name in ("uname", "dirname", "mkdir"):
            path = shutil.which(name)
            if path is None:
                self.skipTest(f"launcher utility {name} unavailable")
            target = tools / name
            if not target.exists():
                target.symlink_to(path)
        return dict(self.environment, PATH=str(tools))
