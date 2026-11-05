"""Migration seam: compatibility launcher, workspace metadata and CPU target.

No interactive gallery or visual approval; the builder is exercised through its
existing metadata and registered headless compositor target, not blanket proof.
"""

import json
import os
import re
import shutil
import struct
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class WorkspaceTest(unittest.TestCase):
    def invoke(self, *arguments):
        result = subprocess.run([str(ROOT / "tools/b"), *arguments], cwd=ROOT / "personal/b",
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_doctor_resolves_worktree_not_checkout(self):
        doctor = self.invoke("doctor")
        self.assertIn(str(ROOT), doctor)
        self.assertIn("-Werror", doctor)
        self.assertIn("target vexspoke", doctor)
        self.assertFalse((ROOT / "tools/b.c").exists())

    def test_metadata_and_preserved_shader_wiring(self):
        metadata = json.loads(self.invoke("ide"))
        self.assertIsInstance(metadata, dict)
        self.assertIn("vexspoke", json.dumps(metadata))
        engine = (ROOT / "tools/workspace.c").read_text()
        entries = re.search(r"const char \*compositorShaders\[\] = \{(.*?)\};", engine, re.S)
        self.assertIsNotNone(entries)
        self.assertEqual(re.findall(r'"([^"]+)"', entries.group(1)),
                         ["scatter.vert", "scatter.frag", "resolve.vert", "resolve.frag", "color.frag", "scope.frag"])
        self.assertIn('if (!strcmp(compositorShaders[i], "color.frag"))\n'
                      '            add_gen(strf("%s/src/filter/filter_type.h", base), out, g);', engine)
        readme = (ROOT / "README.md").read_text()
        self.assertIn("`tools/b`", readme)
        self.assertIn("forwarding launcher", readme)

    def test_registered_cpu_target_still_builds_and_runs(self):
        self.assertIn("PASS", self.invoke("test", "compositor_scope_test"))

    def test_reorganized_paths_keep_storage_in_engine_and_cpu_in_vexspoke(self):
        """Assert migrated storage provenance without moving retained CPU reflection."""
        metadata = json.dumps(json.loads(self.invoke("ide")))
        self.assertIn("host-behavior/vexspoke/src", metadata)
        self.assertIn("drivers/graphvex/src", metadata)
        self.assertIn("host-behavior/hotcwap", metadata)
        self.assertIn("interfaces/darling-framework/src", metadata)
        self.assertNotIn("ecosystem/repos/", metadata)
        self.assertNotIn("ecosystem/projects/", metadata)
        self.assertNotIn("ecosystem/drivers/", metadata)
        self.assertNotIn("ecosystem/interface/", metadata)
        self.assertNotIn("personal/relational-engine", metadata)
        self.assertIn("vex/re/src", metadata)
        for part, unit in (("nio", "mem"), ("io", "file")):
            for suffix in (".c", ".h"):
                self.assertTrue((ROOT / "vex/re/src" / part / (unit + suffix)).is_file())
                self.assertFalse((ROOT / "host-behavior/vexspoke/src" / part / (unit + suffix)).exists())
        for part, unit in (("relational", "symbol_table"), ("reflection", "field")):
            for suffix in (".c", ".h"):
                self.assertTrue((ROOT / "host-behavior/vexspoke/src" / part / (unit + suffix)).is_file())

    def test_color_pass_target_has_vulkan_headers_and_loader_link(self):
        self.assert_vulkan_target_client("color_pass_test")

    def test_determinant_registration_is_r2_only_and_excludes_fixtures(self):
        """Exercise registration over disposable names, not a fake runtime library."""
        with tempfile.TemporaryDirectory(prefix="determinant graph ") as scratch:
            home = Path(scratch)
            for relative in ("drivers/determinant/src/cli/command.c",
                             "drivers/determinant/src/terminal/console.c",
                             "drivers/determinant/src/widgets/button.c",
                             "determinant/test/cli/command_test.c",
                             "determinant/test/terminal/console_test.c",
                             "determinant/test/ui/surface/terminal_reload_test.c",
                             "determinant/test/fixtures/view_module.c",
                             "determinant/test/fixtures/ignored_test.c",
                             "determinant/test/terminal/helper.c"):
                path = home / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("")
            client = home / "registration.c"
            client.write_text(r'''
#define main workspaceEntry
#include "workspace.c"
#undef main
#include <assert.h>
int main(int argc, char **argv) {
    assert(argc == 2);
    g_root = argv[1];
    TargetList targets = {0};
    g_out = strf("%s/out", g_root);
    setup_determinant(&targets);
    assert(targets.count == 6);
    const Target *lib = find_target(&targets, "determinant");
    assert(lib != nullptr && (*lib).kind == T_LIB && (*lib).srcs.count == 3);
    assert((*lib).deps.count == 1 && !strcmp((*lib).deps.items[0], "vexspoke"));
    const Target *owner = find_target(&targets, "determinant_console_test");
    assert(owner != nullptr && (*owner).is_test && (*owner).srcs.count == 1);
    assert((*owner).deps.count == 1 && !strcmp((*owner).deps.items[0], "determinant"));
    assert(find_target(&targets, "command_test") != nullptr);
    assert(find_target(&targets, "ignored_test") == nullptr);
    assert(find_target(&targets, "helper") == nullptr);
    const Target *moduleA = find_target(&targets, "determinant_view_a");
    const Target *moduleB = find_target(&targets, "determinant_view_b");
    assert(moduleA != nullptr && moduleB != nullptr);
    assert((*moduleA).kind == T_MOD && (*moduleB).kind == T_MOD);
    assert(sl_has(&(*moduleA).defs, "DETERMINANT_FIXTURE_MARK=65"));
    assert(sl_has(&(*moduleB).defs, "DETERMINANT_FIXTURE_MARK=66"));
    const Target *reload = find_target(&targets, "terminal_reload_test");
    assert(reload != nullptr && (*reload).is_test);
    assert(sl_has(&(*reload).defs, strf("DETERMINANT_MODULE_A=\"%s\"", (*moduleA).out_path)));
    assert(sl_has(&(*reload).defs, strf("DETERMINANT_MODULE_B=\"%s\"", (*moduleB).out_path)));
    g_root = strf("%s/source-free", argv[1]);
    TargetList empty = {0};
    setup_determinant(&empty);
    assert(empty.count == 0);
    return 0;
}
''')
            binary = home / "registration"
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu23", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "tools"), str(client), "-o", str(binary)],
                           capture_output=True, check=True, timeout=120)
            subprocess.run([str(binary), str(home)], capture_output=True, check=True, timeout=30)

    def test_filter_gallery_target_has_vulkan_headers_and_loader_link(self):
        self.assert_vulkan_target_client("filter_gallery_fixture_test")
        engine = (ROOT / "tools/workspace.c").read_text()
        apps = engine.split("static void setup_apps(", 1)[1].split("static void setup_graphvex(", 1)[0]
        for flag in ("-L/opt/homebrew/lib", "-lvulkan", "-Wl,-rpath,/opt/homebrew/lib"):
            self.assertIn(f'strl_push(&(*t).syslibs, "{flag}");', apps)

    def test_gpu_scope_target_has_vulkan_headers_and_loader_link(self):
        # Registration proof needs only filenames, not a partly authored GPU
        # implementation. The real loader clients above prove header/link flags.
        with tempfile.TemporaryDirectory(prefix="b Vulkan targets ") as scratch:
            home = Path(scratch)
            owner = home / "graphvex/test/compositor"
            owner.mkdir(parents=True)
            names = ["vk_renderer_test", "device_test", "gpu_render_test", "resize_clip_test",
                     "surface_gpu_test", "clip_rounded_test", "color_pass_test", "gpu_scope_test",
                     "filter_gallery_fixture_test", "cpu_only_test"]
            for name in names:
                (owner / (name + ".c")).write_text("")
            client = home / "targets.c"
            client.write_text(r'''
#define main workspaceEntry
#include "workspace.c"
#undef main
#include <assert.h>
int main(int argc, char **argv) {
    assert(argc == 2);
    g_root = argv[1];
    TargetList targets = {0};
    setup_graphvex_tests(&targets);
    assert(targets.count == 10);
    for (int i = 0; i < targets.count; ++i) {
        const Target *target = &targets.items[i];
        assert((*target).is_test);
        bool vulkan = strcmp((*target).name, "cpu_only_test") != 0;
        assert(sl_has(&(*target).includes, "/opt/homebrew/include") == vulkan);
        assert(sl_has(&(*target).syslibs, "-L/opt/homebrew/lib") == vulkan);
        assert(sl_has(&(*target).syslibs, "-lvulkan") == vulkan);
        assert(sl_has(&(*target).syslibs, "-Wl,-rpath,/opt/homebrew/lib") == vulkan);
    }
    return 0;
}
''')
            binary = home / "targets"
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu23", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "tools"), str(client), "-o", str(binary)],
                           capture_output=True, check=True, timeout=120)
            subprocess.run([str(binary), str(home)], capture_output=True, check=True, timeout=30)

    def assert_vulkan_target_client(self, name):
        metadata = json.loads(self.invoke("ide"))
        target = next(test for test in metadata["tests"] if test["name"] == name)
        self.assertIn("/opt/homebrew/include", target["includes"])
        loader_flags = ["-L/opt/homebrew/lib", "-lvulkan", "-Wl,-rpath,/opt/homebrew/lib"]
        for flag in loader_flags:
            self.assertIn(flag, target["libraries"])
        with tempfile.TemporaryDirectory(prefix="b Vulkan link ") as scratch:
            home = Path(scratch)
            client = home / "loader.c"
            client.write_text('#include <vulkan/vulkan.h>\n'
                              'int main(void) { return vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance") == 0; }\n')
            binary = home / "loader"
            command = [os.environ.get("CC", "cc"), "-std=gnu23", "-Wall", "-Wextra", "-Werror"]
            for directory in target["includes"]:
                command.extend(["-I", directory])
            command.extend([str(client), *loader_flags, "-o", str(binary)])
            subprocess.run(command, capture_output=True, check=True, timeout=120)
            subprocess.run([str(binary)], capture_output=True, check=True, timeout=30)

    def test_compositor_generators_compile_and_header_changes_invalidate(self):
        """Execute setup_graphvex/run_gens only, without building adjacent passes.

        Input copies and fixed mtimes isolate generator decisions from other
        agents. Real GLSL compilation proves wiring, not GPU rendering.
        """
        if shutil.which("glslangValidator") is None:
            self.skipTest("glslangValidator unavailable")
        with tempfile.TemporaryDirectory(prefix="b shader wiring ") as scratch:
            home = Path(scratch)
            base = home / "drivers/graphvex/src"
            shaders = base / "shaders/compositor"
            shaders.mkdir(parents=True)
            (base / "filter").mkdir()
            owner = ROOT / "drivers/graphvex/src"
            names = ["scatter.vert", "scatter.frag", "resolve.vert", "resolve.frag", "color.frag", "scope.frag"]
            for name in names:
                shutil.copyfile(owner / "shaders/compositor" / name, shaders / name)
            self.assertIn('#include "filter/filter_type.h"', (shaders / "color.frag").read_text())
            header = base / "filter/filter_type.h"
            shutil.copyfile(owner / "filter/filter_type.h", header)
            for path in [header, *(shaders / name for name in names)]:
                os.utime(path, ns=(10**18, 10**18))
            client = home / "generators.c"
            client.write_text(r'''
#define main workspaceEntry
#include "workspace.c"
#undef main
#include <assert.h>
int main(int argc, char **argv) {
    assert(argc == 3);
    g_root = argv[1];
    g_out = argv[2];
    g_verbose = true;
    TargetList targets = {0};
    setup_graphvex(&targets);
    int count = 0;
    int headers = 0;
    for (int i = 0; i < g_genCount; ++i) {
        if (strstr(g_gens[i].out, "/shader/compositor/") == nullptr)
            continue;
        if (strstr(g_gens[i].src, "/filter/filter_type.h") != nullptr)
            ++headers;
        char *includeRoot = strf("-I%s/drivers/graphvex/src", g_root);
        const Cmd *command = &g_gens[i].cmd;
        bool hasCanonicalRoot = false;
        for (int k = 0; k < (*command).count; ++k)
            if (!strcmp((*command).items[k], includeRoot))
                hasCanonicalRoot = true;
        free(includeRoot);
        assert(hasCanonicalRoot);
        g_gens[count++] = g_gens[i];
    }
    assert(count == 7 && headers == 1);
    g_genCount = count;
    run_gens();
    return 0;
}
''')
            binary = home / "generators"
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu23", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "tools"), str(client), "-o", str(binary)],
                           capture_output=True, check=True, timeout=120)
            out = home / "out"

            def generate(expected=0):
                result = subprocess.run([str(binary), str(home), str(out)],
                                        capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                return result

            generate()
            outputs = [out / "shader/compositor" / (name + ".spv") for name in names]
            for path in outputs:
                self.assertEqual(struct.unpack("<I", path.read_bytes()[:4])[0], 0x07230203)
                os.utime(path, ns=(12 * 10**17, 12 * 10**17))
            self.assertEqual(generate().stdout, "")
            header.write_text(header.read_text() + "\n// generator invalidation fixture\n")
            os.utime(header, ns=(13 * 10**17, 13 * 10**17))
            rebuilt = generate().stdout
            self.assertIn("color.frag.spv", rebuilt)
            for name in names:
                if name == "color.frag":
                    continue
                self.assertNotIn(name + ".spv", rebuilt)
            self.assertEqual(generate().stdout, "")
            color = shaders / "color.frag"
            original = color.read_text()
            color.write_text(original + "\ninvalid GLSL syntax !!!\n")
            os.utime(color, ns=(14 * 10**17, 14 * 10**17))
            color_output = outputs[names.index("color.frag")]
            os.utime(color_output, ns=(12 * 10**17, 12 * 10**17))
            self.assertIn("command failed", generate(expected=1).stderr)
            color.write_text(original)
            os.utime(color, ns=(14 * 10**17, 14 * 10**17))
            generate()


if __name__ == "__main__":
    unittest.main(verbosity=2)
