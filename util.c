// MODULE: b shared utilities. No owned class; argv and paths are borrowed.
// DEFINITION: One place spawns children (never a shell), builds the per-project
// output directory outside the source tree, and collects a directory's sources
// with glob metacharacters treated literally. The environment helper exists so
// an adapter can redirect a tool's cache (e.g. Python bytecode) out of source.
#include "b.h"

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

void *Util_allocate(size_t size) {
    void *memory = calloc(1, size);
    if (memory == nullptr) {
        THROW("out of memory");
        exit(EXIT_FAILURE);
    }
    return memory;
}

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

bool Util_endsWith(const char *text, const char *suffix) {
    size_t a = strlen(text), b = strlen(suffix);
    return a >= b && strcmp(text + a - b, suffix) == 0;
}

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

int Util_executeWithEnvironment(char **arguments, char **environment) {
    pid_t child;
    int error = posix_spawnp(&child, arguments[0], nullptr, nullptr, arguments, environment);
    if (error != 0) {
        THROW("cannot launch %s: %s", arguments[0], strerror(error));
        return EXIT_FAILURE;
    }
    return reap(child);
}

int Util_execute(char **arguments) {
    return Util_executeWithEnvironment(arguments, environ);
}

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

char *Util_parentDirectory(const char *file) {
    char *directory = Util_combine(file, "");
    char *slash = strrchr(directory, '/');
    if (slash != nullptr)
        slash[1] = '\0';
    return directory;
}

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
        if (strchr("*?[\\", project[i]) != nullptr)
            escaped[position++] = '\\';
        escaped[position++] = project[i];
    }
    char *pattern = Util_combine(escaped, extension);
    free(escaped);
    glob_t files = {0};
    int found = glob(pattern, 0, nullptr, &files);
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

void Util_freeSources(char **sources, size_t count) {
    if (sources == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        free(sources[i]);
    free(sources);
}

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
