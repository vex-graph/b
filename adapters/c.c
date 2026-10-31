// CLASS: C language adapter.
// DEFINITION: Compiles a directory's top-level .c files into one native
// executable with the C23 dialect and the project's build flags; a single
// source is compiled on demand for `run exec`. C has no source runtime, so
// `run instance` rejects and points at `run exec`. The compiler is $CC or cc.
#include "c.h"
#include "../util/b.h"
#include "territory.h"

;;DEFINITION
/* C compilation produces one native executable from supplied sources, using
 * the selected CC executable, strict C23 warnings and platform baseline flags.
 * Directory builds discover top-level .c inputs; exec builds one entry source
 * before launching it. Instance rejects because there is no C source runtime.
 * Child failures propagate and prevent launching previous output. The unit's
 * declared include roots (build.includes in its assembly) resolve against the
 * territory root and reach the compiler as -I paths; the compiler, not b, reads
 * the #include lines.
 */
;;OVERVIEW
/* MODULE: C adapter; exported record: C_ADAPTER (adapters/c.h).
 * METADATA: tools="CC=cc"; capabilities="native build/exec; source instance rejects".
 * PRIVATE STATIC: assemblyIncludes — read the build.includes string array from
 * the unit's b.assembly.json; compileC — compile explicit sources into an
 * out-of-tree program with the declared include roots; buildC — collect
 * directory sources and compile; entryC — owned path of the file defining main;
 * runC — reject instance or compile and execute with borrowed program args.
 * RECORD FIELDS: name="c"; extension=".c"; build=buildC; run=runC; entry=entryC.
 * No owned class state; temporary argv/path/source lists are freed per call.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Read the string items of the unit's build.includes array from b.assembly.json.
// The reader accepts only the array b assembly writes: quoted paths, no escapes.
// A missing file or an empty array yields no roots; a malformed array sets *ok.
// Two passes size the list exactly, so the reader never grows storage in place.
static char **assemblyIncludes(const char *project, size_t *count, bool *ok) {
    *count = 0;
    *ok = true;
    char *path = Util_combine(project, "/b.assembly.json");
    char *text = Util_readText(path);
    Util_release(path);
    if (text == nullptr)
        return nullptr;
    char *key = strstr(text, "\"includes\"");
    char *open = key != nullptr ? strchr(key, '[') : nullptr;
    char *end = open != nullptr ? strchr(open, ']') : nullptr;
    if (end == nullptr) {
        Util_release(text);
        return nullptr;
    }
    size_t items = 0;
    for (char *quote = strchr(open, '"'); quote != nullptr && quote < end; quote = strchr(quote + 1, '"')) {
        char *close = strchr(quote + 1, '"');
        if (close == nullptr || close > end) {
            THROW("malformed build.includes in %s", project);
            *ok = false;
            Util_release(text);
            return nullptr;
        }
        ++items;
        quote = close;
    }
    char **list = Util_allocate((items != 0 ? items : 1) * sizeof(char*));
    size_t filled = 0;
    for (char *quote = strchr(open, '"'); quote != nullptr && quote < end; ) {
        char *close = strchr(quote + 1, '"');
        size_t length = (size_t) (close - quote - 1);
        char *item = Util_allocate(length + 1);
        memcpy(item, quote + 1, length);
        list[filled++] = item;
        quote = strchr(close + 1, '"');
    }
    *count = filled;
    Util_release(text);
    return list;
}

// Compile the supplied C23 sources into one out-of-tree native executable. Declared
// include roots resolve against the territory root and are passed as -I paths.
static int compileC(const char *project, char **sources, size_t count, char **output) {
    if (count > SIZE_MAX / sizeof(char*) - 20) {
        THROW("source argument count overflow");
        return EXIT_FAILURE;
    }
    size_t includeCount = 0;
    bool includesOk = true;
    char **includes = assemblyIncludes(project, &includeCount, &includesOk);
    if (!includesOk) {
        Util_freeSources(includes, includeCount);
        return EXIT_FAILURE;
    }
    char *root = nullptr;
    if (includeCount > 0) {
        root = Territory_find(project);
        if (root == nullptr) {
            THROW("build.includes needs a claimed territory above %s", project);
            Util_freeSources(includes, includeCount);
            return EXIT_FAILURE;
        }
    }
    char *directory = Util_outputDirectory(project);
    if (directory == nullptr) {
        free(root);
        Util_freeSources(includes, includeCount);
        return EXIT_FAILURE;
    }
    *output = Util_combine(directory, "/program");
    char **paths = Util_allocate((includeCount != 0 ? includeCount : 1) * sizeof(char*));
    // Fixed option overhead, not a ceiling on the dynamic source list.
    char **arguments = Util_allocate((count + 20 + 2 * includeCount) * sizeof(char*));
    size_t n = 0;
    const char *compiler = getenv("CC");
    arguments[n++] = (char*) (compiler != nullptr && *compiler ? compiler : "cc");
    arguments[n++] = "-std=gnu2x";
    arguments[n++] = "-Wall";
    arguments[n++] = "-Wextra";
    arguments[n++] = "-Werror";
    arguments[n++] = "-Wno-unused-but-set-variable";
#ifdef __APPLE__
#if defined(__arm64__) || defined(__aarch64__)
    arguments[n++] = "-arch";
    arguments[n++] = "arm64";
    arguments[n++] = "-mcpu=apple-m1";
    arguments[n++] = "-mmacosx-version-min=14.0";
#elif defined(__x86_64__)
    arguments[n++] = "-arch";
    arguments[n++] = "x86_64";
    arguments[n++] = "-mmacosx-version-min=13.0";
#endif
#endif
    arguments[n++] = "-I";
    arguments[n++] = (char*) project;
    for (size_t i = 0; i < includeCount; ++i) {
        char *prefix = Util_combine(root, "/");
        paths[i] = Util_combine(prefix, includes[i]);
        free(prefix);
        arguments[n++] = "-I";
        arguments[n++] = paths[i];
    }
    arguments[n++] = "-o";
    arguments[n++] = *output;
    for (size_t i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    int status = Util_execute(arguments);
    for (size_t i = 0; i < includeCount; ++i)
        free(paths[i]);
    free(paths);
    free(arguments);
    free(directory);
    free(root);
    Util_freeSources(includes, includeCount);
    return status;
}

// Discover top-level C sources and compile them as one project executable.
static int buildC(const char *project, char **output) {
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, "/*.c", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    int status = compileC(project, sources, count, output);
    Util_freeSources(sources, count);
    return status;
}

// Compile a source entry for exec mode and launch it with the caller's arguments.
static int runC(const char *file, int argc, char **argv, bool buildArtifact) {
    if (!buildArtifact) {
        THROW("C has no source runtime; use b run exec to compile then launch");
        return EXIT_FAILURE;
    }
    char *project = Util_projectDirectory(file);
    char *output = nullptr;
    char *sources[1] = { (char*) file };
    int status = compileC(project, sources, 1, &output);
    if (status == 0) {
        char **arguments = Util_allocate(((size_t) argc + 2) * sizeof(char*));
        arguments[0] = output;
        for (int i = 0; i < argc; ++i)
            arguments[i + 1] = argv[i];
        status = Util_execute(arguments);
        free(arguments);
    }
    free(output);
    free(project);
    return status;
}

// Return an owned path to the top-level C source that defines main(), or nullptr.
static char *entryC(const char *project) {
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, "/*.c", &count, &ok);
    char *found = nullptr;
    for (size_t i = 0; ok && i < count && found == nullptr; ++i)
        if (Util_fileContains(sources[i], "int main("))
            found = Util_combine(sources[i], "");
    Util_freeSources(sources, count);
    return found;
}

const Adapter C_ADAPTER = { "c", ".c", buildC, runC,
    "CC=cc", "native build/exec; source instance rejects", entryC };
