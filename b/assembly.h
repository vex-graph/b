#ifndef ASSEMBLY_H
#define ASSEMBLY_H

#include "vex.h"
#include <stdio.h>

/* Load and validate b.assembly.json against workspace.json.
 * Returns true if the assembly is well-formed and all sources/deps resolve. */
i32 Build_assembly(i32 argc, utf8 **argv);

bool Assembly_validate(const utf8 *assembly_path, const utf8 *workspace_path, FILE *out);

#endif
