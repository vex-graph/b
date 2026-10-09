// Recursive cold assessment/build entry. No source execution or privilege escalation.
#pragma once
#include "annotation.h"
;;DEFINITION
/* One synchronous CLI operation: directory plus optional --plan or --confirm.
 * Borrowed argv, no retained state. A failed assessment invokes no build tools;
 * build failures continue and produce a nonzero aggregate result. Caller keeps
 * the filesystem stable; native build scripts/includes are not sandboxed.
 */
;;OVERVIEW
/* MODULE: Workspace_command — assess recursively and perform build-only units.
 * Broad roots require confirmation; no privilege escalation, upload or run mode.
 */
int Workspace_command(int argc, char **argv);
