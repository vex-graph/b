/**
 * CLASS: BoxManifest, one user-authored packaging description.
 * DEFINITION: `b.sh create manifest <path>` writes a starter form; the user fills
 * it in; `b.sh box <manifest> <path-to-export>` reads it, validates it, and exports
 * the declared payload. This class owns that description: the declared
 * application metadata, the payload entry and files, and the requested export
 * formats. It is project/toolchain agnostic and has no ecosystem dependency.
 * STRUCT FIELDS:
 * utf8 *baseDirectory  manifest directory; payload paths resolve against it
 * i32 schemaVersion    declared schema number (only 1 is accepted)
 * utf8 *identifier     stable application id used for platform identity
 * utf8 *name           display name
 * utf8 *version        release version supplied by the author
 * utf8 *publisher      publisher/authority text (not a signing identity)
 * utf8 *description    one-line summary
 * utf8 *license        license family or identifier
 * utf8 *homepage       project URL
 * utf8 *icon           relative icon path, when declared
 * utf8 *entry          relative payload entry
 * utf8 *statePolicy    machine/user state policy ("clean" only)
 * utf8 **authors       declared author names
 * bytes authorCount   number of declared authors
 * utf8 **sources       payload file sources, relative to baseDirectory
 * utf8 **destinations  export-relative destinations, parallel to sources
 * bytes fileCount     number of declared payload files
 * utf8 **formats       requested export formats
 * bytes formatCount   number of requested export formats
 */

#pragma once

#include "vex.h"

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
 * utf8 *baseDirectory, directory of the manifest file; payload resolution base.
 * i32 schemaVersion, declared schema number; only 1 is accepted.
 * utf8 *identifier, stable application identity (platform identity source).
 * utf8 *name, display name.
 * utf8 *version, author-supplied release version.
 * utf8 *publisher, publisher/authority text.
 * utf8 *description, i16 summary.
 * utf8 *license, license family or identifier.
 * utf8 *homepage, project URL.
 * utf8 *icon, relative icon path when declared.
 * utf8 *entry, relative payload entry.
 * utf8 *statePolicy, state policy; only "clean" is accepted.
 * utf8 **authors, bytes authorCount, declared authors.
 * utf8 **sources, utf8 **destinations, bytes fileCount, payload files.
 * utf8 **formats, bytes formatCount, requested export formats.
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
    utf8 *baseDirectory;
    i32 schemaVersion;
    utf8 *identifier;
    utf8 *name;
    utf8 *version;
    utf8 *publisher;
    utf8 *description;
    utf8 *license;
    utf8 *homepage;
    utf8 *icon;
    utf8 *entry;
    utf8 *statePolicy;
    utf8 **authors;
    bytes authorCount;
    utf8 **sources;
    utf8 **destinations;
    bytes fileCount;
    utf8 **formats;
    bytes formatCount;
} BoxManifest;

/** Write the starter schema-1 JSON form; never replaces an existing path. */
i32 BoxManifest_writeTemplate(string path);
/** Parse and validate a manifest; nullptr with a THROW diagnostic on rejection. */
BoxManifest *BoxManifest_load(string path);
/** Release every owned string and the record itself; nullptr is permitted. */
fn BoxManifest_free(BoxManifest *self);
/** Export declared payload into destination (which must not exist yet). */
i32 BoxManifest_export(const BoxManifest *self, string destination);
/** Bounded value projection; dest-last with a truncation flag. */
fn BoxManifest_toString(const BoxManifest *self, utf8 *dest, bytes capacity, bool *outTruncated);
/** Bounded field projection mirroring STRUCT FIELDS; one layer only. */
fn BoxManifest_toStringStruct(const BoxManifest *self, utf8 *dest, bytes capacity, bool *outTruncated);
