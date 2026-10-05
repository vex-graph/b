// CLASS: Language — one adapter per source language.
// DEFINITION: b is a language-agnostic suite. Each language owns exactly one
// Language record describing how to compile a directory and how to run a file.
// The suite never grows a per-language branch; it asks this registry which
// adapter handles a name or a file extension. build writes into a fresh
// out-of-tree output path; run either interprets/executes the file as-is or
// (buildArtifact) compiles it first, then launches it.
// STRUCT FIELDS:
//   const char *name       CLI language name ("c", "java", "python", ...)
//   const char *extension  source suffix matched at end of path (".c", ".py", ...)
//   int (*build)(project, output)          compile a directory's sources
//   int (*run)(file, argc, argv, buildArtifact)  run one file
#pragma once

#include "annotation.h"

;;DEFINITION
/* Language is the immutable adapter contract. The suite borrows static records
 * and invokes their build/run callbacks. A successful build supplies a
 * caller-owned result path (artifact or checked directory); run returns child
 * status. Interpreted adapters document their exec exception explicitly.
 */
;;OVERVIEW
/* CLASS: Language. STRUCT FIELDS (declaration order):
 * const char *name — CLI adapter name.
 * const char *extension — source suffix, or nullptr for build-only backends.
 * int (*build)(const char *project, char **output) — build/check a directory;
 *     output is allocated on success and released by the caller.
 * int (*run)(const char *file, int argc, char **argv, bool buildArtifact) —
 *     direct runtime or compile-then-run operation, with borrowed arguments.
 * PUBLIC: Language_forName; Language_forFile (implemented in language.c).
 * No ownership transfer of records; no allocation during registry lookup.
 */

#include <stdbool.h>
#include <stddef.h>

typedef struct Language {
    const char *name;
    const char *extension;
    int (*build)(const char *project, char **output);
    int (*run)(const char *file, int argc, char **argv, bool buildArtifact);
} Language;

const Language *Language_forName(const char *name);
const Language *Language_forFile(const char *path);
