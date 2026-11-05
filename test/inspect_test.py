"""Standalone discovery: no tool execution; selected failures and PATH boundaries.

CLI arguments are literal. Doctor's promise is executable presence, not versions,
hardware, SDKs, dependency health or compilation success.
"""
from adapter_support import AdapterCase, ROOT


class InspectTest(AdapterCase):
    def test_discovery_alias_matches_and_includes_nonlanguages(self):
        canonical = self.invoke("adapters")
        alias = self.invoke("languages")
        self.assertEqual(canonical.stdout, alias.stdout)
        self.assertEqual(canonical.stderr + alias.stderr, "")
        for name in ("cmake", "npm", "cargo", "html", "arduino", "go", "lua", "zig"):
            self.assertIn(name, canonical.stdout)
        self.assertIn("NOT type-checking", canonical.stdout)
        self.assertIn("b adapters", self.invoke("--help").stdout)
        self.invoke("adapters", "extra", expected=None)
        self.invoke("languages", "extra", expected=None)
        self.invoke("doctor", "lua", "extra", expected=None)

    def test_missing_unknown_and_full_report_recovery(self):
        environment = self.missing_tool_environment()
        environment.update(LUA="missing-lua", LUAC="missing-luac")
        result = self.invoke("doctor", "lua", environment=environment, expected=None)
        self.assertIn("MISSING", result.stdout)
        self.assertEqual(result.stderr.count("[vex]"), 1)
        self.assertIn("missing executable requirements", result.stderr)
        result = self.invoke("doctor", "does-not-exist", expected=None)
        self.assertIn("unknown adapter", result.stderr)
        self.assertEqual(result.stderr.count("[vex]"), 1)
        full = self.invoke("doctor", environment=environment)
        self.assertIn("MISSING", full.stdout)
        self.assertEqual(full.stderr, "")
        result = self.invoke("doctor", "shell", environment=environment)
        self.assertNotIn("MISSING", result.stdout)
        self.assertEqual(result.stderr, "")

    def test_literal_override_executable_permission_and_no_execution(self):
        tool = self.source("fake lua ; literal", "#!/bin/sh\necho ran > marker\nexit 99\n")
        tool.chmod(0o755)
        environment = dict(self.environment, LUA=str(tool), LUAC=str(tool))
        result = self.invoke("doctor", "lua", environment=environment)
        self.assertIn(str(tool), result.stdout)
        self.assertFalse((self.project / "marker").exists())
        tool.chmod(0o644)
        self.invoke("doctor", "lua", environment=environment, expected=None)
        environment.update(LUA=str(self.project), LUAC=str(self.project))
        self.invoke("doctor", "lua", environment=environment, expected=None)

    def test_empty_path_entry_and_empty_override_use_defaults(self):
        self.source("lua", "#!/bin/sh\nexit 99\n").chmod(0o755)
        self.source("luac", "#!/bin/sh\nexit 99\n").chmod(0o755)
        environment = self.missing_tool_environment()
        environment.update(PATH=environment["PATH"] + ":", LUA="", LUAC="")
        result = self.invoke("doctor", "lua", environment=environment)
        self.assertNotIn("MISSING", result.stdout)

    def test_no_stale_language_contract_in_sources(self):
        self.assertFalse(any((ROOT / "languages").glob("*.[ch]")))
        paths = [ROOT / "b.c", ROOT / "inspect.c", ROOT / "inspect.h",
                 *sorted((ROOT / "adapters").glob("*.[ch]"))]
        for path in paths:
            with self.subTest(path=path.name):
                text = path.read_text()
                self.assertNotIn("Language_", text)
                self.assertNotIn("const Language", text)
                self.assertNotIn("languages/", text)


if __name__ == "__main__":
    import unittest
    unittest.main(verbosity=2)
