#pragma once
#include "annotation.h"
#include "languages/language.h"
;;DEFINITION
/* Installed PHP CLI script execution and parse-only checks, not a web server. */
;;OVERVIEW
/* PUBLIC RECORD: PHP_LANGUAGE {name="php", extension=".php",
 * build=buildPhp, run=runPhp}; implementation: php.c. No owned state.
 */
extern const Language PHP_LANGUAGE;
