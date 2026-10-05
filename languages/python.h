#pragma once
#include "annotation.h"
;;DEFINITION
/* Immutable interpreter/bytecode adapter contract; behavior lives in python.c. */
;;OVERVIEW
/* PUBLIC RECORD: PYTHON_LANGUAGE; fields name="python", extension=".py",
 * build=buildPython, run=runPython. No functions or owned state in this header.
 */
#include "languages/language.h"
extern const Language PYTHON_LANGUAGE;
