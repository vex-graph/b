// MODULE: b.sh shared utilities. No owned class; argv and paths are borrowed.
// DEFINITION: One place spawns children (never a shell), builds the per-project
// output directory outside the source tree, and collects a directory's sources
// with glob metacharacters treated literally. The environment helper exists so
// an adapter can redirect a tool's cache (e.g. Python bytecode) out of source.
#include "b.h"
#include "territory.h"

#include "vex.h"

;;DEFINITION
/* Utility services keep adapters focused on their language contracts. Dynamic
 * allocations belong to the caller; allocation failure terminates this CLI.
 * POSIX children inherit the environment, run without a shell, and are reaped
 * synchronously. Build stdout is redirected to stderr so only the result path
 * appears on suite stdout. Output storage lives inside the territory, under the
 * marker, so the walk never sees it and the territory's ignore rule covers it;
 * source globs escape the directory portion literally.
 */
;;OVERVIEW
/* MODULE: shared utility implementation; public declarations: b.sh.h.
 * PUBLIC MEMORY/PATH: Util_allocate; Util_combine; Util_endsWith;
 * Util_makeDirectory; Util_outputDirectory (territory .b/obj/<unit>, else
 * $B_HOME); Util_parentDirectory;
 * Util_projectDirectory; Util_createDirectory; Util_setFileMode.
 * PUBLIC PROCESS: Util_executeWithEnvironment; Util_execute; Util_executeBuild;
 * Util_runCommand — append borrowed program arguments without shell parsing.
 * PUBLIC CHECKS: Util_checkSources — run a checker on each discovered source.
 * PUBLIC DISCOVERY: Util_collectSources; Util_freeSources.
 * PUBLIC ENVIRONMENT: Util_environmentWith; Util_freeEnvironment — allocate one
 * replacement entry, borrow inherited entries, free only owned storage.
 * PRIVATE STATIC: processEnvironment — access the inherited environment;
 * reap — wait for child and map exit/signal status.
 * BORROWED GLOBAL: char **environ — inherited process environment.
 */

#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <dirent.h>
#include <direct.h>
#include <windows.h>
#define snprintf _snprintf
#else
#include <glob.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>
#endif

#ifdef _WIN32
// Use the Microsoft CRT accessor macro rather than declaring POSIX environ.
static char **processEnvironment(void) {
    return environ;
}
#else
extern char **environ;
static char **processEnvironment(void) {
    return environ;
}
#endif

// Allocate zero-initialized storage; report and terminate the CLI if allocation fails.
void *Util_allocate(bytes size) {
    void *memory = calloc(1, size);
    if (memory == nullptr) {
        THROW("out of memory");
        exit(EXIT_FAILURE);
    }
    return memory;
}

// Release storage from Util_allocate; the single release seam for the suite.
// Relational Memory Law: one owner for allocation, no scattered free().
void Util_release(void *block) {
    free(block);
}

// Allocate and return the concatenation of two strings, rejecting size overflow.
char *Util_combine(const char *left, const char *right) {
    bytes a = strlen(left), b = strlen(right);
    if (a > SIZE_MAX - b - 1) {
        THROW("path size overflow");
        exit(EXIT_FAILURE);
    }
    char *text = Util_allocate(a + b + 1);
    memcpy(text, left, a);
    memcpy(text + a, right, b + 1);
    return text;
}

