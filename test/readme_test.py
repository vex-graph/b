"""Offline documentation contracts; no claim of GUI or cross-platform proof."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

_CANDIDATES = [
    Path(__file__).resolve().parents[3] / "vexgraph-hobby-projects/b",
    Path(__file__).resolve().parents[2] / "personal/b",
    Path(__file__).resolve().parents[2] / "suite/personal/b",
]
ROOT = next((p for p in _CANDIDATES if p.exists()), _CANDIDATES[0])


def doc(name):
    target = ROOT / "docs" / name
    return target if target.exists() else ROOT / name


class ReadmeTest(unittest.TestCase):
    def test_build_breeze_box_artwork_precedes_title(self):
        text = (ROOT / "README.md").read_text()
        self.assertTrue(text.startswith('<p align="center">\n'))
        self.assertIn('src="https://raw.githubusercontent.com/vex-graph/vex-graph/main/resources/b.png"', text)
        self.assertIn('alt="build, breeze, box!"', text)
        self.assertLess(text.index("resources/b.png"), text.index("# b\n"))

    def test_command_tree_link_names_and_honest_modes(self):
        readme = (ROOT / "README.md").read_text()
        self.assertIn("[docs/TREE.md](docs/TREE.md)", readme)
        text = doc("TREE.md").read_text()
        for phrase in ("build", "breeze", "run", "box", "adapter", "workspace",
                       "portable", "tools/workspace.c",
                       "not a sandbox",
                       "[Back to README](../README.md)"):
            self.assertIn(phrase, text)
        adapters = doc("ADAPTERS.md").read_text()
        for source in (ROOT / "adapters").glob("*.c"):
            for name in re.findall(r'const Adapter \w+ = \{\s*"([^"]+)"', source.read_text()):
                self.assertIn(f"`{name}`", adapters, f"Missing adapter name from {source.name}")
        for command in ("workspace", "build", "breeze", "run", "box", "adapter"):
            self.assertRegex(text, rf"(?m)^[├└]── {command}\b")

    def test_readme_identity_usage_and_honest_scope(self):
        text = (ROOT / "README.md").read_text()
        for phrase in ("general-purpose, language-agnostic build system", "Tsoding",
                       "nob", "git clone https://github.com/vex-graph/b.git",
                       "b breeze", "b run", "b build", "b box", "b adapter",
                       "JETBRAINS.md", "portable", "not a sandbox",
                       "parse-only", "exactly one", "SDK 10+", "main.rs",
                       "https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a"):
            self.assertIn(phrase, text)
        adapters = doc("ADAPTERS.md").read_text()
        for language in ("C /", "Java /", "Python /", "Rust /", "C# /", "R /", "Arduino /",
                         "Swift /", "Objective-C /", "JavaScript /", "TypeScript /", "PHP /", "SQL /", "C++ /", "POSIX shell /"):
            self.assertIn(language, adapters)
        self.assertIn("orchestrator on top", text)
        for claim in ("Build, breeze, box", "B_SQL_DATABASE", "not a sandbox",
                      "not type-checking", "no default database"):
            self.assertIn(claim.lower(), (text + adapters).lower())

    def test_readme_outline_and_alphabetical_languages(self):
        """Keep the requested overview ordered and its language inventory alphabetical."""
        text = (ROOT / "README.md").read_text()
        headings = re.findall(r"^## (.+)$", text, re.M)
        self.assertEqual(headings, [
            "Status", "The command set", "workspace", "build",
            "breeze and run", "box", "adapter", "Languages",
            "Install with curl", "Building b itself",
            "Scope and Limitations", "Documentation"
        ])
        table = text.split("## Languages\n", 1)[1].split("## Install with curl\n", 1)[0]
        names = re.findall(r"^\| ([^|]+?) \|", table, re.M)[2:]
        self.assertEqual(names, sorted(names, key=str.casefold))
        self.assertEqual(len(names), 21)
        self.assertIn("[docs/ADAPTERS.md](docs/ADAPTERS.md)", text)

    def test_shader_inventory_matches_project_owned_implementation(self):
        """Describe real GLSL generators without inventing standalone shader dispatch."""
        readme = (ROOT / "README.md").read_text()
        adapters = doc("ADAPTERS.md").read_text()
        for text in (readme, adapters):
            for phrase in ("GLSL / SPIR-V", ".vert", ".frag", ".comp", ".glsl", ".spv"):
                self.assertIn(phrase, text)

    def test_workspace_and_shader_proof_lawbook_has_registered_owners(self):
        """Keep proof laws tied to runnable owners without claiming skipped compilers."""
        text = (Path(__file__).resolve().parents[1] / "test-preferences.md").read_text()
        for phrase in ("Workspace Assessment and Build Proof Law", "Shader Tool Delegation Proof Law",
                       "glsl_test.py", "metal_test.py",
                       "shaderc fixtures are not real glslc proof", "not inherit a green result"):
            self.assertIn(phrase, text)
        for owner in ("workspace", "glsl", "metal"):
            self.assertTrue((Path(__file__).resolve().parent / "adapters" / f"{owner}_test.py").is_file())

    def test_jetbrains_external_tool_instructions(self):
        text = doc("JETBRAINS.md").read_text()
        for phrase in ("Tools → External Tools", "Program", "Arguments", "Working directory",
                       'run exec "$FilePath$"', "$FileDir$", "$ProjectFileDir$",
                       "Keymap", "Before launch", "debugger", "PATH", "save",
                       "CLion", "IntelliJ IDEA", "Rider", "PyCharm"):
            self.assertIn(phrase, text)
        self.assertNotIn("$USER_HOME$/vexgraph", text)
        self.assertIn('arduino upload "$FilePath$" --port', text)
        self.assertIn('// b_build("arduino:avr:uno")', text)
        self.assertIn("Serial Monitor", text)
        self.assertIn("b does not add those IDE features", text)
        self.assertIn("b box --init", text)
        self.assertIn("formats are unimplemented", text)

    def test_jetbrains_two_methods_and_xml_reference_consistency(self):
        text = doc("JETBRAINS.md").read_text()
        self.assertIn("Method 1: manual UI", text)
        self.assertIn("Method 2: inspectable XML", text)
        self.assertIn(".idea/runConfigurations/", text)
        self.assertIn("IDE-level settings", text)
        blocks = re.findall(r"```xml\n(.*?)\n```", text, re.S)
        self.assertEqual(len(blocks), 2)
        tools, run = map(ET.fromstring, blocks)
        tool = tools.find("tool")
        action = run.find(".//option[@name='ToolBeforeRunTask']")
        self.assertEqual(action.get("actionId"), f"Tool_{tools.get('name')}_{tool.get('name')}")
        options = {item.get("name"): item.get("value") for item in tool.findall("exec/option")}
        self.assertEqual(options["PARAMETERS"], 'upload arduino "$FilePath$" --port /dev/cu.YOUR_BOARD')
        self.assertEqual(options["WORKING_DIRECTORY"], "$FileDir$")
        self.assertEqual(run.find(".//option[@name='SCRIPT_TEXT']").get("value"), ":")


if __name__ == "__main__":
    unittest.main(verbosity=2)
