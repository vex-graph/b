// CLASS: Project — named roots and adapter inventory in b's external state directory.
// DEFINITION: Project records live under B_HOME/projects/<name>/; root.path maps
// a name to its canonical root and index.tsv inventories recognized files.
// OVERVIEW: ProjectPaths; validName; canonicalDirectory; projectDirectory;
// recordPath; ignoredDirectory; pushDirectory; indexableName; Project_register;
// Project_remove; Project_root; Project_refresh; Project_contains; Project_file.
#include "project/project.h"
#include "adapters/adapter.h"
#include "util/b.h"

;;DEFINITION
/* The project registry is user-local state, outside source trees. A project
 * name is a stable lookup key and root.path is its canonical directory. File
 * resolution is bounded to that root when --in is supplied; callers retain
 * ownership of returned paths.
 */
;;OVERVIEW
/* CLASS: Project. PRIVATE SLOTS: ProjectPaths.items — owned traversal stack;
 * ProjectPaths.count/capacity — live stack extent and allocation bound.
 * PUBLIC: Project_register — create/update a named root;
 * Project_remove — clear the saved registration and index;
 * Project_root — return its canonical root; Project_contains — path boundary;
 * Project_file — resolve a selected file inside the root;
 * Project_refresh — atomically replace the adapter-recognized file inventory.
 * PRIVATE STATIC: validName; canonicalDirectory; projectDirectory; recordPath;
 * ignoredDirectory; pushDirectory; indexableName; removeIfPresent.
 * RECORDS: B_HOME/projects/<name>/root.path; index.tsv — versioned adapter name
 * and project-relative path pairs. Names contain only ASCII letters, digits,
 * dash and underscore; roots are canonical absolute paths.
 */

#include <errno.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

typedef struct ProjectPaths {
    char **items;
    size_t count;
    size_t capacity;
} ProjectPaths;

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

// Admit a short stable name that cannot escape the registry; reserve remove for the CLI.
static bool validName(const char *name) {
    if (name == nullptr || *name == '\0' || strlen(name) > 80)
        return false;
    for (const unsigned char *cursor = (const unsigned char*) name; *cursor; ++cursor)
        if (!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '-' || *cursor == '_'))
            return false;
    return strcmp(name, "remove") != 0;
}

// Return a canonical existing directory or nullptr on invalid input.
static char *canonicalDirectory(const char *path) {
    if (path == nullptr || *path == '\0' || strchr(path, '\n') != nullptr || strchr(path, '\r') != nullptr)
        return nullptr;
#ifdef _WIN32
    char *canonicalPath = _fullpath(nullptr, path, 0);
#else
    char *canonicalPath = realpath(path, nullptr);
#endif
    if (canonicalPath == nullptr)
        return nullptr;
    struct stat information;
    if (stat(canonicalPath, &information) != 0 || !S_ISDIR(information.st_mode)) {
        free(canonicalPath);
        return nullptr;
    }
    return canonicalPath;
}

// Build the per-project directory beneath b's external state directory.
static char *projectDirectory(const char *stateDirectory, const char *name) {
    char *projects = Util_combine(stateDirectory, "/projects");
    char *directory = Util_combine(projects, "/");
    free(projects);
    char *namedDirectory = Util_combine(directory, name);
    free(directory);
    return namedDirectory;
}

// Build the saved canonical-root record path.
static char *recordPath(const char *stateDirectory, const char *name) {
    char *directory = projectDirectory(stateDirectory, name);
    char *path = Util_combine(directory, "/root.path");
    free(directory);
    return path;
}

// Exclude generated and dependency trees from the generic adapter inventory.
static bool ignoredDirectory(const char *name) {
    const char *ignored[] = { ".", "..", ".git", ".hg", ".svn", "node_modules",
        "target", "build", "dist", "out", ".cache", "__pycache__", ".venv", "venv" };
    for (size_t index = 0; index < sizeof ignored / sizeof ignored[0]; ++index)
        if (strcmp(name, ignored[index]) == 0)
            return true;
    return false;
}

