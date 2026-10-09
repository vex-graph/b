#include "adapters/objc.h"
#include "b.h"

;;DEFINITION
/* Objective-C uses clang, ARC and Foundation on Apple Silicon/macOS 14+.
 * Exec builds a source entry; directory build compiles top-level .m files.
 * Instance rejects: Objective-C has no source runtime here. This adapter does
 * not construct an Xcode project or infer extra frameworks/libraries. Non-Apple
 * hosts reject explicitly rather than claiming an untested GNUstep contract.
 */
;;OVERVIEW
/* MODULE: Objective-C adapter; PUBLIC RECORD: OBJC_ADAPTER (objc.h).
 * METADATA: tools="CC=clang";
 * capabilities="macOS Foundation build/exec; source instance rejects".
 * PRIVATE STATIC: compileObjc — strict ARC/Foundation compilation;
 * buildObjc — directory source discovery; runObjc — compiled-only execution.
 * RECORD FIELDS: name="objc", extension=".m", build=buildObjc, run=runObjc.
 * Temporary paths/arguments are freed; failed compilation never launches output.
 */

#include <stdint.h>
#include <stdlib.h>

// Compile Objective-C sources with ARC/Foundation on the supported Apple host.
static int compileObjc(const char *project, char **sources, size_t count, char **output) {
#ifndef __APPLE__
    (void) project;
    (void) sources;
    (void) count;
    (void) output;
    THROW("Objective-C Foundation adapter currently requires macOS");
    return EXIT_FAILURE;
#else
    if (count > SIZE_MAX / sizeof(char*) - 20) {
        THROW("Objective-C source count overflow");
        return EXIT_FAILURE;
    }
    char *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    *output = Util_combine(directory, "/objc-program");
    free(directory);
    char **arguments = Util_allocate((count + 20) * sizeof(char*));
    size_t n = 0;
    const char *compiler = getenv("CC");
    arguments[n++] = (char*) (compiler != nullptr && *compiler ? compiler : "clang");
    arguments[n++] = "-x";
    arguments[n++] = "objective-c";
    arguments[n++] = "-fobjc-arc";
    arguments[n++] = "-Wall";
    arguments[n++] = "-Wextra";
    arguments[n++] = "-Werror";
    arguments[n++] = "-arch";
    arguments[n++] = "arm64";
    arguments[n++] = "-mcpu=apple-m1";
    arguments[n++] = "-mmacosx-version-min=14.0";
    arguments[n++] = "-framework";
    arguments[n++] = "Foundation";
    arguments[n++] = "-I";
    arguments[n++] = (char*) project;
    arguments[n++] = "-o";
    arguments[n++] = *output;
    for (size_t i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    int status = Util_executeBuild(arguments);
    free(arguments);
    return status;
#endif
}

// Discover top-level Objective-C files and compile them into one program.
static int buildObjc(const char *project, char **output) {
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, "/*.m", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    int status = compileObjc(project, sources, count, output);
    Util_freeSources(sources, count);
    return status;
}

// Compile one Objective-C source for exec mode and launch it with caller arguments.
static int runObjc(const char *file, int argc, char **argv, bool buildArtifact) {
    if (!buildArtifact) {
        THROW("Objective-C has no source runtime; use b run exec");
        return EXIT_FAILURE;
    }
    char *project = Util_parentDirectory(file);
    char *output = nullptr;
    char *sources[] = { (char*) file };
    int status = compileObjc(project, sources, 1, &output);
    if (status == 0) {
        char *prefix[] = { output };
        status = Util_runCommand(prefix, 1, argc, argv);
    }
    free(output);
    free(project);
    return status;
}

const Adapter OBJC_ADAPTER = { "objc", ".m", buildObjc, runObjc,
    "CC=clang", "macOS Foundation build/exec; source instance rejects" };
