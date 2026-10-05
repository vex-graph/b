#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* Local HTML browser-launch adapter; web builds belong to declared project tools. */
;;OVERVIEW
/* PUBLIC RECORDS: HTML_ADAPTER (html/.html), HTM_ADAPTER (web/.htm);
 * build=buildHtml, run=runHtml. Implementation: html.c; no owned state.
 */
extern const Adapter HTML_ADAPTER;
extern const Adapter HTM_ADAPTER;
