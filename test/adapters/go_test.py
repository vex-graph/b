"""Go owner: real offline package/file builds, argv/status and stale-run rejection.
No automatic module creation, dependency install or executable claim for libraries.
"""
from pathlib import Path
from adapter_support import AdapterCase


class GoTest(AdapterCase):
    def setUp(self):
        super().setUp()
        self.require_tool("go", "version")
        self.environment = dict(self.environment, GOTOOLCHAIN="local", GOPROXY="off", GOSUMDB="off")

    def test_file_modes_arguments_failure_and_recovery(self):
        source = self.source("main.go", 'package main\nimport("fmt";"os";"strings")\n'
                             'func main(){fmt.Println(strings.Join(os.Args[1:], "|")); os.Exit(7)}\n')
        args = ["", "a b", "$(touch marker)", *map(str, range(10))]
        for mode in ("instance", "exec"):
            result = self.invoke("run", mode, source, "--", *args, expected=7 if mode == "exec" else 1)
            self.assertEqual(result.stdout.strip(), "|".join(args))
            if mode == "exec":
                self.assertEqual(result.returncode, 7)
        self.assertFalse((self.project / "marker").exists())
        source.write_text("package main\nfunc main(\n")
        self.assertNotIn("a b", self.invoke("run", "exec", source, expected=None).stdout)
        source.write_text('package main\nfunc main(){}\n')
        self.invoke("go", source)

    def test_package_build_external_output_and_missing_dependency(self):
        self.source("go.mod", "module example.test/local\n\ngo 1.20\n")
        source = self.source("main.go", "package main\nfunc main(){}\n")
        output = Path(self.invoke("build", "go", self.project).stdout.strip())
        self.assertTrue(output.is_file())
        self.assertFalse(output.is_relative_to(self.project))
        self.invoke("run", "exec", output)
        source.write_text('package main\nimport _ "example.invalid/uncached"\nfunc main(){}\n')
        self.invoke("build", "go", self.project, expected=None)
        source.write_text("package main\nfunc main(){}\n")
        self.invoke("build", "go", self.project)

    def test_missing_module_and_tool_do_not_create_sources(self):
        self.invoke("build", "go", self.project, expected=None)
        source = self.source("main.go", "package main\nfunc main(){}\n")
        environment = dict(self.environment, GO="missing-go")
        self.invoke("run", "exec", source, environment=environment, expected=None)
        self.assertFalse((self.project / "go.mod").exists())


if __name__ == "__main__":
    import unittest
    unittest.main(verbosity=2)