#ifdef _WIN32
// Resolve an existing path through the filesystem, including Windows reparse points.
char *Util_realPath(const char *path, char *resolved) {
    if (path == nullptr || *path == '\0') {
        errno = ENOENT;
        return nullptr;
    }
    char *absolute = _fullpath(nullptr, path, 0);
    if (absolute == nullptr)
        return nullptr;
    HANDLE handle = CreateFileA(absolute, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    free(absolute);
    if (handle == INVALID_HANDLE_VALUE) {
        errno = ENOENT;
        return nullptr;
    }
    DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    DWORD length = GetFinalPathNameByHandleA(handle, nullptr, 0, flags);
    if (length == 0) {
        CloseHandle(handle);
        errno = ENOENT;
        return nullptr;
    }
    char *canonical = Util_allocate((bytes) length + 1);
    DWORD written = GetFinalPathNameByHandleA(handle, canonical, length + 1, flags);
    CloseHandle(handle);
    if (written == 0 || written > length) {
        free(canonical);
        errno = ENOENT;
        return nullptr;
    }
    if (strncmp(canonical, "\\\\?\\UNC\\", 8) == 0) {
        memmove(canonical + 2, canonical + 8, strlen(canonical + 8) + 1);
        canonical[0] = '\\';
        canonical[1] = '\\';
    } else if (strncmp(canonical, "\\\\?\\", 4) == 0)
        memmove(canonical, canonical + 4, strlen(canonical + 4) + 1);
    if (resolved != nullptr) {
        strcpy(resolved, canonical);
        free(canonical);
        return resolved;
    }
    return canonical;
}

// Inspect a path without following a Windows reparse point.
int Util_lstat(const char *path, struct stat *information) {
    if (path == nullptr || information == nullptr) {
        errno = EINVAL;
        return -1;
    }
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        errno = ENOENT;
        return -1;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        if (stat(path, information) != 0)
            memset(information, 0, sizeof *information);
        (*information).st_mode = S_IFLNK;
        return 0;
    }
    return stat(path, information);
}

// Adapt scandir's comparator to qsort's standard callback signature.
static int compareEntryPointers(const void *left, const void *right) {
    const struct dirent *const *leftEntry = left;
    const struct dirent *const *rightEntry = right;
    return strcmp((*(*leftEntry)).d_name, (*(*rightEntry)).d_name);
}

// Enumerate directory entries into separately owned records in name order.
int Util_scandir(const char *directory, struct dirent ***entries,
                 int (*filter)(const struct dirent *),
                 int (*compare)(const struct dirent **, const struct dirent **)) {
    if (directory == nullptr || entries == nullptr) {
        errno = EINVAL;
        return -1;
    }
    *entries = nullptr;
    DIR *stream = opendir(directory);
    if (stream == nullptr)
        return -1;
    struct dirent **items = nullptr;
    bytes count = 0, capacity = 0;
    bool ok = true;
    errno = 0;
    struct dirent *entry;
    while (ok && (entry = readdir(stream)) != nullptr) {
        if (filter != nullptr && filter(entry) == 0)
            continue;
        if (count == (bytes) INT_MAX || count == SIZE_MAX / sizeof *items) {
            errno = EOVERFLOW;
            ok = false;
            break;
        }
        if (count == capacity) {
            bytes next = capacity == 0 ? 16 : capacity * 2;
            if (next < capacity || next > SIZE_MAX / sizeof *items) {
                errno = EOVERFLOW;
                ok = false;
                break;
            }
            struct dirent **replacement = realloc(items, next * sizeof *items);
            if (replacement == nullptr) {
                errno = ENOMEM;
                ok = false;
                break;
            }
            items = replacement;
            capacity = next;
        }
        items[count] = malloc(sizeof **items);
        if (items[count] == nullptr) {
            errno = ENOMEM;
            ok = false;
            break;
        }
        memcpy(items[count++], entry, sizeof **items);
    }
    if (entry == nullptr && errno != 0)
        ok = false;
    if (closedir(stream) != 0)
        ok = false;
    if (!ok) {
        for (bytes index = 0; index < count; ++index)
            free(items[index]);
        free(items);
        return -1;
    }
    if (count > 1 && compare == Util_alphasort)
        qsort(items, count, sizeof *items, compareEntryPointers);
    else if (count > 1 && compare != nullptr) {
        for (bytes index = 1; index < count; ++index) {
            struct dirent *entryToInsert = items[index];
            bytes position = index;
            while (position > 0 && compare((const struct dirent **) &items[position - 1],
                                           (const struct dirent **) &entryToInsert) > 0) {
                items[position] = items[position - 1];
                --position;
            }
            items[position] = entryToInsert;
        }
    }
    *entries = items;
    return (int) count;
}

