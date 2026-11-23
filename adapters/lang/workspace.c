#include "vex.h"
#include "adapters/lang/workspace.h"
#include "adapters/lang/workspace_internal.h"
#include "adapters/lang/adapter.h"
#include "b.h"
#include "b/b_str.h"

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
 * ScanSlot: utf8 *path, owned directory; bool owned, ancestor manifest owns it.
 * BuildSlot: utf8 *path, borrowed scan path; const Adapter *adapter, registry.
 * PRIVATE: grow, checked directory/task storage; ignored, traversal exclusions;
 * broad, sensitive root admission; addUnit, deduplicate and reject ambiguity;
 * assess, iterative directory inventory; Workspace_command, assess, confirm, build/free.
 * All arrays grow cold; paths survive builds and are freed after all children.
 * Scan errors prevent any build; build failures continue and return nonzero.
 */
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>



;;SETTER
static inline fn ScanSlot_setPath(ScanSlot *self, utf8 *path)
{
    (*self).path = path;
}
;;SETTER
static inline fn ScanSlot_setOwned(ScanSlot *self, bool owned)
{
    (*self).owned = owned;
}
;;GETTER
static inline utf8 *BuildSlot_path(const BuildSlot *self)
{
    return (*self).path;
}
;;SETTER
static inline fn BuildSlot_setPath(BuildSlot *self, utf8 *path)
{
    (*self).path = path;
}
;;GETTER
static inline const Adapter *BuildSlot_adapter(const BuildSlot *self)
{
    return (*self).adapter;
}
;;SETTER
static inline fn BuildSlot_setAdapter(BuildSlot *self, const Adapter *adapter)
{
    (*self).adapter = adapter;
}
;;GETTER
static inline const utf8 *Adapter_name(const Adapter *self)
{
    return (*self).name;
}
;;GETTER
static inline const utf8 *Adapter_tools(const Adapter *self)
{
    return (*self).tools;
}
;;GETTER
static inline i32 (*Adapter_build(const Adapter *self))(const utf8 *, utf8 **)
{
    return (*self).build;
}
;;GETTER
static inline const utf8 *dirent_d_name(const struct dirent *self)
{
    return (*self).d_name;
}

/** Review thresholds are safety prompts, never hard rejection/collection limits. */
#define WORKSPACE_REVIEW_FILES 1000u
#define WORKSPACE_REVIEW_UNITS 100u
#define WORKSPACE_INITIAL_CAPACITY 16u

/** Grow the cold slot directory with overflow checks, preserving old storage on error. */
static bool grow(bytes count, bytes size, bytes *capacity, any *storage) {
    if (count < *capacity)
        return true;
    bytes next = *capacity == 0 ? WORKSPACE_INITIAL_CAPACITY : *capacity;
    if (next > SIZE_MAX / 2 || next * 2 > SIZE_MAX / size) {
        THROW("workspace inventory overflow");
        return false;
    }
    next *= 2;
    any replacement = realloc(*storage, next * size);
    if (replacement == nullptr) {
        THROW("workspace inventory allocation failed");
        return false;
    }
    *storage = replacement;
    *capacity = next;
    return true;
}

