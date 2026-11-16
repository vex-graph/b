// CLASS: BoxManifest — manifest creation, strict validation and portable export.
// DEFINITION: box is b.sh's packaging surface. `b.sh create manifest <path>` writes a
// starter JSON form that a human fills in; `b.sh box <manifest> <destination>`
// parses that form, validates it against schema 1, and exports the declared
// payload. Validation is cold and exhaustive because a manifest is untrusted
// user input: malformed JSON, unsupported escapes, unknown or duplicate keys,
// missing fields, invalid characters, absolute paths and "."/".." components
// are all rejected before a single byte is copied. The machine-state policy is
// explicit: "clean" exports declared payload only, never user or machine state.
// STRUCT FIELDS (mirroring box.h): baseDirectory; schemaVersion; identifier;
// name; version; publisher; description; license; homepage; icon; entry;
// statePolicy; authors/authorCount; sources/destinations/fileCount;
// formats/formatCount. PRIVATE HELPERS: struct stat is not owned here.
// OVERVIEW: the annotation registry below names every function in this file.

#include "vex.h"
#include "box.h"
#include "b.h"

;;DEFINITION
/* The parser is a schema walk, not a general JSON tree: the cursor advances
 * through exactly the declared structure, so nesting depth is fixed by the
 * language of the reader and hostile depth cannot recurse. Text is read into a
 * bounded buffer (BOX_MANIFEST_MAX_BYTES); every string is copied into owned
 * storage. Export stages into a private sibling directory and publishes with a
 * single rename, so a failure leaves any previous export untouched. Copying
 * refuses symlinks and non-regular payload entries and documents that a stable
 * tree is required: this is not a sandbox or a hostile-writer defence.
 */
