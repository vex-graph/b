#ifndef BUILD_H
#define BUILD_H

#include "vex.h"
#include <stdio.h>

/* Build a single source file into .b/out/<name> using assembly.json for
 * include roots and deps. Returns true on success. */
i32 Build_command(i32 argc, utf8 **argv);

/* Compile every runnable source the territory inventory lists. */
bool Build_workspace(string workspacePath, FILE *out);

bool Build_file(string source_path, string assembly_path,
                string workspace_path, string out_dir, FILE *out);

#endif
