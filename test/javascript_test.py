"""Real Node execution, module suffixes and non-evaluating syntax checks."""
from pathlib import Path
from adapter_support import AdapterCase


class JavascriptTest(AdapterCase):
    def setUp(self):
        super().setUp()
        self.require_tool("node", "--version")

    def test_modes_aliases_and_module_suffixes(self):
        for suffix in ("js", "mjs", "cjs"):
            source = self.source(f"hello with spaces.{suffix}",
                                 'console.log(process.argv.slice(2).join("|")); process.exit(8);\n')
            for command in (("run", "exec"), ("run", "instance"), ("javascript",), ("node",), ("js",)):
                result = self.invoke(*command, source, "--", "Hello World", "$(touch NEVER)", expected=8)
                self.assertEqual(result.stdout.strip(), "Hello World|$(touch NEVER)")
        self.assertFalse((self.project / "NEVER").exists())

    def test_build_checks_without_evaluation_and_recovers(self):
        self.invoke("build", "javascript", self.project, expected=1)
        source = self.source("hello.js", 'require("node:fs").writeFileSync("NEVER", "bad");\n')
        checked = Path(self.invoke("build", "javascript", self.project).stdout.strip())
        self.assertEqual(checked, self.project.resolve())
        self.assertFalse((self.project / "NEVER").exists())
        source.write_text("function broken(\n")
        self.invoke("build", "javascript", self.project, expected=None)
        source.write_text('console.log("recovered")\n')
        self.assertEqual(self.invoke("javascript", source).stdout.strip(), "recovered")

    def test_real_esm_imports(self):
        self.source("value.mjs", "export const answer = 42;\n")
        source = self.source("main.mjs", 'import {answer} from "./value.mjs"; console.log(answer);\n')
        self.assertEqual(self.invoke("node", source).stdout.strip(), "42")
        self.invoke("build", "node", self.project)

    def test_extension_sets_do_not_expand_directory_braces(self):
        project = self.project / "literal {one,two} [dir]"
        project.mkdir()
        (project / "one.cjs").write_text("module.exports = 42;\n")
        self.assertEqual(Path(self.invoke("build", "javascript", project).stdout.strip()), project.resolve())

    def test_missing_node_is_observable(self):
        source = self.source("hello.js", 'console.log("Hello World")\n')
        self.assertIn("cannot launch node", self.invoke("node", source, expected=1,
                      environment=self.missing_tool_environment()).stderr)
