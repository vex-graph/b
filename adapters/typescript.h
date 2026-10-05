#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* Native Node TypeScript type-stripping adapter, not a TypeScript type checker. */
;;OVERVIEW
/* PUBLIC RECORDS: TYPESCRIPT_ADAPTER (typescript/.ts), TS_ADAPTER (ts/.mts),
 * CTS_ADAPTER (node-ts/.cts); build=buildTypescript, run=runTypescript.
 * No owned state; implementation: typescript.c.
 */
extern const Adapter TYPESCRIPT_ADAPTER;
extern const Adapter TS_ADAPTER;
extern const Adapter CTS_ADAPTER;
