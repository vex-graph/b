"""Build/run the box manifest owner with assertions and ASan/UBSan.

The C owner captures its own stderr, asserts the exact diagnostic count and the
message fragments for every rejected case, and writes an empty stderr on
success; the run below additionally proves the declared payload layout.
"""
import os
from pathlib import Path
import subprocess
from adapter_support import AdapterCase, ROOT


class BoxTest(AdapterCase):
    def test_manifest_validation_and_portable_export(self):
        self.require_tool("cc", "--version")
        output = self.project / "box-test"
        source = Path(__file__).with_suffix(".c")
        command = ["cc", "-std=gnu23", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(ROOT),
                   str(source), str(ROOT / "box.c"), str(ROOT / "util.c"), "-o", str(output)]
        compiled = subprocess.run(command, capture_output=True, text=True, timeout=120)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(output)], cwd=self.project, capture_output=True,
                                text=True, timeout=120,
                                env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertTrue((self.project / "release" / "resources" / "logo.txt").is_file())
        self.assertTrue((self.project / "release" / "nested" / "deep.txt").is_file())
        self.assertFalse(list(self.project.glob(".b-box-staging-*")))


if __name__ == "__main__":
    import unittest
    unittest.main(verbosity=2)
