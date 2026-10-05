#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* Strict C++23 source/native compiler adapter using the installed CXX tool. */
;;OVERVIEW
/* PUBLIC RECORDS: CPP_ADAPTER (cpp/.cpp), CXX_ADAPTER (cxx/.cc),
 * CPP_LONG_ADAPTER (c++/.cxx); all build=buildCpp, run=runCpp.
 * Implementation: cpp.c; no owned state.
 */
extern const Adapter CPP_ADAPTER;
extern const Adapter CXX_ADAPTER;
extern const Adapter CPP_LONG_ADAPTER;
