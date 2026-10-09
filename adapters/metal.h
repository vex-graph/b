// Metal build-only adapter: Apple tools emit a metallib, never host execution.
#pragma once
#include "adapters/adapter.h"
;;DEFINITION
/* Build-only Apple Metal adapter. Borrowed source directory, caller-owned output
 * directory on success, unchanged output on failure. Requires installed tools.
 */
;;OVERVIEW
/* MODULE: METAL_ADAPTER record: name, extension, build, run, tools, capabilities.
 * XCRUN overrides one executable. Both run modes reject; non-Apple builds reject.
 */
extern const Adapter METAL_ADAPTER;
