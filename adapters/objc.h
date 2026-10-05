#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* Objective-C Foundation compiler adapter; macOS-only in this version. */
;;OVERVIEW
/* PUBLIC RECORD: OBJC_ADAPTER {name="objc", extension=".m",
 * build=buildObjc, run=runObjc}; implementation: objc.c. No owned state.
 */
extern const Adapter OBJC_ADAPTER;
