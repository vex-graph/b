// CLASS: Territory — implementation of the marker, its walk and its inventory.
// DEFINITION: find resolves upward to the nearest .b/; workspacePath names the
// generated file; claim creates the marker and writes what it found.
// OVERVIEW: find; workspacePath; claim; PRIVATE: grow; excluded; normalize;
// unitFor; addSource; relative; escape; inventory; emit.
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "territory.h"
#include "adapter.h"
#include "../util/b.h"

;;DEFINITION
/* The walk is cold and total: every directory under the claimed root is read
 * once, in alphasort order, and units are recorded as they are found. A
 * directory owns the sources sitting directly in it, so no unit ever merges a
 * subdirectory upward. Generated, cache and control names are excluded; the
 * walk never leaves the claimed root and never follows a symlink out of it.
 * The inventory is disposable: regenerating replaces it, and no consumer reads
 * it back as intent.
 */
;;OVERVIEW
/* PRIVATE RECORDS (co-located, one inventory):
 * TerritoryUnit — char *directory (relative, owned), char **names (owned),
 *     count/capacity of the directory's compiled sources.
 * TerritoryWalk — directory stack plus unit array, source and exclusion counts.
 * PUBLIC (added): Territory_buildAll builds every unit at or below a directory,
 * scoped by the caller's location; Territory_writeAssembly authors one unit's
 * b.assembly.json once, with its include roots.
 * PRIVATE (added): absoluteOf joins a unit onto the root; inScope is the scope
 * test; classify picks one adapter family per unit; entryOf returns the owned
 * entry-source path; displayNameOf title-cases; freeEntries releases a listing;
 * keyText writes a quoted key; releaseWalk releases an inventory walk.
 * PRIVATE STATIC: grow — checked doubling; excluded — fixed skip names;
 * normalize — strip one trailing separator; unitFor/addSource — record a source
 * under its directory; relative — root-relative unit name; escape — JSON string
 * emission; inventory — the cold walk; emit — write the file.
 * Errors report through THROW and abort the claim; the caller owns returned
 * paths, and every slot is released before the claim returns.
 */

#define TERRITORY_INITIAL_CAPACITY 8u

typedef struct TerritoryUnit {
    char *directory;
    char **names;
    size_t count;
    size_t capacity;
} TerritoryUnit;

typedef struct TerritoryWalk {
    char **directories;
    size_t directoryCount;
    size_t directoryCapacity;
    TerritoryUnit *units;
    size_t unitCount;
    size_t unitCapacity;
    size_t sources;
    size_t excluded;
} TerritoryWalk;

// Grow a cold slot array with overflow checks, preserving old storage on error.
static bool grow(size_t count, size_t size, size_t *capacity, void **storage) {
    if (count < *capacity)
        return true;
    size_t next = *capacity == 0 ? TERRITORY_INITIAL_CAPACITY : *capacity;
    if (next > SIZE_MAX / 2 || next * 2 > SIZE_MAX / size) {
        THROW("territory inventory overflow");
        return false;
    }
    next *= 2;
    void *replacement = realloc(*storage, next * size);
    if (replacement == nullptr) {
        THROW("territory inventory allocation failed");
        return false;
    }
    *storage = replacement;
    *capacity = next;
    return true;
}

// Fixed generated, cache and control names; ordinary dot directories remain.
static bool excluded(const char *name) {
    const char *names[] = { ".", "..", ".git", ".hg", ".svn", ".b", "node_modules",
        "target", "build", "dist", "out", ".cache", "__pycache__", ".venv", "venv" };
    for (size_t index = 0; index < sizeof names / sizeof names[0]; ++index)
        if (strcmp(name, names[index]) == 0)
            return true;
    return false;
}

// Drop trailing separators while keeping a root ("/" or "C:/") intact.
static void normalize(char *path) {
    size_t length = strlen(path);
    while (length > 1 && (path[length - 1] == '/' || path[length - 1] == '\\')) {
        if (length == 3 && path[1] == ':')
            break;
        path[--length] = '\0';
    }
}

