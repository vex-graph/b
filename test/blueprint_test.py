"""Source blueprint inventory proof, not behavioral correctness evidence."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2] / "personal/b"


class BlueprintTest(unittest.TestCase):
    def test_every_implementation_lists_its_actual_functions_near_start(self):
        for path in sorted(ROOT.glob("*.c")) + sorted((ROOT / "adapters").glob("*.c")):
            with self.subTest(file=path.name):
                text = path.read_text()
                first = "\n".join(text.splitlines()[:150])
                self.assertIn(";;DEFINITION", first)
                self.assertIn(";;OVERVIEW", first)
                overview = first.split(";;OVERVIEW", 1)[1].split("*/", 1)[0]
                functions = re.findall(r"^(?:static )?[\w *]+\b(\w+)\([^;\n]*\)\s*\{", text, re.M)
                self.assertTrue(functions)
                for name in functions:
                    self.assertRegex(overview, rf"\b{re.escape(name)}\b")

    def test_public_headers_carry_contract_blueprints(self):
        for path in [ROOT / "b.h", ROOT / "inspect.h", ROOT / "box.h",
                     *sorted((ROOT / "adapters").glob("*.h"))]:
            with self.subTest(file=path.name):
                text = path.read_text()
                self.assertIn(";;DEFINITION", text)
                self.assertIn(";;OVERVIEW", text)
        marker = (ROOT / "annotation.h").read_text()
        self.assertIn('#define OVERVIEW _Static_assert(1, "@Overview");', marker)
        self.assertIn('#define DEFINITION _Static_assert(1, "@Definition");', marker)


if __name__ == "__main__":
    unittest.main(verbosity=2)
