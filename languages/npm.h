#pragma once
#include "annotation.h"
#include "languages/language.h"
;;DEFINITION
/* npm project-script orchestration. Existing package.json owns its commands. */
;;OVERVIEW
/* PUBLIC RECORD: NPM_LANGUAGE {name="npm", extension="package.json",
 * build=buildNpm, run=runNpm}; implementation: npm.c. No owned state.
 */
extern const Language NPM_LANGUAGE;
