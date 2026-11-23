#include "vex.h"
#include "adapters/cargo.h"
#include "b.h"
#include "b/b_str.h"

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
 * PRIVATE STATIC: tool, CARGO override; manifest, require regular Cargo.toml;
 * buildCargo, offline directory build; runCargo, literal manifest/name validation
 * and offline native run, with program arguments forwarded after Cargo's --.
 * Temporary paths freed; target directory persists; child status preserved.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/** Select the configured Cargo executable or the standard command name. */
static string tool() {
    string value = getenv("CARGO");
    return value != nullptr && *value != '\0' ? value : "cargo";
}

/** Return the project's regular Cargo manifest, reporting its absence otherwise. */
static utf8 *manifest(string project) {
    utf8 *path = Util_combine(project, "/Cargo.toml");
    struct stat info;
    if (stat(path, &info) == 0 && S_ISREG(info.st_mode))
        return path;
    THROW("Cargo requires a regular Cargo.toml in the selected project");
    free(path);
    return nullptr;
}

/** Build the existing Cargo package offline into b.sh's external target directory. */
static i32 buildCargo(string project, utf8 **output) {
    utf8 *path = manifest(project);
    if (path == nullptr)
        return EXIT_FAILURE;
    utf8 *directory = Util_outputDirectory(project);
    if (directory == nullptr) {
        free(path);
        return EXIT_FAILURE;
    }
    utf8 *target = Util_combine(directory, "/cargo-target");
    free(directory);
    utf8 *arguments[] = { (utf8*) tool(), "build", "--offline", "--manifest-path", path,
        "--target-dir", target, nullptr, nullptr };
    i32 status = Util_executeBuild(arguments);
    if (status == 0)
        *output = target;
    else
        free(target);
    free(path);
    return status;
}

/** Validate a Cargo.toml input and run its package offline, forwarding program args. */
static i32 runCargo(string file, i32 argc, utf8 **argv, bool buildArtifact) {
    utf8 *path = realpath(file, nullptr);
    if (path == nullptr) {
        THROW("Cargo manifest cannot be resolved");
        return EXIT_FAILURE;
    }
    string name = strrchr(path, '/');
    if (name == nullptr || !bStrEq(name + 1, "Cargo.toml")) {
        THROW("Cargo run requires the project's Cargo.toml file");
        free(path);
        return EXIT_FAILURE;
    }
    utf8 *parent = Util_projectDirectory(path);
    utf8 *project = realpath(parent, nullptr);
    free(parent);
    if (project == nullptr) {
        THROW("Cargo project cannot be resolved");
        free(path);
        return EXIT_FAILURE;
    }
    utf8 *target = nullptr;
    i32 status = 0;
    if (buildArtifact)
        status = buildCargo(project, &target);
    else {
        utf8 *directory = Util_outputDirectory(project);
        if (directory != nullptr) {
            target = Util_combine(directory, "/cargo-target");
            free(directory);
        } else
            status = EXIT_FAILURE;
    }
    if (status == 0) {
        utf8 *prefix[] = { (utf8*) tool(), "run", "--offline", "--manifest-path", path,
            "--target-dir", target, "--" };
        status = Util_runCommand(prefix, 8, argc, argv);
    }
    free(target);
    free(project);
    free(path);
    return status;
}

const Adapter CARGO_ADAPTER = { "cargo", "Cargo.toml", buildCargo, runCargo,
    "CARGO=cargo,rustc", "offline project build/run", nullptr, nullptr };
