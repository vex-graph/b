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
#include "languages/r.h"
#include "languages/arduino.h"
#include "languages/cmake.h"
#include "languages/swift.h"
#include "languages/objc.h"
#include "languages/javascript.h"
#include "languages/typescript.h"
#include "languages/npm.h"
#include "languages/html.h"
#include "languages/php.h"
#include "languages/sql.h"
#include "languages/cpp.h"
#include "languages/shell.h"
#include "b.h"

;;DEFINITION
/* One registry is the suite's adapter source of truth. Its static pointer list
 * borrows immutable Language records exported by the adapter headers. Adding a
 * record makes its CLI name and extension discoverable without toolchain logic
 * in the suite. Lookup is linear over the current list and allocates nothing.
 */
;;OVERVIEW
/* MODULE: adapter registry; public declarations: languages/language.h.
 * PUBLIC: Language_forName — lookup by CLI name;
 * Language_forFile — lookup by filename extension.
 * PRIVATE DATA: LANGUAGES — borrowed const Language pointers, in dispatch order.
 * RECORDS: C_LANGUAGE; JAVA_LANGUAGE; PYTHON_LANGUAGE; RUST_LANGUAGE;
 * CSHARP_LANGUAGE; R_LANGUAGE; R_LOWER_LANGUAGE; ARDUINO_LANGUAGE; CMAKE_LANGUAGE.
 * SWIFT_LANGUAGE; OBJC_LANGUAGE; JAVASCRIPT_LANGUAGE; NODE_LANGUAGE; JS_LANGUAGE;
 * TYPESCRIPT_LANGUAGE; TS_LANGUAGE; CTS_LANGUAGE; NPM_LANGUAGE; HTML_LANGUAGE;
 * HTM_LANGUAGE; PHP_LANGUAGE; SQL_LANGUAGE; CPP_LANGUAGE; CXX_LANGUAGE;
 * CPP_LONG_LANGUAGE; SHELL_LANGUAGE.
 * Records carry name, extension, build and run fields documented in language.h.
 */

#include <string.h>

static const Language *const LANGUAGES[] = {
    &C_LANGUAGE,
    &JAVA_LANGUAGE,
    &PYTHON_LANGUAGE,
    &RUST_LANGUAGE,
    &CSHARP_LANGUAGE,
    &R_LANGUAGE,
    &R_LOWER_LANGUAGE,
    &ARDUINO_LANGUAGE,
    &CMAKE_LANGUAGE,
    &SWIFT_LANGUAGE,
    &OBJC_LANGUAGE,
    &JAVASCRIPT_LANGUAGE,
    &NODE_LANGUAGE,
    &JS_LANGUAGE,
    &TYPESCRIPT_LANGUAGE,
    &TS_LANGUAGE,
    &CTS_LANGUAGE,
    &NPM_LANGUAGE,
    &HTML_LANGUAGE,
    &HTM_LANGUAGE,
    &PHP_LANGUAGE,
    &SQL_LANGUAGE,
    &CPP_LANGUAGE,
    &CXX_LANGUAGE,
    &CPP_LONG_LANGUAGE,
    &SHELL_LANGUAGE,
};

const Language *Language_forName(const char *name) {
    for (size_t i = 0; i < sizeof LANGUAGES / sizeof LANGUAGES[0]; ++i)
        if (strcmp((*LANGUAGES[i]).name, name) == 0)
            return LANGUAGES[i];
    return nullptr;
}

const Language *Language_forFile(const char *path) {
    for (size_t i = 0; i < sizeof LANGUAGES / sizeof LANGUAGES[0]; ++i)
        if ((*LANGUAGES[i]).extension != nullptr && Util_endsWith(path, (*LANGUAGES[i]).extension))
            return LANGUAGES[i];
    return nullptr;
}
