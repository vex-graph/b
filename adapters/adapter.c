// MODULE: adapter registry. No owned class; adapter records are borrowed.
// DEFINITION: The single list of source, project, runtime and deployment adapters.
// Adding an adapter means
// adding one file pair and one entry here; no other file changes. Lookup is by
// CLI name or by file extension, both left-to-right.
#include "adapters/adapter.h"
#include "adapters/c.h"
#include "adapters/csharp.h"
#include "adapters/java.h"
#include "adapters/python.h"
#include "adapters/rust.h"
#include "adapters/r.h"
#include "adapters/arduino.h"
#include "adapters/cmake.h"
#include "adapters/swift.h"
#include "adapters/objc.h"
#include "adapters/javascript.h"
#include "adapters/typescript.h"
#include "adapters/npm.h"
#include "adapters/html.h"
#include "adapters/php.h"
#include "adapters/sql.h"
#include "adapters/cpp.h"
#include "adapters/shell.h"
#include "adapters/go.h"
#include "adapters/cargo.h"
#include "adapters/lua.h"
#include "adapters/zig.h"
#include "b.h"

;;DEFINITION
/* One registry is the suite's adapter source of truth. Its static pointer list
 * borrows immutable Adapter records exported by the adapter headers. Adding a
 * record makes its CLI name and extension discoverable without toolchain logic
 * in the suite. Lookup is linear over the current list and allocates nothing.
 */
;;OVERVIEW
/* MODULE: adapter registry; public declarations: adapters/adapter.h.
 * PUBLIC: Adapter_forName — lookup by CLI name;
 * Adapter_forFile — lookup by suffix or exact manifest basename.
 * Adapter_count — registry extent; Adapter_at — bounded cold enumeration.
 * PRIVATE DATA: ADAPTERS — borrowed const Adapter pointers, in dispatch order.
 * RECORDS: C_ADAPTER; JAVA_ADAPTER; PYTHON_ADAPTER; RUST_ADAPTER;
 * CSHARP_ADAPTER; R_ADAPTER; R_LOWER_ADAPTER; ARDUINO_ADAPTER; CMAKE_ADAPTER.
 * SWIFT_ADAPTER; OBJC_ADAPTER; JAVASCRIPT_ADAPTER; NODE_ADAPTER; JS_ADAPTER;
 * TYPESCRIPT_ADAPTER; TS_ADAPTER; CTS_ADAPTER; NPM_ADAPTER; HTML_ADAPTER;
 * HTM_ADAPTER; PHP_ADAPTER; SQL_ADAPTER; CPP_ADAPTER; CXX_ADAPTER;
 * CPP_LONG_ADAPTER; SHELL_ADAPTER.
 * GO_ADAPTER; CARGO_ADAPTER; LUA_ADAPTER; ZIG_ADAPTER.
 * Records carry name, extension, build, run, tools and capabilities fields
 * documented in language.h. No per-language behavior is implemented here.
 */

#include <string.h>

static const Adapter *const ADAPTERS[] = {
    &C_ADAPTER,
    &JAVA_ADAPTER,
    &PYTHON_ADAPTER,
    &RUST_ADAPTER,
    &CSHARP_ADAPTER,
    &R_ADAPTER,
    &R_LOWER_ADAPTER,
    &ARDUINO_ADAPTER,
    &CMAKE_ADAPTER,
    &SWIFT_ADAPTER,
    &OBJC_ADAPTER,
    &JAVASCRIPT_ADAPTER,
    &NODE_ADAPTER,
    &JS_ADAPTER,
    &TYPESCRIPT_ADAPTER,
    &TS_ADAPTER,
    &CTS_ADAPTER,
    &NPM_ADAPTER,
    &HTML_ADAPTER,
    &HTM_ADAPTER,
    &PHP_ADAPTER,
    &SQL_ADAPTER,
    &CPP_ADAPTER,
    &CXX_ADAPTER,
    &CPP_LONG_ADAPTER,
    &SHELL_ADAPTER,
    &GO_ADAPTER,
    &CARGO_ADAPTER,
    &LUA_ADAPTER,
    &ZIG_ADAPTER,
};

const Adapter *Adapter_forName(const char *name) {
    if (name == nullptr) {
        THROW("adapter name is null");
        return nullptr;
    }
    for (size_t i = 0; i < Adapter_count(); ++i)
        if (strcmp((*ADAPTERS[i]).name, name) == 0)
            return ADAPTERS[i];
    return nullptr;
}

const Adapter *Adapter_forFile(const char *path) {
    if (path == nullptr) {
        THROW("adapter file path is null");
        return nullptr;
    }
    const char *basename = strrchr(path, '/');
    basename = basename != nullptr ? basename + 1 : path;
    for (size_t i = 0; i < Adapter_count(); ++i) {
        const Adapter *adapter = ADAPTERS[i];
        const char *selector = (*adapter).extension;
        if (selector == nullptr)
            continue;
        if (*selector == '.' ? Util_endsWith(basename, selector) : strcmp(basename, selector) == 0)
            return adapter;
    }
    return nullptr;
}

// Return the number of immutable adapter records in the registry.
size_t Adapter_count(void) {
    return sizeof ADAPTERS / sizeof ADAPTERS[0];
}

const Adapter *Adapter_at(size_t index) {
    if (index >= Adapter_count()) {
        THROW("adapter index is out of range");
        return nullptr;
    }
    return ADAPTERS[index];
}
