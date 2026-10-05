#pragma once
#include "annotation.h"
#include "adapters/adapter.h"

;;DEFINITION
/* Build-backend contract for existing CMake projects; no compiler replacement. */
;;OVERVIEW
/* MODULE: CMake backend header. PUBLIC RECORD: CMAKE_ADAPTER.
 * Record fields: name="cmake", extension=nullptr, build=buildCmake,
 * run=runCmake (host source execution rejected). Implementation: cmake.c.
 */
extern const Adapter CMAKE_ADAPTER;
