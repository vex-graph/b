#pragma once
#include "vex.h"
;;DEFINITION
/* Immutable .NET file-based C# adapter contract; behavior lives in csharp.c. */
;;OVERVIEW
/* PUBLIC RECORD: CSHARP_ADAPTER; fields name="csharp", extension=".cs",
 * build=buildCsharp, run=runCsharp. No functions or owned state in this header.
 */
#include "adapters/lang/adapter.h"
extern const Adapter CSHARP_ADAPTER;
