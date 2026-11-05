"""macOS Foundation proof with strict compiler flags; other hosts are unproved."""
import sys
from pathlib import Path
from adapter_support import AdapterCase


class ObjcTest(AdapterCase):
    def setUp(self):
        super().setUp()
        if sys.platform != "darwin":
            self.skipTest("macOS Foundation adapter")
        self.require_tool("clang", "--version")

    def test_exec_arguments_status_and_instance_rejection(self):
        source = self.source("hello with spaces.m", '#import <Foundation/Foundation.h>\n'
                             '#include <stdio.h>\nint main(int argc, char **argv) {\n'
                             '@autoreleasepool { for (int i=1; i<argc; ++i) puts(argv[i]); } return 7; }\n')
        result = self.invoke("run", "exec", source, "--", "Hello World", "$(touch NEVER)", expected=7)
        self.assertEqual(result.stdout.splitlines(), ["Hello World", "$(touch NEVER)"])
        self.assertFalse((self.project / "NEVER").exists())
        self.assertIn("no source runtime", self.invoke("objc", source, expected=1).stderr)

    def test_directory_sources_compile_failure_and_recovery(self):
        self.invoke("build", "objc", self.project, expected=1)
        source = self.source("main.m", 'int answer(void); int main(void) { return answer(); }\n')
        self.source("answer.m", 'int answer(void) { return 12; }\n')
        output = Path(self.invoke("build", "objc", self.project).stdout.strip())
        self.invoke("run", "exec", output, expected=12)
        source.write_text("not Objective C\n")
        self.invoke("build", "objc", self.project, expected=None)
        source.write_text("int main(void) { return 0; }\n")
        self.invoke("run", "exec", source)
