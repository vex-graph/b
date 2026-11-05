#ifndef ASSEMBLY_H
#define ASSEMBLY_H

#include <stdbool.h>
#include <stdio.h>

/* Load and validate b.assembly.json against workspace.json.
 * Returns true if the assembly is well-formed and all sources/deps resolve. */
int Build_assembly(int argc, char **argv);

bool Assembly_validate(const char *assembly_path, const char *workspace_path, FILE *out);

#endif