// Compare directory names in byte order for deterministic inventory results.
int Util_alphasort(const struct dirent **left, const struct dirent **right) {
    return strcmp((*(*left)).d_name, (*(*right)).d_name);
}
#endif

// Create exactly one directory; unlike Util_makeDirectory, an existing path fails.
int Util_createDirectory(const char *path, int mode) {
#ifdef _WIN32
    (void) mode;
    return _mkdir(path);
#else
    return mkdir(path, (mode_t) mode);
#endif
}

// Set POSIX file mode bits where available; Windows has no executable mode bits.
int Util_setFileMode(int descriptor, int mode) {
#ifdef _WIN32
    (void) descriptor;
    (void) mode;
    return 0;
#else
    return fchmod(descriptor, (mode_t) mode);
#endif
}

// Report whether a readable regular file's bytes contain the needle.
// Whole-file read: adequate for source files, not for arbitrary large data.
bool Util_fileContains(const char *path, const char *needle) {
    FILE *input = fopen(path, "rb");
    if (input == nullptr)
        return false;
    bool found = false;
    if (fseek(input, 0, SEEK_END) == 0) {
        long size = ftell(input);
        if (size >= 0 && fseek(input, 0, SEEK_SET) == 0) {
            char *text = Util_allocate((bytes) size + 1);
            bytes bytes = fread(text, 1, (bytes) size, input);
            found = bytes == (bytes) size && strstr(text, needle) != nullptr;
            free(text);
        }
    }
    fclose(input);
    return found;
}

// Read a whole regular file as NUL-terminated text; nullptr when it cannot be read.
char *Util_readText(const char *path) {
    FILE *input = fopen(path, "rb");
    if (input == nullptr)
        return nullptr;
    char *text = nullptr;
    if (fseek(input, 0, SEEK_END) == 0) {
        long size = ftell(input);
        if (size >= 0 && fseek(input, 0, SEEK_SET) == 0) {
            text = Util_allocate((bytes) size + 1);
            if (fread(text, 1, (bytes) size, input) != (bytes) size) {
                free(text);
                text = nullptr;
            }
        }
    }
    fclose(input);
    return text;
}

// Test whether text ends with the complete suffix string.
bool Util_endsWith(const char *text, const char *suffix) {
    bytes a = strlen(text), b = strlen(suffix);
    return a >= b && strcmp(text + a - b, suffix) == 0;
}

// Wait for a child and translate its exit or signal into a process status.
static int reap(int child) {
#ifdef _WIN32
    DWORD status = 0;
    DWORD wait_result = WaitForSingleObject((HANDLE)(intptr_t) child, INFINITE);
    if (wait_result != WAIT_OBJECT_0) {
        THROW("cannot wait for child: %lu", GetLastError());
        return EXIT_FAILURE;
    }
    if (!GetExitCodeProcess((HANDLE)(intptr_t) child, &status)) {
        THROW("cannot get exit code: %lu", GetLastError());
        return EXIT_FAILURE;
    }
    return (int) status;
#else
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        THROW("cannot reap child: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return EXIT_FAILURE;
#endif
}

#ifdef _WIN32
// Construct a quoted Windows command line string from an argv vector.
static char *buildCommandLine(char **arguments) {
    if (arguments == nullptr || arguments[0] == nullptr)
        return nullptr;
    bytes total = 0;
    for (bytes i = 0; arguments[i] != nullptr; ++i)
        total += strlen(arguments[i]) * 2 + 4;

    char *line = (char*) Util_allocate(total + 1);
    char *p = line;
    for (bytes i = 0; arguments[i] != nullptr; ++i) {
        if (i > 0)
            *p++ = ' ';
        const char *arg = arguments[i];
        bool need_quotes = (strpbrk(arg, " \t\"") != nullptr) || (*arg == '\0');
        if (need_quotes)
            *p++ = '"';
        for (const char *c = arg; *c; ++c) {
            if (*c == '"')
                *p++ = '\\';
            *p++ = *c;
        }
        if (need_quotes)
            *p++ = '"';
    }
    *p = '\0';
    return line;
}
#endif

// Spawn an argv command with the supplied environment and return its status.
int Util_executeWithEnvironment(char **arguments, char **environment) {
#ifdef _WIN32
    (void) environment;
    char *cmdline = buildCommandLine(arguments);
    if (cmdline == nullptr)
        return EXIT_FAILURE;

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(
            nullptr, cmdline, nullptr, nullptr, FALSE,
            0, nullptr, nullptr, &si, &pi)) {
        THROW("cannot launch %s: %lu", arguments[0], GetLastError());
        free(cmdline);
        return EXIT_FAILURE;
    }
    free(cmdline);
    CloseHandle(pi.hThread);
    int status = reap((int)(intptr_t) pi.hProcess);
    CloseHandle(pi.hProcess);
    return status;
#else
    pid_t child;
    int error = posix_spawnp(&child, arguments[0], nullptr, nullptr, arguments, environment);
    if (error != 0) {
        THROW("cannot launch %s: %s", arguments[0], strerror(error));
        return EXIT_FAILURE;
    }
    return reap(child);
#endif
}

