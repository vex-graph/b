// MODULE: b language CLI. No owned public class; argv and source paths are borrowed.
// DEFINITION: A cold command dispatcher invokes compilers through posix_spawnp,
// never through a shell. Child status reaches the caller; export requests fail
// before outputs or processes exist. Instance runs use the source runtime or
// an existing executable, without constructing an artifact. Exec builds C/Java/
// Rust source artifacts first. Build artifacts are outside
// source trees. This initial adapter has no incremental graph or supervisor.
// OVERVIEW: command validation; execute (child launch/status); outputDirectory
// (canonical project identity); compile (C/Java/Rust); run (source/executable dispatch).
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <glob.h>
#include <inttypes.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define THROW(...) do { fprintf(stderr, "[vex] %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)

static void *allocate(size_t size) {
    void *memory = calloc(1, size);
    if (memory == nullptr) {
        THROW("out of memory");
        exit(EXIT_FAILURE);
    }
    return memory;
}

static char *combine(const char *left, const char *right) {
    size_t a = strlen(left), b = strlen(right);
    if (a > SIZE_MAX - b - 1) {
        THROW("path size overflow");
        exit(EXIT_FAILURE);
    }
    char *text = allocate(a + b + 1);
    memcpy(text, left, a);
    memcpy(text + a, right, b + 1);
    return text;
}

