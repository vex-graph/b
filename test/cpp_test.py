"""C++23 compiler proof; full project orchestration is tested separately."""
from pathlib import Path
from adapter_support import AdapterCase


class CppTest(AdapterCase):
    def setUp(self):
        super().setUp()
        self.require_tool("c++", "--version")

    def test_suffixes_literal_arguments_status_and_instance_reject(self):
        for suffix in ("cpp", "cc", "cxx"):
            source = self.source(f"hello with spaces.{suffix}", '#include <iostream>\n'
                                 'int main(int argc, char **argv) { for(int i=1;i<argc;++i) std::cout << argv[i] << "\\n"; return 8; }\n')
            result = self.invoke("run", "exec", source, "--", "Hello World", "$(touch NEVER)", expected=8)
            self.assertEqual(result.stdout.splitlines(), ["Hello World", "$(touch NEVER)"])
        self.invoke("cpp", source, expected=1)
        self.assertFalse((self.project / "NEVER").exists())

    def test_directory_multiple_sources_failure_and_compiler_override(self):
        self.invoke("build", "cpp", self.project, expected=1)
        main = self.source("main.cpp", "int answer(); int main() {return answer();}\n")
        self.source("answer.cc", "int answer() {return 14;}\n")
        output = Path(self.invoke("build", "cpp", self.project).stdout.strip())
        self.assertTrue(output.is_relative_to(self.home / "state"))
        self.invoke("run", "exec", output, expected=14)
        main.write_text("not C plus plus\n")
        self.invoke("build", "cpp", self.project, expected=None)
        self.assertIn("cannot launch", self.invoke("build", "cpp", self.project, expected=1,
                      environment=dict(self.environment, CXX="missing-b-test-cxx")).stderr)