// Return the unit owning one directory, appending a new unit when absent.
static TerritoryUnit *unitFor(TerritoryWalk *walk, const char *directory) {
    size_t count = (*walk).unitCount;
    TerritoryUnit *units = (*walk).units;
    for (size_t index = 0; index < count; ++index)
        if (strcmp(units[index].directory, directory) == 0)
            return &units[index];
    void *storage = units;
    if (!grow(count, sizeof(TerritoryUnit), &(*walk).unitCapacity, &storage))
        return nullptr;
    (*walk).units = storage;
    units = (*walk).units;
    TerritoryUnit *unit = &units[count];
    (*walk).unitCount = count + 1;
    (*unit).directory = Util_combine(directory, "");
    (*unit).names = nullptr;
    (*unit).count = 0;
    (*unit).capacity = 0;
    return unit;
}

// Record one compiled source under the directory that owns it.
static bool addSource(TerritoryWalk *walk, const char *directory, const char *name) {
    TerritoryUnit *unit = unitFor(walk, directory);
    if (unit == nullptr)
        return false;
    size_t count = (*unit).count;
    void *storage = (*unit).names;
    if (!grow(count, sizeof(char*), &(*unit).capacity, &storage))
        return false;
    (*unit).names = storage;
    (*unit).names[count] = Util_combine(name, "");
    (*unit).count = count + 1;
    (*walk).sources = (*walk).sources + 1;
    return true;
}

// Name a directory relative to the territory root; the root itself is ".".
static char *relative(const char *root, const char *directory) {
    size_t length = strlen(root);
    if (strcmp(root, directory) == 0)
        return Util_combine(".", "");
    if (strncmp(directory, root, length) == 0 && directory[length] == '/')
        return Util_combine(directory + length + 1, "");
    return Util_combine(directory, "");
}

// Emit one JSON string, escaping what cannot appear literally in a path.
static void escape(FILE *file, const char *text) {
    fputc('"', file);
    for (const unsigned char *cursor = (const unsigned char*) text; *cursor != '\0'; ++cursor) {
        if (*cursor == '"' || *cursor == '\\') {
            fputc('\\', file);
            fputc(*cursor, file);
        } else if (*cursor < ' ') {
            fprintf(file, "\\u%04x", *cursor);
        } else {
            fputc(*cursor, file);
        }
    }
    fputc('"', file);
}

// Read every directory under the root once and record each unit it finds.
static bool inventory(const char *root, TerritoryWalk *walk) {
    void *storage = (*walk).directories;
    if (!grow((*walk).directoryCount, sizeof(char*), &(*walk).directoryCapacity, &storage))
        return false;
    (*walk).directories = storage;
    (*walk).directories[0] = Util_combine(root, "");
    (*walk).directoryCount = 1;
    for (size_t index = 0; index < (*walk).directoryCount; ++index) {
        char **directories = (*walk).directories;
        const char *current = directories[index];
        struct dirent **entries = nullptr;
        int total = scandir(current, &entries, nullptr, alphasort);
        if (total < 0) {
            THROW("cannot read %s: %s", current, strerror(errno));
            return false;
        }
        bool ok = true;
        for (int entryIndex = 0; entryIndex < total && ok; ++entryIndex) {
            struct dirent *entry = entries[entryIndex];
            const char *name = (*entry).d_name;
            if (excluded(name)) {
                if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
                    (*walk).excluded = (*walk).excluded + 1;
                continue;
            }
            char *prefix = Util_combine(current, "/");
            char *path = Util_combine(prefix, name);
            free(prefix);
            struct stat information;
            if (lstat(path, &information) != 0) {
                THROW("cannot inspect %s: %s", path, strerror(errno));
                free(path);
                ok = false;
                break;
            }
            if (S_ISDIR(information.st_mode)) {
                size_t count = (*walk).directoryCount;
                void *next = (*walk).directories;
                ok = grow(count, sizeof(char*), &(*walk).directoryCapacity, &next);
                (*walk).directories = next;
                if (ok) {
                    char **grown = (*walk).directories;
                    grown[count] = path;
                    (*walk).directoryCount = count + 1;
                } else {
                    free(path);
                }
            } else if (S_ISREG(information.st_mode)) {
                if (Adapter_forFile(path) != nullptr) {
                    char *directory = relative(root, current);
                    ok = addSource(walk, directory, name);
                    free(directory);
                }
                free(path);
            } else {
                (*walk).excluded = (*walk).excluded + 1;
                free(path);
            }
        }
        for (int entryIndex = 0; entryIndex < total; ++entryIndex)
            free(entries[entryIndex]);
        free(entries);
        if (!ok)
            return false;
    }
    return true;
}

typedef enum { UNIT_OK, UNIT_MIXED } UnitVerdict;

