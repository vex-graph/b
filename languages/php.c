#include "languages/php.h"
#include "b.h"

;;DEFINITION
/* PHP uses the installed CLI in both modes. Build checks each top-level .php
 * with php -l, without evaluating application code, and returns the checked
 * directory. Runtime includes/config/extensions retain PHP semantics; no Apache,
 * PHP-FPM, Composer install or development server is started automatically.
 */
;;OVERVIEW
/* MODULE: PHP adapter; PUBLIC RECORD: PHP_LANGUAGE (php.h).
 * PRIVATE STATIC: buildPhp — independent parse checks; runPhp — CLI execution.
 * RECORD FIELDS: name="php", extension=".php", build=buildPhp, run=runPhp.
 * Arguments are borrowed, checker argv temporary, child status preserved.
 */

static int buildPhp(const char *project, char **output) {
    char *prefix[] = { "php", "-l" };
    return Util_checkSources(project, "/*.php", prefix, 2, output);
}

static int runPhp(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) buildArtifact;
    char *prefix[] = { "php", (char*) file };
    return Util_runCommand(prefix, 2, argc, argv);
}

const Language PHP_LANGUAGE = { "php", ".php", buildPhp, runPhp };
