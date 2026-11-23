/**
 * CLASS: Territory, implementation of the marker, its walk and its inventory.
 * DEFINITION: find resolves upward to the nearest .b/; workspacePath names the
 * generated file; claim creates the marker and writes what it found.
 * OVERVIEW: find; workspacePath; claim; PRIVATE: grow; excluded; normalize;
 * unitFor; addSource; relative; escape; inventory; emit.
 */

#include "vex.h"
#include "b/b_str.h"
#include "b/territory_internal.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "territory.h"
#include "adapters/lang/adapter.h"
#include "b.h"


;;DEFINITION
/* The walk is cold and total: every directory under the claimed root is read
 * once, in alphasort order, and every regular file is recorded under the
 * directory that holds it, the path is one layer, the names inside it the
 * next. Compiled sources are also grouped as units so the build can hand one
 * directory at a time to its adapter. Generated, cache and control names are
 * excluded; the walk never leaves the claimed root and never follows a symlink
 * out of it. The inventory is disposable: regenerating replaces it, and no
 * consumer reads it back as intent.
 */
;;OVERVIEW
/* PRIVATE RECORDS (co-located, one inventory):
 * TerritoryUnit, utf8 *directory (relative, owned), utf8 **names (owned),
 *     count/capacity of the directory's compiled sources.
 * TerritoryFiles, utf8 *directory (relative, owned) and the owned names of
 *     the regular files directly inside it: the two layers of the inventory.
 * TerritoryWalk, directory stack, the unit array, and the directory/file
 *     tree; source, file and exclusion counts.
 * PUBLIC (added): Territory_buildAll builds every unit at or below a directory,
 * scoped by the caller's location; Territory_writeAssembly authors one unit's
 * b.sh.assembly.json once, with its include roots.
 * PRIVATE (added): absoluteOf joins a unit onto the root; inScope is the scope
 * test; classify picks one adapter family per unit; entryOf returns the owned
 * entry-source path; displayNameOf title-cases; freeEntries releases a listing;
 * keyText writes a quoted key; releaseWalk releases an inventory walk.
 * PRIVATE STATIC: grow, checked doubling; excluded, fixed skip names;
 * normalize, strip one trailing separator; unitFor/addSource, record a source
 * under its directory; relative, root-relative path; addFile, record one
 * regular file under its directory; escape, JSON string emission; inventory,
 * the cold walk; emit, write the two-layer inventory.
 * Errors report through THROW and abort the claim; the caller owns returned
 * paths, and every slot is released before the claim returns.
 */

#define TERRITORY_INITIAL_CAPACITY 8u



;;GETTER
static inline string dirent_d_name(const struct dirent *self)
{
    return (*self).d_name;
}
;;GETTER
static inline string Adapter_name(const Adapter *self)
{
    return (*self).name;
}
;;GETTER
static inline i32 (*Adapter_build(const Adapter *self))(string , utf8 **)
{
    return (*self).build;
}
;;GETTER
static inline i32 (*Adapter_archive(const Adapter *self))(string , utf8 **)
{
    return (*self).archive;
}
;;GETTER
static inline utf8 *(*Adapter_entry(const Adapter *self))(string) { return (*self).entry; }

;;GETTER
static inline utf8 *TerritoryUnit_directory(const TerritoryUnit *self)
{
    return (*self).directory;
}
;;SETTER
static inline fn TerritoryUnit_setDirectory(TerritoryUnit *self, utf8 *directory)
{
    (*self).directory = directory;
}
;;GETTER
static inline utf8 **TerritoryUnit_names(const TerritoryUnit *self)
{
    return (*self).names;
}
;;SETTER
static inline fn TerritoryUnit_setNames(TerritoryUnit *self, utf8 **names)
{
    (*self).names = names;
}
;;GETTER
static inline bytes TerritoryUnit_count(const TerritoryUnit *self)
{
    return (*self).count;
}
;;SETTER
static inline fn TerritoryUnit_setCount(TerritoryUnit *self, bytes count)
{
    (*self).count = count;
}
;;GETTER
static inline bytes *TerritoryUnit_capacityPtr(TerritoryUnit *self)
{
    return &(*self).capacity;
}
;;SETTER
static inline fn TerritoryUnit_setCapacity(TerritoryUnit *self, bytes capacity)
{
    (*self).capacity = capacity;
}

