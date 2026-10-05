#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* Node.js script/check adapter records. Node owns modules and runtime semantics. */
;;OVERVIEW
/* PUBLIC RECORDS: JAVASCRIPT_ADAPTER (javascript/.js), NODE_ADAPTER (node/.mjs),
 * JS_ADAPTER (js/.cjs); each build=buildJavascript, run=runJavascript.
 * No owned state; implementation: javascript.c.
 */
extern const Adapter JAVASCRIPT_ADAPTER;
extern const Adapter NODE_ADAPTER;
extern const Adapter JS_ADAPTER;
