#include "vex.h"
#include "adapters/zig.h"
#include "b.h"

;;DEFINITION
/* Zig compiles one explicit source root with ReleaseSafe; sibling imports belong
 * to Zig. Both run modes compile because Zig has no source interpreter. Directory
 * build delegates existing build.zig when present, otherwise chooses main.zig
 * or exactly one .zig source. Native project outputs use an external --prefix;
 * project steps/dependency fetching remain build.zig policy, not b.sh's invention.
 * No executable is guessed for a project build. ZIG selects one executable.
 */
;;OVERVIEW
/* MODULE: Zig adapter. PUBLIC RECORD (zig.h): ZIG_ADAPTER.
 * RECORD FIELDS: name="zig", extension=".zig", build=buildZig, run=runZig,
 * tools="ZIG=zig", capabilities="source/project build; compile-then-run".
 * PRIVATE STATIC: tool, executable override; compileZig, native source root;
 * buildZig, build.zig delegation or unambiguous root; runZig, compile then run.
 * All temporary paths/source rows freed; no stale artifact launch after failure.
 */

#include <stdlib.h>
#include <sys/stat.h>

/** Select the configured Zig executable or the standard command name. */
static string tool() {
    string value = getenv("ZIG");
    return value != nullptr && *value != '\0' ? value : "zig";
}

/** Compile one Zig source root into an external ReleaseSafe executable. */
static i32 compileZig(string project, string file, utf8 **output) {
    utf8 *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    utf8 *artifact = Util_combine(directory, "/zig-program");
    free(directory);
    utf8 *emit = Util_combine("-femit-bin=", artifact);
    utf8 *arguments[] = { (utf8*) tool(), "build-exe", "-O", "ReleaseSafe", emit, (utf8*) file, nullptr, nullptr };
    i32 status = Util_executeBuild(arguments);
    free(emit);
    if (status == 0)
        *output = artifact;
    else
        free(artifact);
    return status;
}

/** Delegate to build.zig or select an unambiguous standalone source root. */
static i32 buildZig(string project, utf8 **output) {
    utf8 *script = Util_combine(project, "/build.zig");
    struct stat info;
    if (stat(script, &info) == 0 && S_ISREG(info.st_mode)) {
        utf8 *directory = Util_outputDirectory(project);
        if (directory == nullptr) {
            free(script);
            return EXIT_FAILURE;
        }
        utf8 *prefix = Util_combine(directory, "/zig-out");
        free(directory);
        utf8 *arguments[] = { (utf8*) tool(), "build", "--build-file", script, "--prefix", prefix, nullptr, nullptr };
        i32 status = Util_executeBuild(arguments);
        if (status == 0)
            *output = prefix;
        else
            free(prefix);
        free(script);
        return status;
    }
    free(script);
    utf8 *main = Util_combine(project, "/main.zig");
    if (stat(main, &info) == 0 && S_ISREG(info.st_mode)) {
        i32 status = compileZig(project, main, output);
        free(main);
        return status;
    }
    free(main);
    bytes count = 0;
    bool ok = false;
    utf8 **sources = Util_collectSources(project, "/*.zig", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    i32 status = EXIT_FAILURE;
    if (count == 1)
        status = compileZig(project, sources[0], output);
    else
        THROW("Zig directory requires build.zig, main.zig or exactly one source root");
    Util_freeSources(sources, count);
    return status;
}

/** Compile the resolved Zig source and run the resulting program with its arguments. */
static i32 runZig(string file, i32 argc, utf8 **argv, bool buildArtifact) {
    (discard) buildArtifact;
    utf8 *path = realpath(file, nullptr);
    if (path == nullptr) {
        THROW("Zig source cannot be resolved");
        return EXIT_FAILURE;
    }
    utf8 *project = Util_projectDirectory(path);
    utf8 *output = nullptr;
    i32 status = compileZig(project, path, &output);
    if (status == 0) {
        utf8 *prefix[] = { output };
        status = Util_runCommand(prefix, 1, argc, argv);
    }
    free(output);
    free(project);
    free(path);
    return status;
}

const Adapter ZIG_ADAPTER = { "zig", ".zig", buildZig, runZig,
    "ZIG=zig", "source/project build; compile-then-run", nullptr, nullptr };
