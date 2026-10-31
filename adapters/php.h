#pragma once
#include "../util/annotation.h"
#include "adapter.h"
;;DEFINITION
/* Installed PHP CLI script execution and parse-only checks, not a web server. */
;;OVERVIEW
/* PUBLIC RECORD: PHP_ADAPTER {name="php", extension=".php",
 * build=buildPhp, run=runPhp}; implementation: php.c. No owned state.
 */
extern const Adapter PHP_ADAPTER;
