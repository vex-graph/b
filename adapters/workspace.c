#include "adapters/workspace.h"
#include "adapters/adapter.h"
#include "b.h"

;;DEFINITION
/* The standalone workspace command borrows the assess/build separation from
 * Vexgraph tools/workspace.c without importing its ecosystem target graph.
 * An iterative, sorted walk inventories first, then runs build-only units.
 * Manifests own their subtrees; loose sources group by directory/build callback.
 * Multiple project manifests or mixed C-family source groups reject rather than
 * guessing linkage. Headers/unknown files are reported as unassigned, never run.
 * Broad roots/large inventories require explicit --confirm after assessment;
 * --plan performs no tool execution or output allocation. No sudo or downloads.
 * Caller must keep the tree stable during assessment/build; this is not a
 * hostile-filesystem sandbox. Native project scripts retain their authority.
 */
;;OVERVIEW
/* MODULE: recursive workspace builder; public entry: Workspace_command.
 * PRIVATE SLOT RECORDS:
 * ScanSlot: char *path — owned directory; bool owned — ancestor manifest owns it.
 * BuildSlot: char *path — borrowed scan path; const Adapter *adapter — registry.
 * PRIVATE: grow — checked directory/task storage; ignored — traversal exclusions;
 * broad — sensitive root admission; addUnit — deduplicate and reject ambiguity;
 * assess — iterative directory inventory; Workspace_command — assess, confirm, build/free.
 * All arrays grow cold; paths survive builds and are freed after all children.
 * Scan errors prevent any build; build failures continue and return nonzero.
 */
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct ScanSlot { char *path; bool owned; } ScanSlot;
typedef struct BuildSlot { char *path; const Adapter *adapter; } BuildSlot;

// Review thresholds are safety prompts, never hard rejection/collection limits.
#define WORKSPACE_REVIEW_FILES 1000u
#define WORKSPACE_REVIEW_UNITS 100u
#define WORKSPACE_INITIAL_CAPACITY 16u

// Grow the cold slot directory with overflow checks, preserving old storage on error.
static bool grow(size_t count, size_t size, size_t *capacity, void **storage) {
    if (count < *capacity)
        return true;
    size_t next = *capacity == 0 ? WORKSPACE_INITIAL_CAPACITY : *capacity;
    if (next > SIZE_MAX / 2 || next * 2 > SIZE_MAX / size) {
        THROW("workspace inventory overflow");
        return false;
    }
    next *= 2;
    void *replacement = realloc(*storage, next * size);
    if (replacement == nullptr) {
        THROW("workspace inventory allocation failed");
        return false;
    }
    *storage = replacement;
    *capacity = next;
    return true;
}

// Fixed generated/cache/control names are excluded; ordinary dot directories remain.
static bool ignored(const char *name) {
    const char *names[] = { ".", "..", ".git", ".hg", ".svn", "node_modules",
        "target", "build", "dist", "out", ".cache", "__pycache__", ".venv", "venv" };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (strcmp(name, names[i]) == 0)
            return true;
    return false;
}

// Detect filesystem/home roots and common broad user folders after canonicalization.
static bool broad(const char *root) {
    if (strcmp(root, "/") == 0)
        return true;
    const char *name = strrchr(root, '/');
    if (name != nullptr && (strcmp(name, "/Downloads") == 0 || strcmp(name, "/Documents") == 0 ||
                            strcmp(name, "/Desktop") == 0))
        return true;
    const char *home = getenv("HOME");
    char *resolved = home == nullptr ? nullptr : realpath(home, nullptr);
    size_t length = strlen(root);
    bool result = resolved != nullptr && strncmp(root, resolved, length) == 0 &&
        (resolved[length] == '\0' || resolved[length] == '/');
    free(resolved);
    return result;
}

