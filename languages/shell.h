#pragma once
#include "annotation.h"
#include "languages/language.h"
;;DEFINITION
/* POSIX sh script execution and non-evaluating syntax checks. */
;;OVERVIEW
/* PUBLIC RECORD: SHELL_LANGUAGE {name="shell", extension=".sh",
 * build=buildShell, run=runShell}; implementation: shell.c. No owned state.
 */
extern const Language SHELL_LANGUAGE;
