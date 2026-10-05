#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* Go source execution and native package builds through the installed Go tool. */
;;OVERVIEW
/* MODULE: Go adapter contract. PUBLIC RECORD: GO_ADAPTER (go.c).
 * FIELDS: name="go", extension=".go", build=buildGo, run=runGo,
 * tools="GO=go", capabilities="package build; source instance; native exec".
 * No owned state; native Go owns module resolution and tool caches.
 */
extern const Adapter GO_ADAPTER;