// Add one build unit per callback and directory, rejecting ambiguous native groups.
static bool addUnit(char *path, const Adapter *adapter, BuildSlot **units, size_t *count, size_t *capacity) {
    for (size_t i = 0; i < *count; ++i) {
        BuildSlot *unit = &(*units)[i];
        if (strcmp((*unit).path, path) != 0)
            continue;
        const Adapter *prior = (*unit).adapter;
        if ((*prior).build == (*adapter).build)
            return true;
        bool native = strcmp((*adapter).name, "c") == 0 || strcmp((*adapter).name, "objc") == 0 ||
            strcmp((*adapter).name, "cpp") == 0 || strcmp((*adapter).name, "cxx") == 0 || strcmp((*adapter).name, "c++") == 0;
        bool priorNative = strcmp((*prior).name, "c") == 0 || strcmp((*prior).name, "objc") == 0 ||
            strcmp((*prior).name, "cpp") == 0 || strcmp((*prior).name, "cxx") == 0 || strcmp((*prior).name, "c++") == 0;
        if (native && priorNative) {
            THROW("mixed C-family sources need an explicit project manifest: %s", path);
            return false;
        }
    }
    void *storage = *units;
    if (!grow(*count, sizeof(BuildSlot), capacity, &storage))
        return false;
    *units = storage;
    BuildSlot *unit = &(*units)[(*count)++];
    (*unit).path = path;
    (*unit).adapter = adapter;
    return true;
}

// Inventory every eligible directory before invoking any native tool.
static bool assess(ScanSlot **dirs, size_t *dirCount, size_t *dirCapacity,
                   BuildSlot **units, size_t *unitCount, size_t *unitCapacity,
                   size_t *files, size_t *unassigned, size_t *skipped) {
    const char *state = getenv("B_HOME");
    char *cache = state == nullptr ? nullptr : realpath(state, nullptr);
    bool ok = true;
    for (size_t index = 0; index < *dirCount && ok; ++index) {
        ScanSlot current = (*dirs)[index];
        struct dirent **entries = nullptr;
        int total = scandir(current.path, &entries, nullptr, alphasort);
        if (total < 0) {
            THROW("cannot assess %s: %s", current.path, strerror(errno));
            ok = false;
            break;
        }
        const Adapter *owner = nullptr;
        // Detect competing native manifests before assigning loose sources.
        for (int i = 0; i < total && ok && !current.owned; ++i) {
            struct dirent *entry = entries[i];
            const char *name = (*entry).d_name;
            const char *backend = strcmp(name, "Cargo.toml") == 0 ? "cargo" :
                strcmp(name, "package.json") == 0 ? "npm" :
                strcmp(name, "CMakeLists.txt") == 0 ? "cmake" :
                strcmp(name, "build.zig") == 0 ? "zig" : nullptr;
            if (backend == nullptr)
                continue;
            char *prefix = Util_combine(current.path, "/");
            char *path = Util_combine(prefix, name);
            struct stat info;
            bool regular = lstat(path, &info) == 0 && S_ISREG(info.st_mode);
            free(path);
            free(prefix);
            if (!regular) {
                THROW("project manifest must be a regular non-symlink file: %s", name);
                ok = false;
                break;
            }
            if (owner != nullptr) {
                THROW("multiple project manifests need explicit backend selection: %s", current.path);
                ok = false;
            } else
                owner = Adapter_forName(backend);
        }
        if (ok && owner != nullptr)
            ok = addUnit(current.path, owner, units, unitCount, unitCapacity);
        for (int i = 0; i < total && ok; ++i) {
            struct dirent *entry = entries[i];
            const char *name = (*entry).d_name;
            if (ignored(name)) {
                if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
                    ++*skipped;
                continue;
            }
            for (const unsigned char *c = (const unsigned char*) name; *c; ++c)
                if (*c < ' ' || *c == 127) {
                    THROW("workspace names containing control characters are unsupported");
                    ok = false;
                    break;
                }
            if (!ok)
                break;
            char *prefix = Util_combine(current.path, "/");
            char *path = Util_combine(prefix, name);
            free(prefix);
            struct stat info;
            if (lstat(path, &info) != 0) {
                THROW("cannot inspect %s", path);
                free(path);
                ok = false;
                break;
            }
            if (S_ISDIR(info.st_mode) && (cache == nullptr || strcmp(path, cache) != 0)) {
                void *storage = *dirs;
                ok = grow(*dirCount, sizeof(ScanSlot), dirCapacity, &storage);
                *dirs = storage;
                if (ok) {
                    ScanSlot *slot = &(*dirs)[(*dirCount)++];
                    (*slot).path = path;
                    (*slot).owned = current.owned || owner != nullptr;
                    path = nullptr;
                }
            } else if (S_ISREG(info.st_mode)) {
                ++*files;
                const Adapter *adapter = Adapter_forFile(path);
                if (!current.owned && owner == nullptr) {
                    if (adapter == nullptr || strcmp((*adapter).name, "html") == 0 ||
                        strcmp((*adapter).name, "web") == 0 || strcmp((*adapter).name, "sql") == 0)
                        ++*unassigned;
                    else
                        ok = addUnit(current.path, adapter, units, unitCount, unitCapacity);
                }
            } else {
                ++*skipped;
                // Existing directory adapters glob their inputs. Never admit a
                // known source symlink that such a glob could follow later.
                if (S_ISLNK(info.st_mode) && (Adapter_forFile(path) != nullptr || strcmp(name, "CMakeLists.txt") == 0)) {
                    THROW("source/manifest symlink requires explicit build outside workspace discovery: %s", path);
                    ok = false;
                }
            }
            free(path);
        }
        for (int i = 0; i < total; ++i)
            free(entries[i]);
        free(entries);
    }
    free(cache);
    return ok;
}