;;GETTER
static inline utf8 *TerritoryFiles_directory(const TerritoryFiles *self)
{
    return (*self).directory;
}
;;GETTER
static inline utf8 **TerritoryFiles_names(const TerritoryFiles *self)
{
    return (*self).names;
}
;;SETTER
static inline fn TerritoryFiles_setNames(TerritoryFiles *self, utf8 **names)
{
    (*self).names = names;
}
;;GETTER
static inline bytes TerritoryFiles_count(const TerritoryFiles *self)
{
    return (*self).count;
}
;;SETTER
static inline fn TerritoryFiles_setCount(TerritoryFiles *self, bytes count)
{
    (*self).count = count;
}
;;GETTER
static inline bytes *TerritoryFiles_capacityPtr(TerritoryFiles *self)
{
    return &(*self).capacity;
}

;;GETTER
static inline utf8 **TerritoryWalk_directories(const TerritoryWalk *self)
{
    return (*self).directories;
}
;;SETTER
static inline fn TerritoryWalk_setDirectories(TerritoryWalk *self, utf8 **directories)
{
    (*self).directories = directories;
}
;;GETTER
static inline bytes TerritoryWalk_directoryCount(const TerritoryWalk *self)
{
    return (*self).directoryCount;
}
;;SETTER
static inline fn TerritoryWalk_setDirectoryCount(TerritoryWalk *self, bytes count)
{
    (*self).directoryCount = count;
}
;;GETTER
static inline bytes *TerritoryWalk_directoryCapacityPtr(TerritoryWalk *self)
{
    return &(*self).directoryCapacity;
}
;;GETTER
static inline TerritoryUnit *TerritoryWalk_units(const TerritoryWalk *self)
{
    return (*self).units;
}
;;SETTER
static inline fn TerritoryWalk_setUnits(TerritoryWalk *self, TerritoryUnit *units)
{
    (*self).units = units;
}
;;GETTER
static inline bytes TerritoryWalk_unitCount(const TerritoryWalk *self)
{
    return (*self).unitCount;
}
;;SETTER
static inline fn TerritoryWalk_setUnitCount(TerritoryWalk *self, bytes count)
{
    (*self).unitCount = count;
}
;;GETTER
static inline bytes *TerritoryWalk_unitCapacityPtr(TerritoryWalk *self)
{
    return &(*self).unitCapacity;
}
;;GETTER
static inline TerritoryFiles *TerritoryWalk_tree(const TerritoryWalk *self)
{
    return (*self).tree;
}
;;SETTER
static inline fn TerritoryWalk_setTree(TerritoryWalk *self, TerritoryFiles *tree)
{
    (*self).tree = tree;
}
;;GETTER
static inline bytes TerritoryWalk_treeCount(const TerritoryWalk *self)
{
    return (*self).treeCount;
}
;;SETTER
static inline fn TerritoryWalk_setTreeCount(TerritoryWalk *self, bytes count)
{
    (*self).treeCount = count;
}
;;GETTER
static inline bytes *TerritoryWalk_treeCapacityPtr(TerritoryWalk *self)
{
    return &(*self).treeCapacity;
}
;;GETTER
static inline bytes TerritoryWalk_files(const TerritoryWalk *self)
{
    return (*self).files;
}
;;SETTER
static inline fn TerritoryWalk_setFiles(TerritoryWalk *self, bytes files)
{
    (*self).files = files;
}
;;GETTER
static inline bytes TerritoryWalk_sources(const TerritoryWalk *self)
{
    return (*self).sources;
}
;;SETTER
static inline fn TerritoryWalk_setSources(TerritoryWalk *self, bytes sources)
{
    (*self).sources = sources;
}
;;GETTER
static inline bytes TerritoryWalk_excluded(const TerritoryWalk *self)
{
    return (*self).excluded;
}
;;SETTER
static inline fn TerritoryWalk_setExcluded(TerritoryWalk *self, bytes excluded)
{
    (*self).excluded = excluded;
}

/** Grow a cold slot array with overflow checks, preserving old storage on error. */
static bool grow(bytes count, bytes size, bytes *capacity, any *storage) {
    if (count < *capacity)
        return true;
    bytes next = *capacity == 0 ? TERRITORY_INITIAL_CAPACITY : *capacity;
    if (next > SIZE_MAX / 2 || next * 2 > SIZE_MAX / size) {
        THROW("territory inventory overflow");
        return false;
    }
    next *= 2;
    any replacement = realloc(*storage, next * size);
    if (replacement == nullptr) {
        THROW("territory inventory allocation failed");
        return false;
    }
    *storage = replacement;
    *capacity = next;
    return true;
}

