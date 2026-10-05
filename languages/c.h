#pragma once
#include "annotation.h"
;;DEFINITION
/* Immutable C adapter contract; compilation and execution live in c.c. */
;;OVERVIEW
/* PUBLIC RECORD: C_LANGUAGE; fields name="c", extension=".c",
 * build=buildC, run=runC. No functions or owned state in this header.
 */
#include "languages/language.h"
extern const Language C_LANGUAGE;
