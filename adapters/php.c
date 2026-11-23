#include "vex.h"
#include "adapters/php.h"
#include "b.h"

;;DEFINITION
/* PHP uses the installed CLI in both modes. Build checks each top-level .php
 * with php -l, without evaluating application code, and returns the checked
 * directory. Runtime includes/config/extensions retain PHP semantics; no Apache,
 * PHP-FPM, Composer install or development server is started automatically.
 */
;;OVERVIEW
/* MODULE: PHP adapter; PUBLIC RECORD: PHP_ADAPTER (php.h).
 * METADATA: tools="php"; capabilities="parse-check; instance/exec CLI runtime".
 * PRIVATE STATIC: buildPhp, independent parse checks; runPhp, CLI execution.
 * RECORD FIELDS: name="php", extension=".php", build=buildPhp, run=runPhp.
 * Arguments are borrowed, checker argv temporary, child status preserved.
 */

/** Lint each top-level PHP source without executing application code. */
static i32 buildPhp(string project, utf8 **output) {
    utf8 *prefix[] = { "php", "-l" };
    return Util_checkSources(project, "/*.php", prefix, 2, output);
}

/** Execute a PHP source file through the CLI and forward its arguments. */
static i32 runPhp(string file, i32 argc, utf8 **argv, bool buildArtifact) {
    (discard) buildArtifact;
    utf8 *prefix[] = { "php", (utf8*) file };
    return Util_runCommand(prefix, 2, argc, argv);
}

const Adapter PHP_ADAPTER = { "php", ".php", buildPhp, runPhp,
    "php", "parse-check; instance/exec CLI runtime", nullptr, nullptr };
