"""Cargo owner: real offline native manifests, ambiguous binaries and recovery.
Cargo owns lockfiles/build scripts; no dependency fetch or executable guessing.
"""
from pathlib import Path
from adapter_support import AdapterCase


class CargoTest(AdapterCase):
    def setUp(self):
        super().setUp()
        self.require_tool("cargo", "--version")
        (self.project / "src").mkdir()
        self.manifest = self.source("Cargo.toml", '[package]\nname="local_b_test"\nversion="0.1.0"\nedition="2021"\n')
        self.main = self.source("src/main.rs", 'fn main(){println!("{}",std::env::args().skip(1).collect::<Vec<_>>().join("|"));}\n')

    def test_build_and_both_run_modes_with_literal_arguments(self):
        output = Path(self.invoke("build", "cargo", self.project).stdout.strip())
        self.assertTrue(output.is_dir())
        self.assertFalse(output.is_relative_to(self.project))
        self.assertFalse((self.project / "target").exists())
        args = ["", "a b", "$(touch marker)", *map(str, range(10))]
        for mode in ("instance", "exec"):
            result = self.invoke("run", mode, self.manifest, "--", *args)
            self.assertEqual(result.stdout.strip(), "|".join(args))
        self.assertFalse((self.project / "marker").exists())
        self.assertEqual(self.invoke("cargo", self.manifest, "--", "ok").stdout.strip(), "ok")

    def test_compile_failure_blocks_run_and_recovers(self):
        self.invoke("build", "cargo", self.project)
        self.main.write_text("fn main(\n")
        result = self.invoke("run", "exec", self.manifest, "--", "MUST_NOT_RUN", expected=None)
        self.assertNotIn("MUST_NOT_RUN", result.stdout)
        self.main.write_text('fn main(){std::process::exit(7);}\n')
        self.invoke("cargo", self.manifest, expected=7)

    def test_ambiguous_binary_rejects_then_native_default_run_selects(self):
        (self.project / "src/bin").mkdir()
        self.source("src/bin/other.rs", "fn main(){}\n")
        self.invoke("cargo", self.manifest, expected=None)
        text = self.manifest.read_text().replace('[package]\n', '[package]\ndefault-run="other"\n')
        self.manifest.write_text(text)
        self.invoke("cargo", self.manifest)

    def test_missing_corrupt_manifest_tool_and_offline_dependency(self):
        self.manifest.unlink()
        self.invoke("build", "cargo", self.project, expected=None)
        self.manifest.write_text("not valid toml !!!")
        self.invoke("build", "cargo", self.project, expected=None)
        self.manifest.write_text('[package]\nname="local_b_test"\nversion="0.1.0"\n'
                                 '[dependencies]\nb_missing_test_dependency="=999.0.0"\n')
        result = self.invoke("build", "cargo", self.project, expected=None)
        self.assertIn("offline", result.stderr)
        environment = dict(self.environment, CARGO="missing-cargo")
        self.invoke("cargo", self.manifest, environment=environment, expected=None)


if __name__ == "__main__":
    import unittest
    unittest.main(verbosity=2)
