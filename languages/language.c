// MODULE: language registry. No owned class; adapter records are borrowed.
// DEFINITION: The single list of language adapters. Adding a language means
// adding one file pair and one entry here; no other file changes. Lookup is by
// CLI name or by file extension, both left-to-right.
#include "languages/language.h"
#include "languages/c.h"
#include "languages/csharp.h"
#include "languages/java.h"
#include "languages/python.h"
#include "languages/rust.h"
#include "b.h"

#include <string.h>

static const Language *const LANGUAGES[] = {
    &C_LANGUAGE,
    &JAVA_LANGUAGE,
    &PYTHON_LANGUAGE,
    &RUST_LANGUAGE,
    &CSHARP_LANGUAGE,
};

const Language *Language_forName(const char *name) {
    for (size_t i = 0; i < sizeof LANGUAGES / sizeof LANGUAGES[0]; ++i)
        if (strcmp((*LANGUAGES[i]).name, name) == 0)
            return LANGUAGES[i];
    return nullptr;
}

const Language *Language_forFile(const char *path) {
    for (size_t i = 0; i < sizeof LANGUAGES / sizeof LANGUAGES[0]; ++i)
        if (Util_endsWith(path, (*LANGUAGES[i]).extension))
            return LANGUAGES[i];
    return nullptr;
}