// Join a unit's relative directory onto the root; "." names the root itself.
static char *absoluteOf(const char *root, const char *directory) {
    if (strcmp(directory, ".") == 0)
        return Util_combine(root, "");
    char *prefix = Util_combine(root, "/");
    char *path = Util_combine(prefix, directory);
    free(prefix);
    return path;
}

// Whether a unit lies at or below the scope directory; scope "." means everything.
static bool inScope(const char *directory, const char *scope) {
    if (strcmp(scope, ".") == 0)
        return true;
    size_t length = strlen(scope);
    return strcmp(directory, scope) == 0 ||
        (strncmp(directory, scope, length) == 0 && directory[length] == '/');
}

// Choose the adapter that claims a unit's sources. Adapters that share a build
// callback form one family (glsl stages, the js/ts extensions), so only sources
// from different families make a unit mixed.
static UnitVerdict classify(const TerritoryUnit *unit, const Adapter **adapter) {
    *adapter = nullptr;
    for (size_t source = 0; source < (*unit).count; ++source) {
        const Adapter *claimed = Adapter_forFile((*unit).names[source]);
        if (claimed == nullptr)
            continue;
        if (*adapter == nullptr)
            *adapter = claimed;
        else if ((**adapter).build != (*claimed).build)
            return UNIT_MIXED;
    }
    return UNIT_OK;
}

// Ask the unit's adapter for the owned path of its entry source; nullptr means none.
static char *entryOf(const Adapter *adapter, const char *absolute) {
    return (*adapter).entry != nullptr ? (*adapter).entry(absolute) : nullptr;
}

// Write the generated fact file; the only writer of .b/workspace.json.
static bool emit(const char *path, const char *root, TerritoryWalk *walk) {
    FILE *file = fopen(path, "wb");
    if (file == nullptr) {
        THROW("cannot write %s: %s", path, strerror(errno));
        return false;
    }
    size_t unitCount = (*walk).unitCount;
    TerritoryUnit *units = (*walk).units;
    fprintf(file, "{\n  \"root\": ");
    escape(file, root);
    fprintf(file, ",\n  \"units\": [");
    for (size_t index = 0; index < unitCount; ++index) {
        TerritoryUnit *unit = &units[index];
        fprintf(file, "%s\n    { \"dir\": ", index == 0 ? "" : ",");
        escape(file, (*unit).directory);
        const Adapter *adapter = nullptr;
        const char *kind = "ambiguous";
        if (classify(unit, &adapter) == UNIT_OK && adapter != nullptr) {
            char *absolute = absoluteOf(root, (*unit).directory);
            char *entry = entryOf(adapter, absolute);
            kind = entry != nullptr ? "app" : "library";
            free(entry);
            free(absolute);
        }
        fprintf(file, ", \"kind\": \"%s\"", kind);
        fprintf(file, ", \"sources\": [");
        for (size_t source = 0; source < (*unit).count; ++source) {
            if (source != 0)
                fputs(", ", file);
            escape(file, (*unit).names[source]);
        }
        fputs("] }", file);
    }
    fprintf(file, "%s]\n}\n", unitCount == 0 ? "" : "\n  ");
    bool ok = fflush(file) == 0 && ferror(file) == 0;
    if (fclose(file) != 0)
        ok = false;
    if (!ok)
        THROW("cannot complete %s", path);
    return ok;
}

// Resolve upward to the nearest claimed ancestor, or nullptr when none exists.
char *Territory_find(const char *startDirectory) {
    if (startDirectory == nullptr || *startDirectory == '\0') {
        THROW("territory lookup requires a directory");
        return nullptr;
    }
    char *current = realpath(startDirectory, nullptr);
    if (current == nullptr) {
        THROW("cannot resolve directory: %s", startDirectory);
        return nullptr;
    }
    normalize(current);
    while (true) {
        char *marker = Util_combine(current, "/.b");
        struct stat information;
        bool present = stat(marker, &information) == 0 && S_ISDIR(information.st_mode);
        free(marker);
        if (present)
            return current;
        char *parent = Util_parentDirectory(current);
        normalize(parent);
        if (strcmp(parent, current) == 0) {
            free(parent);
            free(current);
            return nullptr;
        }
        free(current);
        current = parent;
    }
}

// Name the generated file inside a claimed root.
char *Territory_workspacePath(const char *root) {
    return Util_combine(root, "/.b/workspace.json");
}

