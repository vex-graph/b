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

// Write the generated fact file; the only writer of .b/workspace.json.
static bool emit(const char *path, const char *root, TerritoryWalk *walk) {
    FILE *file = fopen(path, "wb");
    if (file == nullptr) {
        THROW("cannot write %s: %s", path, strerror(errno));
        return false;
    }
    size_t unitCount = (*walk).unitCount;
    TerritoryUnit *units = (*walk).units;
    fprintf(file, "{\n  \"schemaVersion\": 1,\n  \"root\": ");
    escape(file, root);
    fprintf(file, ",\n  \"units\": [");
    for (size_t index = 0; index < unitCount; ++index) {
        TerritoryUnit *unit = &units[index];
        fprintf(file, "%s\n    { \"dir\": ", index == 0 ? "" : ",");
        escape(file, (*unit).directory);
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
