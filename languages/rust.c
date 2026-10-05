// CLASS: Rust language adapter.
// DEFINITION: Compiles a directory's single crate root with rustc (edition
// 2021) into one native executable; `run exec` compiles a single file on demand.
// Rust has no source runtime, so `run instance` rejects. Requires rustc on PATH;
// b does not download it. Cargo projects are future work.
#include "languages/rust.h"
#include "b.h"

;;DEFINITION
/* Rust uses one rustc crate root, never treats every module as a separate root.
 * A directory chooses main.rs, otherwise its sole .rs file; ambiguous roots
 * reject before compilation. Exec builds then launches; instance rejects.
 * Modules are resolved by Rust's mod declarations. Output is an out-of-tree
 * native program; Cargo discovery is not implemented.
 */
;;OVERVIEW
/* MODULE: Rust adapter; exported record: RUST_LANGUAGE (languages/rust.h).
 * PRIVATE STATIC: compileRust — rustc edition-2021 compilation;
 * buildRust — choose directory crate root and compile;
 * runRust — reject instance or compile then execute with program args.
 * RECORD FIELDS: name="rust"; extension=".rs"; build=buildRust; run=runRust.
 * OWNERSHIP: temporary paths/source lists/argv are released after child status.
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static int compileRust(const char *project, char **sources, size_t count, char **output) {
    if (count > SIZE_MAX / sizeof(char*) - 8) {
        THROW("source argument count overflow");
        return EXIT_FAILURE;
    }
    char *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    *output = Util_combine(directory, "/program");
    char **arguments = Util_allocate((count + 8) * sizeof(char*));
    size_t n = 0;
    arguments[n++] = "rustc";
    arguments[n++] = "--edition=2021";
    for (size_t i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    arguments[n++] = "-o";
    arguments[n++] = *output;
    int status = Util_execute(arguments);
    free(arguments);
    free(directory);
    return status;
}

static int buildRust(const char *project, char **output) {
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, "/*.rs", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    // rustc accepts one crate root; sibling modules are discovered by `mod`.
    char *mainFile = Util_combine(project, "/main.rs");
    char *root = access(mainFile, R_OK) == 0 ? mainFile : count == 1 ? sources[0] : nullptr;
    int status = EXIT_FAILURE;
    if (root == nullptr)
        THROW("multiple Rust files need a main.rs crate root; Cargo projects are not supported yet");
    else
        status = compileRust(project, &root, 1, output);
    free(mainFile);
    Util_freeSources(sources, count);
    return status;
}

static int runRust(const char *file, int argc, char **argv, bool buildArtifact) {
    if (!buildArtifact) {
        THROW("Rust has no source runtime; use b run exec to compile then launch");
        return EXIT_FAILURE;
    }
    char *project = Util_parentDirectory(file);
    char *output = nullptr;
    char *sources[1] = { (char*) file };
    int status = compileRust(project, sources, 1, &output);
    if (status == 0) {
        char **arguments = Util_allocate(((size_t) argc + 2) * sizeof(char*));
        arguments[0] = output;
        for (int i = 0; i < argc; ++i)
            arguments[i + 1] = argv[i];
        status = Util_execute(arguments);
        free(arguments);
    }
    free(output);
    free(project);
    return status;
}

const Language RUST_LANGUAGE = { "rust", ".rs", buildRust, runRust };
