"""Source-level Reference form proof, not blanket constitutional compliance.

Preserve comments and string output. Lex C tokens after line-splice processing
so member arrows hidden by comments or continuations cannot bypass the gate.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2] / "personal/b"
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|->', re.S)


def member_arrows(source):
    source = re.sub(r"\\\r?\n", "", source)
    return [match.start() for match in TOKENS.finditer(source) if match.group() == "->"]


class PreferencesTest(unittest.TestCase):
    def test_reference_form_across_all_c_sources_and_headers(self):
        paths = sorted(path for path in ROOT.rglob("*") if path.suffix in (".c", ".h"))
        self.assertTrue(paths)
        for path in paths:
            with self.subTest(file=str(path.relative_to(ROOT))):
                self.assertEqual(member_arrows(path.read_text()), [],
                                 "Semantic Consistency Law (Reference form): use (*pointer).field")

    def test_gate_ignores_prose_and_literals_but_detects_real_operators(self):
        self.assertEqual(member_arrows('// keep -> comment\n/* -> */ "->" \'x\' (*p).field;'), [])
        self.assertEqual(len(member_arrows('p->field; p /* keep */ -> other;')), 2)
        self.assertEqual(len(member_arrows('p-\\\n>field;')), 1)
        self.assertEqual(member_arrows('"escaped \\" ->"; // ->\n'), [])

    def test_constitution_is_referenced_at_both_entry_surfaces(self):
        for path, reference in ((ROOT / "b.h", "gist/PREFERENCES.md"),
                                (ROOT.parents[1] / "tools/workspace.c", "gist/PREFERENCES.md")):
            text = path.read_text()
            self.assertIn(reference, text)
            self.assertIn("Semantic Consistency Law (Reference form)", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
