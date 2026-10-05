#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* Cargo orchestrates existing Rust manifests without guessing binary targets. */
;;OVERVIEW
/* MODULE: Cargo project adapter. PUBLIC RECORD: CARGO_ADAPTER (cargo.c).
 * FIELDS: name="cargo", extension="Cargo.toml", build=buildCargo, run=runCargo,
 * tools="CARGO=cargo,rustc", capabilities="offline project build/run".
 * Build returns an external target directory, never a guessed executable.
 */
extern const Adapter CARGO_ADAPTER;
