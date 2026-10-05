#pragma once
#include "annotation.h"
#include "languages/language.h"
;;DEFINITION
/* Local HTML browser-launch adapter; web builds belong to declared project tools. */
;;OVERVIEW
/* PUBLIC RECORDS: HTML_LANGUAGE (html/.html), HTM_LANGUAGE (web/.htm);
 * build=buildHtml, run=runHtml. Implementation: html.c; no owned state.
 */
extern const Language HTML_LANGUAGE;
extern const Language HTM_LANGUAGE;