;;OVERVIEW
/* CLASS: BoxManifest. STRUCT FIELDS (declaration order, mirroring box.h):
 * char *baseDirectory — manifest directory; payload paths resolve against it.
 * i32 schemaVersion — only 1 is accepted.
 * char *identifier — stable identity for platform mapping.
 * char *name — display name. char *version — author release version.
 * char *publisher — publisher text. char *description — i16 summary.
 * char *license — license family. char *homepage — project URL.
 * char *icon — relative icon path. char *entry — relative payload entry.
 * char *statePolicy — only "clean" is accepted.
 * char **authors, bytes authorCount — declared authors.
 * char **sources, char **destinations, bytes fileCount — payload files.
 * char **formats, bytes formatCount — requested export formats.
 * PUBLIC: BoxManifest_writeTemplate; BoxManifest_load; BoxManifest_free;
 * BoxManifest_export; BoxManifest_toString; BoxManifest_toStringStruct.
 * PRIVATE STATIC: allocateText; copyText; appendText; skipSpace;
 * expectCharacter (parseString value); parseStringArray; parseInteger;
 * parseSchemaVersion; parseApplication; parseFileEntry; parsePayload;
 * parseTargets; parseRoot; validateCharacters; validateIdentifier;
 * validateVersion; validateRelativePath; validateFormats; validateDestinations;
 * validateManifest;
 * freeTextList; joinPath; baseNameOf; parentOf; stagingPath; removeTree;
 * copyRegularFile; copyTree; emitText; emitQuoted; emitCount; emitList;
 * readManifestText.
 * SCHEMA 1: schemaVersion; application{id,name,version,authors,publisher,
 * description,license,homepage,icon}; payload{entry,files}; targets (empty);
 * formats; statePolicy. Every operation is a cold path; no hot-path work,
 * allocation or logging exists in this file.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#define BOX_BINARY_MODE _O_BINARY
#else
#define BOX_BINARY_MODE 0
#endif

// Named bounds: each is a validation limit, stated where it is enforced.
#define BOX_MANIFEST_MAX_BYTES (256u * 1024u)
#define BOX_TEXT_MAX 255u
#define BOX_DESCRIPTION_MAX 4096u
#define BOX_VERSION_MAX 64u
#define BOX_PATH_MAX 1024u
#define BOX_IDENTIFIER_MAX 255u
#define BOX_FORMAT_MAX 32u
#define BOX_ITEM_MAX 4096u
#define BOX_COPY_DEPTH_MAX 32u
#define BOX_COPY_BUFFER 65536u
#define BOX_FORMAT_PORTABLE "portable"

static const char *const BOX_KNOWN_FORMATS[] = {
    "portable", "exe", "app", "msi", "pkg", "dmg", "iso", "zip", "tar",
};

// Allocate plain storage for owned text; report and fail without terminating.
static char *allocateText(bytes size) {
    char *text = malloc(size);
    if (text == nullptr)
        THROW("out of memory while reading a manifest");
    return text;
}

// Copy owned text; nullptr propagates so callers can fail without a check.
static char *copyText(const char *text) {
    if (text == nullptr)
        return nullptr;
    bytes length = strlen(text);
    char *copy = allocateText(length + 1);
    if (copy == nullptr)
        return nullptr;
    memcpy(copy, text, length + 1);
    return copy;
}

// Append one owned string to a growable list bounded by BOX_ITEM_MAX.
static bool appendText(char ***items, bytes *count, char *value) {
    if (*count >= BOX_ITEM_MAX) {
        THROW("manifest declares more than %u entries", BOX_ITEM_MAX);
        return false;
    }
    bytes next = *count + 1;
    char **grown = realloc(*items, next * sizeof(char*));
    if (grown == nullptr) {
        THROW("out of memory while reading a manifest");
        return false;
    }
    grown[*count] = value;
    *items = grown;
    *count = next;
    return true;
}

// Skip JSON whitespace; the manifest grammar is whitespace-insensitive.
static const char *skipSpace(const char *cursor) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r')
        ++cursor;
    return cursor;
}

// Require one structural character and advance past it.
static bool expectCharacter(const char **cursor, char expected, const char *field) {
    const char *position = skipSpace(*cursor);
    if (*position != expected) {
        THROW("manifest: %s expects '%c'", field, expected);
        return false;
    }
    *cursor = position + 1;
    return true;
}

// Parse one quoted string with escape handling; \u escapes are unsupported.
static bool parseString(const char **cursor, const char *field, char **out) {
    const char *position = skipSpace(*cursor);
    if (*position != '"') {
        THROW("manifest: %s must be a string", field);
        return false;
    }
    ++position;
    bytes capacity = strlen(position) + 1;
    char *text = allocateText(capacity);
    if (text == nullptr)
        return false;
    bytes length = 0;
    while (*position != '\0' && *position != '"') {
        u8 character = (u8) *position;
        if (character < 0x20) {
            THROW("manifest: %s contains a control character", field);
            free(text);
            return false;
        }
        if (character != '\\') {
            if (length + 1 >= BOX_PATH_MAX * 4) {
                THROW("manifest: %s is too long", field);
                free(text);
                return false;
            }
            text[length++] = (char) character;
            ++position;
            continue;
        }
        ++position;
        char escaped = *position;
        if (escaped == '"' || escaped == '\\' || escaped == '/')
            text[length++] = escaped;
        else if (escaped == 'b')
            text[length++] = '\b';
        else if (escaped == 'f')
            text[length++] = '\f';
        else if (escaped == 'n')
            text[length++] = '\n';
        else if (escaped == 'r')
            text[length++] = '\r';
        else if (escaped == 't')
            text[length++] = '\t';
        else {
            THROW("manifest: %s uses an unsupported escape sequence", field);
            free(text);
            return false;
        }
        ++position;
    }
    if (*position != '"') {
        THROW("manifest: %s is unterminated", field);
        free(text);
        return false;
    }
    text[length] = '\0';
    *cursor = position + 1;
    *out = text;
    return true;
}

// Parse an array of strings; an empty array yields a null list.
static bool parseStringArray(const char **cursor, const char *field, char ***items, bytes *count) {
    if (!expectCharacter(cursor, '[', field))
        return false;
    const char *position = skipSpace(*cursor);
    if (*position == ']') {
        *cursor = position + 1;
        return true;
    }
    while (true) {
        char *value = nullptr;
        if (!parseString(cursor, field, &value))
            return false;
        if (!appendText(items, count, value)) {
            free(value);
            return false;
        }
        position = skipSpace(*cursor);
        if (*position == ',') {
            *cursor = position + 1;
            continue;
        }
        if (*position == ']') {
            *cursor = position + 1;
            return true;
        }
        THROW("manifest: %s elements must be separated by ','", field);
        return false;
    }
}

// Parse a non-negative decimal integer without accepting a fractional part.
static bool parseInteger(const char **cursor, const char *field, i32 *out) {
    const char *position = skipSpace(*cursor);
    if (*position < '0' || *position > '9') {
        THROW("manifest: %s must be an integer", field);
        return false;
    }
    i64 value = 0;
    while (*position >= '0' && *position <= '9') {
        value = value * 10 + (*position - '0');
        if (value > 1000000) {
            THROW("manifest: %s is out of range", field);
            return false;
        }
        ++position;
    }
    *cursor = position;
    *out = (i32) value;
    return true;
}

// Parse the declared schema number.
static bool parseSchemaVersion(const char **cursor, i32 *out) {
    return parseInteger(cursor, "schemaVersion", out);
}

;;GETTER
static inline const char *dirent_d_name(const struct dirent *self) { return (*self).d_name; }
;;GETTER
static inline i32 *BoxManifest_schemaVersionPtr(BoxManifest *self) { return &(*self).schemaVersion; }
;;SETTER
static inline fn BoxManifest_setSchemaVersion(BoxManifest *self, i32 v) { (*self).schemaVersion = v; }
;;GETTER
static inline char *BoxManifest_baseDirectory(const BoxManifest *self) { return (*self).baseDirectory; }
;;SETTER
static inline fn BoxManifest_setBaseDirectory(BoxManifest *self, char *v) { (*self).baseDirectory = v; }
;;GETTER
static inline char *BoxManifest_identifier(const BoxManifest *self) { return (*self).identifier; }
;;GETTER
static inline char **BoxManifest_identifierPtr(BoxManifest *self) { return &(*self).identifier; }
;;GETTER
static inline char *BoxManifest_name(const BoxManifest *self) { return (*self).name; }
;;GETTER
static inline char **BoxManifest_namePtr(BoxManifest *self) { return &(*self).name; }
;;GETTER
static inline char *BoxManifest_version(const BoxManifest *self) { return (*self).version; }
;;GETTER
static inline char **BoxManifest_versionPtr(BoxManifest *self) { return &(*self).version; }
;;GETTER
static inline char *BoxManifest_publisher(const BoxManifest *self) { return (*self).publisher; }
;;GETTER
static inline char **BoxManifest_publisherPtr(BoxManifest *self) { return &(*self).publisher; }
;;GETTER
static inline char *BoxManifest_description(const BoxManifest *self) { return (*self).description; }
;;GETTER
static inline char **BoxManifest_descriptionPtr(BoxManifest *self) { return &(*self).description; }
;;GETTER
static inline char *BoxManifest_license(const BoxManifest *self) { return (*self).license; }
;;GETTER
static inline char **BoxManifest_licensePtr(BoxManifest *self) { return &(*self).license; }
;;GETTER
static inline char *BoxManifest_homepage(const BoxManifest *self) { return (*self).homepage; }
;;GETTER
static inline char **BoxManifest_homepagePtr(BoxManifest *self) { return &(*self).homepage; }
;;GETTER
static inline char *BoxManifest_icon(const BoxManifest *self) { return (*self).icon; }
;;GETTER
static inline char **BoxManifest_iconPtr(BoxManifest *self) { return &(*self).icon; }
;;GETTER
static inline char *BoxManifest_entry(const BoxManifest *self) { return (*self).entry; }
;;GETTER
static inline char **BoxManifest_entryPtr(BoxManifest *self) { return &(*self).entry; }
;;GETTER
static inline char *BoxManifest_statePolicy(const BoxManifest *self) { return (*self).statePolicy; }
;;GETTER
static inline char **BoxManifest_statePolicyPtr(BoxManifest *self) { return &(*self).statePolicy; }
;;SETTER
static inline fn BoxManifest_setStatePolicy(BoxManifest *self, char *v) { (*self).statePolicy = v; }
;;GETTER
static inline char **BoxManifest_authors(const BoxManifest *self) { return (*self).authors; }
;;GETTER
static inline char ***BoxManifest_authorsPtr(BoxManifest *self) { return &(*self).authors; }
;;GETTER
static inline bytes BoxManifest_authorCount(const BoxManifest *self) { return (*self).authorCount; }
;;GETTER
static inline bytes *BoxManifest_authorCountPtr(BoxManifest *self) { return &(*self).authorCount; }
;;GETTER
static inline char **BoxManifest_sources(const BoxManifest *self) { return (*self).sources; }
;;GETTER
static inline char ***BoxManifest_sourcesPtr(BoxManifest *self) { return &(*self).sources; }
;;GETTER
static inline char **BoxManifest_destinations(const BoxManifest *self) { return (*self).destinations; }
;;GETTER
static inline char ***BoxManifest_destinationsPtr(BoxManifest *self) { return &(*self).destinations; }
;;GETTER
static inline bytes BoxManifest_fileCount(const BoxManifest *self) { return (*self).fileCount; }
;;SETTER
static inline fn BoxManifest_setFileCount(BoxManifest *self, bytes v) { (*self).fileCount = v; }
;;GETTER
static inline char **BoxManifest_formats(const BoxManifest *self) { return (*self).formats; }
;;GETTER
static inline char ***BoxManifest_formatsPtr(BoxManifest *self) { return &(*self).formats; }
;;GETTER
static inline bytes BoxManifest_formatCount(const BoxManifest *self) { return (*self).formatCount; }
;;GETTER
static inline bytes *BoxManifest_formatCountPtr(BoxManifest *self) { return &(*self).formatCount; }

// Parse the application object and copy its declared fields.
static bool parseApplication(const char **cursor, BoxManifest *manifest) {
    if (!expectCharacter(cursor, '{', "application"))
        return false;
    bool identifierSeen = false, nameSeen = false, versionSeen = false;
    bool authorsSeen = false, publisherSeen = false, descriptionSeen = false;
    bool licenseSeen = false, homepageSeen = false, iconSeen = false;
    const char *position = skipSpace(*cursor);
    if (*position == '}') {
        THROW("manifest: application must not be empty");
        return false;
    }
    while (true) {
        char *key = nullptr;
        if (!parseString(cursor, "application key", &key))
            return false;
        if (!expectCharacter(cursor, ':', "application")) {
            free(key);
            return false;
        }
        const char *field = nullptr;
        char **target = nullptr;
        bool *seen = nullptr;
        if (strcmp(key, "id") == 0) {
            field = "application.id";
            target = BoxManifest_identifierPtr(manifest);
            seen = &identifierSeen;
        } else if (strcmp(key, "name") == 0) {
            field = "application.name";
            target = BoxManifest_namePtr(manifest);
            seen = &nameSeen;
        } else if (strcmp(key, "version") == 0) {
            field = "application.version";
            target = BoxManifest_versionPtr(manifest);
            seen = &versionSeen;
        } else if (strcmp(key, "publisher") == 0) {
            field = "application.publisher";
            target = BoxManifest_publisherPtr(manifest);
            seen = &publisherSeen;
        } else if (strcmp(key, "description") == 0) {
            field = "application.description";
            target = BoxManifest_descriptionPtr(manifest);
            seen = &descriptionSeen;
        } else if (strcmp(key, "license") == 0) {
            field = "application.license";
            target = BoxManifest_licensePtr(manifest);
            seen = &licenseSeen;
        } else if (strcmp(key, "homepage") == 0) {
            field = "application.homepage";
            target = BoxManifest_homepagePtr(manifest);
            seen = &homepageSeen;
        } else if (strcmp(key, "icon") == 0) {
            field = "application.icon";
            target = BoxManifest_iconPtr(manifest);
            seen = &iconSeen;
        }
        if (target != nullptr) {
            if (*seen) {
                THROW("manifest: duplicate %s", field);
                free(key);
                return false;
            }
            *seen = true;
            bool parsed = parseString(cursor, field, target);
            free(key);
            if (!parsed)
                return false;
        } else if (strcmp(key, "authors") == 0) {
            if (authorsSeen) {
                THROW("manifest: duplicate application.authors");
                free(key);
                return false;
            }
            authorsSeen = true;
            bool parsed = parseStringArray(cursor, "application.authors", BoxManifest_authorsPtr(manifest), BoxManifest_authorCountPtr(manifest));
            free(key);
            if (!parsed)
                return false;
        } else {
            THROW("manifest: unknown application key: %s", key);
            free(key);
            return false;
        }
        position = skipSpace(*cursor);
        if (*position == ',') {
            *cursor = position + 1;
            continue;
        }
        if (*position == '}') {
            *cursor = position + 1;
            return true;
        }
        THROW("manifest: application keys must be separated by ','");
        return false;
    }
}

// Parse one payload.files entry: a source string or a {source, destination}.
static bool parseFileEntry(const char **cursor, char **source, char **destination) {
    const char *position = skipSpace(*cursor);
    if (*position == '"')
        return parseString(cursor, "payload.files source", source);
    if (*position != '{') {
        THROW("manifest: payload.files entries must be a string or an object");
        return false;
    }
    if (!expectCharacter(cursor, '{', "payload.files entry"))
        return false;
    bool sourceSeen = false, destinationSeen = false;
    const char *inner = skipSpace(*cursor);
    if (*inner == '}') {
        THROW("manifest: payload.files entry requires a source");
        return false;
    }
    while (true) {
        char *key = nullptr;
        if (!parseString(cursor, "payload.files entry key", &key))
            return false;
        if (!expectCharacter(cursor, ':', "payload.files entry")) {
            free(key);
            return false;
        }
        bool parsed = true;
        if (strcmp(key, "source") == 0) {
            if (sourceSeen) {
                THROW("manifest: duplicate payload.files source");
                parsed = false;
            } else {
                sourceSeen = true;
                parsed = parseString(cursor, "payload.files source", source);
            }
        } else if (strcmp(key, "destination") == 0) {
            if (destinationSeen) {
                THROW("manifest: duplicate payload.files destination");
                parsed = false;
            } else {
                destinationSeen = true;
                parsed = parseString(cursor, "payload.files destination", destination);
            }
        } else {
            THROW("manifest: unknown payload.files entry key: %s", key);
            parsed = false;
        }
        free(key);
        if (!parsed)
            return false;
        inner = skipSpace(*cursor);
        if (*inner == ',') {
            *cursor = inner + 1;
            continue;
        }
        if (*inner == '}') {
            *cursor = inner + 1;
            break;
        }
        THROW("manifest: payload.files entry keys must be separated by ','");
        return false;
    }
    if (!sourceSeen) {
        THROW("manifest: payload.files entry requires a source");
        return false;
    }
    return true;
}

// Parse the payload object: entry plus the declared file list.
static bool parsePayload(const char **cursor, BoxManifest *manifest) {
    if (!expectCharacter(cursor, '{', "payload"))
        return false;
    bool entrySeen = false, filesSeen = false;
    const char *position = skipSpace(*cursor);
    if (*position == '}') {
        THROW("manifest: payload must not be empty");
        return false;
    }
    while (true) {
        char *key = nullptr;
        if (!parseString(cursor, "payload key", &key))
            return false;
        if (!expectCharacter(cursor, ':', "payload")) {
            free(key);
            return false;
        }
        bool parsed = true;
        if (strcmp(key, "entry") == 0) {
            if (entrySeen) {
                THROW("manifest: duplicate payload.entry");
                parsed = false;
            } else {
                entrySeen = true;
                parsed = parseString(cursor, "payload.entry", BoxManifest_entryPtr(manifest));
            }
        } else if (strcmp(key, "files") == 0) {
            if (filesSeen) {
                THROW("manifest: duplicate payload.files");
                parsed = false;
            } else {
                filesSeen = true;
                if (!expectCharacter(cursor, '[', "payload.files")) {
                    parsed = false;
                } else {
                    position = skipSpace(*cursor);
                    if (*position == ']') {
                        *cursor = position + 1;
                    } else {
                        // sources and destinations are parallel lists: keep separate
                        // counters so each entry lands at the same index.
                        bytes sourceCount = BoxManifest_fileCount(manifest);
                        bytes destinationCount = sourceCount;
                        while (true) {
                            char *source = nullptr, *destination = nullptr;
                            if (!parseFileEntry(cursor, &source, &destination)) {
                                free(source);
                                free(destination);
                                parsed = false;
                                break;
                            }
                            if (!appendText(BoxManifest_sourcesPtr(manifest), &sourceCount, source)) {
                                free(source);
                                free(destination);
                                parsed = false;
                                break;
                            }
                            if (!appendText(BoxManifest_destinationsPtr(manifest), &destinationCount, destination)) {
                                char **sources = BoxManifest_sources(manifest);
                                free(sources[sourceCount - 1]);
                                sources[sourceCount - 1] = nullptr;
                                sourceCount -= 1;
                                free(destination);
                                parsed = false;
                                break;
                            }
                            BoxManifest_setFileCount(manifest, sourceCount);
                            position = skipSpace(*cursor);
                            if (*position == ',') {
                                *cursor = position + 1;
                                continue;
                            }
                            if (*position == ']') {
                                *cursor = position + 1;
                                break;
                            }
                            THROW("manifest: payload.files elements must be separated by ','");
                            parsed = false;
                            break;
                        }
                    }
                }
            }
        } else {
            THROW("manifest: unknown payload key: %s", key);
            parsed = false;
        }
        free(key);
        if (!parsed)
            return false;
        position = skipSpace(*cursor);
        if (*position == ',') {
            *cursor = position + 1;
            continue;
        }
        if (*position == '}') {
            *cursor = position + 1;
            return true;
        }
        THROW("manifest: payload keys must be separated by ','");
        return false;
    }
}

// Require the schema-1 `targets` array to be empty; target exports are future.
static bool parseTargets(const char **cursor) {
    if (!expectCharacter(cursor, '[', "targets"))
        return false;
    const char *position = skipSpace(*cursor);
    if (*position != ']') {
        THROW("manifest: target-specific exports are not implemented; targets must be empty");
        return false;
    }
    *cursor = position + 1;
    return true;
}

// Parse the root object and collect the declared top-level fields.
static bool parseRoot(const char **cursor, BoxManifest *manifest) {
    if (!expectCharacter(cursor, '{', "manifest root"))
        return false;
    bool versionSeen = false, applicationSeen = false, payloadSeen = false;
    bool targetsSeen = false, formatsSeen = false, stateSeen = false;
    const char *position = skipSpace(*cursor);
    if (*position == '}') {
        THROW("manifest: the root object must not be empty");
        return false;
    }
    while (true) {
        char *key = nullptr;
        if (!parseString(cursor, "manifest key", &key))
            return false;
        if (!expectCharacter(cursor, ':', "manifest")) {
            free(key);
            return false;
        }
        bool parsed = true;
        if (strcmp(key, "schemaVersion") == 0) {
            if (versionSeen) {
                THROW("manifest: duplicate schemaVersion");
                parsed = false;
            } else {
                versionSeen = true;
                parsed = parseSchemaVersion(cursor, BoxManifest_schemaVersionPtr(manifest));
            }
        } else if (strcmp(key, "application") == 0) {
            if (applicationSeen) {
                THROW("manifest: duplicate application");
                parsed = false;
            } else {
                applicationSeen = true;
                parsed = parseApplication(cursor, manifest);
            }
        } else if (strcmp(key, "payload") == 0) {
            if (payloadSeen) {
                THROW("manifest: duplicate payload");
                parsed = false;
            } else {
                payloadSeen = true;
                parsed = parsePayload(cursor, manifest);
            }
        } else if (strcmp(key, "targets") == 0) {
            if (targetsSeen) {
                THROW("manifest: duplicate targets");
                parsed = false;
            } else {
                targetsSeen = true;
                parsed = parseTargets(cursor);
            }
        } else if (strcmp(key, "formats") == 0) {
            if (formatsSeen) {
                THROW("manifest: duplicate formats");
                parsed = false;
            } else {
                formatsSeen = true;
                parsed = parseStringArray(cursor, "formats", BoxManifest_formatsPtr(manifest), BoxManifest_formatCountPtr(manifest));
            }
        } else if (strcmp(key, "statePolicy") == 0) {
            if (stateSeen) {
                THROW("manifest: duplicate statePolicy");
                parsed = false;
            } else {
                stateSeen = true;
                parsed = parseString(cursor, "statePolicy", BoxManifest_statePolicyPtr(manifest));
            }
        } else {
            THROW("manifest: unknown root key: %s", key);
            parsed = false;
        }
        free(key);
        if (!parsed)
            return false;
        position = skipSpace(*cursor);
        if (*position == ',') {
            *cursor = position + 1;
            continue;
        }
        if (*position == '}') {
            *cursor = position + 1;
            return true;
        }
        THROW("manifest: root keys must be separated by ','");
        return false;
    }
}

// Require every character of text to appear in the allowed set.
static bool validateCharacters(const char *text, const char *field, const char *allowed) {
    for (const char *position = text; *position != '\0'; ++position) {
        if (strchr(allowed, *position) == nullptr) {
            THROW("manifest: %s contains an unsupported character: %s", field, text);
            return false;
        }
    }
    return true;
}

// Validate the stable application identifier used for platform identity.
static bool validateIdentifier(const char *text) {
    if (text == nullptr) {
        THROW("manifest: application.id is required");
        return false;
    }
    bytes length = strlen(text);
    if (length == 0 || length > BOX_IDENTIFIER_MAX) {
        THROW("manifest: application.id must be 1..%u characters", BOX_IDENTIFIER_MAX);
        return false;
    }
    if (text[0] == '.' || text[length - 1] == '.') {
        THROW("manifest: application.id must not start or end with '.': %s", text);
        return false;
    }
    return validateCharacters(text, "application.id", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-");
}

// Validate the author-supplied release version without inventing a new one.
static bool validateVersion(const char *text) {
    if (text == nullptr) {
        THROW("manifest: application.version is required");
        return false;
    }
    bytes length = strlen(text);
    if (length == 0 || length > BOX_VERSION_MAX) {
        THROW("manifest: application.version must be 1..%u characters", BOX_VERSION_MAX);
        return false;
    }
    if (text[0] < '0' || text[0] > '9') {
        THROW("manifest: application.version must start with a digit: %s", text);
        return false;
    }
    return validateCharacters(text, "application.version", "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz._+-");
}

// Require a relative path with no "." or ".." component and no control bytes.
static bool validateRelativePath(const char *text, const char *field) {
    if (text == nullptr || *text == '\0') {
        THROW("manifest: %s is required", field);
        return false;
    }
    if (strlen(text) > BOX_PATH_MAX) {
        THROW("manifest: %s exceeds %u characters", field, BOX_PATH_MAX);
        return false;
    }
    if (*text == '/' || *text == '~') {
        THROW("manifest: %s must be relative: %s", field, text);
        return false;
    }
    const char *position = text;
    while (true) {
        const char *slash = strchr(position, '/');
        bytes length = slash == nullptr ? strlen(position) : (bytes) (slash - position);
        if (length == 0) {
            THROW("manifest: %s has an empty path component: %s", field, text);
            return false;
        }
        if ((length == 1 && position[0] == '.') || (length == 2 && position[0] == '.' && position[1] == '.')) {
            THROW("manifest: %s must not contain '.' or '..' components: %s", field, text);
            return false;
        }
        for (bytes index = 0; index < length; ++index) {
            if ((u8) position[index] < 0x20) {
                THROW("manifest: %s has a control character: %s", field, text);
                return false;
            }
        }
        if (slash == nullptr)
            return true;
        position = slash + 1;
    }
}

// Validate the requested export formats against the known id set.
static bool validateFormats(const BoxManifest *self, const char *field) {
    bytes formatCount = BoxManifest_formatCount(self);
    if (formatCount == 0) {
        THROW("manifest: formats must declare at least one export format");
        return false;
    }
    char **formats = BoxManifest_formats(self);
    for (bytes index = 0; index < formatCount; ++index) {
        const char *format = formats[index];
        if (format == nullptr || *format == '\0' || strlen(format) > BOX_FORMAT_MAX) {
            THROW("manifest: formats contains an invalid entry");
            return false;
        }
        if (!validateCharacters(format, field, "abcdefghijklmnopqrstuvwxyz0123456789-"))
            return false;
        bool known = false;
        for (bytes candidate = 0; candidate < sizeof BOX_KNOWN_FORMATS / sizeof BOX_KNOWN_FORMATS[0]; ++candidate)
            known = known || strcmp(BOX_KNOWN_FORMATS[candidate], format) == 0;
        if (!known) {
            THROW("manifest: formats has an unknown export format: %s", format);
            return false;
        }
    }
    return true;
}

// Return the final path component of an owned or borrowed path.
static const char *baseNameOf(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash == nullptr ? path : slash + 1;
}

// Require every declared payload destination to be distinct before exporting.
static bool validateDestinations(const BoxManifest *self) {
    const char *entryName = baseNameOf(BoxManifest_entry(self));
    bytes fileCount = BoxManifest_fileCount(self);
    char **sources = BoxManifest_sources(self);
    char **destinations = BoxManifest_destinations(self);
    for (bytes index = 0; index < fileCount; ++index) {
        const char *destination = destinations[index] != nullptr
            ? destinations[index] : baseNameOf(sources[index]);
        if (strcmp(destination, entryName) == 0) {
            THROW("manifest: payload entry and %s share one export destination: %s", sources[index], destination);
            return false;
        }
        for (bytes other = 0; other < index; ++other) {
            const char *earlier = destinations[other] != nullptr
                ? destinations[other] : baseNameOf(sources[other]);
            if (strcmp(earlier, destination) == 0) {
                THROW("manifest: payload.files declares the same destination twice: %s", destination);
                return false;
            }
        }
    }
    return true;
}

// Validate the parsed manifest and report the first defect loudly.
static bool validateManifest(BoxManifest *manifest) {
    if (!validateIdentifier(BoxManifest_identifier(manifest)))
        return false;
    const char *name = BoxManifest_name(manifest);
    if (name == nullptr) {
        THROW("manifest: application.name is required");
        return false;
    }
    bytes nameLength = strlen(name);
    if (nameLength == 0 || nameLength > BOX_TEXT_MAX) {
        THROW("manifest: application.name must be 1..%u characters", BOX_TEXT_MAX);
        return false;
    }
    if (!validateVersion(BoxManifest_version(manifest)))
        return false;
    const char *description = BoxManifest_description(manifest);
    if (description != nullptr && strlen(description) > BOX_DESCRIPTION_MAX) {
        THROW("manifest: application.description exceeds %u characters", BOX_DESCRIPTION_MAX);
        return false;
    }
    const char *icon = BoxManifest_icon(manifest);
    if (icon != nullptr && !validateRelativePath(icon, "application.icon"))
        return false;
    if (!validateRelativePath(BoxManifest_entry(manifest), "payload.entry"))
        return false;
    bytes authorCount = BoxManifest_authorCount(manifest);
    char **authors = BoxManifest_authors(manifest);
    for (bytes index = 0; index < authorCount; ++index) {
        const char *author = authors[index];
        if (author == nullptr || *author == '\0' || strlen(author) > BOX_TEXT_MAX) {
            THROW("manifest: application.authors entries must be 1..%u characters", BOX_TEXT_MAX);
            return false;
        }
    }
    bytes fileCount = BoxManifest_fileCount(manifest);
    char **sources = BoxManifest_sources(manifest);
    char **destinations = BoxManifest_destinations(manifest);
    for (bytes index = 0; index < fileCount; ++index) {
        const char *source = sources[index];
        if (!validateRelativePath(source, "payload.files source"))
            return false;
        const char *destination = destinations[index];
        if (destination != nullptr && !validateRelativePath(destination, "payload.files destination"))
            return false;
    }
    if (BoxManifest_statePolicy(manifest) == nullptr)
        BoxManifest_setStatePolicy(manifest, copyText("clean"));
    const char *statePolicy = BoxManifest_statePolicy(manifest);
    if (statePolicy == nullptr)
        return false;
    if (strcmp(statePolicy, "clean") != 0) {
        THROW("manifest: statePolicy must be \"clean\"; exporting machine or user state is not implemented");
        return false;
    }
    if (!validateFormats(manifest, "formats"))
        return false;
    return validateDestinations(manifest);
}

// Release an owned string list; the count is informational only.
static fn freeTextList(char **items, bytes count) {
    for (bytes index = 0; index < count; ++index)
        free(items[index]);
    free(items);
}

// Join a base path and a relative path with exactly one separator.
static char *joinPath(const char *base, const char *relative) {
    bytes baseLength = strlen(base);
    bool separator = baseLength > 0 && base[baseLength - 1] != '/';
    bytes relativeLength = strlen(relative);
    char *path = allocateText(baseLength + (separator ? 1 : 0) + relativeLength + 1);
    if (path == nullptr)
        return nullptr;
    memcpy(path, base, baseLength);
    if (separator)
        path[baseLength] = '/';
    memcpy(path + baseLength + (separator ? 1 : 0), relative, relativeLength + 1);
    return path;
}

// Return the directory containing a path, or "." when it has no separator.
static char *parentOf(const char *path) {
    char *directory = copyText(path);
    if (directory == nullptr)
        return nullptr;
    char *slash = strrchr(directory, '/');
    if (slash == nullptr) {
        free(directory);
        return copyText(".");
    }
    if (slash == directory) {
        slash[1] = '\0';
        return directory;
    }
    *slash = '\0';
    return directory;
}

// Build the private staging path next to the export destination.
static char *stagingPath(const char *parent) {
    char suffix[64];
    snprintf(suffix, sizeof suffix, ".b-box-staging-%lld", (i64) getpid());
    return joinPath(parent, suffix);
}

// Remove a staged tree after a failed export; report a refusal, never silence.
static bool removeTree(const char *path) {
    struct stat info;
    if (lstat(path, &info) != 0)
        return true;
    if (S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode)) {
        DIR *directory = opendir(path);
        if (directory == nullptr) {
            THROW("cannot clean staged export directory: %s", path);
            return false;
        }
        bool removed = true;
        struct dirent *entry;
        while ((entry = readdir(directory)) != nullptr) {
            const char *name = dirent_d_name(entry);
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            char *child = joinPath(path, name);
            if (child == nullptr) {
                removed = false;
                break;
            }
            removed = removeTree(child) && removed;
            free(child);
        }
        closedir(directory);
        if (rmdir(path) != 0) {
            THROW("cannot clean staged export directory: %s", path);
            removed = false;
        }
        return removed;
    }
    if (unlink(path) != 0) {
        THROW("cannot clean staged export file: %s", path);
        return false;
    }
    return true;
}

// Copy one regular file, preserving its permission bits.
static bool copyRegularFile(const char *source, const char *target, mode_t mode) {
    i32 input = open(source, O_RDONLY | BOX_BINARY_MODE);
    if (input < 0) {
        THROW("cannot read payload file %s: %s", source, strerror(errno));
        return false;
    }
    i32 output = open(target, O_WRONLY | O_CREAT | O_EXCL | BOX_BINARY_MODE, mode & 0777);
    if (output < 0) {
        THROW("cannot create payload file %s: %s", target, strerror(errno));
        close(input);
        return false;
    }
    char buffer[BOX_COPY_BUFFER];
    bool copied = true;
    while (copied) {
        ssize_t count = read(input, buffer, sizeof buffer);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            THROW("cannot read payload file %s: %s", source, strerror(errno));
            copied = false;
            break;
        }
        if (count == 0)
            break;
        ssize_t offset = 0;
        while (offset < count) {
            ssize_t written = write(output, buffer + offset, (bytes) (count - offset));
            if (written < 0) {
                if (errno == EINTR)
                    continue;
                THROW("cannot write payload file %s: %s", target, strerror(errno));
                copied = false;
                break;
            }
            offset += written;
        }
    }
    if (copied && Util_setFileMode(output, mode & 0777) != 0) {
        THROW("cannot set payload permissions on %s: %s", target, strerror(errno));
        copied = false;
    }
    if (close(output) != 0 && copied) {
        THROW("cannot finalize payload file %s: %s", target, strerror(errno));
        copied = false;
    }
    close(input);
    if (!copied)
        unlink(target);
    return copied;
}

// Copy a declared payload entry, refusing symlinks and unsupported types.
static bool copyTree(const char *source, const char *target, bytes depth) {
    if (depth > BOX_COPY_DEPTH_MAX) {
        THROW("payload tree is deeper than %u levels: %s", BOX_COPY_DEPTH_MAX, source);
        return false;
    }
    struct stat info;
    if (lstat(source, &info) != 0) {
        THROW("payload source is unavailable: %s", source);
        return false;
    }
    if (S_ISLNK(info.st_mode)) {
        THROW("refusing to export a symbolic link: %s", source);
        return false;
    }
    if (S_ISREG(info.st_mode)) {
        char *parent = parentOf(target);
        if (parent == nullptr)
            return false;
        bool prepared = Util_makeDirectory(parent);
        free(parent);
        return prepared && copyRegularFile(source, target, info.st_mode);
    }
    if (!S_ISDIR(info.st_mode)) {
        THROW("payload source is not a regular file or directory: %s", source);
        return false;
    }
    char *parent = parentOf(target);
    if (parent == nullptr)
        return false;
    bool prepared = Util_makeDirectory(parent);
    free(parent);
    if (!prepared)
        return false;
    if (Util_createDirectory(target, info.st_mode & 0777) != 0) {
        THROW("cannot create payload directory %s: %s", target, strerror(errno));
        return false;
    }
    DIR *directory = opendir(source);
    if (directory == nullptr) {
        THROW("cannot open payload directory %s: %s", source, strerror(errno));
        return false;
    }
    bool copied = true;
    struct dirent *entry;
    while (copied && (entry = readdir(directory)) != nullptr) {
        const char *name = dirent_d_name(entry);
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        char *childSource = joinPath(source, name);
        char *childTarget = childSource == nullptr ? nullptr : joinPath(target, name);
        if (childTarget == nullptr) {
            free(childSource);
            copied = false;
            break;
        }
        copied = copyTree(childSource, childTarget, depth + 1);
        free(childSource);
        free(childTarget);
    }
    closedir(directory);
    return copied;
}

// Append literal text to a bounded projection, flagging any truncation.
static fn emitText(char *dest, bytes capacity, bytes *used, bool *outTruncated, const char *text) {
    bytes length = strlen(text);
    if (*used + length >= capacity) {
        bytes room = capacity > 0 && *used < capacity - 1 ? capacity - 1 - *used : 0;
        if (room > 0)
            memcpy(dest + *used, text, room);
        *used += room;
        if (capacity > 0)
            dest[*used] = '\0';
        *outTruncated = true;
        return;
    }
    memcpy(dest + *used, text, length);
    *used += length;
    dest[*used] = '\0';
}

// Append one quoted, escaped string field.
static fn emitQuoted(char *dest, bytes capacity, bytes *used, bool *outTruncated, const char *text) {
    emitText(dest, capacity, used, outTruncated, "\"");
    for (const char *position = text; *position != '\0'; ++position) {
        char character = *position;
        if (character == '"' || character == '\\') {
            char escaped[3] = { '\\', character, '\0' };
            emitText(dest, capacity, used, outTruncated, escaped);
        } else if (character == '\n') {
            emitText(dest, capacity, used, outTruncated, "\\n");
        } else if (character == '\t') {
            emitText(dest, capacity, used, outTruncated, "\\t");
        } else if (character == '\r') {
            emitText(dest, capacity, used, outTruncated, "\\r");
        } else {
            char single[2] = { character, '\0' };
            emitText(dest, capacity, used, outTruncated, single);
        }
    }
    emitText(dest, capacity, used, outTruncated, "\"");
}

// Append one u32 count.
static fn emitCount(char *dest, bytes capacity, bytes *used, bool *outTruncated, bytes value) {
    char text[32];
    snprintf(text, sizeof text, "%zu", value);
    emitText(dest, capacity, used, outTruncated, text);
}

// Append a bracketed list of quoted strings.
static fn emitList(char *dest, bytes capacity, bytes *used, bool *outTruncated, char **items, bytes count) {
    emitText(dest, capacity, used, outTruncated, "[");
    for (bytes index = 0; index < count; ++index) {
        if (index > 0)
            emitText(dest, capacity, used, outTruncated, ", ");
        emitQuoted(dest, capacity, used, outTruncated, items[index] != nullptr ? items[index] : "");
    }
    emitText(dest, capacity, used, outTruncated, "]");
}

// Read the whole manifest into owned, NUL-terminated text within a fixed cap.
static char *readManifestText(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        THROW("manifest does not exist or is not a regular file: %s", path);
        return nullptr;
    }
    if (info.st_size <= 0 || (bytes) info.st_size > BOX_MANIFEST_MAX_BYTES) {
        THROW("manifest must be 1..%u bytes: %s", BOX_MANIFEST_MAX_BYTES, path);
        return nullptr;
    }
    i32 file = open(path, O_RDONLY);
    if (file < 0) {
        THROW("cannot read manifest %s: %s", path, strerror(errno));
        return nullptr;
    }
    bytes size = (bytes) info.st_size;
    char *text = allocateText(size + 1);
    if (text == nullptr) {
        close(file);
        return nullptr;
    }
    bytes offset = 0;
    while (offset < size) {
        ssize_t count = read(file, text + offset, size - offset);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            THROW("cannot read manifest %s: %s", path, strerror(errno));
            free(text);
            close(file);
            return nullptr;
        }
        if (count == 0)
            break;
        offset += (bytes) count;
    }
    close(file);
    text[offset] = '\0';
    return text;
}

// Write the starter schema-1 form without replacing an existing path.
i32 BoxManifest_writeTemplate(const char *path) {
    static const char template[] =
        "{\n"
        "  \"application\": {\n"
        "    \"id\": \"com.example.myapp\",\n"
        "    \"name\": \"My Application\",\n"
        "    \"version\": \"1.0.0\",\n"
        "    \"authors\": [\"Your Name\"],\n"
        "    \"publisher\": \"Your Organization\",\n"
        "    \"description\": \"Describe your application.\",\n"
        "    \"license\": \"UNLICENSED\"\n"
        "  },\n"
        "  \"payload\": {\n"
        "    \"entry\": \"dist/myapp\",\n"
        "    \"files\": []\n"
        "  },\n"
        "  \"targets\": [],\n"
        "  \"formats\": [\"portable\"],\n"
        "  \"statePolicy\": \"clean\"\n"
        "}\n";
    if (path == nullptr || *path == '\0') {
        THROW("manifest path must not be empty");
        return EXIT_FAILURE;
    }
    i32 file = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) {
        THROW("cannot create manifest without replacing an existing path: %s", path);
        return EXIT_FAILURE;
    }
    bytes length = sizeof(template) - 1;
    bytes offset = 0;
    while (offset < length) {
        ssize_t written = write(file, template + offset, length - offset);
        if (written <= 0)
            break;
        offset += (bytes) written;
    }
    i32 closed = close(file);
    if (offset != length || closed != 0) {
        unlink(path);
        THROW("could not completely write manifest: %s", path);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

// Parse and validate a manifest file into an owned record.
BoxManifest *BoxManifest_load(const char *path) {
    if (path == nullptr || *path == '\0') {
        THROW("manifest path must not be empty");
        return nullptr;
    }
    char *text = readManifestText(path);
    if (text == nullptr)
        return nullptr;
    BoxManifest *manifest = (BoxManifest*) allocateText(sizeof(BoxManifest));
    if (manifest == nullptr) {
        free(text);
        return nullptr;
    }
    memset(manifest, 0, sizeof(BoxManifest));
    BoxManifest_setSchemaVersion(manifest, 1);   // locked: there is only one schema
    BoxManifest_setBaseDirectory(manifest, parentOf(path));
    const char *cursor = text;
    bool valid = BoxManifest_baseDirectory(manifest) != nullptr && parseRoot(&cursor, manifest);
    if (valid && *skipSpace(cursor) != '\0') {
        THROW("manifest has unexpected trailing content");
        valid = false;
    }
    if (valid)
        valid = validateManifest(manifest);
    free(text);
    if (!valid) {
        BoxManifest_free(manifest);
        return nullptr;
    }
    return manifest;
}

// Release every owned field; a null record is a no-op.
fn BoxManifest_free(BoxManifest *self) {
    if (self == nullptr)
        return;
    free(BoxManifest_baseDirectory(self));
    free(BoxManifest_identifier(self));
    free(BoxManifest_name(self));
    free(BoxManifest_version(self));
    free(BoxManifest_publisher(self));
    free(BoxManifest_description(self));
    free(BoxManifest_license(self));
    free(BoxManifest_homepage(self));
    free(BoxManifest_icon(self));
    free(BoxManifest_entry(self));
    free(BoxManifest_statePolicy(self));
    freeTextList(BoxManifest_authors(self), BoxManifest_authorCount(self));
    bytes fileCount = BoxManifest_fileCount(self);
    freeTextList(BoxManifest_sources(self), fileCount);
    freeTextList(BoxManifest_destinations(self), fileCount);
    freeTextList(BoxManifest_formats(self), BoxManifest_formatCount(self));
    free(self);
}

// Export the declared payload as a portable directory payload.
i32 BoxManifest_export(const BoxManifest *self, const char *destination) {
    if (self == nullptr || destination == nullptr || *destination == '\0') {
        THROW("export requires a loaded manifest and a destination path");
        return EXIT_FAILURE;
    }
    bytes formatCount = BoxManifest_formatCount(self);
    char **formats = BoxManifest_formats(self);
    for (bytes index = 0; index < formatCount; ++index) {
        if (strcmp(formats[index], BOX_FORMAT_PORTABLE) != 0) {
            THROW("export format is not implemented yet: %s (only %s is available)", formats[index], BOX_FORMAT_PORTABLE);
            return EXIT_FAILURE;
        }
    }
    const char *baseDir = BoxManifest_baseDirectory(self);
    const char *entry = BoxManifest_entry(self);
    char *entrySource = joinPath(baseDir, entry);
    if (entrySource == nullptr)
        return EXIT_FAILURE;
    struct stat info;
    if (lstat(entrySource, &info) != 0 || S_ISLNK(info.st_mode)) {
        THROW("payload entry is unavailable or a symbolic link: %s", entrySource);
        free(entrySource);
        return EXIT_FAILURE;
    }
    bytes fileCount = BoxManifest_fileCount(self);
    char **sources = BoxManifest_sources(self);
    char **destinations = BoxManifest_destinations(self);
    if (S_ISDIR(info.st_mode)) {
        const char *entryName = baseNameOf(entry);
        bytes nameLength = strlen(entryName);
        for (bytes index = 0; index < fileCount; ++index) {
            const char *destName = destinations[index] != nullptr
                ? destinations[index] : baseNameOf(sources[index]);
            if (strncmp(destName, entryName, nameLength) == 0 &&
                (destName[nameLength] == '\0' || destName[nameLength] == '/')) {
                THROW("payload file destination is inside the entry directory: %s", destName);
                free(entrySource);
                return EXIT_FAILURE;
            }
        }
    }
    for (bytes index = 0; index < fileCount; ++index) {
        char *source = joinPath(baseDir, sources[index]);
        bool available = source != nullptr && lstat(source, &info) == 0 && !S_ISLNK(info.st_mode);
        if (!available) {
            THROW("payload file is unavailable or a symbolic link: %s", source != nullptr ? source : sources[index]);
            free(source);
            free(entrySource);
            return EXIT_FAILURE;
        }
        free(source);
    }
    if (lstat(destination, &info) == 0) {
        THROW("export destination already exists; refusing to replace it: %s", destination);
        free(entrySource);
        return EXIT_FAILURE;
    }
    char *parent = parentOf(destination);
    char *staging = parent == nullptr ? nullptr : stagingPath(parent);
    if (staging == nullptr) {
        free(parent);
        free(entrySource);
        return EXIT_FAILURE;
    }
    if (lstat(parent, &info) != 0 || !S_ISDIR(info.st_mode)) {
        THROW("export destination parent is not a directory: %s", parent);
        free(staging);
        free(parent);
        free(entrySource);
        return EXIT_FAILURE;
    }
    if (lstat(staging, &info) == 0 || Util_createDirectory(staging, 0755) != 0) {
        THROW("cannot create private staging directory: %s", staging);
        free(staging);
        free(parent);
        free(entrySource);
        return EXIT_FAILURE;
    }
    bool copied = true;
    char *entryTarget = joinPath(staging, baseNameOf(entry));
    if (entryTarget == nullptr)
        copied = false;
    else
        copied = copyTree(entrySource, entryTarget, 0);
    for (bytes index = 0; index < fileCount && copied; ++index) {
        char *source = joinPath(baseDir, sources[index]);
        if (source == nullptr) {
            copied = false;
            break;
        }
        const char *declared = destinations[index];
        char *target = joinPath(staging, declared != nullptr ? declared : baseNameOf(sources[index]));
        if (target == nullptr)
            copied = false;
        else
            copied = copyTree(source, target, 0);
        free(target);
        free(source);
    }
    i32 status = EXIT_SUCCESS;
    if (copied && entryTarget != nullptr && lstat(entryTarget, &info) != 0) {
        THROW("staged export is missing its declared entry");
        copied = false;
    }
    if (!copied) {
        removeTree(staging);
        THROW("export failed; no destination was created or replaced");
        status = EXIT_FAILURE;
    } else if (rename(staging, destination) != 0) {
        THROW("cannot publish export to %s: %s", destination, strerror(errno));
        removeTree(staging);
        status = EXIT_FAILURE;
    }
    free(entryTarget);
    free(staging);
    free(parent);
    free(entrySource);
    return status;
}

// Project the manifest's value state; a null record writes "nullptr".
fn BoxManifest_toString(const BoxManifest *self, char *dest, bytes capacity, bool *outTruncated) {
    if (dest == nullptr)
        return;
    if (capacity == 0) {
        *outTruncated = true;
        return;
    }
    dest[0] = '\0';
    *outTruncated = false;
    bytes used = 0;
    if (self == nullptr) {
        emitText(dest, capacity, &used, outTruncated, "nullptr");
        return;
    }
    const char *name = BoxManifest_name(self);
    const char *version = BoxManifest_version(self);
    const char *identifier = BoxManifest_identifier(self);
    const char *entry = BoxManifest_entry(self);
    const char *statePolicy = BoxManifest_statePolicy(self);
    emitText(dest, capacity, &used, outTruncated, name != nullptr ? name : "unnamed");
    emitText(dest, capacity, &used, outTruncated, " ");
    emitText(dest, capacity, &used, outTruncated, version != nullptr ? version : "unversioned");
    emitText(dest, capacity, &used, outTruncated, " [");
    emitText(dest, capacity, &used, outTruncated, identifier != nullptr ? identifier : "unidentified");
    emitText(dest, capacity, &used, outTruncated, "] entry=");
    emitText(dest, capacity, &used, outTruncated, entry != nullptr ? entry : "missing");
    emitText(dest, capacity, &used, outTruncated, " authors=");
    emitCount(dest, capacity, &used, outTruncated, BoxManifest_authorCount(self));
    emitText(dest, capacity, &used, outTruncated, " files=");
    emitCount(dest, capacity, &used, outTruncated, BoxManifest_fileCount(self));
    emitText(dest, capacity, &used, outTruncated, " formats=");
    emitList(dest, capacity, &used, outTruncated, BoxManifest_formats(self), BoxManifest_formatCount(self));
    emitText(dest, capacity, &used, outTruncated, " state=");
    emitText(dest, capacity, &used, outTruncated, statePolicy != nullptr ? statePolicy : "unset");
}

// Dump this class's own fields in declaration order; one layer, no recursion.
fn BoxManifest_toStringStruct(const BoxManifest *self, char *dest, bytes capacity, bool *outTruncated) {
    if (dest == nullptr)
        return;
    if (capacity == 0) {
        *outTruncated = true;
        return;
    }
    dest[0] = '\0';
    *outTruncated = false;
    bytes used = 0;
    if (self == nullptr) {
        emitText(dest, capacity, &used, outTruncated, "nullptr");
        return;
    }
    const char *baseDir = BoxManifest_baseDirectory(self);
    const char *identifier = BoxManifest_identifier(self);
    const char *name = BoxManifest_name(self);
    const char *version = BoxManifest_version(self);
    const char *publisher = BoxManifest_publisher(self);
    const char *description = BoxManifest_description(self);
    const char *license = BoxManifest_license(self);
    const char *homepage = BoxManifest_homepage(self);
    const char *icon = BoxManifest_icon(self);
    const char *entry = BoxManifest_entry(self);
    const char *statePolicy = BoxManifest_statePolicy(self);
    bytes authorCount = BoxManifest_authorCount(self);
    bytes fileCount = BoxManifest_fileCount(self);
    bytes formatCount = BoxManifest_formatCount(self);
    emitText(dest, capacity, &used, outTruncated, "{baseDirectory: ");
    emitQuoted(dest, capacity, &used, outTruncated, baseDir != nullptr ? baseDir : "");
    emitText(dest, capacity, &used, outTruncated, ", identifier: ");
    emitQuoted(dest, capacity, &used, outTruncated, identifier != nullptr ? identifier : "");
    emitText(dest, capacity, &used, outTruncated, ", name: ");
    emitQuoted(dest, capacity, &used, outTruncated, name != nullptr ? name : "");
    emitText(dest, capacity, &used, outTruncated, ", version: ");
    emitQuoted(dest, capacity, &used, outTruncated, version != nullptr ? version : "");
    emitText(dest, capacity, &used, outTruncated, ", publisher: ");
    emitQuoted(dest, capacity, &used, outTruncated, publisher != nullptr ? publisher : "");
    emitText(dest, capacity, &used, outTruncated, ", description: ");
    emitQuoted(dest, capacity, &used, outTruncated, description != nullptr ? description : "");
    emitText(dest, capacity, &used, outTruncated, ", license: ");
    emitQuoted(dest, capacity, &used, outTruncated, license != nullptr ? license : "");
    emitText(dest, capacity, &used, outTruncated, ", homepage: ");
    emitQuoted(dest, capacity, &used, outTruncated, homepage != nullptr ? homepage : "");
    emitText(dest, capacity, &used, outTruncated, ", icon: ");
    emitQuoted(dest, capacity, &used, outTruncated, icon != nullptr ? icon : "");
    emitText(dest, capacity, &used, outTruncated, ", entry: ");
    emitQuoted(dest, capacity, &used, outTruncated, entry != nullptr ? entry : "");
    emitText(dest, capacity, &used, outTruncated, ", statePolicy: ");
    emitQuoted(dest, capacity, &used, outTruncated, statePolicy != nullptr ? statePolicy : "");
    emitText(dest, capacity, &used, outTruncated, ", authors: ");
    emitList(dest, capacity, &used, outTruncated, BoxManifest_authors(self), authorCount);
    emitText(dest, capacity, &used, outTruncated, ", authorCount: ");
    emitCount(dest, capacity, &used, outTruncated, authorCount);
    emitText(dest, capacity, &used, outTruncated, ", sources: ");
    emitList(dest, capacity, &used, outTruncated, BoxManifest_sources(self), fileCount);
    emitText(dest, capacity, &used, outTruncated, ", destinations: ");
    emitList(dest, capacity, &used, outTruncated, BoxManifest_destinations(self), fileCount);
    emitText(dest, capacity, &used, outTruncated, ", fileCount: ");
    emitCount(dest, capacity, &used, outTruncated, fileCount);
    emitText(dest, capacity, &used, outTruncated, ", formats: ");
    emitList(dest, capacity, &used, outTruncated, BoxManifest_formats(self), formatCount);
    emitText(dest, capacity, &used, outTruncated, ", formatCount: ");
    emitCount(dest, capacity, &used, outTruncated, formatCount);
    emitText(dest, capacity, &used, outTruncated, "}");
}
