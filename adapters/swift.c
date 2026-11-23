#include "vex.h"
#include "adapters/swift.h"
#include "b.h"

;;DEFINITION
/* Swift instance delegates a single script to swift; exec uses swiftc then
 * launches the native program only on success. Directory builds compile its
 * top-level .swift sources together, following Swift's main.swift conventions.
 * Apple builds target arm64/macOS 14. SwiftPM/package discovery is not guessed.
 */
;;OVERVIEW
/* MODULE: Swift adapter; PUBLIC RECORD: SWIFT_ADAPTER (swift.h).
 * METADATA: tools="swift,swiftc"; capabilities="native build/exec; script instance".
 * PRIVATE STATIC: compileSwift, compile explicit sources into external program;
 * buildSwift, discover directory sources; runSwift, interpreter or native exec.
 * RECORD FIELDS: name="swift", extension=".swift", build=buildSwift, run=runSwift.
 * Paths/argv are temporary, source strings borrowed; child exit status propagates.
 */

#include <stdlib.h>

/** Compile the supplied Swift sources into an external native executable. */
static i32 compileSwift(const utf8 *project, utf8 **sources, bytes count, utf8 **output) {
    if (count > SIZE_MAX / sizeof(utf8*) - 8) {
        THROW("Swift source count overflow");
        return EXIT_FAILURE;
    }
    utf8 *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    *output = Util_combine(directory, "/swift-program");
    free(directory);
    utf8 **arguments = Util_allocate((count + 8) * sizeof(utf8*));
    bytes n = 0;
    arguments[n++] = "swiftc";
    arguments[n++] = "-warnings-as-errors";
#ifdef __APPLE__
    arguments[n++] = "-target";
    arguments[n++] = "arm64-apple-macosx14.0";
#endif
    arguments[n++] = "-o";
    arguments[n++] = *output;
    for (bytes i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    i32 status = Util_executeBuild(arguments);
    free(arguments);
    return status;
}

/** Discover top-level Swift sources and compile them together. */
static i32 buildSwift(const utf8 *project, utf8 **output) {
    bytes count = 0;
    bool ok = false;
    utf8 **sources = Util_collectSources(project, "/*.swift", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    i32 status = compileSwift(project, sources, count, output);
    Util_freeSources(sources, count);
    return status;
}

/** Interpret the Swift file or compile then launch it, forwarding program arguments. */
static i32 runSwift(const utf8 *file, i32 argc, utf8 **argv, bool buildArtifact) {
    if (!buildArtifact) {
        utf8 *prefix[] = { "swift", (utf8*) file };
        return Util_runCommand(prefix, 2, argc, argv);
    }
    utf8 *project = Util_projectDirectory(file);
    utf8 *output = nullptr;
    utf8 *sources[] = { (utf8*) file };
    i32 status = compileSwift(project, sources, 1, &output);
    if (status == 0) {
        utf8 *prefix[] = { output };
        status = Util_runCommand(prefix, 1, argc, argv);
    }
    free(output);
    free(project);
    return status;
}

const Adapter SWIFT_ADAPTER = { "swift", ".swift", buildSwift, runSwift,
    "swift,swiftc", "native build/exec; script instance", nullptr, nullptr };
