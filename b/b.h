/**
 * MODULE: b.sh shared seam, process launch, path and output helpers.
 * DEFINITION: Language adapters must not each re-implement spawning, safe path
 * joining, or the out-of-tree output directory. This header declares that one
 * shared surface; util.c owns the implementation. THROW is the cold rejection
 * reporter used across the suite and every adapter.
 * Constitution: the real workspace metadata/PREFERENCES.md (canonical Gist);
 * Semantic Consistency Law (Reference form) requires explicit dereferences.
 * OVERVIEW: allocation/path helpers (Util_allocate, Util_combine, Util_endsWith,
 * Util_makeDirectory, Util_createDirectory, Util_setFileMode, Util_outputDirectory,
 * Util_parentDirectory, Util_projectDirectory); process launch
 * (Util_execute, Util_executeWithEnvironment); source discovery
 * (Util_collectSources, Util_freeSources); environment (Util_environmentWith,
 * Util_freeEnvironment).
 */
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
 * MACRO: THROW, one recoverable cold rejection diagnostic.
 * No owned class or persistent state; callers release successful allocations.
 */

#include <stdio.h>

#ifdef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif

/** The suite owns one allocation pair; adapters never call malloc/calloc/free. */
any Util_allocate(bytes size);
fn Util_release(any block);
utf8 *Util_combine(string left, string right);
bool Util_endsWith(string text, string suffix);
bool Util_makeDirectory(utf8 *path);
i32 Util_createDirectory(string path, i32 mode);
i32 Util_setFileMode(i32 descriptor, i32 mode);
utf8 *Util_outputDirectory(string project);
utf8 *Util_parentDirectory(string file);
utf8 *Util_projectDirectory(string file);

i32 Util_execute(utf8 **arguments);
/** Build diagnostics go to stderr; stdout remains the build result path. */
i32 Util_executeBuild(utf8 **arguments);
i32 Util_executeWithEnvironment(utf8 **arguments, utf8 **environment);
/** Append literal program arguments to a borrowed command prefix. */
i32 Util_runCommand(utf8 **prefix, bytes prefixCount, i32 argc, utf8 **argv);
/** Check each matching source independently; return the checked directory. */
i32 Util_checkSources(string project, string pattern, utf8 **prefix, bytes prefixCount, utf8 **output);

utf8 **Util_collectSources(string project, string extension, bytes *count, bool *ok);
fn Util_freeSources(utf8 **sources, bytes count);
/** Report whether a readable regular file's bytes contain the needle (whole-file read; source-sized inputs). */
bool Util_fileContains(string path, string needle);
/** Read a whole regular file as NUL-terminated text; nullptr when it cannot be read. Caller frees. */
utf8 *Util_readText(string path);

utf8 **Util_environmentWith(string name, string value);
fn Util_freeEnvironment(utf8 **environment);

#ifdef _WIN32
utf8 *Util_realPath(string path, utf8 *resolved);
i32 Util_lstat(string path, struct stat *information);
i32 Util_scandir(string directory, struct dirent ***entries,
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
