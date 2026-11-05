"""Real Swift script/native proof: argv, status, directory modules and failures."""
from pathlib import Path
from adapter_support import AdapterCase


class SwiftTest(AdapterCase):
    def setUp(self):
        super().setUp()
        self.require_tool("swiftc", "--version")

    def test_interpreter_and_compiled_entry_preserve_arguments_status(self):
        source = self.source("hello with spaces.swift", 'import Foundation\n'
                             'print(CommandLine.arguments.dropFirst().joined(separator: "|"))\nexit(6)\n')
        for mode in ("instance", "exec"):
            result = self.invoke("run", mode, source, "--", "Hello World", "$(touch NEVER)", expected=6)
            self.assertEqual(result.stdout.strip(), "Hello World|$(touch NEVER)")
        self.assertFalse((self.project / "NEVER").exists())

    def test_directory_build_modules_and_no_source_artifacts(self):
        self.source("main.swift", 'print(answer())\n')
        self.source("value.swift", 'func answer() -> Int { 42 }\n')
        output = Path(self.invoke("build", "swift", self.project).stdout.strip())
        self.assertTrue(output.is_relative_to(self.home / "state"))
        self.assertEqual(self.invoke("run", "exec", output).stdout.strip(), "42")
        self.assertEqual(sorted(p.suffix for p in self.project.iterdir()), [".swift", ".swift"])

    def test_compile_failure_blocks_old_output_and_recovers(self):
        self.invoke("build", "swift", self.project, expected=1)
        source = self.source("hello.swift", 'print("old")\n')
        self.invoke("run", "exec", source)
        source.write_text("not Swift\n")
        self.assertNotIn("old", self.invoke("run", "exec", source, expected=None).stdout)
        source.write_text('print("recovered")\n')
        self.assertEqual(self.invoke("swift", source).stdout.strip(), "recovered")

    def test_missing_swift_tool_is_observable(self):
        source = self.source("hello.swift", 'print("Hello World")\n')
        self.assertIn("cannot launch swift", self.invoke("swift", source, expected=1,
                      environment=self.missing_tool_environment()).stderr)
