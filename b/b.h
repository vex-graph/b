// MODULE: b.sh shared seam — process launch, path and output helpers.
// DEFINITION: Language adapters must not each re-implement spawning, safe path
// joining, or the out-of-tree output directory. This header declares that one
// shared surface; util.c owns the implementation. THROW is the cold rejection
// reporter used across the suite and every adapter.
// Constitution: the real workspace metadata/PREFERENCES.md (canonical Gist);
// Semantic Consistency Law (Reference form) requires explicit dereferences.
// OVERVIEW: allocation/path helpers (Util_allocate, Util_combine, Util_endsWith,
// Util_makeDirectory, Util_createDirectory, Util_setFileMode, Util_outputDirectory,
// Util_parentDirectory, Util_projectDirectory); process launch
// (Util_execute, Util_executeWithEnvironment); source discovery
// (Util_collectSources, Util_freeSources); environment (Util_environmentWith,
// Util_freeEnvironment).
#pragma once

#include "vex.h"

;;DEFINITION
/* Shared standalone services for every adapter. Arguments and inherited
 * environment entries are borrowed; returned strings/arrays are caller-owned.
 * Children run without a shell. Build output is separated from diagnostics.
 */
;;OVERVIEW
/* MODULE: shared utility API (implementation: src/util.c).
 * PUBLIC: Util_allocate; Util_combine; Util_endsWith; Util_makeDirectory;
 * Util_createDirectory; Util_setFileMode;
 * Util_outputDirectory; Util_parentDirectory; Util_projectDirectory;
 * Util_execute; Util_executeBuild;
 * Util_executeWithEnvironment; Util_runCommand; Util_checkSources;
 * Util_collectSources; Util_freeSources;
 * Util_fileContains; Util_readText (read-only file helpers);
 * Util_environmentWith; Util_freeEnvironment.
 * MACRO: THROW — one recoverable cold rejection diagnostic.
 * No owned class or persistent state; callers release successful allocations.
 */

#include <stdio.h>

#ifdef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif

// The suite owns one allocation pair; adapters never call malloc/calloc/free.
any Util_allocate(bytes size);
fn Util_release(any block);
char *Util_combine(const char *left, const char *right);
bool Util_endsWith(const char *text, const char *suffix);
bool Util_makeDirectory(char *path);
i32 Util_createDirectory(const char *path, i32 mode);
i32 Util_setFileMode(i32 descriptor, i32 mode);
char *Util_outputDirectory(const char *project);
char *Util_parentDirectory(const char *file);
char *Util_projectDirectory(const char *file);

i32 Util_execute(char **arguments);
// Build diagnostics go to stderr; stdout remains the build result path.
i32 Util_executeBuild(char **arguments);
i32 Util_executeWithEnvironment(char **arguments, char **environment);
// Append literal program arguments to a borrowed command prefix.
i32 Util_runCommand(char **prefix, bytes prefixCount, i32 argc, char **argv);
// Check each matching source independently; return the checked directory.
i32 Util_checkSources(const char *project, const char *pattern, char **prefix, bytes prefixCount, char **output);

char **Util_collectSources(const char *project, const char *extension, bytes *count, bool *ok);
fn Util_freeSources(char **sources, bytes count);
// Report whether a readable regular file's bytes contain the needle (whole-file read; source-sized inputs).
bool Util_fileContains(const char *path, const char *needle);
// Read a whole regular file as NUL-terminated text; nullptr when it cannot be read. Caller frees.
char *Util_readText(const char *path);

char **Util_environmentWith(const char *name, const char *value);
fn Util_freeEnvironment(char **environment);

#ifdef _WIN32
char *Util_realPath(const char *path, char *resolved);
i32 Util_lstat(const char *path, struct stat *information);
i32 Util_scandir(const char *directory, struct dirent ***entries,
                 i32 (*filter)(const struct dirent *),
                 i32 (*compare)(const struct dirent **, const struct dirent **));
i32 Util_alphasort(const struct dirent **left, const struct dirent **right);

#define realpath Util_realPath
#define lstat Util_lstat
#define scandir Util_scandir
#define alphasort Util_alphasort
#ifndef S_IFMT
#define S_IFMT 0170000
#endif
#ifndef S_IFLNK
#define S_IFLNK 0120000
#endif
#ifndef S_ISLNK
#define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)
#endif
#endif
