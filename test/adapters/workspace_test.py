"""Recursive workspace owner: assessment precedes tools; native units never run.

Real C/Python builds, manifest ownership, exclusions, review gates, growth and
failure recovery. Native manifests/scripts and include paths remain trusted;
filesystem mutation during build is outside the documented contract.
"""
from pathlib import Path
import os
import platform
import shutil
import subprocess
from adapter_support import AdapterCase, ROOT


class WorkspaceBuildTest(AdapterCase):
    def nested(self, name, text):
        path = self.project / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def test_recursive_plan_is_read_only_and_build_reports_each_unit(self):
        self.nested("a native/main.c", 'int main(void){return 19;}\n')
        self.nested("z scripts/good.py", 'raise RuntimeError("must not execute")\n')
        self.nested("z scripts/readme.txt", "unassigned\n")
        self.nested("node_modules/evil.c", "broken")
        self.nested("build/evil.py", "broken")
        outside = self.project.parent / "external source"
        outside.mkdir(exist_ok=True)
        (outside / "bad.c").write_text("broken")
        (self.project / "linked-directory").symlink_to(outside, target_is_directory=True)
        before = sorted(str(p) for p in (self.home / "state").rglob("*"))
        plan = self.invoke("build", "workspace", self.project, "--plan")
        self.assertIn("2 build units", plan.stdout)
        self.assertNotIn("[1/", plan.stdout)
        self.assertEqual(before, sorted(str(p) for p in (self.home / "state").rglob("*")))
        result = self.invoke("build", "workspace", self.project)
        self.assertIn("[1/2] Building c", result.stdout)
        self.assertIn("[2/2] Building python", result.stdout)
        self.assertIn("Building 50%", result.stdout)
        self.assertIn("Building 100%", result.stdout)
        self.assertIn("2 succeeded, 0 failed", result.stdout)
        self.assertEqual(result.stderr, "")

    def test_failure_continues_and_retry_recovers(self):
        source = self.nested("a/main.c", "broken C\n")
        self.nested("z/check.py", "pass\n")
        result = self.invoke("build", "workspace", self.project, expected=None)
        self.assertIn("[2/2]", result.stdout)
        self.assertIn("1 succeeded, 1 failed", result.stdout)
        source.write_text("int main(void){return 0;}\n")
        self.assertIn("2 succeeded", self.invoke("build", "workspace", self.project).stdout)

    def test_manifest_owns_subtree_and_ambiguity_rejects_before_tools(self):
        self.nested("package/Cargo.toml", "not valid toml")
        self.nested("package/src/main.rs", "broken")
        self.nested("package/extra/bad.py", "broken")
        plan = self.invoke("build", "workspace", self.project, "--plan")
        self.assertIn("1 build units", plan.stdout)
        self.assertIn("cargo:", plan.stdout)
        self.assertNotIn("python:", plan.stdout)
        self.nested("package/package.json", "{}")
        result = self.invoke("build", "workspace", self.project, expected=None)
        self.assertIn("multiple project manifests", result.stderr)
        self.assertNotIn("Building", result.stdout)

    def test_source_symlinks_and_mixed_languages_reject(self):
        source = self.nested("main.c", "int main(void){return 0;}\n")
        (self.project / "alias.c").symlink_to(source)
        self.assertIn("symlink", self.invoke("build", "workspace", self.project, expected=None).stderr)
        (self.project / "alias.c").unlink()
        cpp = self.source("other.cpp", "int main(){return 0;}")
        self.assertIn("mixed C-family", self.invoke("build", "workspace", self.project, expected=None).stderr)
        cpp.unlink()
        self.source("script.py", "raise RuntimeError('never executed')")
        self.assertIn("2 build units", self.invoke("build", "workspace", self.project, "--plan").stdout)
        self.invoke("build", "workspace", self.project)

    def test_real_manifest_project_builds_without_running_program(self):
        self.require_tool("cmake", "--version")
        self.nested("native/CMakeLists.txt", "cmake_minimum_required(VERSION 3.24)\n"
                    "project(local C)\nadd_executable(local src/main.c)\n")
        self.nested("native/src/main.c", 'int main(void){return 37;}\n')
        self.nested("native/nested/broken.py", "broken python [")
        result = self.invoke("build", "workspace", self.project)
        self.assertIn("1 build units", result.stdout)
        self.assertIn("1 succeeded, 0 failed", result.stdout)
        self.assertNotIn("[vex]", result.stderr)

    def test_manifest_symlink_rejects_before_build(self):
        target = self.source("manifest.txt", "{}")
        (self.project / "package.json").symlink_to(target)
        result = self.invoke("build", "workspace", self.project, expected=None)
        self.assertIn("regular non-symlink", result.stderr)
        self.assertNotIn("Building", result.stdout)

    def test_apple_leaks_tool_reports_no_leaks_on_plan_and_shader_build(self):
        """Apple ASan cannot detect leaks; use the OS leaks tool on the CLI seam."""
        if platform.system() != "Darwin" or not Path("/usr/bin/leaks").exists():
            self.skipTest("Apple leaks tool unavailable; leak proof unproved on this host")
        tools = self.project / "leaky-tools"
        tools.mkdir()
        for name in ("uname", "dirname", "mkdir", "cat", "sh"):
            path = shutil.which(name)
            if path is None:
                self.skipTest(f"launcher utility {name} unavailable")
            (tools / name).symlink_to(path)
        base = [os.environ.get("CC", "cc"), "-std=gnu23", "-Wall", "-Wextra", "-Werror",
                "-g", "-I", str(ROOT), str(ROOT / "b.c"), str(ROOT / "inspect.c"),
                str(ROOT / "box.c"),
                str(ROOT / "util.c"), *map(str, sorted((ROOT / "adapters").glob("*.c")))]
        cli = self.project / "leak-cli"
        subprocess.run([*base, "-o", str(cli)], capture_output=True, check=True, timeout=120)
        tree = self.project / "leak-tree"
        (tree / "nested").mkdir(parents=True)
        (tree / "nested" / "script.py").write_text("raise Exception('never executed')\n")
        (tree / "shader.comp").write_text("shader")
        tool = tools / "copy-compiler"
        tool.write_text("#!/bin/sh\ncp \"$3\" \"$5\"\n")
        tool.chmod(0o755)
        environment = dict(self.environment, PATH=str(tools), GLSLC=str(tool))

        def leak_check(*command):
            result = subprocess.run(["/usr/bin/leaks", "--atExit", "--", str(cli), *command],
                                    env=environment, capture_output=True, text=True, timeout=120)
            self.assertIn("0 leaks for 0 total leaked bytes", result.stdout + result.stderr)

        leak_check("build", "workspace", str(tree), "--plan")
        leak_check("build", "glsl", str(tree))

    def test_broad_scope_confirmation_empty_and_invalid_forms(self):
        downloads = self.project / "Downloads"
        downloads.mkdir()
        (downloads / "main.c").write_text("int main(void){return 0;}\n")
        self.invoke("build", "workspace", downloads, "--plan")
        rejected = self.invoke("build", "workspace", downloads, expected=None)
        self.assertIn("--confirm", rejected.stderr)
        self.assertNotIn("Building", rejected.stdout)
        self.invoke("build", "workspace", downloads, "--confirm")
        home = dict(self.environment, HOME=str(downloads))
        self.invoke("build", "workspace", downloads, environment=home, expected=None)
        for args in ((), (self.project, "--bad"), (self.project, "--plan", "extra"),
                     (self.project / "missing",), (downloads / "main.c",)):
            self.assertIn("[vex]", self.invoke("build", "workspace", *args, expected=None).stderr)
        empty = self.project / "empty"
        empty.mkdir()
        self.assertIn("no buildable units", self.invoke("build", "workspace", empty).stdout)

    def test_inventory_grows_and_control_names_reject(self):
        for i in range(40):
            self.nested(f"dir {i:02d}/script.py", "pass\n")
        result = self.invoke("build", "workspace", self.project, "--plan")
        self.assertIn("40 build units", result.stdout)
        bad = self.source("evil\nname.py", "pass")
        self.assertIn("control characters", self.invoke("build", "workspace", self.project, expected=None).stderr)
        bad.unlink()
        for i in range(1000):
            self.source(f"note{i}.txt", "")
        self.assertIn("broad workspace", self.invoke("build", "workspace", self.project, expected=None).stderr)

    def test_sanitized_public_clients_and_real_recursive_build(self):
        client = self.source("public-client.txt", r'''
#include "adapters/workspace.h"
#include "adapters/glsl.h"
#include "adapters/metal.h"
#include <assert.h>
#include <stdint.h>
int main(void) {
    char *bad[] = { ".", nullptr };
    assert(Workspace_command(0, nullptr) != 0);
    assert(Workspace_command(1, nullptr) != 0);
    assert(Workspace_command(2, bad) != 0);
    const Adapter *a = &GLSL_ADAPTER;
    const Adapter *b = &METAL_ADAPTER;
    char *output = (char*) (uintptr_t) 1;
    assert((*a).build(nullptr, &output) != 0);
    assert((*a).build(".", nullptr) != 0);
    assert((*b).build(nullptr, &output) != 0);
    assert((*b).build(".", nullptr) != 0);
    assert(output == (char*) (uintptr_t) 1);
    return 0;
}
''')
        common = [os.environ.get("CC", "cc"), "-std=gnu23", "-Wall", "-Wextra", "-Werror",
                  "-fsanitize=address,undefined", "-g", "-I", str(ROOT)]
        dependencies = [str(ROOT / "util.c"),
                        *map(str, sorted((ROOT / "adapters").glob("*.c")))]
        binary = self.project / "client"
        subprocess.run([*common, "-x", "c", str(client), *dependencies, "-o", str(binary)],
                       capture_output=True, check=True, timeout=120)
        # Apple ASan aborts with leak detection enabled; ASan/UBSan remain active.
        # This run does not prove leaks on macOS (explicit platform gap).
        env = dict(self.environment, ASAN_OPTIONS="detect_leaks=0" if platform.system() == "Darwin" else "detect_leaks=1")
        result = subprocess.run([str(binary)], env=env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr.count("[vex]"), 7)
        cli = self.project / "sanitized-b"
        subprocess.run([*common, str(ROOT / "b.c"), str(ROOT / "inspect.c"), str(ROOT / "box.c"),
                        *dependencies, "-o", str(cli)], capture_output=True, check=True, timeout=120)
        tree = self.project / "tree"
        tree.mkdir()
        for i in range(40):
            folder = tree / str(i)
            folder.mkdir()
            (folder / "script.py").write_text("raise Exception('not executed')\n")
        for option in ("--plan", None):
            args = [str(cli), "build", "workspace", str(tree)] + ([] if option is None else [option])
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
        shader = self.project / "shaders"
        shader.mkdir()
        (shader / "main.comp").write_text("input")
        (shader / "main.metal").write_text("input")
        tool = self.source("sanitizer compiler fixture", "#!/usr/bin/env python3\n"
                           "import os,pathlib,sys\n"
                           "pathlib.Path(sys.argv[-1]).write_bytes(b'compiled')\n"
                           "sys.exit(5 if os.getenv('SHADER_FAIL') else 0)\n")
        tool.chmod(0o755)
        for adapter in ("glsl", "metal"):
            if adapter == "metal" and platform.system() != "Darwin":
                continue
            shader_env = dict(env, GLSLC=str(tool), XCRUN=str(tool), GLSL_BACKEND="shaderc")
            args = [str(cli), "build", adapter, str(shader)]
            result = subprocess.run(args, env=shader_env, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            output = Path(result.stdout.strip())
            before = {p.name: p.read_bytes() for p in output.iterdir()}
            result = subprocess.run(args, env=dict(shader_env, SHADER_FAIL="1"),
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 5, result.stderr)
            self.assertNotIn("Sanitizer", result.stderr)
            self.assertEqual(before, {p.name: p.read_bytes() for p in output.iterdir()})


if __name__ == "__main__":
    import unittest
    unittest.main(verbosity=2)
