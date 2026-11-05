"""Arduino owner proof: mocked uploads never touch hardware; real compile only.

Proves flag rejection, literal arguments, standalone staging, compile-before-
upload, tool failures and recovery. Real Uno compilation supplements mocks;
physical flashing is separately recorded after explicit user authorization.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2] / "personal/b"
BUNDLED = Path("/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli")


class ArduinoTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="b-arduino-")
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        self.sketch = self.home / "Blink"
        self.sketch.mkdir()
        self.source = self.sketch / "Blink.ino"
        self.source.write_text('// b_build("arduino:avr:uno")\nvoid setup() {}\nvoid loop() {}\n')
        self.log = self.home / "calls.jsonl"
        self.fake = self.home / "fake cli"
        self.fake.write_text(f"#!{sys.executable}\n" +
                             "import json,os,sys\n"
                             "with open(os.environ['CALL_LOG'], 'a') as f: f.write(json.dumps(sys.argv[1:])+'\\n')\n"
                             "sys.exit(int(os.environ.get('FAIL_'+sys.argv[1].upper(), '0')))\n")
        self.fake.chmod(0o755)
        self.env = dict(os.environ, B_HOME=str(self.home / "state"),
                        ARDUINO_CLI=str(self.fake), CALL_LOG=str(self.log))

    def invoke(self, *args, expected=0, env=None):
        result = subprocess.run([str(ROOT / "b"), *map(str, args)],
                                env=env or self.env, capture_output=True,
                                text=True, timeout=120)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def upload(self, source=None, **kwargs):
        return self.invoke("upload", "arduino", source or self.source,
                           "--fqbn", "arduino:avr:uno", "--port", "TEST_PORT", **kwargs)

    def test_compile_before_upload_and_literal_paths(self):
        self.upload()
        calls = self.calls()
        self.assertEqual([call[0] for call in calls], ["compile", "upload"])
        self.assertEqual(calls[0][-1], str(self.sketch.resolve()))
        self.assertEqual(calls[1][calls[1].index("--port") + 1], "TEST_PORT")
        output = Path(calls[0][calls[0].index("--output-dir") + 1])
        self.assertTrue(output.is_relative_to(self.home / "state"))
        self.assertEqual(calls[1][calls[1].index("--input-dir") + 1], str(output))

    def test_compile_failure_blocks_upload_and_recovery(self):
        self.upload(expected=9, env=dict(self.env, FAIL_COMPILE="9"))
        self.assertEqual([call[0] for call in self.calls()], ["compile"])
        self.upload()
        self.assertEqual([call[0] for call in self.calls()], ["compile", "compile", "upload"])

    def test_upload_failure_is_propagated(self):
        self.upload(expected=7, env=dict(self.env, FAIL_UPLOAD="7"))

    def test_flags_reject_before_launch(self):
        for flags in ((), ("--fqbn", "uno"),
                      ("--fqbn", ""), ("--wat", "x"),
                      ("--fqbn", "uno", "--fqbn", "uno", "--port", "x"),
                      ("--fqbn", "uno", "--port")):
            result = self.invoke("upload", "arduino", self.source, *flags, expected=1)
            self.assertIn("[vex]", result.stderr)
        self.assertEqual(self.calls(), [])

    def test_standalone_file_staged_unchanged_and_arguments_not_shell(self):
        source = self.home / "one-off.ino"
        content = self.source.read_bytes()
        source.write_bytes(content)
        self.invoke("upload", "arduino", source, "--port", "$(touch NEVER)",
                    "--fqbn", "arduino:avr:uno")
        staged = Path(self.calls()[0][-1])
        self.assertEqual(staged.name, "one-off")
        self.assertEqual((staged / source.name).read_bytes(), content)
        self.assertEqual(source.read_bytes(), content)
        self.assertEqual(self.calls()[1][4], "$(touch NEVER)")
        self.assertFalse((self.home / "NEVER").exists())

    def test_build_requires_header_and_run_rejects(self):
        env = dict(self.env)
        source = self.source.read_text()
        self.source.write_text("void setup() {}\nvoid loop() {}\n")
        self.invoke("build", "arduino", self.sketch, env=env, expected=1)
        for mode in ("exec", "instance"):
            self.invoke("run", mode, self.source, expected=1)
        self.assertEqual(self.calls(), [])
        self.source.write_text(source)
        self.invoke("build", "arduino", self.sketch, env=env)
        self.assertEqual(self.calls()[0][0], "compile")

    def test_header_supplies_board_without_flag_and_mismatch_rejects(self):
        self.invoke("upload", "arduino", self.source, "--port", "TEST_PORT")
        for call in self.calls():
            self.assertEqual(call[call.index("--fqbn") + 1], "arduino:avr:uno")
        self.log.unlink()
        self.invoke("upload", "arduino", self.source, "--port", "TEST_PORT",
                    "--fqbn", "arduino:avr:nano", expected=1)
        self.assertEqual(self.calls(), [])

    def test_malformed_missing_and_oversized_headers_reject_before_cli(self):
        for header in ('', '\n// b_build("arduino:avr:uno")\n',
                       '// b_build("uno")\n', '// b_build("")\n',
                       '// b_build("arduino:avr:uno"\n',
                       '// b_build("arduino:avr:uno"); touch NEVER\n',
                       '// b_build("$(touch NEVER)")\n',
                       '// b_build("arduino:avr:' + 'x' * 1100 + '")\n'):
            self.source.write_text(header + 'void setup() {}\nvoid loop() {}\n')
            self.assertIn("[vex]", self.upload(expected=1).stderr)
        self.assertEqual(self.calls(), [])

    def test_board_options_and_crlf_header(self):
        self.source.write_bytes(b'// b_build("arduino:avr:nano:cpu=atmega328old")\r\n'
                                b'void setup() {}\r\nvoid loop() {}\r\n')
        self.invoke("upload", "arduino", self.source, "--port", "TEST_PORT")
        for call in self.calls():
            self.assertEqual(call[call.index("--fqbn") + 1],
                             "arduino:avr:nano:cpu=atmega328old")

    def test_missing_source_or_cli_rejects(self):
        self.upload(self.home / "missing.ino", expected=1)
        result = self.upload(expected=1, env=dict(self.env, ARDUINO_CLI="missing-b-test-cli"))
        self.assertIn("cannot launch", result.stderr)
        self.assertEqual(self.calls(), [])

    def test_real_uno_compile_only(self):
        if not BUNDLED.is_file():
            self.skipTest("Bundled Arduino CLI unavailable; real compile unproved")
        probe = subprocess.run([str(BUNDLED), "core", "list"], capture_output=True,
                               text=True, timeout=30)
        if probe.returncode != 0 or "arduino:avr" not in probe.stdout:
            self.skipTest("Arduino AVR core unavailable")
        env = dict(self.env, ARDUINO_CLI=str(BUNDLED))
        output = Path(self.invoke("build", "arduino", self.sketch, env=env).stdout.strip())
        self.assertTrue(list(output.glob("*.hex")))
        self.source.write_text('// b_build("arduino:avr:uno")\nthis is not a sketch\n')
        self.invoke("build", "arduino", self.sketch, env=env, expected=1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