/** Fixed generated, cache and control names; ordinary dot directories remain. */
static bool excluded(string name) {
    string names[] = { ".", "..", ".git", ".hg", ".svn", ".b", "node_modules",
        "target", "build", "dist", "out", ".cache", "__pycache__", ".venv", "venv" };
    for (bytes index = 0; index < sizeof names / sizeof names[0]; ++index)
        if (bStrEq(name, names[index]))
            return true;
    return false;
}

/** Drop trailing separators while keeping a root ("/" or "C:/") intact. */
static fn normalize(utf8 *path) {
    bytes length = strlen(path);
    while (length > 1 && (path[length - 1] == '/' || path[length - 1] == '\\')) {
        if (length == 3 && path[1] == ':')
            break;
        path[--length] = '\0';
    }
}

/** Return the unit owning one directory, appending a new unit when absent. */
static TerritoryUnit *unitFor(TerritoryWalk *walk, string directory) {
    bytes count = TerritoryWalk_unitCount(walk);
    TerritoryUnit *units = TerritoryWalk_units(walk);
    for (bytes index = 0; index < count; ++index)
        if (bStrEq(TerritoryUnit_directory(&units[index]), directory))
            return &units[index];
    any storage = units;
    if (!grow(count, sizeof(TerritoryUnit), TerritoryWalk_unitCapacityPtr(walk), &storage))
        return nullptr;
    TerritoryWalk_setUnits(walk, storage);
    units = TerritoryWalk_units(walk);
    TerritoryUnit *unit = &units[count];
    TerritoryWalk_setUnitCount(walk, count + 1);
    TerritoryUnit_setDirectory(unit, Util_combine(directory, ""));
    TerritoryUnit_setNames(unit, nullptr);
    TerritoryUnit_setCount(unit, 0);
    TerritoryUnit_setCapacity(unit, 0);
    return unit;
}

/** Record one compiled source under the directory that owns it. */
static bool addSource(TerritoryWalk *walk, string directory, string name) {
    TerritoryUnit *unit = unitFor(walk, directory);
    if (unit == nullptr)
        return false;
    bytes count = TerritoryUnit_count(unit);
    any storage = TerritoryUnit_names(unit);
    if (!grow(count, sizeof(utf8*), TerritoryUnit_capacityPtr(unit), &storage))
        return false;
    TerritoryUnit_setNames(unit, storage);
    TerritoryUnit_names(unit)[count] = Util_combine(name, "");
    TerritoryUnit_setCount(unit, count + 1);
    TerritoryWalk_setSources(walk, TerritoryWalk_sources(walk) + 1);
    return true;
}

/** Name a directory relative to the territory root; the root itself is ".". */
static utf8 *relative(string root, string directory) {
    bytes length = strlen(root);
    if (bStrEq(root, directory))
        return Util_combine(".", "");
    if (bStrPrefixN(directory, root, length) && directory[length] == '/')
        return Util_combine(directory + length + 1, "");
    return Util_combine(directory, "");
}

/**
 * Record one regular file under its directory: the directory is the first
 * layer, the names inside it are the second.
 */
static bool addFile(TerritoryWalk *walk, string root, string directory, string name) {
    utf8 *path = relative(root, directory);
    bytes count = TerritoryWalk_treeCount(walk);
    TerritoryFiles *tree = TerritoryWalk_tree(walk);
    bytes index = 0;
    while (index < count && !bStrEq(TerritoryFiles_directory(&tree[index]), path))
        ++index;
    if (index == count) {
        any storage = tree;
        if (!grow(count, sizeof(TerritoryFiles), TerritoryWalk_treeCapacityPtr(walk), &storage)) {
            free(path);
            return false;
        }
        TerritoryWalk_setTree(walk, storage);
        tree = TerritoryWalk_tree(walk);
        tree[count].directory = path;
        tree[count].names = nullptr;
        tree[count].count = 0;
        tree[count].capacity = 0;
        TerritoryWalk_setTreeCount(walk, count + 1);
    } else {
        free(path);
    }
    TerritoryFiles *entry = &tree[index];
    bytes nameCount = TerritoryFiles_count(entry);
    any names = TerritoryFiles_names(entry);
    if (!grow(nameCount, sizeof(utf8*), TerritoryFiles_capacityPtr(entry), &names))
        return false;
    TerritoryFiles_setNames(entry, names);
    utf8 **grown = TerritoryFiles_names(entry);
    grown[nameCount] = Util_combine(name, "");
    TerritoryFiles_setCount(entry, nameCount + 1);
    TerritoryWalk_setFiles(walk, TerritoryWalk_files(walk) + 1);
    return true;
}

