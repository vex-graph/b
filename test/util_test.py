"""Build/run the new shared helper boundaries with assertions and ASan/UBSan."""
import os
import sys
from pathlib import Path
import subprocess
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from adapter_support import AdapterCase, ROOT


class UtilTest(AdapterCase):
    def test_command_boundaries_and_memory_safety(self):
        cc = os.environ.get("CC", "cc")
        self.require_tool(cc, "--version")
        output = self.project / "util-test"
        source = Path(__file__).with_suffix(".c")
        # The utility owner moved under util/ and now includes the R0 platform header,
        # so the harness carries the same include roots the root build gives it.
        lifetime = Path(__file__).resolve().parents[3] / "vexgraph-ecosystem/lifetime/src"
        command = [cc, "-std=gnu23", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-I", str(ROOT), "-I", str(ROOT / "util"), "-I", str(lifetime),
                   str(source), str(ROOT / "util/util.c"), "-o", str(output)]
        compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(output)], cwd=self.project, capture_output=True,
                                text=True, timeout=30, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        diagnostics = result.stderr.splitlines()
        self.assertEqual(len(diagnostics), 11, result.stderr)
        self.assertTrue(all(line.startswith("[vex]") for line in diagnostics), result.stderr)
        self.assertFalse((self.project / "NEVER").exists())


if __name__ == "__main__":
    unittest.main()