// Spawn an argv command with the process's inherited environment.
int Util_execute(char **arguments) {
    return Util_executeWithEnvironment(arguments, environ);
}

// Spawn a build command while routing its stdout to stderr for clean path output.
int Util_executeBuild(char **arguments) {
#ifdef _WIN32
    char *cmdline = buildCommandLine(arguments);
    if (cmdline == nullptr)
        return EXIT_FAILURE;

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessA(
            nullptr, cmdline, nullptr, nullptr, TRUE,
            0, nullptr, nullptr, &si, &pi)) {
        THROW("cannot launch %s: %lu", arguments[0], GetLastError());
        free(cmdline);
        return EXIT_FAILURE;
    }
    free(cmdline);
    CloseHandle(pi.hThread);
    int status = reap((int)(intptr_t) pi.hProcess);
    CloseHandle(pi.hProcess);
    return status;
#else
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error != 0) {
        THROW("cannot initialize build output: %s", strerror(error));
        return EXIT_FAILURE;
    }
    error = posix_spawn_file_actions_adddup2(&actions, STDERR_FILENO, STDOUT_FILENO);
    pid_t child;
    if (error == 0)
        error = posix_spawnp(&child, arguments[0], &actions, nullptr, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) {
        THROW("cannot launch %s: %s", arguments[0], strerror(error));
        return EXIT_FAILURE;
    }
    return reap(child);
#endif
}

// Append caller arguments to a fixed argv prefix and execute without a shell.
int Util_runCommand(char **prefix, bytes prefixCount, int argc, char **argv) {
    bytes limit = SIZE_MAX / sizeof(char*);
    if (prefix == nullptr || prefixCount == 0 || argc < 0 ||
        (bytes) argc >= limit || prefixCount > limit - (bytes) argc - 1 ||
        prefix[0] == nullptr || (argc > 0 && argv == nullptr)) {
        THROW("invalid command prefix or argument count");
        return EXIT_FAILURE;
    }
    char **arguments = Util_allocate((prefixCount + (bytes) argc + 1) * sizeof(char*));
    for (bytes i = 0; i < prefixCount; ++i)
        arguments[i] = prefix[i];
    for (int i = 0; i < argc; ++i)
        arguments[prefixCount + (bytes) i] = argv[i];
    int status = Util_execute(arguments);
    free(arguments);
    return status;
}

