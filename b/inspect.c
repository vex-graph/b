#include "vex.h"
#include "inspect.h"
#include "b.h"
#include "b/b_str.h"
#include "adapters/lang/adapter.h"

;;DEFINITION
/* Standalone introspection enumerates the same immutable registry that dispatches
 * builds and runs. Adapters own diagnostic metadata; no per-language execution
 * policy lives here. doctor checks literal executable paths or PATH entries and
 * ENV=default overrides without running arbitrary version probes, contacting the
 * network or entering the workspace engine. Executable presence is not proof of
 * compatible version, SDK, board core or successful compilation. Optional tools
 * may be absent; selecting one adapter makes missing requirements a cold error.
 */
;;OVERVIEW
/* MODULE: standalone diagnostics (inspect.h). PUBLIC: Inspect_adapters;
 * Inspect_doctor. PRIVATE STATIC: executable, regular/executable file test;
 * findTool, literal PATH search including empty/current-directory entries;
 * inspectAdapter, expand comma-separated ENV=default requirements and print
 * presence. No persistent state; every allocated token/path is released.
 * Adapter metadata is borrowed; only cold inspection allocates/formats.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/** Check that a path names a regular file executable by the current user. */
static bool executable(const utf8 *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && access(path, X_OK) == 0;
}

/** Resolve an executable by literal path or by searching PATH entries in order. */
static utf8 *findTool(const utf8 *tool) {
    if (*tool == '\0')
        return nullptr;
    if (bStrFindChar(tool, '/') != nullptr)
        return executable(tool) ? Util_combine(tool, "") : nullptr;
    const utf8 *path = getenv("PATH");
    if (path == nullptr)
        return nullptr;
    utf8 *entries = Util_combine(path, "");
    utf8 *entry = entries;
    utf8 *found = nullptr;
    while (true) {
        utf8 *separator = bStrFindCharMut(entry, ':');
        if (separator != nullptr)
            *separator = '\0';
        utf8 *base = Util_combine(*entry != '\0' ? entry : ".", "/");
        utf8 *candidate = Util_combine(base, tool);
        free(base);
        if (executable(candidate)) {
            found = candidate;
            break;
        }
        free(candidate);
        if (separator == nullptr)
            break;
        entry = separator + 1;
    }
    free(entries);
    return found;
}

;;GETTER
static inline const utf8 *Adapter_name(const Adapter *self)
{
    return (*self).name;
}
;;GETTER
static inline const utf8 *Adapter_capabilities(const Adapter *self)
{
    return (*self).capabilities;
}
;;GETTER
static inline const utf8 *Adapter_tools(const Adapter *self)
{
    return (*self).tools;
}
;;GETTER
static inline const utf8 *Adapter_extension(const Adapter *self)
{
    return (*self).extension;
}

/** Print one adapter's requirements and report whether all tools are present. */
static bool inspectAdapter(const Adapter *adapter) {
    printf("%s: %s\n", Adapter_name(adapter), Adapter_capabilities(adapter));
    utf8 *requirements = Util_combine(Adapter_tools(adapter), "");
    utf8 *requirement = requirements;
    bool ready = true;
    while (true) {
        utf8 *separator = bStrFindCharMut(requirement, ',');
        if (separator != nullptr)
            *separator = '\0';
        utf8 *fallback = bStrFindCharMut(requirement, '=');
        const utf8 *tool = requirement;
        if (fallback != nullptr) {
            *fallback++ = '\0';
            const utf8 *override = getenv(requirement);
            tool = override != nullptr && *override != '\0' ? override : fallback;
        }
        utf8 *path = findTool(tool);
        printf("  %s: %s\n", tool, path != nullptr ? path : "MISSING");
        if (path == nullptr)
            ready = false;
        free(path);
        if (separator == nullptr)
            break;
        requirement = separator + 1;
    }
    free(requirements);
    return ready;
}

/** List registered adapter names, selectors, capabilities, and tool requirements. */
i32 Inspect_adapters() {
    for (bytes i = 0; i < Adapter_count(); ++i) {
        const Adapter *adapter = Adapter_at(i);
        const utf8 *ext = Adapter_extension(adapter);
        printf("%-12s %-14s %s [tools: %s]\n", Adapter_name(adapter),
            ext != nullptr ? ext : "(project)",
            Adapter_capabilities(adapter), Adapter_tools(adapter));
    }
    return EXIT_SUCCESS;
}

/** Inspect one named adapter or enumerate all tools without executing them. */
i32 Inspect_doctor(const utf8 *adapter) {
    puts("b.sh doctor: executable discovery only; no version/SDK checks or tool execution");
    if (adapter != nullptr) {
        const Adapter *record = Adapter_forName(adapter);
        if (record == nullptr) {
            THROW("unknown adapter for doctor: %s", adapter);
            return EXIT_FAILURE;
        }
        if (!inspectAdapter(record)) {
            THROW("requested adapter has missing executable requirements: %s", adapter);
            return EXIT_FAILURE;
        }
    } else {
        for (bytes i = 0; i < Adapter_count(); ++i)
            inspectAdapter(Adapter_at(i));
    }
    return EXIT_SUCCESS;
}
