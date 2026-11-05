"""Offline npm-script delegation: no install, downloads or third-party packages."""
import json
from pathlib import Path
from adapter_support import AdapterCase


class NpmTest(AdapterCase):
    def setUp(self):
        super().setUp()
        self.require_tool("npm", "--version")
        self.require_tool("node", "--version")
        self.manifest = self.source("package.json", json.dumps({"scripts": {
            "build": "node build.cjs", "start": "node start.cjs"}}))
        self.builder = self.source("build.cjs", 'const fs = require("node:fs");\n'
                                   'fs.mkdirSync("dist", {recursive:true});\n'
                                   'fs.writeFileSync("dist/hello.txt", "built");\n')
        self.source("start.cjs", 'const fs = require("node:fs");\n'
                    'fs.writeFileSync("STARTED", "yes");\n'
                    'console.log(process.argv.slice(2).join("|")); process.exit(5);\n')

    def test_build_script_controls_its_output_and_no_install_occurs(self):
        output = Path(self.invoke("build", "npm", self.project).stdout.strip())
        self.assertEqual(output, self.project.resolve())
        self.assertEqual((self.project / "dist/hello.txt").read_text(), "built")
        self.assertFalse((self.project / "node_modules").exists())
        self.assertFalse((self.project / "package-lock.json").exists())

    def test_instance_skips_build_and_exec_builds_before_start(self):
        result = self.invoke("npm", self.manifest, "--", "Hello World", "$(touch NEVER)", expected=5)
        self.assertEqual(result.stdout.strip(), "Hello World|$(touch NEVER)")
        self.assertFalse((self.project / "dist").exists())
        result = self.invoke("run", "exec", self.manifest, "--", "built run", expected=5)
        self.assertEqual(result.stdout.strip(), "built run")
        self.assertTrue((self.project / "dist/hello.txt").exists())
        self.assertFalse((self.project / "NEVER").exists())

    def test_failed_build_never_starts_old_output_and_can_recover(self):
        self.invoke("build", "npm", self.project)
        self.builder.write_text("process.exit(12);\n")
        self.invoke("run", "exec", self.manifest, expected=12)
        self.assertFalse((self.project / "STARTED").exists())
        self.builder.write_text("process.exit(0);\n")
        self.invoke("run", "exec", self.manifest, expected=5)
        self.assertTrue((self.project / "STARTED").exists())

    def test_missing_scripts_malformed_manifest_and_wrong_name(self):
        self.manifest.write_text("{}\n")
        self.invoke("build", "npm", self.project, expected=None)
        self.invoke("npm", self.manifest, expected=None)
        self.manifest.write_text("not json\n")
        self.invoke("build", "npm", self.project, expected=None)
        wrong = self.source("badpackage.json", "{}\n")
        # Manifest selectors now match exact basenames at registry admission,
        # rather than reaching npm's second validation via a suffix false-match.
        rejected = self.invoke("npm", wrong, expected=1)
        self.assertIn("not an executable or a supported source", rejected.stderr)
        self.assertEqual(rejected.stderr.count("[vex]"), 1)
        self.manifest.unlink()
        self.assertIn("requires", self.invoke("build", "npm", self.project, expected=1).stderr)

    def test_missing_npm_is_observable(self):
        self.assertIn("cannot launch npm", self.invoke("npm", self.manifest, expected=1,
                      environment=self.missing_tool_environment()).stderr)
