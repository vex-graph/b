/**
 * MODULE: R adapter, Rscript execution and parse-only builds; no owned class.
 * DEFINITION: Uses the installed Rscript with --vanilla for reproducible startup.
 * Both run modes execute source; R has no standalone executable compilation here.
 * Build parses each top-level .R/.r file without evaluating it and returns the
 * checked directory, not a fabricated binary. Package installation is not owned.
 * OVERVIEW: buildR; runR; R_ADAPTER and R_LOWER_ADAPTER (.R/.r aliases).
 */

#include "vex.h"
#include "adapters/r.h"
#include "b.h"

;;DEFINITION
/* Rscript --vanilla supplies a clean startup without saved workspaces/profiles.
 * Both modes execute source; build parses each top-level .R/.r without evaluating
 * it. The build result is the checked directory, not a pretend executable.
 * Programs retain their arguments and exit status; package installation stays
 * outside this adapter. Both extension aliases share the same callbacks.
 */
;;OVERVIEW
/* MODULE: R adapter; exported records: R_ADAPTER/R_LOWER_ADAPTER (r.h).
 * PRIVATE STATIC: buildR, parse-only directory validation; runR, Rscript run.
 * RECORD FIELDS: R_ADAPTER {name="r", extension=".R", build=buildR, run=runR};
 * R_LOWER_ADAPTER {name="R", extension=".r", build=buildR, run=runR}.
 * METADATA (both): tools="Rscript"; capabilities="parse-check; instance/exec runtime".
 * OWNERSHIP: temporary argv/source lists are freed; records remain static.
 */

#include <stdlib.h>

/** Parse every top-level R source without evaluating it. */
static i32 buildR(string project, utf8 **output) {
    bytes count = 0;
    bool ok = false;
    utf8 **sources = Util_collectSources(project, "/*.[Rr]", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    utf8 **arguments = Util_allocate((count + 6) * sizeof(utf8*));
    bytes n = 0;
    arguments[n++] = "Rscript";
    arguments[n++] = "--vanilla";
    arguments[n++] = "-e";
    arguments[n++] = "for (f in commandArgs(trailingOnly=TRUE)) invisible(parse(file=f))";
    for (bytes i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    i32 status = Util_executeBuild(arguments);
    if (status == 0)
        *output = Util_combine(project, "");
    free(arguments);
    Util_freeSources(sources, count);
    return status;
}

/** Run an R script with a clean startup and forward its trailing arguments. */
static i32 runR(string file, i32 argc, utf8 **argv, bool buildArtifact) {
    (discard) buildArtifact;
    utf8 **arguments = Util_allocate(((bytes) argc + 4) * sizeof(utf8*));
    arguments[0] = "Rscript";
    arguments[1] = "--vanilla";
    arguments[2] = (utf8*) file;
    for (i32 i = 0; i < argc; ++i)
        arguments[i + 3] = argv[i];
    i32 status = Util_execute(arguments);
    free(arguments);
    return status;
}

const Adapter R_ADAPTER = { "r", ".R", buildR, runR,
    "Rscript", "parse-check; instance/exec runtime", nullptr, nullptr };
const Adapter R_LOWER_ADAPTER = { "R", ".r", buildR, runR,
    "Rscript", "parse-check; instance/exec runtime", nullptr, nullptr };
