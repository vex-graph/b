#pragma once
#include "vex.h"
;;DEFINITION
/* Immutable crate-root compiler adapter contract; behavior lives in rust.c. */
;;OVERVIEW
/* PUBLIC RECORD: RUST_ADAPTER; fields name="rust", extension=".rs",
 * build=buildRust, run=runRust. No functions or owned state in this header.
 */
#include "adapters/lang/adapter.h"
extern const Adapter RUST_ADAPTER;
