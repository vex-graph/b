#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* Swift source interpreter and native compiler adapter, using installed tools. */
;;OVERVIEW
/* PUBLIC RECORD: SWIFT_ADAPTER {name="swift", extension=".swift",
 * build=buildSwift, run=runSwift}; implementation: swift.c. No owned state.
 */
extern const Adapter SWIFT_ADAPTER;