// Run a checker once per matched source and return the project path on success.
int Util_checkSources(const char *project, const char *pattern, char **prefix, bytes prefixCount, char **output) {
    if (project == nullptr || pattern == nullptr || output == nullptr || prefix == nullptr ||
        prefixCount == 0 || prefixCount > SIZE_MAX / sizeof(char*) - 2 || prefix[0] == nullptr) {
        THROW("invalid source checker input or argument count");
        return EXIT_FAILURE;
    }
    bytes count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, pattern, &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    char **arguments = Util_allocate((prefixCount + 2) * sizeof(char*));
    for (bytes i = 0; i < prefixCount; ++i)
        arguments[i] = prefix[i];
    int status = 0;
    for (bytes i = 0; i < count && status == 0; ++i) {
        arguments[prefixCount] = sources[i];
        status = Util_executeBuild(arguments);
    }
    if (status == 0)
        *output = Util_combine(project, "");
    free(arguments);
    Util_freeSources(sources, count);
    return status;
}

// Create a directory path recursively, accepting existing directory components.
bool Util_makeDirectory(char *path) {
    char *start = path + 1;
#ifdef _WIN32
    if (((path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z')) &&
        path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
        start = path + 3;
#endif
    for (char *part = start; ; ++part) {
        if (*part != '/' && *part != '\\' && *part != '\0')
            continue;
        char saved = *part;
        *part = 0;
#ifdef _WIN32
        int error = _mkdir(path); // Windows: create directory, fail on existing.
        struct _stat info;
        bool valid = error == 0 || (errno == EEXIST && _stat(path, &info) == 0 && (info.st_mode & _S_IFDIR));
#else
        int error = mkdir(path, 0755); // POSIX: create directory, fail on existing.
        struct stat info;
        bool valid = error == 0 || (errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode));
#endif
        *part = saved;
        if (!valid) {
            THROW("cannot create output directory %s", path);
            return false;
        }
        if (saved == '\0')
            return true;
    }
}

// Create and return the project's hashed output directory under B_HOME or the user cache.
char *Util_outputDirectory(const char *project) {
    // Artifacts live in their territory: .b/obj/<unit>, beside the marker. The
    // walk excludes .b, and .b/* is ignored, so output never becomes inventory.
    char *root = Territory_find(project);
    if (root != nullptr) {
        bytes length = strlen(root);
        bool inside = strncmp(project, root, length) == 0 &&
            (project[length] == '\0' || project[length] == '/' || project[length] == '\\');
        if (inside) {
            const char *relative = project + length;
            while (*relative == '/' || *relative == '\\')
                ++relative;
            char *base = Util_combine(root, "/.b/obj");
            char *directory;
            if (*relative == '\0')
                directory = Util_combine(base, "");
            else {
                char *prefix = Util_combine(base, "/");
                directory = Util_combine(prefix, relative);
                free(prefix);
            }
            free(base);
            free(root);
            if (!Util_makeDirectory(directory)) {
                free(directory);
                return nullptr;
            }
            return directory;
        }
        free(root);
    }
    const char *state = getenv("B_HOME");
    char *defaultState = nullptr;
    if (state == nullptr || *state == '\0') {
        const char *home = getenv("HOME");
        if (home == nullptr || *home == '\0') {
            THROW("HOME or B_HOME must be set");
            return nullptr;
        }
#ifdef _WIN32
        const char *cache = getenv("LOCALAPPDATA");
        if (cache == nullptr || *cache == '\0')
            cache = getenv("APPDATA");
        if (cache == nullptr || *cache == '\0')
            cache = getenv("USERPROFILE");
        if (cache == nullptr || *cache == '\0') {
            THROW("LOCALAPPDATA/APPDATA/USERPROFILE must be set");
            return nullptr;
        }
        defaultState = Util_combine(cache, "\\b.sh");
#else
        const char *cache = getenv("XDG_CACHE_HOME");
        defaultState = Util_combine(cache != nullptr && *cache != '\0' ? cache : home,
                                    cache != nullptr && *cache != '\0' ? "/b.sh" : "/.cache/b.sh");
#endif
        state = defaultState;
    }
    // FNV-1a constants define the path-identity hash, not configurable limits.
    u64 hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char*) project; *p; ++p)
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    char suffix[sizeof "/out/" + sizeof(u64) * 2];
    snprintf(suffix, sizeof suffix, "/out/%016" PRIx64, hash);
    char *directory = Util_combine(state, suffix);
    free(defaultState);
    if (!Util_makeDirectory(directory)) {
        free(directory);
        return nullptr;
    }
    return directory;
}

