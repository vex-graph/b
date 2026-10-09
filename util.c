// MODULE: b shared utilities. No owned class; argv and paths are borrowed.
// DEFINITION: One place spawns children (never a shell), builds the per-project
// output directory outside the source tree, and collects a directory's sources
// with glob metacharacters treated literally. The environment helper exists so
// an adapter can redirect a tool's cache (e.g. Python bytecode) out of source.
#include "b.h"

;;DEFINITION
/* Utility services keep adapters focused on their language contracts. Dynamic
 * allocations belong to the caller; allocation failure terminates this CLI.
 * POSIX children inherit the environment, run without a shell, and are reaped
 * synchronously. Build stdout is redirected to stderr so only the result path
 * appears on suite stdout. Output storage is outside project sources and keyed
 * by project-path hash; source globs escape the directory portion literally.
 */
;;OVERVIEW
/* MODULE: shared utility implementation; public declarations: b.h.
 * PUBLIC MEMORY/PATH: Util_allocate; Util_combine; Util_endsWith;
 * Util_makeDirectory; Util_outputDirectory; Util_parentDirectory.
 * PUBLIC PROCESS: Util_executeWithEnvironment; Util_execute; Util_executeBuild;
 * Util_runCommand — append borrowed program arguments without shell parsing.
 * PUBLIC CHECKS: Util_checkSources — run a checker on each discovered source.
 * PUBLIC DISCOVERY: Util_collectSources; Util_freeSources.
 * PUBLIC ENVIRONMENT: Util_environmentWith; Util_freeEnvironment — allocate one
 * replacement entry, borrow inherited entries, free only owned storage.
 * PRIVATE STATIC: reap — wait for child and map exit/signal status.
 * BORROWED GLOBAL: char **environ — inherited process environment.
 */

#include <errno.h>
#include <glob.h>
#include <inttypes.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

// Allocate zero-initialized storage; report and terminate the CLI if allocation fails.
void *Util_allocate(size_t size) {
    void *memory = calloc(1, size);
    if (memory == nullptr) {
        THROW("out of memory");
        exit(EXIT_FAILURE);
    }
    return memory;
}

// Allocate and return the concatenation of two strings, rejecting size overflow.
char *Util_combine(const char *left, const char *right) {
    size_t a = strlen(left), b = strlen(right);
    if (a > SIZE_MAX - b - 1) {
        THROW("path size overflow");
        exit(EXIT_FAILURE);
    }
    char *text = Util_allocate(a + b + 1);
    memcpy(text, left, a);
    memcpy(text + a, right, b + 1);
    return text;
}

// Test whether text ends with the complete suffix string.
bool Util_endsWith(const char *text, const char *suffix) {
    size_t a = strlen(text), b = strlen(suffix);
    return a >= b && strcmp(text + a - b, suffix) == 0;
}

// Wait for a child and translate its exit or signal into a process status.
static int reap(pid_t child) {
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
        return 128 + WTERMSIG(status); // POSIX shell signal-status convention.
    return EXIT_FAILURE;
}

// Spawn an argv command with the supplied environment and return its status.
int Util_executeWithEnvironment(char **arguments, char **environment) {
    pid_t child;
    int error = posix_spawnp(&child, arguments[0], nullptr, nullptr, arguments, environment);
    if (error != 0) {
        THROW("cannot launch %s: %s", arguments[0], strerror(error));
        return EXIT_FAILURE;
    }
    return reap(child);
}

// Spawn an argv command with the process's inherited environment.
int Util_execute(char **arguments) {
    return Util_executeWithEnvironment(arguments, environ);
}

// Spawn a build command while routing its stdout to stderr for clean path output.
int Util_executeBuild(char **arguments) {
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
}

// Append caller arguments to a fixed argv prefix and execute without a shell.
int Util_runCommand(char **prefix, size_t prefixCount, int argc, char **argv) {
    size_t limit = SIZE_MAX / sizeof(char*);
    if (prefix == nullptr || prefixCount == 0 || argc < 0 ||
        (size_t) argc >= limit || prefixCount > limit - (size_t) argc - 1 ||
        prefix[0] == nullptr || (argc > 0 && argv == nullptr)) {
        THROW("invalid command prefix or argument count");
        return EXIT_FAILURE;
    }
    char **arguments = Util_allocate((prefixCount + (size_t) argc + 1) * sizeof(char*));
    for (size_t i = 0; i < prefixCount; ++i)
        arguments[i] = prefix[i];
    for (int i = 0; i < argc; ++i)
        arguments[prefixCount + (size_t) i] = argv[i];
    int status = Util_execute(arguments);
    free(arguments);
    return status;
}

