// MODULE: R adapter — Rscript execution and parse-only builds; no owned class.
// DEFINITION: Uses the installed Rscript with --vanilla for reproducible startup.
// Both run modes execute source; R has no standalone executable compilation here.
// Build parses each top-level .R/.r file without evaluating it and returns the
// checked directory, not a fabricated binary. Package installation is not owned.
// OVERVIEW: buildR; runR; R_LANGUAGE and R_LOWER_LANGUAGE (.R/.r aliases).
#include "languages/r.h"
#include "b.h"

;;DEFINITION
/* Rscript --vanilla supplies a clean startup without saved workspaces/profiles.
 * Both modes execute source; build parses each top-level .R/.r without evaluating
 * it. The build result is the checked directory, not a pretend executable.
 * Programs retain their arguments and exit status; package installation stays
 * outside this adapter. Both extension aliases share the same callbacks.
 */
;;OVERVIEW
/* MODULE: R adapter; exported records: R_LANGUAGE/R_LOWER_LANGUAGE (r.h).
 * PRIVATE STATIC: buildR — parse-only directory validation; runR — Rscript run.
 * RECORD FIELDS: R_LANGUAGE {name="r", extension=".R", build=buildR, run=runR};
 * R_LOWER_LANGUAGE {name="R", extension=".r", build=buildR, run=runR}.
 * OWNERSHIP: temporary argv/source lists are freed; records remain static.
 */

#include <stdlib.h>

static int buildR(const char *project, char **output) {
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, "/*.[Rr]", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    char **arguments = Util_allocate((count + 6) * sizeof(char*));
    size_t n = 0;
    arguments[n++] = "Rscript";
    arguments[n++] = "--vanilla";
    arguments[n++] = "-e";
    arguments[n++] = "for (f in commandArgs(trailingOnly=TRUE)) invisible(parse(file=f))";
    for (size_t i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    int status = Util_executeBuild(arguments);
    if (status == 0)
        *output = Util_combine(project, "");
    free(arguments);
    Util_freeSources(sources, count);
    return status;
}

static int runR(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) buildArtifact; // Interpreter execution in both modes; no packaging.
    char **arguments = Util_allocate(((size_t) argc + 4) * sizeof(char*));
    arguments[0] = "Rscript";
    arguments[1] = "--vanilla";
    arguments[2] = (char*) file;
    for (int i = 0; i < argc; ++i)
        arguments[i + 3] = argv[i];
    int status = Util_execute(arguments);
    free(arguments);
    return status;
}

const Language R_LANGUAGE = { "r", ".R", buildR, runR };
const Language R_LOWER_LANGUAGE = { "R", ".r", buildR, runR };