// Return an allocated path through the final slash, or the original filename if none exists.
char *Util_parentDirectory(const char *file) {
    char *directory = Util_combine(file, "");
    char *slash = strrchr(directory, '/');
#ifdef _WIN32
    char *backslash = strrchr(directory, '\\');
    if (backslash != nullptr && (slash == nullptr || backslash > slash))
        slash = backslash;
#endif
    if (slash != nullptr)
        slash[1] = '\0';
    return directory;
}

// Return the registered project root during contextual runs, or the file's parent.
char *Util_projectDirectory(const char *file) {
    const char *root = getenv("B_PROJECT_ROOT");
    if (root != nullptr && *root != '\0')
        return Util_combine(root, "");
    return Util_parentDirectory(file);
}

// Glob an extension under a literal project path and return owned copies of matching paths.
char **Util_collectSources(const char *project, const char *extension, bytes *count, bool *ok) {
    *ok = false;
    *count = 0;
#ifdef _WIN32
    const char *pattern = strrchr(extension, '/');
    pattern = pattern != nullptr ? pattern + 1 : extension;
    if (strncmp(pattern, "*.", 2) != 0) {
        THROW("unsupported source selector: %s", extension);
        return nullptr;
    }
    struct dirent **entries = nullptr;
    int entryCount = Util_scandir(project, &entries, nullptr, Util_alphasort);
    if (entryCount < 0) {
        THROW("cannot enumerate sources in %s", project);
        return nullptr;
    }
    char **sources = nullptr;
    bytes total = 0, capacity = 0;
    bool valid = true;
    for (int index = 0; index < entryCount; ++index) {
        const char *name = (*entries[index]).d_name;
        bool matches = false;
        const char *selector = pattern + 2;
        if (*selector == '{') {
            const char *end = strchr(selector + 1, '}');
            if (end != nullptr && end[1] == '\0') {
                const char *part = selector + 1;
                while (part < end && !matches) {
                    const char *separator = memchr(part, ',', (bytes) (end - part));
                    const char *stop = separator != nullptr ? separator : end;
                    bytes length = (bytes) (stop - part);
                    if (length > 0 && length <= SIZE_MAX - 2) {
                        char *suffix = Util_allocate(length + 2);
                        suffix[0] = '.';
                        memcpy(suffix + 1, part, length);
                        suffix[length + 1] = '\0';
                        bytes nameLength = strlen(name), suffixLength = length + 1;
                        matches = nameLength >= suffixLength &&
                            strcmp(name + nameLength - suffixLength, suffix) == 0;
                        free(suffix);
                    }
                    part = stop + (separator != nullptr);
                }
            }
        } else if (*selector == '[') {
            const char *end = strchr(selector + 1, ']');
            const char *dot = strrchr(name, '.');
            matches = end != nullptr && end[1] == '\0' && dot != nullptr &&
                dot[1] != '\0' && dot[2] == '\0' &&
                memchr(selector + 1, dot[1], (bytes) (end - selector - 1)) != nullptr;
        } else {
            const char *suffix = pattern + 1;
            bytes nameLength = strlen(name), suffixLength = strlen(suffix);
            matches = nameLength >= suffixLength &&
                strcmp(name + nameLength - suffixLength, suffix) == 0;
        }
        if (!matches)
            continue;
        char *prefix = Util_combine(project, "/");
        char *path = Util_combine(prefix, name);
        free(prefix);
        struct stat information;
        if (stat(path, &information) != 0 || !S_ISREG(information.st_mode)) {
            free(path);
            continue;
        }
        if (total == capacity) {
            bytes next = capacity == 0 ? 16 : capacity * 2;
            if (next < capacity || next > SIZE_MAX / sizeof *sources) {
                free(path);
                THROW("source inventory overflow in %s", project);
                valid = false;
                break;
            }
            char **replacement = realloc(sources, next * sizeof *sources);
            if (replacement == nullptr) {
                free(path);
                THROW("source inventory allocation failed");
                valid = false;
                break;
            }
            sources = replacement;
            capacity = next;
        }
        sources[total++] = path;
    }
    for (int index = 0; index < entryCount; ++index)
        free(entries[index]);
    free(entries);
    if (!valid || total == 0) {
        if (total == 0 && valid)
            THROW("cannot enumerate sources in %s", project);
        Util_freeSources(sources, total);
        return nullptr;
    }
    *count = total;
    *ok = true;
    return sources;
#else
    // The directory is literal input, not part of the glob expression.
    bytes length = strlen(project);
    if (length > (SIZE_MAX - 1) / 2) {
        THROW("source directory length overflow");
        return nullptr;
    }
    char *escaped = Util_allocate(length * 2 + 1);
    bytes position = 0;
    for (bytes i = 0; i < length; ++i) {
        if (strchr("*?[\\{}", project[i]) != nullptr)
            escaped[position++] = '\\';
        escaped[position++] = project[i];
    }
    char *pattern = Util_combine(escaped, extension);
    free(escaped);
    glob_t files = {0};
    int flags = 0;
#ifdef GLOB_BRACE
    // BSD/GNU glob supports native adapter extension sets; directory braces
    // were escaped above and never participate in expansion.
    flags = GLOB_BRACE;
#endif
    int found = glob(pattern, flags, nullptr, &files);
    free(pattern);
    if (found != 0) {
        THROW("cannot enumerate sources in %s", project);
        globfree(&files);
        return nullptr;
    }
    bytes total = files.gl_pathc;
    char **sources = Util_allocate((total ? total : 1) * sizeof(char*));
    for (bytes i = 0; i < total; ++i)
        sources[i] = Util_combine(files.gl_pathv[i], "");
    globfree(&files);
    *count = total;
    *ok = true;
    return sources;
#endif
}

