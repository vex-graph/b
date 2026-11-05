"""Lua owner: real script modes/argv/status, non-evaluating parse and recovery.
No binary production, engine-specific dialect, concurrency or package install claim.
"""
from adapter_support import AdapterCase


class LuaTest(AdapterCase):
    def setUp(self):
        super().setUp()
        self.require_tool("lua", "-v")
        self.require_tool("luac", "-v")

    def test_both_modes_literal_arguments_and_exit_status(self):
        source = self.source("hello ; literal.lua", 'print(table.concat(arg, "|"))\nos.exit(7)\n')
        args = ["", "a b", "$(touch marker)", *map(str, range(10))]
        for mode in ("instance", "exec"):
            result = self.invoke("run", mode, source, "--", *args, expected=7)
            self.assertEqual(result.stdout.strip(), "|".join(args))
        self.assertFalse((self.project / "marker").exists())
        source.write_text('print("recovered")\n')
        self.assertEqual(self.invoke("lua", source).stdout.strip(), "recovered")

    def test_build_does_not_evaluate_and_invalid_recovers(self):
        source = self.source("main.lua", 'error("never evaluate during build")\n')
        self.assertEqual(self.invoke("build", "lua", self.project).stdout.strip(), str(self.project.resolve()))
        self.assertFalse((self.project / "luac.out").exists())
        source.write_text("function (\n")
        self.invoke("build", "lua", self.project, expected=None)
        source.write_text("return 1\n")
        self.invoke("build", "lua", self.project)

    def test_empty_missing_tool_and_overrides(self):
        self.invoke("build", "lua", self.project, expected=None)
        source = self.source("empty.lua", "")
        self.invoke("build", "lua", self.project)
        self.invoke("run", "instance", source)
        environment = dict(self.environment, LUA="missing-lua", LUAC="missing-luac")
        self.invoke("build", "lua", self.project, environment=environment, expected=None)
        self.invoke("run", "exec", source, environment=environment, expected=None)


if __name__ == "__main__":
    import unittest
    unittest.main(verbosity=2)