/** Emit one JSON string, escaping what cannot appear literally in a path. */
static fn escape(FILE *file, string text) {
    fputc('"', file);
    for (const u8 *cursor = (const u8*) text; *cursor != '\0'; ++cursor) {
        if (*cursor == '"' || *cursor == '\\') {
            fputc('\\', file);
            fputc(*cursor, file);
        } else if (*cursor < ' ') {
            utf8 esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", (u32) *cursor);
            fputs(esc, file);
        } else {
            fputc(*cursor, file);
        }
    }
    fputc('"', file);
}

/** Read every directory under the root once and record each unit it finds. */
static bool inventory(string root, TerritoryWalk *walk) {
    any storage = TerritoryWalk_directories(walk);
    if (!grow(TerritoryWalk_directoryCount(walk), sizeof(utf8*), TerritoryWalk_directoryCapacityPtr(walk), &storage))
        return false;
    TerritoryWalk_setDirectories(walk, storage);
    TerritoryWalk_directories(walk)[0] = Util_combine(root, "");
    TerritoryWalk_setDirectoryCount(walk, 1);
    for (bytes index = 0; index < TerritoryWalk_directoryCount(walk); ++index) {
        utf8 **directories = TerritoryWalk_directories(walk);
        string current = directories[index];
        struct dirent **entries = nullptr;
        i32 total = scandir(current, &entries, nullptr, alphasort);
        if (total < 0) {
            THROW("cannot read %s: %s", current, strerror(errno));
            return false;
        }
        bool ok = true;
        for (i32 entryIndex = 0; entryIndex < total && ok; ++entryIndex) {
            struct dirent *entry = entries[entryIndex];
            string name = dirent_d_name(entry);
            if (excluded(name)) {
                if (!bStrEq(name, ".") && !bStrEq(name, ".."))
                    TerritoryWalk_setExcluded(walk, TerritoryWalk_excluded(walk) + 1);
                continue;
            }
            utf8 *prefix = Util_combine(current, "/");
            utf8 *path = Util_combine(prefix, name);
            free(prefix);
            struct stat information;
            if (lstat(path, &information) != 0) {
                THROW("cannot inspect %s: %s", path, strerror(errno));
                free(path);
                ok = false;
                break;
            }
            if (S_ISDIR(information.st_mode)) {
                bytes count = TerritoryWalk_directoryCount(walk);
                any next = TerritoryWalk_directories(walk);
                ok = grow(count, sizeof(utf8*), TerritoryWalk_directoryCapacityPtr(walk), &next);
                TerritoryWalk_setDirectories(walk, next);
                if (ok) {
                    utf8 **grown = TerritoryWalk_directories(walk);
                    grown[count] = path;
                    TerritoryWalk_setDirectoryCount(walk, count + 1);
                } else {
                    free(path);
                }
            } else if (S_ISREG(information.st_mode)) {
                if (Adapter_forFile(path) != nullptr) {
                    ok = addFile(walk, root, current, name);
                    if (ok) {
                        utf8 *directory = relative(root, current);
                        ok = addSource(walk, directory, name);
                        free(directory);
                    }
                } else {
                    TerritoryWalk_setExcluded(walk, TerritoryWalk_excluded(walk) + 1);
                }
                free(path);
            } else {
                TerritoryWalk_setExcluded(walk, TerritoryWalk_excluded(walk) + 1);
                free(path);
            }
        }
        for (i32 entryIndex = 0; entryIndex < total; ++entryIndex)
            free(entries[entryIndex]);
        free(entries);
        if (!ok)
            return false;
    }
    return true;
}

typedef enum { UNIT_OK, UNIT_MIXED } UnitVerdict;