// Release the copied source paths and their containing array.
void Util_freeSources(char **sources, bytes count) {
    if (sources == nullptr)
        return;
    for (bytes i = 0; i < count; ++i)
        free(sources[i]);
    free(sources);
}

// Build an environment vector replacing any inherited binding with the supplied name/value.
char **Util_environmentWith(const char *name, const char *value) {
    bytes prefix = strlen(name);
    bytes count = 0;
    char **inheritedEnvironment = processEnvironment();
    if (inheritedEnvironment == nullptr) {
        THROW("cannot read the process environment");
        return nullptr;
    }
    while (inheritedEnvironment[count] != nullptr)
        ++count;
    char **result = Util_allocate((count + 2) * sizeof(char*));
    bytes out = 0;
    for (bytes i = 0; i < count; ++i) {
        if (strncmp(inheritedEnvironment[i], name, prefix) == 0 && inheritedEnvironment[i][prefix] == '=')
            continue; // drop the previous binding so ours wins
        result[out++] = inheritedEnvironment[i];
    }
    char *entry = Util_allocate(prefix + strlen(value) + 2);
    memcpy(entry, name, prefix);
    entry[prefix] = '=';
    strcpy(entry + prefix + 1, value);
    result[out++] = entry;
    result[out] = nullptr;
    return result;
}

// Free an environment array and owned override while preserving inherited entries.
void Util_freeEnvironment(char **environment) {
    if (environment == nullptr)
        return;
    char **inheritedEnvironment = processEnvironment();
    for (bytes i = 0; environment[i] != nullptr; ++i) {
        bool inherited = false;
        for (bytes j = 0; inheritedEnvironment != nullptr && inheritedEnvironment[j] != nullptr; ++j)
            if (environment[i] == inheritedEnvironment[j]) {
                inherited = true;
                break;
            }
        if (!inherited)
            free(environment[i]);
    }
    free(environment);
}
