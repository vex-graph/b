#pragma once
#include "annotation.h"
;;DEFINITION
/* Immutable crate-root compiler adapter contract; behavior lives in rust.c. */
;;OVERVIEW
/* PUBLIC RECORD: RUST_LANGUAGE; fields name="rust", extension=".rs",
 * build=buildRust, run=runRust. No functions or owned state in this header.
 */
#include "languages/language.h"
extern const Language RUST_LANGUAGE;
