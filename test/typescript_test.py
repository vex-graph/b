"""Native Node type stripping; explicitly not tsc/type-checking proof."""
from pathlib import Path
from adapter_support import AdapterCase


class TypescriptTest(AdapterCase):
    def setUp(self):
        super().setUp()
        version = self.require_tool("node", "--version").strip().lstrip("v")
        major, minor, *_ = map(int, version.split("."))
        if major < 22 or (major == 22 and minor < 18):
            self.skipTest("Native Node type stripping requires recent Node")

    def test_erasable_types_modes_and_suffixes(self):
        for suffix in ("ts", "mts", "cts"):
            source = self.source(f"hello with spaces.{suffix}",
                                 'const hello: string = process.argv.slice(2).join("|");\n'
                                 'console.log(hello); process.exit(6);\n')
            for mode in ("instance", "exec"):
                result = self.invoke("run", mode, source, "--", "Hello World", "$(touch NEVER)", expected=6)
                self.assertEqual(result.stdout.strip(), "Hello World|$(touch NEVER)")

    def test_checks_do_not_claim_type_checking_and_recover(self):
        self.invoke("build", "typescript", self.project, expected=1)
        source = self.source("hello.ts", 'const value: number = "not a number"; console.log(value);\n')
        self.assertEqual(Path(self.invoke("build", "typescript", self.project).stdout.strip()), self.project.resolve())
        self.assertEqual(self.invoke("typescript", source).stdout.strip(), "not a number")
        source.write_text("const broken: = ;\n")
        self.invoke("build", "typescript", self.project, expected=None)
        source.write_text('const value: number = 42; console.log(value);\n')
        self.assertEqual(self.invoke("typescript", source).stdout.strip(), "42")

    def test_nonerasable_syntax_rejects_in_native_runtime(self):
        source = self.source("enum.ts", 'enum Color { Red }\nconsole.log(Color.Red);\n')
        self.assertIn("ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX", self.invoke("typescript", source, expected=None).stderr)

    def test_directory_checks_include_module_suffixes_without_running(self):
        self.source("one.mts", 'const value: number = 42; throw new Error("must not execute");\n')
        self.source("two.cts", "const value: string = 'ok';\n")
        self.invoke("build", "typescript", self.project)
