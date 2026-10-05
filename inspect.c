#include "inspect.h"
#include "b.h"
#include "adapters/adapter.h"

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
 * Inspect_doctor. PRIVATE STATIC: executable — regular/executable file test;
 * findTool — literal PATH search including empty/current-directory entries;
 * inspectAdapter — expand comma-separated ENV=default requirements and print
 * presence. No persistent state; every allocated token/path is released.
 * Adapter metadata is borrowed; only cold inspection allocates/formats.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool executable(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) && access(path, X_OK) == 0;
}

static char *findTool(const char *tool) {
    if (*tool == '\0')
        return nullptr;
    if (strchr(tool, '/') != nullptr)
        return executable(tool) ? Util_combine(tool, "") : nullptr;
    const char *path = getenv("PATH");
    if (path == nullptr)
        return nullptr;
    char *entries = Util_combine(path, "");
    char *entry = entries;
    char *found = nullptr;
    while (true) {
        char *separator = strchr(entry, ':');
        if (separator != nullptr)
            *separator = '\0';
        char *base = Util_combine(*entry != '\0' ? entry : ".", "/");
        char *candidate = Util_combine(base, tool);
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

static bool inspectAdapter(const Adapter *adapter) {
    printf("%s: %s\n", (*adapter).name, (*adapter).capabilities);
    char *requirements = Util_combine((*adapter).tools, "");
    char *requirement = requirements;
    bool ready = true;
    while (true) {
        char *separator = strchr(requirement, ',');
        if (separator != nullptr)
            *separator = '\0';
        char *fallback = strchr(requirement, '=');
        const char *tool = requirement;
        if (fallback != nullptr) {
            *fallback++ = '\0';
            const char *override = getenv(requirement);
            tool = override != nullptr && *override != '\0' ? override : fallback;
        }
        char *path = findTool(tool);
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

int Inspect_adapters(void) {
    for (size_t i = 0; i < Adapter_count(); ++i) {
        const Adapter *adapter = Adapter_at(i);
        printf("%-12s %-14s %s [tools: %s]\n", (*adapter).name,
            (*adapter).extension != nullptr ? (*adapter).extension : "(project)",
            (*adapter).capabilities, (*adapter).tools);
    }
    return EXIT_SUCCESS;
}

int Inspect_doctor(const char *adapter) {
    puts("b doctor: executable discovery only; no version/SDK checks or tool execution");
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
        for (size_t i = 0; i < Adapter_count(); ++i)
            inspectAdapter(Adapter_at(i));
    }
    return EXIT_SUCCESS;
}
