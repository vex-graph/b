#pragma once
#include "annotation.h"
#include "languages/language.h"
;;DEFINITION
/* Node.js script/check adapter records. Node owns modules and runtime semantics. */
;;OVERVIEW
/* PUBLIC RECORDS: JAVASCRIPT_LANGUAGE (javascript/.js), NODE_LANGUAGE (node/.mjs),
 * JS_LANGUAGE (js/.cjs); each build=buildJavascript, run=runJavascript.
 * No owned state; implementation: javascript.c.
 */
extern const Language JAVASCRIPT_LANGUAGE;
extern const Language NODE_LANGUAGE;
extern const Language JS_LANGUAGE;
