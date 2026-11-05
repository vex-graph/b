"""Zig owner: real compile/run, directory selection, build.zig delegation.
Version-specific native project API belongs to Zig; no project executable guessing.
"""
from pathlib import Path
from adapter_support import AdapterCase


class ZigTest(AdapterCase):
    def setUp(self):
        super().setUp()
        self.require_tool("zig", "version")

    def test_both_modes_compile_return_status_and_invalid_recovers(self):
        source = self.source("main.zig", 'pub fn main() u8 { return 7; }\n')
        for mode in ("instance", "exec"):
            self.invoke("run", mode, source, expected=7)
        source.write_text("pub fn main(\n")
        self.invoke("run", "exec", source, expected=None)
        source.write_text("pub fn main() void {}\n")
        self.invoke("zig", source)

    def test_directory_main_single_root_ambiguity_and_external_artifact(self):
        source = self.source("one.zig", "pub fn main() void {}\n")
        artifact = Path(self.invoke("build", "zig", self.project).stdout.strip())
        self.assertTrue(artifact.is_file())
        self.assertFalse(artifact.is_relative_to(self.project))
        self.source("two.zig", "pub fn main() void {}\n")
        self.invoke("build", "zig", self.project, expected=None)
        source.rename(self.project / "main.zig")
        self.invoke("build", "zig", self.project)

    def test_native_build_script_external_prefix_and_bad_script(self):
        self.source("build.zig", 'const std = @import("std");\npub fn build(b: *std.Build) void { _ = b; }\n')
        result = self.invoke("build", "zig", self.project)
        prefix = Path(result.stdout.strip())
        self.assertEqual(prefix.name, "zig-out")
        self.assertFalse(prefix.is_relative_to(self.project))
        self.source("build.zig", "pub fn build(\n")
        self.invoke("build", "zig", self.project, expected=None)

    def test_empty_and_missing_tool_reject(self):
        self.invoke("build", "zig", self.project, expected=None)
        source = self.source("main.zig", "pub fn main() void {}\n")
        environment = dict(self.environment, ZIG="missing-zig")
        self.invoke("run", "exec", source, environment=environment, expected=None)

    def test_literal_argv_forwarding_with_isolated_compiler_fixture(self):
        # The real-tool cases above prove compilation. This fixture isolates b's
        # launch seam from version-specific Zig argv APIs; not language semantics.
        fixture = self.source("argv fixture", '#!/bin/sh\nprintf "%s\\n" "$@"\n')
        fixture.chmod(0o755)
        compiler = self.source("mock zig ; literal", '#!/bin/sh\n'
                               'for arg do\ncase "$arg" in\n'
                               '  -femit-bin=*) cp "$B_ZIG_RUN_FIXTURE" "${arg#-femit-bin=}" ;;\n'
                               'esac\ndone\n')
        compiler.chmod(0o755)
        source = self.source("main.zig", "pub fn main() void {}\n")
        environment = dict(self.environment, ZIG=str(compiler), B_ZIG_RUN_FIXTURE=str(fixture))
        args = ["", "a b", "$(touch marker)", *map(str, range(10))]
        for mode in ("instance", "exec"):
            result = self.invoke("run", mode, source, "--", *args, environment=environment)
            self.assertEqual(result.stdout.splitlines(), args)
        self.assertFalse((self.project / "marker").exists())


if __name__ == "__main__":
    import unittest
    unittest.main(verbosity=2)
