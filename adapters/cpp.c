#include "vex.h"
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
 * PRIVATE STATIC: compileCpp, compile explicit sources; buildCpp, discover
 * directory sources; runCpp, compile-before-launch, reject instance.
 * Temporary paths/source lists/argv freed; native child status preserved.
 */

#include <stdlib.h>

/** Compile the provided C++23 sources into one external native executable. */
static i32 compileCpp(const utf8 *project, utf8 **sources, bytes count, utf8 **output) {
    if (count > SIZE_MAX / sizeof(utf8*) - 16) {
        THROW("C++ source count overflow");
        return EXIT_FAILURE;
    }
    utf8 *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    *output = Util_combine(directory, "/cpp-program");
    free(directory);
    utf8 **arguments = Util_allocate((count + 16) * sizeof(utf8*));
    bytes n = 0;
    const utf8 *compiler = getenv("CXX");
    arguments[n++] = (utf8*) (compiler != nullptr && *compiler ? compiler : "c++");
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
    arguments[n++] = (utf8*) project;
    arguments[n++] = "-o";
    arguments[n++] = *output;
    for (bytes i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    i32 status = Util_executeBuild(arguments);
    free(arguments);
    return status;
}

/** Collect supported top-level C++ extensions and compile the project together. */
static i32 buildCpp(const utf8 *project, utf8 **output) {
    bytes count = 0;
    bool ok = false;
    utf8 **sources = Util_collectSources(project, "/*.{cpp,cc,cxx}", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    i32 status = compileCpp(project, sources, count, output);
    Util_freeSources(sources, count);
    return status;
}

/** Compile one source entry for exec mode, then launch with forwarded arguments. */
static i32 runCpp(const utf8 *file, i32 argc, utf8 **argv, bool buildArtifact) {
    if (!buildArtifact) {
        THROW("C++ has no source runtime; use b.sh run exec");
        return EXIT_FAILURE;
    }
    utf8 *project = Util_projectDirectory(file);
    utf8 *output = nullptr;
    utf8 *sources[] = { (utf8*) file };
    i32 status = compileCpp(project, sources, 1, &output);
    if (status == 0) {
        utf8 *prefix[] = { output };
        status = Util_runCommand(prefix, 1, argc, argv);
    }
    free(output);
    free(project);
    return status;
}

const Adapter CPP_ADAPTER = { "cpp", ".cpp", buildCpp, runCpp,
    "CXX=c++", "native build/exec; source instance rejects", nullptr, nullptr };
const Adapter CXX_ADAPTER = { "cxx", ".cc", buildCpp, runCpp,
    "CXX=c++", "native build/exec; source instance rejects", nullptr, nullptr };
const Adapter CPP_LONG_ADAPTER = { "c++", ".cxx", buildCpp, runCpp,
    "CXX=c++", "native build/exec; source instance rejects", nullptr, nullptr };
