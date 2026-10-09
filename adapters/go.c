#include "adapters/go.h"
#include "b.h"

;;DEFINITION
/* Go directory builds compile the selected package with go -C project build .;
 * native go.mod/go.work and Go configuration own dependency resolution. b never
 * creates a module or invokes go get. A single-file exec compiles that file only;
 * instance invokes go run. Toolchain selection/downloads and caches follow Go's
 * environment; use GOTOOLCHAIN=local/GOPROXY=off for offline work. Package builds
 * may produce archives for non-main packages; b never guesses they are programs.
 * Child status is preserved: go run reports program failure through its native
 * wrapper status (typically 1), whereas exec returns the program's exit code.
 */
;;OVERVIEW
/* MODULE: Go adapter. PUBLIC RECORD (go.h): GO_ADAPTER.
 * RECORD FIELDS: name="go", extension=".go", build=buildGo, run=runGo,
 * tools="GO=go", capabilities="package build; source instance; native exec".
 * PRIVATE STATIC: tool — select GO executable; compileGo — build explicit file
 * or selected package; buildGo — package directory; runGo — runtime/native entry.
 * b-owned artifacts stay outside sources. Child failures block old-artifact runs.
 */

#include <stdlib.h>

// Select the configured Go executable or the standard command name.
static const char *tool(void) {
    const char *value = getenv("GO");
    return value != nullptr && *value != '\0' ? value : "go";
}

// Build one Go file or the selected project package into b's external state.
static int compileGo(const char *project, const char *file, char **output) {
    char *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    char *artifact = Util_combine(directory, "/go-program");
    free(directory);
    char *arguments[] = { (char*) tool(), "-C", (char*) project, "build", "-o", artifact,
        (char*) (file != nullptr ? file : "."), nullptr };
    int status = Util_executeBuild(arguments);
    if (status == 0)
        *output = artifact;
    else
        free(artifact);
    return status;
}

// Build the selected Go package without creating or modifying module metadata.
static int buildGo(const char *project, char **output) {
    return compileGo(project, nullptr, output);
}

// Run a Go source file through go run or compile it before native execution.
static int runGo(const char *file, int argc, char **argv, bool buildArtifact) {
    char *path = realpath(file, nullptr);
    if (path == nullptr) {
        THROW("Go source cannot be resolved");
        return EXIT_FAILURE;
    }
    int status;
    if (buildArtifact) {
        char *project = Util_parentDirectory(path);
        char *output = nullptr;
        status = compileGo(project, path, &output);
        if (status == 0) {
            char *prefix[] = { output };
            status = Util_runCommand(prefix, 1, argc, argv);
        }
        free(output);
        free(project);
    } else {
        char *prefix[] = { (char*) tool(), "run", path };
        status = Util_runCommand(prefix, 3, argc, argv);
    }
    free(path);
    return status;
}

const Adapter GO_ADAPTER = { "go", ".go", buildGo, runGo,
    "GO=go", "package build; source instance; native exec" };
