"""PHP CLI execution and independent parse checks, with no web services."""
from pathlib import Path
from adapter_support import AdapterCase


class PhpTest(AdapterCase):
    def setUp(self):
        super().setUp()
        self.require_tool("php", "--version")

    def test_modes_literal_arguments_and_exit(self):
        source = self.source("hello with spaces.php", '<?php echo implode("|", array_slice($argv, 1)), "\\n"; exit(7);\n')
        for command in (("php",), ("run", "instance"), ("run", "exec")):
            result = self.invoke(*command, source, "--", "Hello World", "$(touch NEVER)", expected=7)
            self.assertEqual(result.stdout.strip(), "Hello World|$(touch NEVER)")
        self.assertFalse((self.project / "NEVER").exists())

    def test_parse_only_build_and_failure_recovery(self):
        self.invoke("build", "php", self.project, expected=1)
        source = self.source("hello.php", '<?php file_put_contents("NEVER", "bad");\n')
        self.assertEqual(Path(self.invoke("build", "php", self.project).stdout.strip()), self.project.resolve())
        self.assertFalse((self.project / "NEVER").exists())
        source.write_text("<?php function broken(\n")
        self.invoke("build", "php", self.project, expected=None)
        source.write_text('<?php echo "recovered\\n";\n')
        self.assertEqual(self.invoke("php", source).stdout.strip(), "recovered")

    def test_missing_php_is_observable(self):
        source = self.source("hello.php", '<?php echo "Hello World";\n')
        self.assertIn("cannot launch php", self.invoke("php", source, expected=1,
                      environment=self.missing_tool_environment()).stderr)