/** Join a unit's relative directory onto the root; "." names the root itself. */
static utf8 *absoluteOf(string root, string directory) {
    if (bStrEq(directory, "."))
        return Util_combine(root, "");
    utf8 *prefix = Util_combine(root, "/");
    utf8 *path = Util_combine(prefix, directory);
    free(prefix);
    return path;
}

/** Whether a unit lies at or below the scope directory; scope "." means everything. */
static bool inScope(string directory, string scope) {
    if (bStrEq(scope, "."))
        return true;
    bytes length = strlen(scope);
    return bStrEq(directory, scope) ||
        (bStrPrefixN(directory, scope, length) && directory[length] == '/');
}

/**
 * Choose the adapter that claims a unit's sources. Adapters that share a build
 * callback form one family (glsl stages, the js/ts extensions), so only sources
 * from different families make a unit mixed.
 */
static UnitVerdict classify(const TerritoryUnit *unit, const Adapter **adapter) {
    *adapter = nullptr;
    bytes count = TerritoryUnit_count(unit);
    utf8 *const *names = TerritoryUnit_names(unit);
    for (bytes source = 0; source < count; ++source) {
        const Adapter *claimed = Adapter_forFile(names[source]);
        if (claimed == nullptr)
            continue;
        if (*adapter == nullptr)
            *adapter = claimed;
        else if (Adapter_build(*adapter) != Adapter_build(claimed))
            return UNIT_MIXED;
    }
    return UNIT_OK;
}

/** Ask the unit's adapter for the owned path of its entry source; nullptr means none. */
static utf8 *entryOf(const Adapter *adapter, string absolute) {
    utf8 *(*entryFn)(string) = Adapter_entry(adapter);
    return entryFn != nullptr ? entryFn(absolute) : nullptr;
}

/** Write the generated fact file; the only writer of .b/workspace.json. */
static bool emit(string path, string root, TerritoryWalk *walk) {
    FILE *file = fopen(path, "wb");
    if (file == nullptr) {
        THROW("cannot write %s: %s", path, strerror(errno));
        return false;
    }
    bytes treeCount = TerritoryWalk_treeCount(walk);
    TerritoryFiles *tree = TerritoryWalk_tree(walk);
    fputs("{\n  \"root\": ", file);
    escape(file, root);
    fputs(",\n  \"directories\": {", file);
    for (bytes index = 0; index < treeCount; ++index) {
        if (index != 0)
            fputc(',', file);
        fputs("\n    ", file);
        escape(file, tree[index].directory);
        fputs(": [", file);
        for (bytes name = 0; name < tree[index].count; ++name) {
            if (name != 0)
                fputs(", ", file);
            escape(file, tree[index].names[name]);
        }
        fputc(']', file);
    }
    fprint(file, treeCount == 0 ? "" : "\n  ", "}\n}");
    bool ok = fflush(file) == 0 && ferror(file) == 0;
    if (fclose(file) != 0)
        ok = false;
    if (!ok)
        THROW("cannot complete %s", path);
    return ok;
}

/** Resolve upward to the nearest claimed ancestor, or nullptr when none exists. */
utf8 *Territory_find(string startDirectory) {
    if (startDirectory == nullptr || *startDirectory == '\0') {
        THROW("territory lookup requires a directory");
        return nullptr;
    }
    utf8 *current = realpath(startDirectory, nullptr);
    if (current == nullptr) {
        THROW("cannot resolve directory: %s", startDirectory);
        return nullptr;
    }
    normalize(current);
    while (true) {
        utf8 *marker = Util_combine(current, "/.b/workspace.json");
        struct stat information;
        bool present = stat(marker, &information) == 0 && S_ISREG(information.st_mode);
        free(marker);
        if (present)
            return current;
        utf8 *parent = Util_parentDirectory(current);
        normalize(parent);
        if (bStrEq(parent, current)) {
            free(parent);
            free(current);
            return nullptr;
        }
        free(current);
        current = parent;
    }
}

/** Name the generated file inside a claimed root. */
utf8 *Territory_workspacePath(string root) {
    return Util_combine(root, "/.b/workspace.json");
}

