#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* Zig native source compilation; project build.zig ownership remains native. */
;;OVERVIEW
/* MODULE: Zig adapter contract. PUBLIC RECORD: ZIG_ADAPTER (zig.c).
 * FIELDS: name="zig", extension=".zig", build=buildZig, run=runZig,
 * tools="ZIG=zig", capabilities="source/project build; compile-then-run".
 * No interpreter claim; both source run modes compile through Zig.
 */
extern const Adapter ZIG_ADAPTER;
