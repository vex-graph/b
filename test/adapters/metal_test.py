"""Metal owner: real Apple compilation if available; offline two-stage faults.
No GPU execution, installed-tool download or cross-platform proof is implied.
"""
import platform
import subprocess
from pathlib import Path
from adapter_support import AdapterCase


class MetalTest(AdapterCase):
    def test_two_stage_failure_preserves_previous_library_and_rejects_run(self):
        source = self.source("main.metal", "source")
        for mode in ("exec", "instance"):
            self.assertIn("build-only", self.invoke("run", mode, source, expected=None).stderr)
        if platform.system() != "Darwin":
            self.assertIn("macOS", self.invoke("build", "metal", self.project, expected=None).stderr)
            return
        tool = self.source("fake xcrun", "#!/usr/bin/env python3\n"
                           "import pathlib,sys\n"
                           "assert sys.argv[1:3]==['-sdk','macosx']\n"
                           "out=pathlib.Path(sys.argv[-1]);out.write_bytes(b'library')\n"
                           "if sys.argv[3]=='metal':\n"
                           " assert '-mmacosx-version-min=14.0' in sys.argv\n"
                           " src=pathlib.Path(sys.argv[sys.argv.index('-c')+1])\n"
                           " sys.exit(9 if src.read_text()=='bad' else 0)\n"
                           "sys.exit(10 if pathlib.Path('fail-link').exists() else 0)\n")
        tool.chmod(0o755)
        env = dict(self.environment, XCRUN=str(tool))
        output = Path(self.invoke("build", "metal", self.project, environment=env).stdout.strip())
        self.assertEqual((output / "main.metal.metallib").read_bytes(), b"library")
        self.assertFalse(any(output.glob("*.air")))
        source.write_text("bad")
        self.invoke("build", "metal", self.project, environment=env, expected=9)
        source.write_text("good")
        marker = self.source("fail-link", "")
        self.invoke("build", "metal", self.project, environment=env, expected=10)
        marker.unlink()
        self.assertEqual(list(output.parent.glob("metal-*")), [output])
        self.invoke("build", "metal", self.project, environment=env)
        self.invoke("build", "metal", self.project, environment=dict(env, XCRUN="missing-xcrun"), expected=None)
        (self.project / "alias.metal").symlink_to(source)
        self.assertIn("non-symlink", self.invoke("build", "metal", self.project, environment=env, expected=None).stderr)

    def test_real_metal_toolchain(self):
        if platform.system() != "Darwin":
            self.skipTest("Apple Metal tools require macOS")
        probe = subprocess.run(["xcrun", "-sdk", "macosx", "metal", "--version"],
                               capture_output=True, timeout=30)
        if probe.returncode != 0:
            self.skipTest("installed Metal compiler unavailable; no download authorized")
        source = self.source("main.metal", "#include <metal_stdlib>\nusing namespace metal;\n"
                             "kernel void fill(device float *out [[buffer(0)]], uint i [[thread_position_in_grid]]){out[i]=1.0;}\n")
        output = Path(self.invoke("build", "metal", self.project).stdout.strip())
        self.assertGreater((output / "main.metal.metallib").stat().st_size, 0)
        source.write_text("malformed metal")
        self.invoke("build", "metal", self.project, expected=None)
        self.assertTrue((output / "main.metal.metallib").is_file())


if __name__ == "__main__":
    import unittest
    unittest.main(verbosity=2)
