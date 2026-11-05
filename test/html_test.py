"""Headless opener contract; never opens a real browser or claims pixel proof."""
import json
import sys
from adapter_support import AdapterCase


class HtmlTest(AdapterCase):
    def setUp(self):
        super().setUp()
        self.log = self.project / "browser.json"
        self.browser = self.source("fake browser", f"#!{sys.executable}\n"
                                   'import json,os,sys\n'
                                   'with open(os.environ["BROWSER_LOG"], "w") as f: json.dump(sys.argv[1:], f)\n'
                                   'sys.exit(int(os.environ.get("BROWSER_STATUS", "0")))\n')
        self.browser.chmod(0o755)
        self.env = dict(self.environment, B_BROWSER=str(self.browser), BROWSER_LOG=str(self.log))

    def test_index_and_htm_modes_use_original_literal_path(self):
        for name in ("index.html", "page with spaces #.htm"):
            source = self.source(name, "<!doctype html><title>Hello World</title>\n")
            for command in (("html",), ("run", "instance"), ("run", "exec")):
                self.invoke(*command, source, environment=self.env)
                self.assertEqual(json.loads(self.log.read_text()), [str(source.resolve())])
                self.assertIn("Hello World", source.read_text())

    def test_opener_failure_is_propagated_and_not_browser_success(self):
        source = self.source("index.html", "<h1>Hello World</h1>\n")
        self.invoke("html", source, expected=23, environment=dict(self.env, BROWSER_STATUS="23"))
        self.assertIn("cannot launch", self.invoke("html", source, expected=1,
                      environment=dict(self.env, B_BROWSER="missing-b-test-browser")).stderr)

    def test_no_guessed_build_pipeline_or_extra_browser_arguments(self):
        source = self.source("index.html", "<h1>Hello World</h1>\n")
        self.invoke("build", "html", self.project, expected=1, environment=self.env)
        self.invoke("html", source, "--", "unused", expected=1, environment=self.env)
        self.assertFalse(self.log.exists())