static int execute(char **arguments) {
    pid_t child;
    int error = posix_spawnp(&child, arguments[0], nullptr, nullptr, arguments, environ);
    if (error != 0) {
        THROW("cannot launch %s: %s", arguments[0], strerror(error));
        return EXIT_FAILURE;
    }
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

static bool makeDirectory(char *path) {
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

static char *outputDirectory(const char *project) {
    const char *state = getenv("B_HOME");
    char *defaultState = nullptr;
    if (state == nullptr || *state == '\0') {
        const char *home = getenv("HOME");
        if (home == nullptr || *home == '\0') {
            THROW("HOME or B_HOME must be set");
            return nullptr;
        }
#ifdef __APPLE__
        defaultState = combine(home, "/Library/Application Support/b");
#else
        const char *cache = getenv("XDG_CACHE_HOME");
        defaultState = combine(cache != nullptr && *cache != '\0' ? cache : home,
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
    char *directory = combine(state, suffix);
    free(defaultState);
    if (!makeDirectory(directory)) {
        free(directory);
        return nullptr;
    }
    return directory;
}

static bool endsWith(const char *text, const char *suffix) {
    size_t a = strlen(text), b = strlen(suffix);
    return a >= b && strcmp(text + a - b, suffix) == 0;
}

static bool supportedLanguage(const char *language) {
    return strcmp(language, "c") == 0 || strcmp(language, "java") == 0 || strcmp(language, "rust") == 0;
}

static const char *sourceExtension(const char *language) {
    if (strcmp(language, "java") == 0)
        return "/*.java";
    if (strcmp(language, "rust") == 0)
        return "/*.rs";
    return "/*.c";
}

static int compile(const char *language, const char *project, char **sources, size_t count, char **output) {
    if (count > SIZE_MAX / sizeof(char*) - 20) {
        THROW("source argument count overflow");
        return EXIT_FAILURE;
    }
    char *directory = outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    bool java = strcmp(language, "java") == 0;
    bool rust = strcmp(language, "rust") == 0;
    *output = combine(directory, java ? "/classes" : "/program");
    if (java && !makeDirectory(*output)) {
        free(directory);
        return EXIT_FAILURE;
    }
    // Fixed option overhead, not a ceiling on the dynamic source list.
    char **arguments = allocate((count + 20) * sizeof(char*));
    size_t n = 0;
    const char *compiler = getenv("CC");
    arguments[n++] = (char*) (java ? "javac" : rust ? "rustc" : compiler != nullptr && *compiler ? compiler : "cc");
    if (java) {
        arguments[n++] = "-d";
        arguments[n++] = *output;
    } else if (rust) {
        arguments[n++] = "--edition=2021";
        arguments[n++] = "-o";
        arguments[n++] = *output;
    } else {
        arguments[n++] = "-std=gnu23";
        arguments[n++] = "-Wall";
        arguments[n++] = "-Wextra";
        arguments[n++] = "-Werror";
#ifdef __APPLE__
        arguments[n++] = "-arch";
        arguments[n++] = "arm64";
        arguments[n++] = "-mcpu=apple-m1";
        arguments[n++] = "-mmacosx-version-min=14.0";
#endif
        arguments[n++] = "-I";
        arguments[n++] = (char*) project;
        arguments[n++] = "-o";
        arguments[n++] = *output;
    }
    for (size_t i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    int status = execute(arguments);
    free(arguments);
    free(directory);
    return status;
}

static int build(const char *language, const char *input) {
    if (!supportedLanguage(language)) {
        THROW("unsupported language: %s", language);
        return EXIT_FAILURE;
    }
    char *project = realpath(input, nullptr);
    struct stat info;
    if (project == nullptr || stat(project, &info) != 0 || !S_ISDIR(info.st_mode)) {
        THROW("build directory does not exist: %s", input);
        free(project);
        return EXIT_FAILURE;
    }
    // The directory is literal input, not part of the glob expression.
    size_t length = strlen(project);
    if (length > (SIZE_MAX - 1) / 2) {
        THROW("source directory length overflow");
        free(project);
        return EXIT_FAILURE;
    }
    char *escaped = allocate(length * 2 + 1);
    size_t position = 0;
    for (size_t i = 0; i < length; ++i) {
        if (strchr("*?[\\", project[i]) != nullptr)
            escaped[position++] = '\\';
        escaped[position++] = project[i];
    }
    char *pattern = combine(escaped, sourceExtension(language));
    free(escaped);
    glob_t files = {0};
    int found = glob(pattern, 0, nullptr, &files);
    free(pattern);
    if (found != 0) {
        THROW("cannot enumerate %s sources in %s", language, input);
        globfree(&files);
        free(project);
        return EXIT_FAILURE;
    }
    char *output = nullptr;
    int status = compile(language, project, files.gl_pathv, files.gl_pathc, &output);
    if (status == 0)
        puts(output);
    free(output);
    globfree(&files);
    free(project);
    return status;
}

static int run(const char *input, int argc, char **argv, bool buildArtifact) {
    char *file = realpath(input, nullptr);
    struct stat info;
    if (file == nullptr || stat(file, &info) != 0 || !S_ISREG(info.st_mode)) {
        THROW("run file does not exist or is not regular: %s", input);
        free(file);
        return EXIT_FAILURE;
    }
    bool java = endsWith(file, ".java");
    bool rust = endsWith(file, ".rs");
    bool c = endsWith(file, ".c");
    bool compiled = c || rust;
    char *output = nullptr;
    char *mainClass = nullptr;
    int status = 0;
    if (compiled && !buildArtifact) {
        THROW("%s has no source runtime; use b run exec to compile then launch", rust ? "Rust" : "C");
        status = EXIT_FAILURE;
    } else if (compiled || (java && buildArtifact)) {
        char *project = combine(file, "");
        char *slash = strrchr(project, '/');
        if (slash != nullptr)
            slash[1] = '\0';
        status = compile(java ? "java" : rust ? "rust" : "c", project, &file, 1, &output);
        free(project);
        if (java) {
            const char *base = strrchr(file, '/');
            mainClass = combine(base != nullptr ? base + 1 : file, "");
            char *dot = strrchr(mainClass, '.');
            if (dot != nullptr)
                *dot = '\0';
        }
    } else if (!java && access(file, X_OK) != 0) {
        THROW("file is not executable or a supported C/Java/Rust source: %s", input);
        status = EXIT_FAILURE;
    }
    if (status == 0) {
        char **arguments = allocate(((size_t) argc + 5) * sizeof(char*));
        size_t n = 0;
        arguments[n++] = java ? "java" : output != nullptr ? output : file;
        if (java) {
            if (buildArtifact) {
                arguments[n++] = "-cp";
                arguments[n++] = output;
                arguments[n++] = mainClass;
            } else {
                arguments[n++] = file;
            }
        }
        for (int i = 0; i < argc; ++i)
            arguments[n++] = argv[i];
        status = execute(arguments);
        free(arguments);
    }
    free(output);
    free(mainClass);
    free(file);
    return status;
}

int main(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0))) {
        puts("b run <exec|instance> <filename> [-- arguments...]\n"
             "b build <c|java|rust> [directory]\n"
             "b export <manifestmainfile> <destination> <exe|app|msi|iso|zip>\n"
             "b java <filename.java> [-- arguments...]\n"
             "instance: direct runtime/executable; exec: build source artifact then launch\n"
             "export: planned, not implemented");
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[1], "build") == 0 && (argc == 3 || argc == 4))
        return build(argv[2], argc == 4 ? argv[3] : ".");
    if (strcmp(argv[1], "run") == 0 && argc >= 4) {
        bool buildArtifact = strcmp(argv[2], "exec") == 0;
        if (!buildArtifact && strcmp(argv[2], "instance") != 0) {
            THROW("unsupported run mode: %s", argv[2]);
            return EXIT_FAILURE;
        }
        int start = argc > 4 && strcmp(argv[4], "--") == 0 ? 5 : 4;
        return run(argv[3], argc - start, argv + start, buildArtifact);
    }
    if (strcmp(argv[1], "java") == 0 && argc >= 3 && endsWith(argv[2], ".java")) {
        int start = argc > 3 && strcmp(argv[3], "--") == 0 ? 4 : 3;
        return run(argv[2], argc - start, argv + start, false);
    }
    if (strcmp(argv[1], "export") == 0 && argc == 5) {
        THROW("export is not implemented; no destination was created");
        return EXIT_FAILURE;
    }
    THROW("invalid command; run b --help");
    return EXIT_FAILURE;
}
