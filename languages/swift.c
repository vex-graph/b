#include "languages/swift.h"
#include "b.h"

;;DEFINITION
/* Swift instance delegates a single script to swift; exec uses swiftc then
 * launches the native program only on success. Directory builds compile its
 * top-level .swift sources together, following Swift's main.swift conventions.
 * Apple builds target arm64/macOS 14. SwiftPM/package discovery is not guessed.
 */
;;OVERVIEW
/* MODULE: Swift adapter; PUBLIC RECORD: SWIFT_LANGUAGE (swift.h).
 * PRIVATE STATIC: compileSwift — compile explicit sources into external program;
 * buildSwift — discover directory sources; runSwift — interpreter or native exec.
 * RECORD FIELDS: name="swift", extension=".swift", build=buildSwift, run=runSwift.
 * Paths/argv are temporary, source strings borrowed; child exit status propagates.
 */

#include <stdint.h>
#include <stdlib.h>

static int compileSwift(const char *project, char **sources, size_t count, char **output) {
    if (count > SIZE_MAX / sizeof(char*) - 8) {
        THROW("Swift source count overflow");
        return EXIT_FAILURE;
    }
    char *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    *output = Util_combine(directory, "/swift-program");
    free(directory);
    char **arguments = Util_allocate((count + 8) * sizeof(char*));
    size_t n = 0;
    arguments[n++] = "swiftc";
    arguments[n++] = "-warnings-as-errors";
#ifdef __APPLE__
    arguments[n++] = "-target";
    arguments[n++] = "arm64-apple-macosx14.0";
#endif
    arguments[n++] = "-o";
    arguments[n++] = *output;
    for (size_t i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    int status = Util_executeBuild(arguments);
    free(arguments);
    return status;
}

static int buildSwift(const char *project, char **output) {
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, "/*.swift", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    int status = compileSwift(project, sources, count, output);
    Util_freeSources(sources, count);
    return status;
}

static int runSwift(const char *file, int argc, char **argv, bool buildArtifact) {
    if (!buildArtifact) {
        char *prefix[] = { "swift", (char*) file };
        return Util_runCommand(prefix, 2, argc, argv);
    }
    char *project = Util_parentDirectory(file);
    char *output = nullptr;
    char *sources[] = { (char*) file };
    int status = compileSwift(project, sources, 1, &output);
    if (status == 0) {
        char *prefix[] = { output };
        status = Util_runCommand(prefix, 1, argc, argv);
    }
    free(output);
    free(project);
    return status;
}

const Language SWIFT_LANGUAGE = { "swift", ".swift", buildSwift, runSwift };