// Claim a directory: create the marker, walk it once, write the inventory.
bool Territory_claim(const char *directory) {
    if (directory == nullptr || *directory == '\0') {
        THROW("generate requires a directory");
        return false;
    }
    char *root = realpath(directory, nullptr);
    if (root == nullptr) {
        THROW("cannot resolve directory: %s", directory);
        return false;
    }
    normalize(root);
    if (strcmp(root, "/") == 0) {
        THROW("refusing to claim the filesystem root");
        free(root);
        return false;
    }
    char *marker = Util_combine(root, "/.b");
    struct stat information;
    bool claimed = stat(marker, &information) == 0;
    if (claimed && !S_ISDIR(information.st_mode)) {
        THROW(".b exists and is not a directory: %s", marker);
        free(marker);
        free(root);
        return false;
    }
    if (!claimed) {
        char *parent = Util_parentDirectory(root);
        char *ancestor = Territory_find(parent);
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
    TerritoryWalk walk = { nullptr, 0, 0, nullptr, 0, 0, 0, 0 };
    bool ok = inventory(root, &walk);
    char *workspace = Territory_workspacePath(root);
    if (ok)
        ok = emit(workspace, root, &walk);
    if (ok)
        printf("b: claimed %s\n   %zu units, %zu sources, %zu excluded\n   %s\n",
               root, walk.unitCount, walk.sources, walk.excluded, workspace);
    for (size_t index = 0; index < walk.unitCount; ++index) {
        TerritoryUnit *unit = &walk.units[index];
        for (size_t source = 0; source < (*unit).count; ++source)
            free((*unit).names[source]);
        free((*unit).names);
        free((*unit).directory);
    }
    for (size_t index = 0; index < walk.directoryCount; ++index)
        free(walk.directories[index]);
    free(walk.units);
    free(walk.directories);
    free(workspace);
    free(marker);
    free(root);
    return ok;
}

// Release every slot of a walk; the caller owns the walk until this runs.
static void releaseWalk(TerritoryWalk *walk) {
    for (size_t index = 0; index < (*walk).unitCount; ++index) {
        TerritoryUnit *unit = &(*walk).units[index];
        for (size_t source = 0; source < (*unit).count; ++source)
            free((*unit).names[source]);
        free((*unit).names);
        free((*unit).directory);
    }
    for (size_t index = 0; index < (*walk).directoryCount; ++index)
        free((*walk).directories[index]);
    free((*walk).units);
    free((*walk).directories);
}

// Build every unit at or below the directory (default: the current directory).
// Units are built in walk order; a mixed unit fails without stopping the others.
bool Territory_buildAll(const char *startDirectory) {
    const char *start = startDirectory != nullptr ? startDirectory : ".";
    char *here = realpath(start, nullptr);
    if (here == nullptr) {
        THROW("cannot resolve directory: %s", start);
        return false;
    }
    normalize(here);
    char *root = Territory_find(here);
    if (root == nullptr) {
        THROW("build outside a territory; run b generate first");
        free(here);
        return false;
    }
    char *scope = relative(root, here);
    TerritoryWalk walk = { nullptr, 0, 0, nullptr, 0, 0, 0, 0 };
    bool ok = inventory(root, &walk);
    size_t scoped = 0, succeeded = 0, failed = 0, skipped = 0;
    for (size_t index = 0; ok && index < walk.unitCount; ++index) {
        TerritoryUnit *unit = &walk.units[index];
        if (!inScope((*unit).directory, scope))
            continue;
        ++scoped;
        const Adapter *adapter = nullptr;
        if (classify(unit, &adapter) != UNIT_OK || adapter == nullptr) {
            THROW("unit %s mixes adapters; move its sources into separate directories", (*unit).directory);
            ++failed;
            continue;
        }
        char *absolute = absoluteOf(root, (*unit).directory);
        char *entry = entryOf(adapter, absolute);
        bool app = entry != nullptr;
        free(entry);
        if (!app) {
            // Library artifacts are not built yet; reporting beats a failing link.
            printf("  skip %s (library: library builds are not implemented yet)\n", absolute);
            ++skipped;
            free(absolute);
            continue;
        }
        char *output = nullptr;
        int status = (*adapter).build(absolute, &output);
        if (status == 0)
            ++succeeded;
        else
            ++failed;
        printf("  %s %s (%s, %s)\n", status == 0 ? "ok  " : "FAIL", absolute,
               app ? "app" : "library", (*adapter).name);
        free(output);
        free(absolute);
    }
    if (ok && scoped == 0)
        printf("b build: no units under %s\n", scope);
    else if (ok)
        printf("b build: %zu succeeded, %zu failed, %zu libraries skipped\n", succeeded, failed, skipped);
    releaseWalk(&walk);
    free(scope);
    free(root);
    free(here);
    return ok && failed == 0;
}

// Title-case a kebab or snake name for display: "my-app" becomes "My App".
static char *displayNameOf(const char *name) {
    char *text = Util_combine(name, "");
    bool start = true;
    for (char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == '-' || *cursor == '_') {
            *cursor = ' ';
            start = true;
            continue;
        }
        if (start && *cursor >= 'a' && *cursor <= 'z')
            *cursor = (char) (*cursor - 'a' + 'A');
        start = *cursor == ' ';
    }
    return text;
}

