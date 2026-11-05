"""Public registry client: enumeration, metadata, lookup and hostile selectors.

Records are immutable/borrowed. Arbitrary pointers, lifetime mutation, numeric
construction, truncation and allocation rollback do not apply to this registry.
Null cold inputs reject once; empty/unknown selectors are normal misses.
"""
import os
import subprocess
from adapter_support import AdapterCase, ROOT


class RegistryTest(AdapterCase):
    def test_public_header_and_all_registry_forms(self):
        source = self.source("registry.c", r'''
#include "adapters/adapter.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
int main(void) {
    size_t count = Adapter_count();
    assert(count > 0);
    for (size_t i = 0; i < count; ++i) {
        const Adapter *a = Adapter_at(i);
        assert(a != nullptr);
        assert((*a).name != nullptr && *(*a).name != '\0');
        assert((*a).tools != nullptr && (*a).capabilities != nullptr);
        assert((*a).build != nullptr && (*a).run != nullptr);
        assert(Adapter_forName((*a).name) == a);
        if ((*a).extension != nullptr)
            assert(Adapter_forFile((*a).extension) == a);
        for (size_t j = 0; j < i; ++j) {
            const Adapter *b = Adapter_at(j);
            assert(strcmp((*a).name, (*b).name) != 0);
        }
    }
    assert(Adapter_forName("") == nullptr);
    assert(Adapter_forName("unknown") == nullptr);
    assert(Adapter_forFile("") == nullptr);
    assert(Adapter_forFile("a.lua.backup") == nullptr);
    assert(Adapter_forFile("/dir.lua/unknown") == nullptr);
    assert(Adapter_forFile("notpackage.json") == nullptr);
    assert(Adapter_forFile("notCargo.toml") == nullptr);
    assert(Adapter_forFile("/space dir/package.json") == Adapter_forName("npm"));
    assert(Adapter_forFile("/space dir/Cargo.toml") == Adapter_forName("cargo"));
    assert(Adapter_forFile("/space dir/a.lua") == Adapter_forName("lua"));
    assert(Adapter_forFile("file.LUA") == nullptr);
    assert(Adapter_forName(nullptr) == nullptr);
    assert(Adapter_forFile(nullptr) == nullptr);
    assert(Adapter_at(count) == nullptr);
    assert(Adapter_at(SIZE_MAX) == nullptr);
    assert(Adapter_forName("lua") != nullptr);
    return 0;
}
''')
        binary = self.project / "registry"
        command = [os.environ.get("CC", "cc"), "-std=gnu23", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-I", str(ROOT), str(source),
                   str(ROOT / "util.c"), *map(str, sorted((ROOT / "adapters").glob("*.c"))),
                   "-o", str(binary)]
        subprocess.run(command, capture_output=True, check=True, timeout=120)
        result = subprocess.run([str(binary)], env=dict(self.environment, ASAN_OPTIONS="detect_leaks=0"),
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stderr.splitlines()
        self.assertEqual(len(lines), 4, result.stderr)
        for line in lines:
            self.assertRegex(line, r"^\[vex\] .*adapter.c:\d+: adapter ")


if __name__ == "__main__":
    import unittest
    unittest.main(verbosity=2)
