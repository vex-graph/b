#pragma once
#include "annotation.h"
;;DEFINITION
/* Immutable .NET file-based C# adapter contract; behavior lives in csharp.c. */
;;OVERVIEW
/* PUBLIC RECORD: CSHARP_LANGUAGE; fields name="csharp", extension=".cs",
 * build=buildCsharp, run=runCsharp. No functions or owned state in this header.
 */
#include "languages/language.h"
extern const Language CSHARP_LANGUAGE;
