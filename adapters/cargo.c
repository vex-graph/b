#include "adapters/cargo.h"
#include "b.h"

;;DEFINITION
/* Existing Cargo.toml owns the package graph and native target selection. Build
 * delegates cargo build --offline with an external target directory. Both run
 * modes use cargo run --offline (which may compile); exec builds first. Multiple
 * binaries without a native default-run reject rather than guessing. No cargo
 * init/add/install, dependency fetch or executable-layout inference is performed.
 * Cargo may write its lockfile and build scripts retain native trusted semantics.
 */
;;OVERVIEW
/* MODULE: Cargo adapter. PUBLIC RECORD (cargo.h): CARGO_ADAPTER.
 * RECORD FIELDS: name="cargo", extension="Cargo.toml", build=buildCargo,
 * run=runCargo, tools="CARGO=cargo,rustc", capabilities="offline project build/run".
 * PRIVATE STATIC: tool — CARGO override; manifest — require regular Cargo.toml;
 * buildCargo — offline directory build; runCargo — literal manifest/name validation
 * and offline native run, with program arguments forwarded after Cargo's --.
 * Temporary paths freed; target directory persists; child status preserved.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *tool(void) {
    const char *value = getenv("CARGO");
    return value != nullptr && *value != '\0' ? value : "cargo";
}

static char *manifest(const char *project) {
    char *path = Util_combine(project, "/Cargo.toml");
    struct stat info;
    if (stat(path, &info) == 0 && S_ISREG(info.st_mode))
        return path;
    THROW("Cargo requires a regular Cargo.toml in the selected project");
    free(path);
    return nullptr;
}

static int buildCargo(const char *project, char **output) {
    char *path = manifest(project);
    if (path == nullptr)
        return EXIT_FAILURE;
    char *directory = Util_outputDirectory(project);
    if (directory == nullptr) {
        free(path);
        return EXIT_FAILURE;
    }
    char *target = Util_combine(directory, "/cargo-target");
    free(directory);
    char *arguments[] = { (char*) tool(), "build", "--offline", "--manifest-path", path,
        "--target-dir", target, nullptr };
    int status = Util_executeBuild(arguments);
    if (status == 0)
        *output = target;
    else
        free(target);
    free(path);
    return status;
}

static int runCargo(const char *file, int argc, char **argv, bool buildArtifact) {
    char *path = realpath(file, nullptr);
    if (path == nullptr) {
        THROW("Cargo manifest cannot be resolved");
        return EXIT_FAILURE;
    }
    const char *name = strrchr(path, '/');
    if (name == nullptr || strcmp(name + 1, "Cargo.toml") != 0) {
        THROW("Cargo run requires the project's Cargo.toml file");
        free(path);
        return EXIT_FAILURE;
    }
    char *parent = Util_parentDirectory(path);
    char *project = realpath(parent, nullptr);
    free(parent);
    if (project == nullptr) {
        THROW("Cargo project cannot be resolved");
        free(path);
        return EXIT_FAILURE;
    }
    char *target = nullptr;
    int status = 0;
    if (buildArtifact)
        status = buildCargo(project, &target);
    else {
        char *directory = Util_outputDirectory(project);
        if (directory != nullptr) {
            target = Util_combine(directory, "/cargo-target");
            free(directory);
        } else
            status = EXIT_FAILURE;
    }
    if (status == 0) {
        char *prefix[] = { (char*) tool(), "run", "--offline", "--manifest-path", path,
            "--target-dir", target, "--" };
        status = Util_runCommand(prefix, 8, argc, argv);
    }
    free(target);
    free(project);
    free(path);
    return status;
}

const Adapter CARGO_ADAPTER = { "cargo", "Cargo.toml", buildCargo, runCargo,
    "CARGO=cargo,rustc", "offline project build/run" };
