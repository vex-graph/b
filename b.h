// MODULE: b shared seam — process launch, path and output helpers.
// DEFINITION: Language adapters must not each re-implement spawning, safe path
// joining, or the out-of-tree output directory. This header declares that one
// shared surface; util.c owns the implementation. THROW is the cold rejection
// reporter used across the suite and every adapter.
// OVERVIEW: allocation/path helpers (Util_allocate, Util_combine, Util_endsWith,
// Util_makeDirectory, Util_outputDirectory, Util_parentDirectory); process launch
// (Util_execute, Util_executeWithEnvironment); source discovery
// (Util_collectSources, Util_freeSources); environment (Util_environmentWith,
// Util_freeEnvironment).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define THROW(...) do { fprintf(stderr, "[vex] %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)

void *Util_allocate(size_t size);
char *Util_combine(const char *left, const char *right);
bool Util_endsWith(const char *text, const char *suffix);
bool Util_makeDirectory(char *path);
char *Util_outputDirectory(const char *project);
char *Util_parentDirectory(const char *file);

int Util_execute(char **arguments);
// Build diagnostics go to stderr; stdout remains the build result path.
int Util_executeBuild(char **arguments);
int Util_executeWithEnvironment(char **arguments, char **environment);

char **Util_collectSources(const char *project, const char *extension, size_t *count, bool *ok);
void Util_freeSources(char **sources, size_t count);

char **Util_environmentWith(const char *name, const char *value);
void Util_freeEnvironment(char **environment);