// Release a directory listing that a writer kept alive while reading its sources.
static void freeEntries(struct dirent **entries, int total) {
    for (int index = 0; index < total; ++index)
        free(entries[index]);
    free(entries);
}

// Emit a quoted key followed by its colon separator; the value follows the caller.
static void keyText(FILE *file, const char *key) {
    escape(file, key);
    fputs(": ", file);
}

// Write one unit's assembly declaration. Authored once: an existing file is refused
// unless force is set. The version may be empty; only box refuses an empty one.
bool Territory_writeAssembly(const char *directory, const char *version, const char *const *includes, size_t includeCount, bool force) {
    char *here = realpath(directory, nullptr);
    if (here == nullptr) {
        THROW("cannot resolve directory: %s", directory);
        return false;
    }
    normalize(here);
    char *root = Territory_find(here);
    if (root == nullptr) {
        THROW("assembly outside a territory; run b generate first");
        free(here);
        return false;
    }
    free(root);
    char *path = Util_combine(here, "/b.assembly.json");
    struct stat information;
    if (stat(path, &information) == 0 && !force) {
        THROW("%s already exists; assemblies are authored once (pass --force to replace)", path);
        free(path);
        free(here);
        return false;
    }
    struct dirent **entries = nullptr;
    int total = scandir(here, &entries, nullptr, alphasort);
    if (total < 0) {
        THROW("cannot read %s: %s", here, strerror(errno));
        free(path);
        free(here);
        return false;
    }
    const Adapter *adapter = nullptr;
    bool mixed = false;
    for (int index = 0; index < total; ++index) {
        const Adapter *claimed = Adapter_forFile((*entries[index]).d_name);
        if (claimed == nullptr)
            continue;
        if (adapter == nullptr)
            adapter = claimed;
        else if ((*adapter).build != (*claimed).build)
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
    char *entry = entryOf(adapter, here);
    bool app = entry != nullptr;
    const char *entryName = "";
    if (entry != nullptr) {
        const char *entrySlash = strrchr(entry, '/');
        entryName = entrySlash != nullptr ? entrySlash + 1 : entry;
    }
    const char *slash = strrchr(here, '/');
    const char *name = slash != nullptr ? slash + 1 : here;
    const char *user = getenv("USER");
    const char *publisher = user != nullptr && *user != '\0' ? user : "Unknown";
    const char *declared = version != nullptr ? version : "";
    char *prefix = Util_combine("local.", publisher);
    char *dotted = Util_combine(prefix, ".");
    char *identifier = Util_combine(dotted, name);
    char *display = displayNameOf(name);
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
    for (int index = 0; index < total; ++index) {
        const Adapter *claimed = Adapter_forFile((*entries[index]).d_name);
        if (claimed == nullptr || (*claimed).build != (*adapter).build)
            continue;
        if (!firstSource)
            fputs(", ", file);
        firstSource = false;
        escape(file, (*entries[index]).d_name);
    }
    fputs("],\n    ", file);
    keyText(file, "includes"); fputs("[", file);
    for (size_t index = 0; index < includeCount; ++index) {
        if (index != 0)
            fputs(", ", file);
        escape(file, includes[index]);
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
        printf("b: assembly %s\n   %s, version %s\n", path, app ? "cli" : "library",
               declared[0] != '\0' ? declared : "(undeclared: box refuses until set)");
    else
        THROW("cannot complete %s", path);
    free(display); free(identifier); free(dotted); free(prefix); free(path); free(here);
    freeEntries(entries, total);
    free(entry);
    return ok;
}