/** Release every slot of a walk; the caller owns the walk until this runs. */
static fn releaseWalk(TerritoryWalk *walk) {
    bytes unitCount = TerritoryWalk_unitCount(walk);
    TerritoryUnit *units = TerritoryWalk_units(walk);
    for (bytes index = 0; index < unitCount; ++index) {
        TerritoryUnit *unit = &units[index];
        bytes count = TerritoryUnit_count(unit);
        utf8 **names = TerritoryUnit_names(unit);
        for (bytes source = 0; source < count; ++source)
            free(names[source]);
        free(names);
        free(TerritoryUnit_directory(unit));
    }
    bytes dirCount = TerritoryWalk_directoryCount(walk);
    utf8 **dirs = TerritoryWalk_directories(walk);
    for (bytes index = 0; index < dirCount; ++index)
        free(dirs[index]);
    bytes treeCount = TerritoryWalk_treeCount(walk);
    TerritoryFiles *tree = TerritoryWalk_tree(walk);
    for (bytes index = 0; index < treeCount; ++index) {
        TerritoryFiles *entry = &tree[index];
        bytes count = TerritoryFiles_count(entry);
        utf8 **names = TerritoryFiles_names(entry);
        for (bytes name = 0; name < count; ++name)
            free(names[name]);
        free(names);
        free(TerritoryFiles_directory(entry));
    }
    free(units);
    free(dirs);
    free(tree);
}

/** Claim a directory: create the marker, walk it once, write the inventory. */
bool Territory_claim(string directory) {
    if (directory == nullptr || *directory == '\0') {
        THROW("generate requires a directory");
        return false;
    }
    utf8 *root = realpath(directory, nullptr);
    if (root == nullptr) {
        THROW("cannot resolve directory: %s", directory);
        return false;
    }
    normalize(root);
    if (bStrEq(root, "/")) {
        THROW("refusing to claim the filesystem root");
        free(root);
        return false;
    }
    utf8 *marker = Util_combine(root, "/.b");
    struct stat information;
    bool claimed = stat(marker, &information) == 0;
    if (claimed && !S_ISDIR(information.st_mode)) {
        THROW(".b exists and is not a directory: %s", marker);
        free(marker);
        free(root);
        return false;
    }
    if (!claimed) {
        utf8 *parent = Util_parentDirectory(root);
        utf8 *ancestor = Territory_find(parent);
        free(parent);
        if (ancestor != nullptr) {
            THROW("%s is already inside the territory at %s; regenerate there, or claim outside it",
                  root, ancestor);
            free(ancestor);
            free(marker);
            free(root);
            return false;
        }
        if (Util_createDirectory(marker, 0755) != 0) {
            THROW("cannot create %s: %s", marker, strerror(errno));
            free(marker);
            free(root);
            return false;
        }
    }
    TerritoryWalk walk = { nullptr, 0, 0, nullptr, 0, 0, nullptr, 0, 0, 0, 0, 0 };
    bool ok = inventory(root, &walk);
    utf8 *workspace = Territory_workspacePath(root);
    if (ok)
        ok = emit(workspace, root, &walk);
    if (ok)
        print("b.sh: claimed ", root, "\n   ", walk.files, " files in ", walk.treeCount, " directories, ", walk.sources, " sources, ", walk.excluded, " excluded\n   ", workspace);
    releaseWalk(&walk);
    free(workspace);
    free(marker);
    free(root);
    return ok;
}

/**
 * Build every unit at or below the directory (default: the current directory).
 * Units are built in walk order; a mixed unit fails without stopping the others.
 */