/** Fixed generated/cache/control names are excluded; ordinary dot directories remain. */
static bool ignored(const utf8 *name) {
    const utf8 *names[] = { ".", "..", ".git", ".hg", ".svn", "node_modules",
        "target", "build", "dist", "out", ".cache", "__pycache__", ".venv", "venv" };
    for (bytes i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (bStrEq(name, names[i]))
            return true;
    return false;
}

/** Detect filesystem/home roots and common broad user folders after canonicalization. */
static bool broad(const utf8 *root) {
    if (bStrEq(root, "/"))
        return true;
    const utf8 *name = strrchr(root, '/');
    if (name != nullptr && (bStrEq(name, "/Downloads") || bStrEq(name, "/Documents") ||
                            bStrEq(name, "/Desktop")))
        return true;
    const utf8 *home = getenv("HOME");
    utf8 *resolved = home == nullptr ? nullptr : realpath(home, nullptr);
    bytes length = strlen(root);
    bool result = resolved != nullptr && bStrPrefixN(root, resolved, length) &&
        (resolved[length] == '\0' || resolved[length] == '/');
    free(resolved);
    return result;
}

/** Add one build unit per callback and directory, rejecting ambiguous native groups. */
static bool addUnit(utf8 *path, const Adapter *adapter, BuildSlot **units, bytes *count, bytes *capacity) {
    for (bytes i = 0; i < *count; ++i) {
        BuildSlot *unit = &(*units)[i];
        if (!bStrEq(BuildSlot_path(unit), path))
            continue;
        const Adapter *prior = BuildSlot_adapter(unit);
        if (Adapter_build(prior) == Adapter_build(adapter))
            return true;
        const utf8 *adapterName = Adapter_name(adapter);
        const utf8 *priorName = Adapter_name(prior);
        bool native = bStrEq(adapterName, "c") || bStrEq(adapterName, "objc") ||
            bStrEq(adapterName, "cpp") || bStrEq(adapterName, "cxx") || bStrEq(adapterName, "c++");
        bool priorNative = bStrEq(priorName, "c") || bStrEq(priorName, "objc") ||
            bStrEq(priorName, "cpp") || bStrEq(priorName, "cxx") || bStrEq(priorName, "c++");
        if (native && priorNative) {
            utf8 *manifest = Util_combine(path, "/b.assembly.json");
            struct stat info;
            bool explicitUnit = lstat(manifest, &info) == 0 && S_ISREG(info.st_mode);
            free(manifest);
            if (explicitUnit)
                return true;
            THROW("mixed C-family sources need an explicit project manifest: %s", path);
            return false;
        }
    }
    any storage = *units;
    if (!grow(*count, sizeof(BuildSlot), capacity, &storage))
        return false;
    *units = storage;
    BuildSlot *unit = &(*units)[(*count)++];
    BuildSlot_setPath(unit, path);
    BuildSlot_setAdapter(unit, adapter);
    return true;
}

/** Inventory every eligible directory before invoking any native tool. */
static bool assess(ScanSlot **dirs, bytes *dirCount, bytes *dirCapacity,
                   BuildSlot **units, bytes *unitCount, bytes *unitCapacity,
                   bytes *files, bytes *unassigned, bytes *skipped) {
    const utf8 *state = getenv("B_HOME");
    utf8 *cache = state == nullptr ? nullptr : realpath(state, nullptr);
    bool ok = true;
    for (bytes index = 0; index < *dirCount && ok; ++index) {
        ScanSlot current = (*dirs)[index];
        struct dirent **entries = nullptr;
        i32 total = scandir(current.path, &entries, nullptr, alphasort);
        if (total < 0) {
            THROW("cannot assess %s: %s", current.path, strerror(errno));
            ok = false;
            break;
        }
        const Adapter *owner = nullptr;
        for (i32 i = 0; i < total && ok && !current.owned; ++i) {
            struct dirent *entry = entries[i];
            const utf8 *name = dirent_d_name(entry);
            const utf8 *backend = bStrEq(name, "Cargo.toml") ? "cargo" :
                bStrEq(name, "package.json") ? "npm" :
                bStrEq(name, "CMakeLists.txt") ? "cmake" :
                bStrEq(name, "build.zig") ? "zig" : nullptr;
            if (backend == nullptr)
                continue;
            utf8 *prefix = Util_combine(current.path, "/");
            utf8 *path = Util_combine(prefix, name);
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
        for (i32 i = 0; i < total && ok; ++i) {
            struct dirent *entry = entries[i];
            const utf8 *name = dirent_d_name(entry);
            if (ignored(name)) {
                if (!bStrEq(name, ".") && !bStrEq(name, ".."))
                    ++*skipped;
                continue;
            }
            for (const u8 *c = (const u8*) name; *c; ++c)
                if (*c < ' ' || *c == 127) {
                    THROW("workspace names containing control characters are unsupported");
                    ok = false;
                    break;
                }
            if (!ok)
                break;
            utf8 *prefix = Util_combine(current.path, "/");
            utf8 *path = Util_combine(prefix, name);
            free(prefix);
            struct stat info;
            if (lstat(path, &info) != 0) {
                THROW("cannot inspect %s", path);
                free(path);
                ok = false;
                break;
            }
            if (S_ISDIR(info.st_mode) && (cache == nullptr || !bStrEq(path, cache))) {
                any storage = *dirs;
                ok = grow(*dirCount, sizeof(ScanSlot), dirCapacity, &storage);
                *dirs = storage;
                if (ok) {
                    ScanSlot *slot = &(*dirs)[(*dirCount)++];
                    ScanSlot_setPath(slot, path);
                    ScanSlot_setOwned(slot, current.owned || owner != nullptr);
                    path = nullptr;
                }
            } else if (S_ISREG(info.st_mode)) {
                ++*files;
                const Adapter *adapter = Adapter_forFile(path);
                if (!current.owned && owner == nullptr) {
                    const utf8 *adapterName = adapter ? Adapter_name(adapter) : nullptr;
                    if (adapter == nullptr || bStrEq(adapterName, "html") ||
                        bStrEq(adapterName, "web") || bStrEq(adapterName, "sql"))
                        ++*unassigned;
                    else
                        ok = addUnit(current.path, adapter, units, unitCount, unitCapacity);
                }
            } else {
                ++*skipped;
                if (S_ISLNK(info.st_mode) && (Adapter_forFile(path) != nullptr || bStrEq(name, "CMakeLists.txt"))) {
                    THROW("source/manifest symlink requires explicit build outside workspace discovery: %s", path);
                    ok = false;
                }
            }
            free(path);
        }
        for (i32 i = 0; i < total; ++i)
            free(entries[i]);
        free(entries);
    }
    free(cache);
    return ok;
}

/** Assess, optionally stop at the plan, then build with completed-unit progress. */
i32 Workspace_command(i32 argc, utf8 **argv) {
    if (argc < 1 || argc > 2 || argv == nullptr || argv[0] == nullptr ||
        (argc == 2 && (argv[1] == nullptr || (!bStrEq(argv[1], "--plan") && !bStrEq(argv[1], "--confirm"))))) {
        THROW("usage: b.sh build workspace <directory> [--plan|--confirm]");
        return EXIT_FAILURE;
    }
    utf8 *root = realpath(argv[0], nullptr);
    struct stat info;
    if (root == nullptr || stat(root, &info) != 0 || !S_ISDIR(info.st_mode)) {
        THROW("workspace root is not a directory");
        free(root);
        return EXIT_FAILURE;
    }
    for (const u8 *c = (const u8*) root; *c; ++c)
        if (*c < ' ' || *c == 127) {
            THROW("workspace root contains control characters");
            free(root);
            return EXIT_FAILURE;
        }
    bytes dirCount = 1, dirCapacity = WORKSPACE_INITIAL_CAPACITY;
    ScanSlot *dirs = Util_allocate(dirCapacity * sizeof(ScanSlot));
    ScanSlot_setPath(dirs, root);
    BuildSlot *units = nullptr;
    bytes unitCount = 0, unitCapacity = 0, files = 0, unassigned = 0, skipped = 0;
    bool ok = assess(&dirs, &dirCount, &dirCapacity, &units, &unitCount, &unitCapacity,
                     &files, &unassigned, &skipped);
    bool plan = argc == 2 && bStrEq(argv[1], "--plan");
    bool confirmed = argc == 2 && bStrEq(argv[1], "--confirm");
    if (ok) {
        printf("Assessment: %zu files, %zu directories, %zu build units; %zu unassigned, %zu excluded entries\n",
               files, dirCount, unitCount, unassigned, skipped);
        for (bytes i = 0; i < unitCount; ++i) {
            BuildSlot *unit = &units[i];
            const Adapter *adapter = BuildSlot_adapter(unit);
            printf("  %s: %s [tools: %s]\n", Adapter_name(adapter), BuildSlot_path(unit), Adapter_tools(adapter));
        }
        if (!plan && !confirmed && (broad(root) || files >= WORKSPACE_REVIEW_FILES || unitCount >= WORKSPACE_REVIEW_UNITS)) {
            THROW("broad workspace: review --plan then repeat with --confirm; sudo is not required");
            ok = false;
        }
    }
    if (ok && !plan) {
        bytes failed = 0;
        for (bytes i = 0; i < unitCount; ++i) {
            BuildSlot *unit = &units[i];
            const Adapter *adapter = BuildSlot_adapter(unit);
            const utf8 *unitPath = BuildSlot_path(unit);
            printf("[%zu/%zu] Building %s: %s\n", i + 1, unitCount, Adapter_name(adapter), unitPath);
            fflush(stdout);
            utf8 *output = nullptr;
            i32 status = Adapter_build(adapter)(unitPath, &output);
            failed += status != 0;
            printf("Building %.0f%%, %s%s%s\n", 100.0 * (d64) (i + 1) / (d64) unitCount,
                   status == 0 ? "OK" : "FAIL", output == nullptr ? "" : ": ", output == nullptr ? "" : output);
            free(output);
        }
        printf("Complete: %zu succeeded, %zu failed%s\n", unitCount - failed, failed,
               unitCount == 0 ? " (no buildable units)" : "");
        ok = failed == 0;
    }
    free(units);
    for (bytes i = 0; i < dirCount; ++i)
        free(dirs[i].path);
    free(dirs);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
