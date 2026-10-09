#include "adapters/metal.h"
#include "b.h"

;;DEFINITION
/* Apple Metal sources compile through xcrun metal to AIR and then metallib.
 * One library per source avoids guessed cross-file linking policy. Each build
 * uses a fresh external generation, leaving previous successful outputs intact.
 * Requires installed Apple Metal tools; never downloads them or creates a GPU.
 * Non-Apple hosts reject. Tool/source/include paths remain caller-trusted.
 */
;;OVERVIEW
/* MODULE: Metal adapter; public record: METAL_ADAPTER (metal.h), name/extension/
 * build/run/tools/capabilities. PRIVATE: buildMetal — per-source AIR/library;
 * rejectRun — reject host execution. Caller owns successful output path.
 * Failed generation artifacts are removed; no persistent state or threads.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Compile top-level Metal sources to independent libraries using the macOS SDK.
static int buildMetal(const char *project, char **output) {
#ifndef __APPLE__
    (void) project; (void) output;
    THROW("Metal compilation requires Apple tools on macOS");
    return EXIT_FAILURE;
#else
    if (project == nullptr || output == nullptr) {
        THROW("invalid Metal build input");
        return EXIT_FAILURE;
    }
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, "/*.metal", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    char *base = Util_outputDirectory(project);
    char *directory = base == nullptr ? nullptr : Util_combine(base, "/metal-XXXXXX");
    free(base);
    if (directory == nullptr || mkdtemp(directory) == nullptr) {
        THROW("cannot create Metal output generation");
        free(directory);
        Util_freeSources(sources, count);
        return EXIT_FAILURE;
    }
    const char *tool = getenv("XCRUN");
    if (tool == nullptr || *tool == '\0')
        tool = "xcrun";
    int status = 0;
    for (size_t i = 0; i < count && status == 0; ++i) {
        struct stat info;
        if (lstat(sources[i], &info) != 0 || !S_ISREG(info.st_mode)) {
            THROW("Metal input must be a regular non-symlink file: %s", sources[i]);
            status = EXIT_FAILURE;
            break;
        }
        char *stem = Util_combine(directory, strrchr(sources[i], '/'));
        char *air = Util_combine(stem, ".air");
        char *library = Util_combine(stem, ".metallib");
        char *compile[] = { (char*) tool, "-sdk", "macosx", "metal", "-Werror",
            "-mmacosx-version-min=14.0", "-c", sources[i], "-o", air, nullptr };
        char *link[] = { (char*) tool, "-sdk", "macosx", "metallib", air, "-o", library, nullptr };
        status = Util_executeBuild(compile);
        if (status == 0)
            status = Util_executeBuild(link);
        if (status == 0 && (stat(library, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size == 0)) {
            THROW("Metal compiler produced no library");
            status = EXIT_FAILURE;
        }
        unlink(air);
        free(stem); free(air); free(library);
    }
    if (status == 0)
        *output = directory;
    else {
        for (size_t i = 0; i < count; ++i) {
            char *stem = Util_combine(directory, strrchr(sources[i], '/'));
            char *library = Util_combine(stem, ".metallib");
            unlink(library);
            free(library); free(stem);
        }
        rmdir(directory);
        free(directory);
    }
    Util_freeSources(sources, count);
    return status;
#endif
}

// A metallib requires a graphics consumer and cannot run as a CLI program.
static int rejectRun(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) file; (void) argc; (void) argv; (void) buildArtifact;
    THROW("Metal is build-only; use b build metal <directory>");
    return EXIT_FAILURE;
}
const Adapter METAL_ADAPTER = { "metal", ".metal", buildMetal, rejectRun,
    "XCRUN=xcrun", "macOS AIR/metallib build; no host run" };