// Push an owned directory path onto the iterative project walk.
static bool pushDirectory(ProjectPaths *directories, char *path) {
    if (directories->count == directories->capacity) {
        size_t capacity = directories->capacity == 0 ? 16 : directories->capacity * 2;
        if (capacity < directories->capacity || capacity > SIZE_MAX / sizeof(char*)) {
            THROW("project inventory overflow");
            free(path);
            return false;
        }
        char **replacement = realloc(directories->items, capacity * sizeof(char*));
        if (replacement == nullptr) {
            THROW("project inventory allocation failed");
            free(path);
            return false;
        }
        directories->items = replacement;
        directories->capacity = capacity;
    }
    directories->items[directories->count++] = path;
    return true;
}

// Reject names that cannot be represented as one tab-separated index field.
static bool indexableName(const char *name) {
    for (const unsigned char *cursor = (const unsigned char*) name; *cursor; ++cursor)
        if (*cursor < ' ' || *cursor == 127)
            return false;
    return true;
}

// Remove one known state file while leaving unfamiliar project data untouched.
static bool removeIfPresent(const char *path) {
    if (remove(path) == 0 || errno == ENOENT)
        return true;
    THROW("cannot remove project state file %s: %s", path, strerror(errno));
    return false;
}

bool Project_register(const char *stateDirectory, const char *name, const char *path) {
    if (stateDirectory == nullptr || *stateDirectory == '\0' || !validName(name)) {
        THROW("project registration needs a valid name and state directory");
        return false;
    }
    char *canonicalPath = canonicalDirectory(path);
    if (canonicalPath == nullptr) {
        THROW("project path must be an existing directory: %s", path != nullptr ? path : "(null)");
        return false;
    }
    char *directory = projectDirectory(stateDirectory, name);
    if (!Util_makeDirectory(directory)) {
        free(canonicalPath);
        free(directory);
        return false;
    }
    char *destination = recordPath(stateDirectory, name);
    char *temporary = Util_combine(destination, ".tmp");
    FILE *record = fopen(temporary, "wb");
    bool written = record != nullptr;
    if (written)
        written = fputs(canonicalPath, record) >= 0 && fputc('\n', record) != EOF;
    if (record != nullptr && fclose(record) != 0)
        written = false;
    if (written) {
#ifdef _WIN32
        written = MoveFileExA(temporary, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        written = rename(temporary, destination) == 0;
#endif
    }
    if (!written) {
        remove(temporary);
        THROW("cannot save project %s: %s", name, strerror(errno));
    }
    char *index = Util_combine(directory, "/index.tsv");
    remove(index);
    free(index);
    free(temporary);
    free(destination);
    free(directory);
    free(canonicalPath);
    return written;
}

// Remove a project registration and its index, never touching project sources.
bool Project_remove(const char *stateDirectory, const char *name) {
    if (stateDirectory == nullptr || *stateDirectory == '\0' || !validName(name)) {
        THROW("project removal needs a valid name and state directory");
        return false;
    }
    char *directory = projectDirectory(stateDirectory, name);
    char *rootPath = Util_combine(directory, "/root.path");
    FILE *record = fopen(rootPath, "rb");
    if (record == nullptr) {
        THROW("unknown project: %s", name);
        free(rootPath);
        free(directory);
        return false;
    }
    fclose(record);
    char *rootTemporary = Util_combine(rootPath, ".tmp");
    char *indexPath = Util_combine(directory, "/index.tsv");
    char *indexTemporary = Util_combine(indexPath, ".tmp");
    bool ok = true;
    const char *files[] = { rootTemporary, indexTemporary, indexPath, rootPath };
    for (size_t index = 0; index < sizeof files / sizeof files[0]; ++index)
        if (!removeIfPresent(files[index]))
            ok = false;
    if (ok) {
#ifdef _WIN32
        int removed = _rmdir(directory);
#else
        int removed = rmdir(directory);
#endif
        if (removed != 0 && errno != ENOENT) {
            THROW("project state directory contains unrecognized data: %s", directory);
            ok = false;
        }
    }
    free(rootPath);
    free(rootTemporary);
    free(indexPath);
    free(indexTemporary);
    free(directory);
    return ok;
}

char *Project_root(const char *stateDirectory, const char *name) {
    if (stateDirectory == nullptr || *stateDirectory == '\0' || !validName(name)) {
        THROW("project lookup needs a valid name");
        return nullptr;
    }
    char *path = recordPath(stateDirectory, name);
    FILE *record = fopen(path, "rb");
    free(path);
    if (record == nullptr) {
        THROW("unknown project: %s", name);
        return nullptr;
    }
    char savedPath[8192];
    bool valid = fgets(savedPath, sizeof savedPath, record) != nullptr;
    int trailing = valid ? fgetc(record) : EOF;
    if (fclose(record) != 0)
        valid = false;
    if (!valid || (trailing != EOF && trailing != '\n') || strchr(savedPath, '\n') == nullptr) {
        THROW("project record is malformed: %s", name);
        return nullptr;
    }
    savedPath[strcspn(savedPath, "\r\n")] = '\0';
    char *canonicalPath = canonicalDirectory(savedPath);
    if (canonicalPath == nullptr)
        THROW("registered project path is unavailable: %s", name);
    return canonicalPath;
}

// Rebuild a language-neutral file inventory without invoking build tools.
char *Project_refresh(const char *stateDirectory, const char *name, size_t *fileCount) {
    if (fileCount != nullptr)
        *fileCount = 0;
    char *root = Project_root(stateDirectory, name);
    if (root == nullptr)
        return nullptr;
    char *stateRoot = realpath(stateDirectory, nullptr);
    char *directory = projectDirectory(stateDirectory, name);
    if (!Util_makeDirectory(directory)) {
        free(root);
        free(stateRoot);
        free(directory);
        return nullptr;
    }
    char *indexPath = Util_combine(directory, "/index.tsv");
    char *temporary = Util_combine(indexPath, ".tmp");
    FILE *index = fopen(temporary, "wb");
    if (index == nullptr) {
        THROW("cannot create project index for %s: %s", name, strerror(errno));
        free(root);
        free(stateRoot);
        free(directory);
        free(indexPath);
        free(temporary);
        return nullptr;
    }

    bool ok = fputs("b-project-index\t1\n", index) >= 0;
    size_t indexed = 0;
    ProjectPaths directories = {0};
    if (ok)
        ok = pushDirectory(&directories, Util_combine(root, ""));
    while (ok && directories.count > 0) {
        char *current = directories.items[--directories.count];
        struct dirent **entries = nullptr;
        int total = scandir(current, &entries, nullptr, alphasort);
        if (total < 0) {
            THROW("cannot refresh project directory %s: %s", current, strerror(errno));
            ok = false;
        }
        for (int entryIndex = 0; ok && entryIndex < total; ++entryIndex) {
            struct dirent *entry = entries[entryIndex];
            const char *entryName = (*entry).d_name;
            if (!indexableName(entryName)) {
                THROW("project path contains control characters");
                ok = false;
                break;
            }
            if (ignoredDirectory(entryName))
                continue;
            size_t currentLength = strlen(current);
            const char *separator = currentLength > 0 &&
                (current[currentLength - 1] == '/' || current[currentLength - 1] == '\\') ? "" : "/";
            char *prefix = Util_combine(current, separator);
            char *path = Util_combine(prefix, entryName);
            free(prefix);
            struct stat information;
            if (lstat(path, &information) != 0) {
                THROW("cannot inspect project path %s: %s", path, strerror(errno));
                free(path);
                ok = false;
                break;
            }
            if (S_ISDIR(information.st_mode)) {
                bool withinState = stateRoot != nullptr && Project_contains(root, stateRoot) &&
                    Project_contains(stateRoot, path);
                if (withinState && strcmp(stateRoot, root) == 0) {
                    char *projects = Util_combine(stateRoot, "/projects");
                    withinState = Project_contains(projects, path);
                    free(projects);
                }
                if (!withinState)
                    ok = pushDirectory(&directories, path);
                else
                    free(path);
            } else if (S_ISREG(information.st_mode)) {
                const Adapter *adapter = Adapter_forFile(path);
                if (adapter != nullptr) {
                    const char *relative = path + strlen(root);
                    while (*relative == '/' || *relative == '\\')
                        ++relative;
                    if (fprintf(index, "%s\t%s\n", (*adapter).name, relative) < 0) {
                        THROW("cannot write project index for %s", name);
                        ok = false;
                    } else
                        ++indexed;
                }
                free(path);
            } else
                free(path); // Symlinks and special files are never followed or indexed.
        }
        for (int entryIndex = 0; entryIndex < total; ++entryIndex)
            free(entries[entryIndex]);
        free(entries);
        free(current);
    }
    for (size_t index = 0; index < directories.count; ++index)
        free(directories.items[index]);
    free(directories.items);
    if (fclose(index) != 0)
        ok = false;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(temporary, indexPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = rename(temporary, indexPath) == 0;
#endif
    }
    if (!ok) {
        remove(temporary);
        THROW("cannot publish project index for %s: %s", name, strerror(errno));
        free(indexPath);
        indexPath = nullptr;
    } else if (fileCount != nullptr)
        *fileCount = indexed;
    free(temporary);
    free(directory);
    free(stateRoot);
    free(root);
    return indexPath;
}

bool Project_contains(const char *root, const char *path) {
    if (root == nullptr || path == nullptr)
        return false;
    size_t rootLength = strlen(root);
    if (rootLength == 0)
        return false;
    if (rootLength == 1 && (root[0] == '/' || root[0] == '\\'))
        return path[0] == '/' || path[0] == '\\';
#ifdef _WIN32
    if (_strnicmp(root, path, rootLength) != 0)
        return false;
#else
    if (strncmp(root, path, rootLength) != 0)
        return false;
#endif
    if (rootLength == 3 && root[1] == ':' && (root[2] == '/' || root[2] == '\\'))
        return true;
    if (path[rootLength] == '\0')
        return true;
    if (rootLength == 1 && (root[0] == '/' || root[0] == '\\'))
        return true;
    return path[rootLength] == '/' || path[rootLength] == '\\';
}

char *Project_file(const char *root, const char *path) {
    if (root == nullptr || path == nullptr || *path == '\0')
        return nullptr;
    bool drivePath = ((path[0] >= 'a' && path[0] <= 'z') ||
                      (path[0] >= 'A' && path[0] <= 'Z')) && path[1] == ':';
    bool absolute = path[0] == '/' || path[0] == '\\' || drivePath;
    char *candidate = nullptr;
    if (absolute)
        candidate = Util_combine(path, "");
    else {
        char *prefix = Util_combine(root, "/");
        candidate = Util_combine(prefix, path);
        free(prefix);
    }
#ifdef _WIN32
    char *canonicalPath = _fullpath(nullptr, candidate, 0);
#else
    char *canonicalPath = realpath(candidate, nullptr);
#endif
    free(candidate);
    if (canonicalPath == nullptr) {
        THROW("project file does not exist: %s", path);
        return nullptr;
    }
    struct stat information;
    if (stat(canonicalPath, &information) != 0 || !S_ISREG(information.st_mode) ||
        !Project_contains(root, canonicalPath)) {
        THROW("project file must be a regular file within the registered project: %s", path);
        free(canonicalPath);
        return nullptr;
    }
    return canonicalPath;
}
