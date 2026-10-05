#include "adapters/cpp.h"
#include "b.h"

;;DEFINITION
/* C++23 uses CXX or c++ to compile native programs with strict warnings. Exec
 * compiles one file; directory build collects top-level .cpp/.cc/.cxx sources.
 * Instance rejects because no C++ source runtime exists.
 * Library/project flags are not guessed; use the CMake backend for full projects.
 */
;;OVERVIEW
/* MODULE: C++ adapter; PUBLIC RECORDS (cpp.h): CPP_ADAPTER {cpp/.cpp},
 * CXX_ADAPTER {cxx/.cc}, CPP_LONG_ADAPTER {c++/.cxx}; build=buildCpp, run=runCpp.
 * METADATA (all records): tools="CXX=c++";
 * capabilities="native build/exec; source instance rejects".
 * PRIVATE STATIC: compileCpp — compile explicit sources; buildCpp — discover
 * directory sources; runCpp — compile-before-launch, reject instance.
 * Temporary paths/source lists/argv freed; native child status preserved.
 */

#include <stdint.h>
#include <stdlib.h>

static int compileCpp(const char *project, char **sources, size_t count, char **output) {
    if (count > SIZE_MAX / sizeof(char*) - 16) {
        THROW("C++ source count overflow");
        return EXIT_FAILURE;
    }
    char *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    *output = Util_combine(directory, "/cpp-program");
    free(directory);
    char **arguments = Util_allocate((count + 16) * sizeof(char*));
    size_t n = 0;
    const char *compiler = getenv("CXX");
    arguments[n++] = (char*) (compiler != nullptr && *compiler ? compiler : "c++");
    arguments[n++] = "-std=c++23";
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
    for (size_t i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    int status = Util_executeBuild(arguments);
    free(arguments);
    return status;
}

static int buildCpp(const char *project, char **output) {
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, "/*.{cpp,cc,cxx}", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    int status = compileCpp(project, sources, count, output);
    Util_freeSources(sources, count);
    return status;
}

static int runCpp(const char *file, int argc, char **argv, bool buildArtifact) {
    if (!buildArtifact) {
        THROW("C++ has no source runtime; use b run exec");
        return EXIT_FAILURE;
    }
    char *project = Util_parentDirectory(file);
    char *output = nullptr;
    char *sources[] = { (char*) file };
    int status = compileCpp(project, sources, 1, &output);
    if (status == 0) {
        char *prefix[] = { output };
        status = Util_runCommand(prefix, 1, argc, argv);
    }
    free(output);
    free(project);
    return status;
}

const Adapter CPP_ADAPTER = { "cpp", ".cpp", buildCpp, runCpp,
    "CXX=c++", "native build/exec; source instance rejects" };
const Adapter CXX_ADAPTER = { "cxx", ".cc", buildCpp, runCpp,
    "CXX=c++", "native build/exec; source instance rejects" };
const Adapter CPP_LONG_ADAPTER = { "c++", ".cxx", buildCpp, runCpp,
    "CXX=c++", "native build/exec; source instance rejects" };
