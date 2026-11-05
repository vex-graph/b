#include "b.h"
#include "lifetime/platform.h"

;;DEFINITION
/* New command/check helpers reject invalid cold inputs before array access.
 * Normal commands preserve exit status and ten literal argv entries. The test
 * owns no persistent memory; its Python owner runs ASan/UBSan and diagnostics.
 */
;;OVERVIEW
/* MODULE: shared command helper boundary proof. PUBLIC ENTRY: main.
 * SUBJECTS: Util_runCommand; Util_checkSources. Eleven rejected cold cases,
 * one normal status case, and a ten-argument passthrough case.
 */

#include <assert.h>
#include <stdint.h>

/* Checks command/source helper rejection, exit-status propagation, and literal ten-argument forwarding. */
int main(void) {
#if defined(VEX_PLATFORM_WINDOWS)
    char *prefix[] = { "sh", "-c", "exit 7" };
#else
    char *prefix[] = { "/bin/sh", "-c", "exit 7" };
#endif
    char *empty[] = { nullptr };
    assert(Util_runCommand(nullptr, 0, 0, nullptr) == 1);
    assert(Util_runCommand(prefix, 0, 0, nullptr) == 1);
    assert(Util_runCommand(prefix, 3, -1, nullptr) == 1);
    assert(Util_runCommand(prefix, SIZE_MAX, 0, nullptr) == 1);
    assert(Util_runCommand(empty, 1, 0, nullptr) == 1);
    assert(Util_runCommand(prefix, 3, 1, nullptr) == 1);
    assert(Util_runCommand(prefix, 3, 0, nullptr) == 7);
    char *arguments[] = { "a space", "$(touch NEVER)", ";", "3", "4", "5", "6", "7", "8", "9" };
#if defined(VEX_PLATFORM_WINDOWS)
    char *ten[] = { "sh", "-c", "test \"$#\" -eq 10 && test \"$1\" = 'a space'", "fixture" };
#else
    char *ten[] = { "/bin/sh", "-c", "test \"$#\" -eq 10 && test \"$1\" = 'a space'", "fixture" };
#endif
    assert(Util_runCommand(ten, 4, 10, arguments) == 0);
    char *output = nullptr;
    assert(Util_checkSources(nullptr, "/*.sh", prefix, 1, &output) == 1);
    assert(Util_checkSources(".", nullptr, prefix, 1, &output) == 1);
    assert(Util_checkSources(".", "/*.sh", prefix, 0, &output) == 1);
    assert(Util_checkSources(".", "/*.sh", prefix, 1, nullptr) == 1);
    assert(Util_checkSources(".", "/*.sh", prefix, SIZE_MAX, &output) == 1);
    assert(output == nullptr);
    return 0;
}
