#pragma once
#include "annotation.h"
#include "languages/language.h"
;;DEFINITION
/* Strict C++23 source/native compiler adapter using the installed CXX tool. */
;;OVERVIEW
/* PUBLIC RECORDS: CPP_LANGUAGE (cpp/.cpp), CXX_LANGUAGE (cxx/.cc),
 * CPP_LONG_LANGUAGE (c++/.cxx); all build=buildCpp, run=runCpp.
 * Implementation: cpp.c; no owned state.
 */
extern const Language CPP_LANGUAGE;
extern const Language CXX_LANGUAGE;
extern const Language CPP_LONG_LANGUAGE;