// Assess, optionally stop at the plan, then build with completed-unit progress.
int Workspace_command(int argc, char **argv) {
    if (argc < 1 || argc > 2 || argv == nullptr || argv[0] == nullptr ||
        (argc == 2 && (argv[1] == nullptr || (strcmp(argv[1], "--plan") != 0 && strcmp(argv[1], "--confirm") != 0)))) {
        THROW("usage: b build workspace <directory> [--plan|--confirm]");
        return EXIT_FAILURE;
    }
    char *root = realpath(argv[0], nullptr);
    struct stat info;
    if (root == nullptr || stat(root, &info) != 0 || !S_ISDIR(info.st_mode)) {
        THROW("workspace root is not a directory");
        free(root);
        return EXIT_FAILURE;
    }
    for (const unsigned char *c = (const unsigned char*) root; *c; ++c)
        if (*c < ' ' || *c == 127) {
            THROW("workspace root contains control characters");
            free(root);
            return EXIT_FAILURE;
        }
    size_t dirCount = 1, dirCapacity = WORKSPACE_INITIAL_CAPACITY;
    ScanSlot *dirs = Util_allocate(dirCapacity * sizeof(ScanSlot));
    (*dirs).path = root;
    BuildSlot *units = nullptr;
    size_t unitCount = 0, unitCapacity = 0, files = 0, unassigned = 0, skipped = 0;
    bool ok = assess(&dirs, &dirCount, &dirCapacity, &units, &unitCount, &unitCapacity,
                     &files, &unassigned, &skipped);
    bool plan = argc == 2 && strcmp(argv[1], "--plan") == 0;
    bool confirmed = argc == 2 && strcmp(argv[1], "--confirm") == 0;
    if (ok) {
        printf("Assessment: %zu files, %zu directories, %zu build units; %zu unassigned, %zu excluded entries\n",
               files, dirCount, unitCount, unassigned, skipped);
        for (size_t i = 0; i < unitCount; ++i) {
            BuildSlot *unit = &units[i];
            const Adapter *adapter = (*unit).adapter;
            printf("  %s: %s [tools: %s]\n", (*adapter).name, (*unit).path, (*adapter).tools);
        }
        if (!plan && !confirmed && (broad(root) || files >= WORKSPACE_REVIEW_FILES || unitCount >= WORKSPACE_REVIEW_UNITS)) {
            THROW("broad workspace: review --plan then repeat with --confirm; sudo is not required");
            ok = false;
        }
    }
    if (ok && !plan) {
        size_t failed = 0;
        for (size_t i = 0; i < unitCount; ++i) {
            BuildSlot *unit = &units[i];
            const Adapter *adapter = (*unit).adapter;
            printf("[%zu/%zu] Building %s: %s\n", i + 1, unitCount, (*adapter).name, (*unit).path);
            fflush(stdout);
            char *output = nullptr;
            int status = (*adapter).build((*unit).path, &output);
            failed += status != 0;
            printf("Building %.0f%% — %s%s%s\n", 100.0 * (double) (i + 1) / (double) unitCount,
                   status == 0 ? "OK" : "FAIL", output == nullptr ? "" : ": ", output == nullptr ? "" : output);
            free(output);
        }
        printf("Complete: %zu succeeded, %zu failed%s\n", unitCount - failed, failed,
               unitCount == 0 ? " (no buildable units)" : "");
        ok = failed == 0;
    }
    free(units);
    for (size_t i = 0; i < dirCount; ++i)
        free(dirs[i].path);
    free(dirs);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
