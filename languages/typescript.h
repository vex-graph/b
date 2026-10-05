#pragma once
#include "annotation.h"
#include "languages/language.h"
;;DEFINITION
/* Native Node TypeScript type-stripping adapter, not a TypeScript type checker. */
;;OVERVIEW
/* PUBLIC RECORDS: TYPESCRIPT_LANGUAGE (typescript/.ts), TS_LANGUAGE (ts/.mts),
 * CTS_LANGUAGE (node-ts/.cts); build=buildTypescript, run=runTypescript.
 * No owned state; implementation: typescript.c.
 */
extern const Language TYPESCRIPT_LANGUAGE;
extern const Language TS_LANGUAGE;
extern const Language CTS_LANGUAGE;
