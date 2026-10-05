#pragma once
#include "annotation.h"
#include "languages/language.h"
;;DEFINITION
/* Objective-C Foundation compiler adapter; macOS-only in this version. */
;;OVERVIEW
/* PUBLIC RECORD: OBJC_LANGUAGE {name="objc", extension=".m",
 * build=buildObjc, run=runObjc}; implementation: objc.c. No owned state.
 */
extern const Language OBJC_LANGUAGE;
