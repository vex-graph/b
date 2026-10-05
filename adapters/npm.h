#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* npm project-script orchestration. Existing package.json owns its commands. */
;;OVERVIEW
/* PUBLIC RECORD: NPM_ADAPTER {name="npm", extension="package.json",
 * build=buildNpm, run=runNpm}; implementation: npm.c. No owned state.
 */
extern const Adapter NPM_ADAPTER;
