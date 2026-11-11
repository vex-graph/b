#pragma once
#include "vex.h"
;;DEFINITION
/* Immutable C adapter contract; compilation and execution live in c.c. */
;;OVERVIEW
/* PUBLIC RECORD: C_ADAPTER; fields name="c", extension=".c",
 * build=buildC, run=runC. No functions or owned state in this header.
 */
#include "adapters/lang/adapter.h"
extern const Adapter C_ADAPTER;
