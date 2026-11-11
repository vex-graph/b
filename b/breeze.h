// CLASS: Breeze — the run/breeze verb: resolve a file, build it, execute it.
// DEFINITION: run and breeze are one behaviour with two spellings. The verb
// resolves the file's unit through the territory, asks its adapter to build or
// interpret it, forwards the program's arguments literally, and returns the
// child's exit status. It never runs a stale artifact after a failed build.
// OVERVIEW: Breeze_command; PRIVATE: regularFile; runFile.
#pragma once


#include "vex.h"
;;DEFINITION
/* One synchronous operation over a borrowed argv. The mode word is the only
 * thing this verb decides: exec builds the artifact first, instance hands the
 * source to its runtime. Compiled adapters reject instance themselves, because
 * only the adapter knows whether a source runtime exists. No state is retained;
 * every path is caller-owned and released before return.
 */
;;OVERVIEW
/* PUBLIC: Breeze_command — parse the optional exec|instance word, then dispatch
 * one file. Exit status is the program's; a missing file, a missing territory or
 * an unsupported adapter is a cold rejection with no execution.
 * PRIVATE: regularFile — reject a path that is not a regular file; runFile —
 * adapter dispatch, else execute an existing native binary.
 */
int Breeze_command(int argc, char **argv);
