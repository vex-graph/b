// CLASS: Territory — the .b/ marker and the inventory it declares.
// DEFINITION: A territory is a directory holding .b/. Its presence is the only
// thing that makes the tree below b's: no .git sniffing, no cwd guess, no home
// sweep. This class answers where the territory is, where its workspace file
// lives, and how to claim one.
// OVERVIEW: find; workspacePath; claim. Claim writes .b/workspace.json, the one
// disposable fact file, and refuses a nested claim.
#pragma once

#include "../util/annotation.h"

;;DEFINITION
/* A territory is claimed, never inferred. Claiming creates .b/ and writes the
 * generated inventory; every command afterwards resolves upward to it. Claiming
 * inside an existing territory is refused, because one file has one owner, one
 * object cache and one answer to "what builds this". No state lives here: every
 * path is returned caller-owned, and the walk is cold.
 */
;;OVERVIEW
/* CLASS: Territory.
 * PUBLIC: Territory_find — nearest ancestor holding .b/, caller-owned, or
 *     nullptr when the tree is unclaimed.
 * Territory_workspacePath — <root>/.b/workspace.json, caller-owned.
 * Territory_claim — create .b/, walk the tree once, write the inventory.
 * Claim is idempotent at the same root and refuses a nested claim; a failed
 * claim leaves no workspace file behind.
 */

#include <stdbool.h>

char *Territory_find(const char *startDirectory);
char *Territory_workspacePath(const char *root);
bool Territory_claim(const char *directory);
// Build every unit at or below the directory (nullptr means the current directory).
bool Territory_buildAll(const char *directory);
// Write <unit>/b.assembly.json once; force replaces it. An empty version is allowed.
bool Territory_writeAssembly(const char *directory, const char *version, const char *const *includes, size_t includeCount, bool force);
