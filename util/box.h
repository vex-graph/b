// CLASS: BoxManifest — one user-authored packaging description.
// DEFINITION: `b create manifest <path>` writes a starter form; the user fills
// it in; `b box <manifest> <path-to-export>` reads it, validates it, and exports
// the declared payload. This class owns that description: the declared
// application metadata, the payload entry and files, and the requested export
// formats. It is project/toolchain agnostic and has no vexgraph-ecosystem-gitrepo dependency.
// STRUCT FIELDS:
//   char *baseDirectory  manifest directory; payload paths resolve against it
//   int schemaVersion    declared schema number (only 1 is accepted)
//   char *identifier     stable application id used for platform identity
//   char *name           display name
//   char *version        release version supplied by the author
//   char *publisher      publisher/authority text (not a signing identity)
//   char *description    one-line summary
//   char *license        license family or identifier
//   char *homepage       project URL
//   char *icon           relative icon path, when declared
//   char *entry          relative payload entry
//   char *statePolicy    machine/user state policy ("clean" only)
//   char **authors       declared author names
//   size_t authorCount   number of declared authors
//   char **sources       payload file sources, relative to baseDirectory
//   char **destinations  export-relative destinations, parallel to sources
//   size_t fileCount     number of declared payload files
//   char **formats       requested export formats
//   size_t formatCount   number of requested export formats
#pragma once

#include "annotation.h"

#include <stdbool.h>
#include <stddef.h>

;;DEFINITION
/* A manifest is untrusted, user-authored input: it is parsed and validated once
 * on a cold path, and only then trusted while it is exported. Strings are owned
 * by the manifest; every field is either present and validated or absent.
 * Loading rejects malformed JSON, unsupported escapes, unknown and duplicate
 * keys, missing required fields, invalid characters, absolute paths, "."/".."
 * components and control characters. The only supported state policy is
 * "clean": an export copies declared payload and never machine or user state.
 */
;;OVERVIEW
/* CLASS: BoxManifest. STRUCT FIELDS (declaration order):
 * char *baseDirectory — directory of the manifest file; payload resolution base.
 * int schemaVersion — declared schema number; only 1 is accepted.
 * char *identifier — stable application identity (platform identity source).
 * char *name — display name.
 * char *version — author-supplied release version.
 * char *publisher — publisher/authority text.
 * char *description — short summary.
 * char *license — license family or identifier.
 * char *homepage — project URL.
 * char *icon — relative icon path when declared.
 * char *entry — relative payload entry.
 * char *statePolicy — state policy; only "clean" is accepted.
 * char **authors, size_t authorCount — declared authors.
 * char **sources, char **destinations, size_t fileCount — payload files.
 * char **formats, size_t formatCount — requested export formats.
 * PUBLIC: BoxManifest_writeTemplate (starter JSON form);
 * BoxManifest_load (parse + validate, nullptr + THROW on rejection);
 * BoxManifest_free; BoxManifest_export (portable payload export; the
 * destination must not exist yet); BoxManifest_toString (value projection);
 * BoxManifest_toStringStruct (field projection, one layer).
 * SCHEMA 1: schemaVersion; application{id,name,version,authors[],publisher,
 * description,license,homepage,icon}; payload{entry,files[]}; targets[] (must be
 * empty); formats[]; statePolicy. Unknown or duplicate keys are defects.
 * All operations are cold-path. Load owns every string and free releases them.
 * Export copies only declared payload and preserves any existing destination;
 * no format beyond `portable` is implemented and unsupported formats reject
 * loudly. This is not a sandbox: a stable tree is required during an export.
 */

typedef struct BoxManifest {
    char *baseDirectory;
    int schemaVersion;
    char *identifier;
    char *name;
    char *version;
    char *publisher;
    char *description;
    char *license;
    char *homepage;
    char *icon;
    char *entry;
    char *statePolicy;
    char **authors;
    size_t authorCount;
    char **sources;
    char **destinations;
    size_t fileCount;
    char **formats;
    size_t formatCount;
} BoxManifest;

// Write the starter schema-1 JSON form; never replaces an existing path.
int BoxManifest_writeTemplate(const char *path);
// Parse and validate a manifest; nullptr with a THROW diagnostic on rejection.
BoxManifest *BoxManifest_load(const char *path);
// Release every owned string and the record itself; nullptr is permitted.
void BoxManifest_free(BoxManifest *self);
// Export declared payload into destination (which must not exist yet).
int BoxManifest_export(const BoxManifest *self, const char *destination);
// Bounded value projection; dest-last with a truncation flag.
void BoxManifest_toString(const BoxManifest *self, char *dest, size_t capacity, bool *outTruncated);
// Bounded field projection mirroring STRUCT FIELDS; one layer only.
void BoxManifest_toStringStruct(const BoxManifest *self, char *dest, size_t capacity, bool *outTruncated);
