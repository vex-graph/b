// CLASS: Adapter — one native-tool integration contract.
// DEFINITION: Source languages, project backends, runtimes and deployment tools
// expose an Adapter record describing how to build/check and how to run a file.
// The suite never grows a per-language branch; it asks this registry which
// adapter handles a name or a file extension. build writes into a fresh
// out-of-tree output path; run either interprets/executes the file as-is or
// (buildArtifact) compiles it first, then launches it.
// STRUCT FIELDS:
//   const char *name       CLI adapter name ("c", "cmake", "npm", ...)
//   const char *extension  file selector: suffix, exact manifest basename or null
//   int (*build)(project, output)          compile a directory's sources
//   int (*run)(file, argc, argv, buildArtifact)  run one file
//   const char *tools          comma-separated executable requirements
//   const char *capabilities   human-readable supported operations
#pragma once

#include "annotation.h"

;;DEFINITION
/* Adapter is the immutable native-tool contract, not a claim that every target
 * is a programming language. The suite borrows static records
 * and invokes their build/run callbacks. A successful build supplies a
 * caller-owned result path (artifact or checked directory); run returns child
 * status. Interpreted adapters document their exec exception explicitly.
 */
;;OVERVIEW
/* CLASS: Adapter. STRUCT FIELDS (declaration order):
 * const char *name — CLI adapter name.
 * const char *extension — file selector: leading dot means suffix; otherwise
 *     exact basename (Cargo.toml/package.json); nullptr disables file discovery.
 * int (*build)(const char *project, char **output) — build/check a directory;
 *     output is allocated on success and released by the caller.
 * int (*run)(const char *file, int argc, char **argv, bool buildArtifact) —
 *     direct runtime or compile-then-run operation, with borrowed arguments.
 * const char *tools — comma-separated tools; ENV=default permits an override.
 * const char *capabilities — adapter-owned operation/scope description.
 * PUBLIC: Adapter_forName; Adapter_forFile; Adapter_count; Adapter_at
 * (implemented in adapter.c). at rejects out-of-range; records are immutable.
 * Null lookup input rejects loudly; unknown/empty names or paths return nullptr.
 * Owner-thread or concurrent immutable lookup; no record lifetime ends at runtime.
 * No ownership transfer of records; no allocation during registry lookup.
 */

#include <stdbool.h>
#include <stddef.h>

typedef struct Adapter {
    const char *name;
    const char *extension;
    int (*build)(const char *project, char **output);
    int (*run)(const char *file, int argc, char **argv, bool buildArtifact);
    const char *tools;
    const char *capabilities;
} Adapter;

const Adapter *Adapter_forName(const char *name);
const Adapter *Adapter_forFile(const char *path);
size_t Adapter_count(void);
const Adapter *Adapter_at(size_t index);
