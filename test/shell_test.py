"""POSIX sh execution and non-evaluating syntax validation."""
from pathlib import Path
from adapter_support import AdapterCase


class ShellTest(AdapterCase):
    def test_modes_preserve_literal_arguments_and_status(self):
        source = self.source("hello with spaces.sh", 'printf "%s\\n" "$@"\nexit 9\n')
        for command in (("shell",), ("run", "instance"), ("run", "exec")):
            result = self.invoke(*command, source, "--", "Hello World", "$(touch NEVER)", expected=9)
            self.assertEqual(result.stdout.splitlines(), ["Hello World", "$(touch NEVER)"])
        self.assertFalse((self.project / "NEVER").exists())

    def test_syntax_only_checks_and_recovery(self):
        self.invoke("build", "shell", self.project, expected=1)
        source = self.source("hello.sh", 'touch NEVER\n')
        self.assertEqual(Path(self.invoke("build", "shell", self.project).stdout.strip()), self.project.resolve())
        self.assertFalse((self.project / "NEVER").exists())
        source.write_text("if then\n")
        self.invoke("build", "shell", self.project, expected=None)
        source.write_text('printf "recovered\\n"\n')
        self.assertEqual(self.invoke("shell", source).stdout.strip(), "recovered")