bool Territory_buildAll(string startDirectory) {
    string start = startDirectory != nullptr ? startDirectory : ".";
    utf8 *here = realpath(start, nullptr);
    if (here == nullptr) {
        THROW("cannot resolve directory: %s", start);
        return false;
    }
    normalize(here);
    utf8 *root = Territory_find(here);
    if (root == nullptr) {
        THROW("build outside a territory; run b.sh generate first");
        free(here);
        return false;
    }
    utf8 *scope = relative(root, here);
    TerritoryWalk walk = { nullptr, 0, 0, nullptr, 0, 0, nullptr, 0, 0, 0, 0, 0 };
    bool ok = inventory(root, &walk);
    bytes scoped = 0, succeeded = 0, failed = 0, skipped = 0;
    for (bytes index = 0; ok && index < walk.unitCount; ++index) {
        TerritoryUnit *unit = &walk.units[index];
        string unitDir = TerritoryUnit_directory(unit);
        if (!inScope(unitDir, scope))
            continue;
        ++scoped;
        const Adapter *adapter = nullptr;
        if (classify(unit, &adapter) != UNIT_OK || adapter == nullptr) {
            THROW("unit %s mixes adapters; move its sources into separate directories", unitDir);
            ++failed;
            continue;
        }
        utf8 *absolute = absoluteOf(root, unitDir);
        utf8 *entry = entryOf(adapter, absolute);
        bool app = entry != nullptr;
        free(entry);
        if (!app && Adapter_archive(adapter) == nullptr) {
            print("  skip ", absolute, " (library: adapter ", Adapter_name(adapter), " has no archive step)");
            ++skipped;
            free(absolute);
            continue;
        }
        utf8 *output = nullptr;
        i32 status = app ? Adapter_build(adapter)(absolute, &output)
                         : Adapter_archive(adapter)(absolute, &output);
        if (status == 0)
            ++succeeded;
        else
            ++failed;
        if (status == 0 && output != nullptr)
            print("  ", status == 0 ? "ok  " : "FAIL", " ", absolute, " (", app ? "app" : "library", ", ", Adapter_name(adapter), ") -> ", output);
        else
            print("  ", status == 0 ? "ok  " : "FAIL", " ", absolute, " (", app ? "app" : "library", ", ", Adapter_name(adapter), ")");

        free(output);
        free(absolute);
    }
    if (ok && scoped == 0)
        print("b.sh build: no units under ", scope);
    else if (ok)
        print("b.sh build: ", succeeded, " succeeded, ", failed, " failed, ", skipped, " libraries skipped");
    releaseWalk(&walk);
    free(scope);
    free(root);
    free(here);
    return ok && failed == 0;
}

/** Title-case a kebab or snake name for display: "my-app" becomes "My App". */
static utf8 *displayNameOf(string name) {
    utf8 *text = Util_combine(name, "");
    bool start = true;
    for (utf8 *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == '-' || *cursor == '_') {
            *cursor = ' ';
            start = true;
            continue;
        }
        if (start && *cursor >= 'a' && *cursor <= 'z')
            *cursor = (utf8) (*cursor - 'a' + 'A');
        start = *cursor == ' ';
    }
    return text;
}

/** Release a directory listing that a writer kept alive while reading its sources. */
static fn freeEntries(struct dirent **entries, i32 total) {
    for (i32 index = 0; index < total; ++index)
        free(entries[index]);
    free(entries);
}

/** Emit a quoted key followed by its colon separator; the value follows the caller. */
static fn keyText(FILE *file, string key) {
    escape(file, key);
    fputs(": ", file);
}

/**
 * Write one unit's assembly declaration. Authored once: an existing file is refused
 * unless force is set. The version may be empty; only box refuses an empty one.
 */
