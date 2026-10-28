// MODULE: named project registry, contained file resolution, and adapter index.
#pragma once

#include "annotation.h"

#include <stdbool.h>
#include <stddef.h>

;;DEFINITION
/* B_HOME owns registry/index state; project source directories stay in place.
 * Inputs are borrowed, returned paths are allocated for the caller, and refresh
 * replaces only b's index after a complete successful scan.
 */
;;OVERVIEW
/* MODULE: Project API; implementation: project.c.
 * PUBLIC: Project_register — save a project name and canonical root;
 * Project_root — retrieve the registered root; Project_contains — test a
 * canonical path boundary; Project_file — resolve a regular in-root file;
 * Project_refresh — atomically inventory files recognized by Adapter_forFile.
 */

bool Project_register(const char *stateDirectory, const char *name, const char *path);
bool Project_remove(const char *stateDirectory, const char *name);
char *Project_root(const char *stateDirectory, const char *name);
char *Project_file(const char *root, const char *path);
bool Project_contains(const char *root, const char *path);
char *Project_refresh(const char *stateDirectory, const char *name, size_t *fileCount);
