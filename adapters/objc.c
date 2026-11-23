#include "vex.h"
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
 * PRIVATE STATIC: compileObjc, strict ARC/Foundation compilation;
 * buildObjc, directory source discovery; runObjc, compiled-only execution.
 * RECORD FIELDS: name="objc", extension=".m", build=buildObjc, run=runObjc.
 * Temporary paths/arguments are freed; failed compilation never launches output.
 */

#include <stdlib.h>

/** Compile Objective-C sources with ARC/Foundation on the supported Apple host. */
static i32 compileObjc(string project, utf8 **sources, bytes count, utf8 **output) {
#ifndef __APPLE__
    (discard) project;
    (discard) sources;
    (discard) count;
    (discard) output;
    THROW("Objective-C Foundation adapter currently requires macOS");
    return EXIT_FAILURE;
#else
    if (count > SIZE_MAX / sizeof(utf8*) - 20) {
        THROW("Objective-C source count overflow");
        return EXIT_FAILURE;
    }
    utf8 *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    *output = Util_combine(directory, "/objc-program");
    free(directory);
    utf8 **arguments = Util_allocate((count + 20) * sizeof(utf8*));
    bytes n = 0;
    string compiler = getenv("CC");
    arguments[n++] = (utf8*) (compiler != nullptr && *compiler ? compiler : "clang");
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
    arguments[n++] = (utf8*) project;
    arguments[n++] = "-o";
    arguments[n++] = *output;
    for (bytes i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    i32 status = Util_executeBuild(arguments);
    free(arguments);
    return status;
#endif
}

/** Discover top-level Objective-C files and compile them into one program. */
static i32 buildObjc(string project, utf8 **output) {
    bytes count = 0;
    bool ok = false;
    utf8 **sources = Util_collectSources(project, "/*.m", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    i32 status = compileObjc(project, sources, count, output);
    Util_freeSources(sources, count);
    return status;
}

/** Compile one Objective-C source for exec mode and launch it with caller arguments. */
static i32 runObjc(string file, i32 argc, utf8 **argv, bool buildArtifact) {
    if (!buildArtifact) {
        THROW("Objective-C has no source runtime; use b.sh run exec");
        return EXIT_FAILURE;
    }
    utf8 *project = Util_projectDirectory(file);
    utf8 *output = nullptr;
    utf8 *sources[] = { (utf8*) file };
    i32 status = compileObjc(project, sources, 1, &output);
    if (status == 0) {
        utf8 *prefix[] = { output };
        status = Util_runCommand(prefix, 1, argc, argv);
    }
    free(output);
    free(project);
    return status;
}

const Adapter OBJC_ADAPTER = { "objc", ".m", buildObjc, runObjc,
    "CC=clang", "macOS Foundation build/exec; source instance rejects", nullptr, nullptr };
