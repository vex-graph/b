#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* POSIX sh script execution and non-evaluating syntax checks. */
;;OVERVIEW
/* PUBLIC RECORD: SHELL_ADAPTER {name="shell", extension=".sh",
 * build=buildShell, run=runShell}; implementation: shell.c. No owned state.
 */
extern const Adapter SHELL_ADAPTER;
