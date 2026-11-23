/**
 * CLASS: Adapter, one native-tool integration contract.
 * DEFINITION: Source languages, project backends, runtimes and deployment tools
 * expose an Adapter record describing how to build/check and how to run a file.
 * The suite never grows a per-language branch; it asks this registry which
 * adapter handles a name or a file extension. build writes into a fresh
 * out-of-tree output path; run either interprets/executes the file as-is or
 * (buildArtifact) compiles it first, then launches it.
 * STRUCT FIELDS:
 * string name       CLI adapter name ("c", "cmake", "npm", ...)
 * string extension  file selector: suffix, exact manifest basename or null
 * i32 (*build)(project, output)          compile a directory's sources
 * i32 (*run)(file, argc, argv, buildArtifact)  run one file
 * string tools          comma-separated executable requirements
 * string capabilities   human-readable supported operations
 */

#pragma once

#include "vex.h"

;;DEFINITION
/* Adapter is the immutable native-tool contract, not a claim that every target
 * is a programming language. The suite borrows static records
 * and invokes their build/run callbacks. A successful build supplies a
 * caller-owned result path (artifact or checked directory); run returns child
 * status. Interpreted adapters document their exec exception explicitly.
 */
;;OVERVIEW
/* CLASS: Adapter. STRUCT FIELDS (declaration order):
 * string name, CLI adapter name.
 * string extension, file selector: leading dot means suffix; otherwise
 *     exact basename (Cargo.toml/package.json); nullptr disables file discovery.
 * i32 (*build)(string project, utf8 **output), build/check a directory;
 *     output is allocated on success and released by the caller.
 * i32 (*run)(string file, i32 argc, utf8 **argv, bool buildArtifact),
 *     direct runtime or compile-then-run operation, with borrowed arguments.
 * string tools, comma-separated tools; ENV=default permits an override.
 * string capabilities, adapter-owned operation/scope description.
 * utf8 *(*entry)(string project), owned path of the source file holding the
 *     program entry point; nullptr means none (library or interpreted units).
 * i32 (*archive)(string project, utf8 **output), build a library unit into
 *     its archive; output is allocated on success, nullptr means the adapter has
 *     no library form and the unit is reported as skipped.
 * PUBLIC: Adapter_forName; Adapter_forFile; Adapter_count; Adapter_at
 * (implemented in adapter.c). at rejects out-of-range; records are immutable.
 * Null lookup input rejects loudly; unknown/empty names or paths return nullptr.
 * Owner-thread or concurrent immutable lookup; no record lifetime ends at runtime.
 * No ownership transfer of records; no allocation during registry lookup.
 */


typedef struct Adapter {
    string name;
    string extension;
    i32 (*build)(string project, utf8 **output);
    i32 (*run)(string file, i32 argc, utf8 **argv, bool buildArtifact);
    string tools;
    string capabilities;
    utf8 *(*entry)(string project);
    i32 (*archive)(string project, utf8 **output);
} Adapter;

const Adapter *Adapter_forName(string name);
const Adapter *Adapter_forFile(string path);
bytes Adapter_count();
const Adapter *Adapter_at(bytes index);
