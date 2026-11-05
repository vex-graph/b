"""CLI owner proof: public grammar, process status, source builds and rejection.

Temporary projects use real compilers; every child has an external timeout.
Java needs an installed JDK; other platforms and packaging remain gaps.
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


_ROOT_CANDIDATES = [
    Path(__file__).resolve().parents[3] / "vexgraph-hobby-projects/b",
    Path(__file__).resolve().parents[2] / "personal/b",
    Path(__file__).resolve().parents[2] / "suite/personal/b",
]
ROOT = next((p for p in _ROOT_CANDIDATES if p.exists()), _ROOT_CANDIDATES[0])


class CliTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix="b-cli-")
        cls.addClassCleanup(cls.scratch.cleanup)
        cls.home = Path(cls.scratch.name)
        cls.environment = dict(os.environ, B_HOME=str(cls.home / "state"),
                               DOTNET_NOLOGO="1", DOTNET_CLI_TELEMETRY_OPTOUT="1")
        subprocess.run([str(ROOT / "b"), "--help"], env=cls.environment,
                       check=True, capture_output=True, timeout=60)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="project ", dir=self.home)
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name)

    def invoke(self, *arguments, expected=0, cwd=None, environment=None):
        result = subprocess.run([str(ROOT / "b"), *map(str, arguments)],
                                cwd=cwd or self.project,
                                env=environment or self.environment,
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def source(self, name="hello.c", text='int main(void) { return 0; }\n'):
        path = self.project / name
        path.write_text(text)
        return path

    def require_tool(self, executable, *probe):
        try:
            result = subprocess.run([executable, *probe], capture_output=True,
                                    text=True, timeout=15)
        except (OSError, subprocess.TimeoutExpired):
            self.skipTest(f"{executable} toolchain unavailable")
        if result.returncode != 0:
            self.skipTest(f"{executable} toolchain unavailable")

    def require_java(self):
        self.require_tool("javac", "-version")

    def require_rust(self):
        self.require_tool("rustc", "--version")

    def require_python(self):
        self.require_tool("python3", "--version")

    def test_python_instance_shorthand_and_arg_passthrough(self):
        self.require_python()
        self.source("hello.py", 'import sys\n'
                    'print(" ".join(sys.argv[1:]))\n'
                    'sys.exit(3)\n')
        for arguments in (("run", "instance", "hello.py"), ("breeze", "hello.py")):
            result = self.invoke(*arguments, "--", "hello", "py", expected=3)
            self.assertEqual(result.stdout.strip(), "hello py")
        self.assertFalse(list(self.project.rglob("__pycache__")))

    def test_python_exec_runs_interpreted_source(self):
        self.require_python()
        self.source("hello.py", 'print("exec py")\n')
        self.assertEqual(self.invoke("run", "exec", "hello.py").stdout.strip(), "exec py")

    def test_python_build_keeps_bytecode_out_of_source(self):
        self.require_python()
        self.source("hello.py", 'print("built")\n')
        output = Path(self.invoke("adapter", "python", "build").stdout.strip())
        self.assertTrue(output.is_relative_to(self.home / "state"))
        self.assertTrue(any(path.suffix == ".pyc" for path in output.rglob("*.pyc")))
        self.assertFalse(list(self.project.rglob("__pycache__")))

    def test_python_build_without_sources(self):
        self.invoke("adapter", "python", "build", expected=1)  # no .py -> cannot enumerate

    def test_rust_exec_builds_then_runs(self):
        self.require_rust()
        self.source("hello.rs", 'fn main() {\n'
                    '    let args: Vec<String> = std::env::args().skip(1).collect();\n'
                    '    println!("{}", args.join(" "));\n'
                    '    std::process::exit(5);\n}\n')
        result = self.invoke("run", "exec", "hello.rs", "--", "hello", "rust", expected=5)
        self.assertEqual(result.stdout.strip(), "hello rust")

    def test_rust_instance_rejects_without_compiling(self):
        self.source("hello.rs", 'fn main() {}\n')
        result = self.invoke("run", "instance", "hello.rs", expected=1)
        self.assertIn("no source runtime", result.stderr)

    def test_rust_directory_modules_and_ambiguous_roots(self):
        self.require_rust()
        self.source("main.rs", 'mod value; fn main() { println!("{}", value::get()); }\n')
        self.source("value.rs", 'pub fn get() -> i32 { 42 }\n')
        output = self.invoke("adapter", "rust", "build").stdout.strip()
        self.assertEqual(self.invoke("run", "exec", output).stdout.strip(), "42")
        (self.project / "main.rs").rename(self.project / "other.rs")
        self.assertIn("crate root", self.invoke("adapter", "rust", "build", expected=1).stderr)

    def test_rust_failure_does_not_run_previous_artifact(self):
        self.require_rust()
        source = self.source("hello.rs", 'fn main() { println!("old"); }\n')
        self.invoke("run", "exec", source)
        source.write_text("not rust\n")
        self.assertNotIn("old", self.invoke("run", "exec", source, expected=1).stdout)

    def test_csharp_hello_modes_arguments_and_exit(self):
        self.require_tool("dotnet", "--version")
        self.source("hello with spaces.cs", 'using System;\n'
                    'Console.WriteLine(string.Join("|", args));\nreturn 4;\n')
        for mode in ("instance", "exec"):
            result = self.invoke("run", mode, "hello with spaces.cs", "--",
                                 "Hello World", "$(touch NEVER)", expected=4)
            self.assertEqual(result.stdout.strip(), "Hello World|$(touch NEVER)")
        self.assertFalse((self.project / "NEVER").exists())
        self.assertFalse((self.project / "bin").exists())
        self.assertFalse((self.project / "obj").exists())

    def test_csharp_build_failure_recovery_and_multiple_entries(self):
        self.require_tool("dotnet", "--version")
        self.invoke("adapter", "csharp", "build", expected=1)
        source = self.source("hello.cs", 'System.Console.WriteLine("Hello World");\n')
        output = Path(self.invoke("adapter", "csharp", "build").stdout.strip())
        self.assertTrue(output.is_relative_to(self.home / "state"))
        result = subprocess.run(["dotnet", str(output)], env=self.environment,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "Hello World")
        source.write_text("not C sharp\n")
        self.assertNotIn("Hello World", self.invoke("run", "exec", source, expected=1).stdout)
        source.write_text('System.Console.WriteLine("recovered");\n')
        self.assertEqual(self.invoke("breeze", source).stdout.strip(), "recovered")
        self.source("second.cs", "// ambiguous entry\n")
        self.assertIn("exactly one", self.invoke("adapter", "csharp", "build", expected=1).stderr)

    def test_r_hello_modes_arguments_exit_and_lowercase_extension(self):
        self.require_tool("Rscript", "--version")
        for extension in ("R", "r"):
            source = self.source(f"hello with spaces.{extension}",
                                 'cat(paste(commandArgs(trailingOnly=TRUE), collapse="|"), "\\n", sep="")\n'
                                 'quit(status=5)\n')
            for command in (("breeze", source), ("run", "instance", source), ("run", "exec", source)):
                result = self.invoke(*command, "--", "Hello World", "$(touch NEVER)", expected=5)
                self.assertEqual(result.stdout.strip(), "Hello World|$(touch NEVER)")
        self.assertFalse((self.project / "NEVER").exists())

    def test_r_parse_only_build_failure_and_recovery(self):
        self.require_tool("Rscript", "--version")
        self.invoke("adapter", "r", "build", expected=1)
        source = self.source("hello.R", 'stop("must not execute during build")\n')
        self.source("helper.r", 'x <- 1\n')
        self.assertEqual(Path(self.invoke("adapter", "r", "build").stdout.strip()), self.project.resolve())
        source.write_text("x <- (\n")
        self.invoke("adapter", "r", "build", expected=1)
        source.write_text('cat("recovered\\n")\n')
        self.assertEqual(self.invoke("breeze", source).stdout.strip(), "recovered")

    def test_python_syntax_failure_and_recovery(self):
        self.require_python()
        source = self.source("hello.py", "def broken(\n")
        self.invoke("adapter", "python", "build", expected=1)
        source.write_text('print("Hello World")\n')
        self.invoke("adapter", "python", "build")
        self.assertEqual(self.invoke("breeze", source).stdout.strip(), "Hello World")

    def test_rust_directory_build_and_documented_missing_toolchain(self):
        self.source("hello.rs", 'fn main() {}\n')
        if shutil.which("rustc") is None:
            result = self.invoke("adapter", "rust", "build", expected=1)
            self.assertIn("cannot launch rustc", result.stderr)
            return
        output = Path(self.invoke("adapter", "rust", "build").stdout.strip())
        self.assertTrue(output.is_file())

    def test_help_and_invalid_grammar(self):
        help_text = self.invoke("--help").stdout
        for command in ("workspace", "build", "breeze", "run", "box", "adapter"):
            self.assertIn(command, help_text)
        for arguments in (("nonsense",), ("run", "exec"),
                          ("adapter", "c", "build", ".", "extra")):
            self.assertIn("[vex]", self.invoke(*arguments, expected=1).stderr)

    def test_c_source_run_arguments_and_exit_status(self):
        self.source("hello with spaces.c", '#include <stdio.h>\n'
                    'int main(int argc, char **argv) {\n'
                    '    for (int i = 1; i < argc; ++i) puts(argv[i]);\n'
                    '    return 7;\n}\n')
        arguments = ["a space", "$(touch NEVER)", ";", *map(str, range(10))]
        result = self.invoke("run", "exec", "hello with spaces.c", "--", *arguments, expected=7)
        self.assertEqual(result.stdout.splitlines(), arguments)
        self.assertFalse((self.project / "NEVER").exists())
        self.assertEqual(result.stderr, "")

    def test_build_current_directory_and_existing_exec(self):
        self.source(text='int main(void) { return 9; }\n')
        output = Path(self.invoke("adapter", "c", "build").stdout.strip())
        self.assertTrue(output.is_file())
        self.assertTrue(output.is_relative_to(self.home / "state"))
        self.invoke("run", "exec", output, expected=9)
        self.assertEqual(list(self.project.iterdir()), [self.project / "hello.c"])

    def test_build_multiple_sources_and_include_directory(self):
        self.source("main.c", '#include "value.h"\nint main(void) { return value(); }\n')
        self.source("value.c", 'int value(void) { return 11; }\n')
        self.source("value.h", 'int value(void);\n')
        output = Path(self.invoke("adapter", "c", "build", self.project).stdout.strip())
        self.invoke("run", "exec", output, expected=11)

    def test_directory_glob_characters_are_literal(self):
        unusual = self.project / "literal [project]"
        unusual.mkdir()
        (unusual / "main.c").write_text('int main(void) { return 0; }\n')
        output = Path(self.invoke("adapter", "c", "build", unusual).stdout.strip())
        self.invoke("run", "exec", output)

    def test_empty_missing_and_non_executable_inputs(self):
        self.invoke("adapter", "c", "build", expected=1)
        self.invoke("adapter", "c", "build", "missing", expected=1)
        self.invoke("adapter", "rust", "build", expected=1)
        self.invoke("run", "exec", "missing.c", expected=1)
        text = self.source("plain.txt", "not executable\n")
        self.invoke("run", "exec", text, expected=1)
        self.invoke("run", "exec", self.project, expected=1)

    def test_compiler_failure_never_runs_previous_result(self):
        source = self.source(text='int main(void) { return 23; }\n')
        self.invoke("run", "exec", source, expected=23)
        source.write_text("not C code\n")
        self.invoke("run", "exec", source, expected=1)

    def test_missing_tool_is_reported(self):
        self.source()
        environment = dict(self.environment, CC="missing-b-test-compiler")
        result = self.invoke("adapter", "c", "build", environment=environment, expected=1)
        self.assertIn("cannot launch", result.stderr)

    def test_c_instance_and_export_reject_without_side_effects(self):
        source = self.source(text='int main(void) { return 23; }\n')
        self.invoke("run", "instance", source, expected=1)
        manifest = self.project / "manifest.json"
        manifest.write_text("{}\n")
        destination = self.project / "release"
        self.invoke("box", manifest, destination, expected=1)
        self.assertFalse(destination.exists())
        destination.mkdir()
        marker = destination / "previous-export.txt"
        marker.write_text("preserve previous output\n")
        self.invoke("box", manifest, destination, expected=1)
        self.assertEqual(marker.read_text(), "preserve previous output\n")
        self.assertIn("does not exist or is not a regular file",
                      self.invoke("box", self.project / "missing.json", destination,
                                  expected=1).stderr)
        self.invoke("export", manifest, destination, "zip", expected=1)
        self.assertEqual(marker.read_text(), "preserve previous output\n")

    def test_create_manifest_template_and_preserve_existing_file(self):
        manifest = self.project / "box.json"
        result = self.invoke("box", "--init", manifest)
        self.assertEqual(result.stdout.strip(), str(manifest))
        text = manifest.read_text()
        parsed = json.loads(text)
        self.assertEqual(parsed["schemaVersion"], 1)
        self.assertEqual(parsed["application"]["version"], "1.0.0")
        self.assertEqual(parsed["payload"]["entry"], "dist/myapp")
        self.assertEqual(parsed["statePolicy"], "clean")
        for field in ('"schemaVersion"', '"application"', '"id"', '"name"',
                      '"version"', '"authors"', '"publisher"', '"description"',
                      '"license"', '"payload"', '"entry"', '"files"',
                      '"targets"', '"formats"', '"statePolicy"'):
            self.assertIn(field, text)
        self.assertIn("clean", text)
        self.assertIn("com.example.myapp", text)
        self.assertIn("cannot create manifest", self.invoke(
            "box", "--init", manifest, expected=1).stderr)
        self.assertEqual(manifest.read_text(), text)
        self.assertIn("box --init <path>", self.invoke("--help").stdout)

    def test_box_exports_declared_payload_and_rejects_defects(self):
        (self.project / "dist").mkdir()
        program = self.source("dist/app", "#!/bin/sh\necho exported\n")
        program.chmod(0o755)
        (self.project / "assets").mkdir()
        self.source("assets/logo.txt", "logo bytes\n")
        manifest = self.project / "box.json"
        manifest.write_text(json.dumps({
            "schemaVersion": 1,
            "application": {"id": "com.example.app", "name": "Example App",
                            "version": "2.0.0", "authors": ["Ada"]},
            "payload": {"entry": "dist/app",
                        "files": [{"source": "assets/logo.txt",
                                   "destination": "resources/logo.txt"}]},
            "targets": [], "formats": ["portable"], "statePolicy": "clean",
        }) + "\n")
        destination = self.project / "release"
        result = self.invoke("box", manifest, destination)
        self.assertEqual(result.stdout.strip(), str(destination))
        self.assertEqual(result.stderr, "")
        self.assertEqual((destination / "app").read_text(), "#!/bin/sh\necho exported\n")
        self.assertTrue((destination / "app").stat().st_mode & 0o100)
        self.assertEqual((destination / "resources" / "logo.txt").read_text(), "logo bytes\n")
        self.assertFalse((destination / "assets").exists())
        self.assertFalse(list(self.project.glob(".b-box-staging-*")))
        # An existing destination is never replaced.
        refused = self.invoke("box", manifest, destination, expected=1)
        self.assertIn("already exists", refused.stderr)
        self.assertEqual((destination / "resources" / "logo.txt").read_text(), "logo bytes\n")
        # Unimplemented formats and invalid manifests reject without a destination.
        manifest.write_text(manifest.read_text().replace('"portable"', '"zip"'))
        self.assertIn("not implemented", self.invoke("box", manifest, "zip-out", expected=1).stderr)
        manifest.write_text('{"schemaVersion": 1, "application": {"name": "X", '
                            '"version": "1.0"}, "payload": {"entry": "dist/app"}, '
                            '"formats": ["portable"]}\n')
        self.assertIn("application.id is required",
                      self.invoke("box", manifest, "invalid-out", expected=1).stderr)
        self.assertEqual(list(self.project.glob("*-out")), [])
        self.assertIn("box <manifest.json> <output>", self.invoke("--help").stdout)

    def test_java_source_alias_and_direct_instance(self):
        self.require_java()
        self.source("Hello.java", 'class Hello { public static void main(String[] args) {'
                    'System.out.println(args[0]); System.exit(6); } }\n')
        before = set((self.home / "state").rglob("*.class"))
        for arguments in (("breeze", "Hello.java"), ("run", "instance", "Hello.java")):
            result = self.invoke(*arguments, "--", "hello world", expected=6)
            self.assertEqual(result.stdout.strip(), "hello world")
        self.assertFalse(list(self.project.rglob("*.class")))
        self.assertEqual(set((self.home / "state").rglob("*.class")), before)

    def test_java_exec_builds_classes_then_runs(self):
        self.require_java()
        self.source("Hello.java", 'class Hello { public static void main(String[] args) {'
                    'System.out.println(args[0]); System.exit(6); } }\n')
        before = set((self.home / "state").rglob("Hello.class"))
        result = self.invoke("run", "exec", "Hello.java", "--", "built artifact", expected=6)
        self.assertEqual(result.stdout.strip(), "built artifact")
        self.assertTrue(set((self.home / "state").rglob("Hello.class")) - before)

    def test_existing_executable_instance_needs_no_compiler(self):
        self.source(text='int main(void) { return 9; }\n')
        output = self.invoke("adapter", "c", "build").stdout.strip()
        environment = dict(self.environment, CC="missing-b-test-compiler")
        self.invoke("run", "instance", output, environment=environment, expected=9)

    def test_java_directory_build(self):
        self.require_java()
        self.source("Hello.java", 'class Hello { public static void main(String[] args) {} }\n')
        output = Path(self.invoke("adapter", "java", "build").stdout.strip())
        self.assertTrue((output / "Hello.class").is_file())

    def test_adapter_list(self):
        result = self.invoke("adapter", "list")
        self.assertIn("c", result.stdout)
        self.assertIn("python", result.stdout)
        self.assertIn("rust", result.stdout)

    def test_workspace_init_and_rescan(self):
        self.source("main.c", "int main(void) { return 0; }\n")
        result = self.invoke("workspace")
        self.assertIn("files discovered", result.stdout)
        ws_file = self.project / ".b" / "workspace.json"
        self.assertTrue(ws_file.is_file())
        data = json.loads(ws_file.read_text())
        self.assertEqual(data["name"], self.project.name)
        paths = [f["path"] for f in data["files"]]
        self.assertIn("main.c", paths)


if __name__ == "__main__":
    unittest.main(verbosity=2)
