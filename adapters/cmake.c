#include "adapters/cmake.h"
#include "b.h"

;;DEFINITION
/* b orchestrates an installed CMake rather than translating its project.
 * Configuration uses the original source tree and an external build directory;
 * CMake owns targets, dependencies, flags, generators and incremental work.
 * Only a successful configure proceeds to build. The returned path is a build
 * directory, not a guessed executable. Installation, tests and IDE generation
 * are not implicitly run. The caller runs its chosen built program explicitly.
 */
;;OVERVIEW
/* MODULE: CMake build backend; exported record: CMAKE_ADAPTER (cmake.h).
 * METADATA: tools="cmake"; capabilities="project configure/build; no source run".
 * PRIVATE STATIC: buildCmake — require CMakeLists.txt, configure, build;
 * runCmake — reject source execution and explain artifact selection.
 * RECORD FIELDS: name="cmake"; extension=nullptr (no filename dispatch);
 * build=buildCmake; run=runCmake.
 * Temporary paths are freed; CMake's external build state persists for reuse.
 */

#include <stdlib.h>
#include <sys/stat.h>

// Configure and build a declared CMake project in b's external build directory.
static int buildCmake(const char *project, char **output) {
    char *manifest = Util_combine(project, "/CMakeLists.txt");
    struct stat info;
    bool valid = stat(manifest, &info) == 0 && S_ISREG(info.st_mode);
    free(manifest);
    if (!valid) {
        THROW("CMake build requires CMakeLists.txt in the selected directory");
        return EXIT_FAILURE;
    }
    char *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    *output = Util_combine(directory, "/cmake");
    free(directory);
    char *configure[] = { "cmake", "-S", (char*) project, "-B", *output, nullptr };
    int status = Util_executeBuild(configure);
    if (status == 0) {
        char *build[] = { "cmake", "--build", *output, nullptr };
        status = Util_executeBuild(build);
    }
    return status;
}

// Reject source execution because CMake does not identify a unique runnable target.
static int runCmake(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) file;
    (void) argc;
    (void) argv;
    (void) buildArtifact;
    THROW("CMake builds projects; use b build cmake <directory>, then run the chosen artifact");
    return EXIT_FAILURE;
}

const Adapter CMAKE_ADAPTER = { "cmake", nullptr, buildCmake, runCmake,
    "cmake", "project configure/build; no source run" };
