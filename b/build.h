#ifndef BUILD_H
#define BUILD_H

#include <stdbool.h>
#include <stdio.h>

/* Build a single source file into .b/out/<name> using assembly.json for
 * include roots and deps. Returns true on success. */
int Build_command(int argc, char **argv);

/* Compile every runnable source the territory inventory lists. */
bool Build_workspace(const char *workspacePath, FILE *out);

bool Build_file(const char *source_path, const char *assembly_path,
                const char *workspace_path, const char *out_dir, FILE *out);

#endif
