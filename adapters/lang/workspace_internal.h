#ifndef ADAPTERS_LANG_WORKSPACE_INTERNAL_H
#define ADAPTERS_LANG_WORKSPACE_INTERNAL_H

#include "vex.h"
#include "adapters/lang/adapter.h"

/** Directory scan entry pairing path with manifest-ownership state. */
typedef struct ScanSlot {
    utf8 *path;
    bool owned;
} ScanSlot;

/** Planned workspace build unit pairing a path with its selected adapter. */
typedef struct BuildSlot {
    utf8 *path;
    const Adapter *adapter;
} BuildSlot;

#endif