// Run a checker once per matched source and return the project path on success.
int Util_checkSources(const char *project, const char *pattern, char **prefix, size_t prefixCount, char **output) {
    if (project == nullptr || pattern == nullptr || output == nullptr || prefix == nullptr ||
        prefixCount == 0 || prefixCount > SIZE_MAX / sizeof(char*) - 2 || prefix[0] == nullptr) {
        THROW("invalid source checker input or argument count");
        return EXIT_FAILURE;
    }
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, pattern, &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    char **arguments = Util_allocate((prefixCount + 2) * sizeof(char*));
    for (size_t i = 0; i < prefixCount; ++i)
        arguments[i] = prefix[i];
    int status = 0;
    for (size_t i = 0; i < count && status == 0; ++i) {
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
    for (char *part = path + 1; ; ++part) {
        if (*part != '/' && *part != '\0')
            continue;
        char saved = *part;
        *part = '\0';
        int error = mkdir(path, 0755); // Conventional user-readable build directory.
        struct stat info;
        bool valid = error == 0 || (errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode));
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
    const char *state = getenv("B_HOME");
    char *defaultState = nullptr;
    if (state == nullptr || *state == '\0') {
        const char *home = getenv("HOME");
        if (home == nullptr || *home == '\0') {
            THROW("HOME or B_HOME must be set");
            return nullptr;
        }
#ifdef __APPLE__
        defaultState = Util_combine(home, "/Library/Application Support/b");
#else
        const char *cache = getenv("XDG_CACHE_HOME");
        defaultState = Util_combine(cache != nullptr && *cache != '\0' ? cache : home,
                                    cache != nullptr && *cache != '\0' ? "/b" : "/.cache/b");
#endif
        state = defaultState;
    }
    // FNV-1a constants define the path-identity hash, not configurable limits.
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char*) project; *p; ++p)
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    char suffix[sizeof "/out/" + sizeof(uint64_t) * 2];
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
    if (slash != nullptr)
        slash[1] = '\0';
    return directory;
}

// Glob an extension under a literal project path and return owned copies of matching paths.
char **Util_collectSources(const char *project, const char *extension, size_t *count, bool *ok) {
    *ok = false;
    *count = 0;
    // The directory is literal input, not part of the glob expression.
    size_t length = strlen(project);
    if (length > (SIZE_MAX - 1) / 2) {
        THROW("source directory length overflow");
        return nullptr;
    }
    char *escaped = Util_allocate(length * 2 + 1);
    size_t position = 0;
    for (size_t i = 0; i < length; ++i) {
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
    size_t total = files.gl_pathc;
    char **sources = Util_allocate((total ? total : 1) * sizeof(char*));
    for (size_t i = 0; i < total; ++i)
        sources[i] = Util_combine(files.gl_pathv[i], "");
    globfree(&files);
    *count = total;
    *ok = true;
    return sources;
}

// Release the copied source paths and their containing array.
void Util_freeSources(char **sources, size_t count) {
    if (sources == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        free(sources[i]);
    free(sources);
}

// Build an environment vector replacing any inherited binding with the supplied name/value.
char **Util_environmentWith(const char *name, const char *value) {
    size_t prefix = strlen(name);
    size_t count = 0;
    while (environ[count] != nullptr)
        ++count;
    char **result = Util_allocate((count + 2) * sizeof(char*));
    size_t out = 0;
    for (size_t i = 0; i < count; ++i) {
        if (strncmp(environ[i], name, prefix) == 0 && environ[i][prefix] == '=')
            continue; // drop the previous binding so ours wins
        result[out++] = environ[i];
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
    for (size_t i = 0; environment[i] != nullptr; ++i) {
        bool inherited = false;
        for (size_t j = 0; environ[j] != nullptr; ++j)
            if (environment[i] == environ[j]) {
                inherited = true;
                break;
            }
        if (!inherited)
            free(environment[i]);
    }
    free(environment);
}
