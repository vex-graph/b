"""Real CMake orchestration proof; no install, IDE or GUI operations."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2] / "personal/b"


class CmakeTest(unittest.TestCase):
    def setUp(self):
        if shutil.which("cmake") is None:
            self.skipTest("cmake unavailable")
        self.temp = tempfile.TemporaryDirectory(prefix="b-cmake-")
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        self.project = self.home / "project with spaces"
        self.project.mkdir()
        self.env = dict(os.environ, B_HOME=str(self.home / "state"))

    def invoke(self, *args, expected=0):
        result = subprocess.run([str(ROOT / "b"), *map(str, args)],
                                env=self.env, capture_output=True,
                                text=True, timeout=120)
        if expected is None:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def test_existing_project_configure_build_run_and_rebuild(self):
        manifest = self.project / "CMakeLists.txt"
        manifest.write_text('cmake_minimum_required(VERSION 3.20)\n'
                            'project(hello C)\nset(CMAKE_C_STANDARD 23)\n'
                            'add_executable(hello main.c)\n'
                            'target_compile_options(hello PRIVATE -Wall -Wextra -Werror)\n')
        source = self.project / "main.c"
        source.write_text('#include <stdio.h>\nint main(void) { puts("Hello CMake"); return 0; }\n')
        before = manifest.read_bytes()
        output = Path(self.invoke("build", "cmake", self.project).stdout.strip())
        self.assertTrue(output.is_relative_to(self.home / "state"))
        self.assertEqual(self.invoke("run", "exec", output / "hello").stdout.strip(), "Hello CMake")
        source.write_text('#include <stdio.h>\nint main(void) { puts("updated"); return 0; }\n')
        # Make generators may compare only whole seconds. Establish explicit
        # source-after-object ordering without a sleep or wall-clock race.
        for object_file in output.rglob("*.o"):
            os.utime(object_file, (1, 1))
        os.utime(output / "hello", (1, 1))
        os.utime(source, (2, 2))
        self.invoke("build", "cmake", self.project)
        self.assertEqual(self.invoke("run", "instance", output / "hello").stdout.strip(), "updated")
        self.assertEqual(manifest.read_bytes(), before)
        self.assertFalse((self.project / "CMakeCache.txt").exists())
        self.assertFalse((self.project / "CMakeFiles").exists())

    def test_missing_manifest_and_configure_failure(self):
        self.assertIn("CMakeLists.txt", self.invoke("build", "cmake", self.project, expected=1).stderr)
        (self.project / "CMakeLists.txt").write_text('cmake_minimum_required(VERSION 3.20)\nmessage(FATAL_ERROR "intentional configure failure")\n')
        result = self.invoke("build", "cmake", self.project, expected=1)
        self.assertEqual(result.stdout, "")
        self.assertIn("intentional configure failure", result.stderr)

    def test_build_failure_is_not_reported_as_success(self):
        (self.project / "CMakeLists.txt").write_text('cmake_minimum_required(VERSION 3.20)\nproject(failure C)\nadd_executable(failure broken.c)\n')
        (self.project / "broken.c").write_text("not C code\n")
        result = self.invoke("build", "cmake", self.project, expected=None)
        self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main(verbosity=2)
