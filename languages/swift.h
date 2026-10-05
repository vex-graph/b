#pragma once
#include "annotation.h"
#include "languages/language.h"
;;DEFINITION
/* Swift source interpreter and native compiler adapter, using installed tools. */
;;OVERVIEW
/* PUBLIC RECORD: SWIFT_LANGUAGE {name="swift", extension=".swift",
 * build=buildSwift, run=runSwift}; implementation: swift.c. No owned state.
 */
extern const Language SWIFT_LANGUAGE;
