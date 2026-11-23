#ifndef B_BUILD_INTERNAL_H
#define B_BUILD_INTERNAL_H

#include "vex.h"

/** Workspace-wide build context tracking root, header directories, and compile totals. */
typedef struct WorkspaceBuild {
    const utf8 *root;
    utf8 **headerDirectories;
    bytes headerCount;
    bytes compiled;
    bytes failed;
} WorkspaceBuild;

#endif