bool Territory_writeAssembly(string directory, string version, string const *includes, bytes includeCount, string const *dependencies, bytes dependencyCount, bool force) {
    utf8 *here = realpath(directory, nullptr);
    if (here == nullptr) {
        THROW("cannot resolve directory: %s", directory);
        return false;
    }
    normalize(here);
    utf8 *root = Territory_find(here);
    if (root == nullptr) {
        THROW("assembly outside a territory; run b.sh generate first");
        free(here);
        return false;
    }
    free(root);
    utf8 *path = Util_combine(here, "/b.sh.assembly.json");
    struct stat information;
    if (stat(path, &information) == 0 && !force) {
        THROW("%s already exists; assemblies are authored once (pass --force to replace)", path);
        free(path);
        free(here);
        return false;
    }
    struct dirent **entries = nullptr;
    i32 total = scandir(here, &entries, nullptr, alphasort);
    if (total < 0) {
        THROW("cannot read %s: %s", here, strerror(errno));
        free(path);
        free(here);
        return false;
    }
    const Adapter *adapter = nullptr;
    bool mixed = false;
    for (i32 index = 0; index < total; ++index) {
        const Adapter *claimed = Adapter_forFile(dirent_d_name(entries[index]));
        if (claimed == nullptr)
            continue;
        if (adapter == nullptr)
            adapter = claimed;
        else if (Adapter_build(adapter) != Adapter_build(claimed))
            mixed = true;
    }
    if (mixed) {
        THROW("%s mixes adapters; move its sources into separate directories", here);
        freeEntries(entries, total);
        free(path);
        free(here);
        return false;
    }
    if (adapter == nullptr) {
        THROW("no buildable sources in %s", here);
        freeEntries(entries, total);
        free(path);
        free(here);
        return false;
    }
    utf8 *entry = entryOf(adapter, here);
    bool app = entry != nullptr;
    string entryName = "";
    if (entry != nullptr) {
        string entrySlash = strrchr(entry, '/');
        entryName = entrySlash != nullptr ? entrySlash + 1 : entry;
    }
    string slash = strrchr(here, '/');
    string name = slash != nullptr ? slash + 1 : here;
    string user = getenv("USER");
    string publisher = user != nullptr && *user != '\0' ? user : "Unknown";
    string declared = version != nullptr ? version : "";
    utf8 *prefix = Util_combine("local.", publisher);
    utf8 *dotted = Util_combine(prefix, ".");
    utf8 *identifier = Util_combine(dotted, name);
    utf8 *display = displayNameOf(name);
    FILE *file = fopen(path, "wb");
    if (file == nullptr) {
        THROW("cannot write %s: %s", path, strerror(errno));
        free(display); free(identifier); free(dotted); free(prefix); free(path); free(here);
        freeEntries(entries, total);
        free(entry);
        return false;
    }
    fputs("{\n  ", file);
    keyText(file, "application"); fputs("{\n    ", file);
    keyText(file, "id"); escape(file, identifier); fputs(",\n    ", file);
    keyText(file, "name"); escape(file, name); fputs(",\n    ", file);
    keyText(file, "displayName"); escape(file, display); fputs(",\n    ", file);
    keyText(file, "version"); escape(file, declared); fputs(",\n    ", file);
    keyText(file, "type"); escape(file, app ? "cli" : "library"); fputs(",\n    ", file);
    keyText(file, "authors"); fputs("[", file); escape(file, publisher); fputs("],\n    ", file);
    keyText(file, "publisher"); escape(file, publisher); fputs(",\n    ", file);
    keyText(file, "description"); escape(file, ""); fputs(",\n    ", file);
    keyText(file, "license"); escape(file, "UNLICENSED"); fputs(",\n    ", file);
    keyText(file, "homepage"); escape(file, ""); fputs(",\n    ", file);
    keyText(file, "icon"); escape(file, ""); fputs("\n  },\n  ", file);
    keyText(file, "build"); fputs("{\n    ", file);
    keyText(file, "entry"); escape(file, entryName); fputs(",\n    ", file);
    keyText(file, "sources"); fputs("[", file);
    bool firstSource = true;
    for (i32 index = 0; index < total; ++index) {
        string entryName = dirent_d_name(entries[index]);
        const Adapter *claimed = Adapter_forFile(entryName);
        if (claimed == nullptr || Adapter_build(claimed) != Adapter_build(adapter))
            continue;
        if (!firstSource)
            fputs(", ", file);
        firstSource = false;
        escape(file, entryName);
    }
    fputs("],\n    ", file);
    keyText(file, "includes"); fputs("[", file);
    for (bytes index = 0; index < includeCount; ++index) {
        if (index != 0)
            fputs(", ", file);
        escape(file, includes[index]);
    }
    fputs("],\n    ", file);
    keyText(file, "dependencies"); fputs("[", file);
    for (bytes index = 0; index < dependencyCount; ++index) {
        if (index != 0)
            fputs(", ", file);
        escape(file, dependencies[index]);
    }
    fputs("],\n    ", file);
    keyText(file, "defines"); fputs("[],\n    ", file);
    keyText(file, "options"); fputs("[],\n    ", file);
    keyText(file, "link"); escape(file, app ? "executable" : "static"); fputs("\n  },\n  ", file);
    keyText(file, "payload"); fputs("{ ", file); keyText(file, "files"); fputs("[] },\n  ", file);
    keyText(file, "targets"); fputs("{},\n  ", file);
    keyText(file, "formats"); fputs("[", file); escape(file, "portable"); fputs("],\n  ", file);
    keyText(file, "statePolicy"); escape(file, "clean"); fputs("\n}\n", file);
    bool ok = fflush(file) == 0 && ferror(file) == 0;
    if (fclose(file) != 0)
        ok = false;
    if (ok)
        print("b.sh: assembly ", path, "\n   ", app ? "cli" : "library", ", version ", declared[0] != '\0' ? declared : "(undeclared: box refuses until set)");
    else
        THROW("cannot complete %s", path);
    free(display); free(identifier); free(dotted); free(prefix); free(path); free(here);
    freeEntries(entries, total);
    free(entry);
    return ok;
}


