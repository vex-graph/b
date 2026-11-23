#ifndef B_TERRITORY_INTERNAL_H
#define B_TERRITORY_INTERNAL_H

#include "vex.h"

/** Compilation unit directory and its discovered source filenames. */
typedef struct TerritoryUnit {
    utf8 *directory;
    utf8 **names;
    bytes count;
    bytes capacity;
} TerritoryUnit;

/** Directory file inventory entry pairing a path with its filenames. */
typedef struct TerritoryFiles {
    utf8 *directory;
    utf8 **names;
    bytes count;
    bytes capacity;
} TerritoryFiles;

/** Accumulated territory scan state across directories, units, and file trees. */
typedef struct TerritoryWalk {
    utf8 **directories;
    bytes directoryCount;
    bytes directoryCapacity;
    TerritoryUnit *units;
    bytes unitCount;
    bytes unitCapacity;
    TerritoryFiles *tree;
    bytes treeCount;
    bytes treeCapacity;
    bytes files;
    bytes sources;
    bytes excluded;
} TerritoryWalk;

#endif
