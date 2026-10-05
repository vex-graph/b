#pragma once
#include "annotation.h"
;;DEFINITION
/* Standalone registry discovery; this never executes a tool or imports a graph. */
;;OVERVIEW
/* MODULE: diagnostics contract. PUBLIC: Inspect_adapters — list adapters,
 * suffixes, operations and requirements; Inspect_doctor — inspect PATH/override
 * executable presence, optionally for one adapter. No owned class/state.
 * doctor returns nonzero for a requested missing/unknown adapter; unfiltered
 * reports optional missing tools successfully. It does not prove tool versions.
 */
int Inspect_adapters(void);
int Inspect_doctor(const char *adapter);
