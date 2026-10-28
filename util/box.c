// CLASS: BoxManifest — manifest creation, strict validation and portable export.
// DEFINITION: box is b's packaging surface. `b create manifest <path>` writes a
// starter JSON form that a human fills in; `b box <manifest> <destination>`
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
 * int schemaVersion — only 1 is accepted.
 * char *identifier — stable identity for platform mapping.
 * char *name — display name. char *version — author release version.
 * char *publisher — publisher text. char *description — short summary.
 * char *license — license family. char *homepage — project URL.
 * char *icon — relative icon path. char *entry — relative payload entry.
 * char *statePolicy — only "clean" is accepted.
 * char **authors, size_t authorCount — declared authors.
 * char **sources, char **destinations, size_t fileCount — payload files.
 * char **formats, size_t formatCount — requested export formats.
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
static char *allocateText(size_t size) {
    char *text = malloc(size);
    if (text == nullptr)
        THROW("out of memory while reading a manifest");
    return text;
}

// Copy owned text; nullptr propagates so callers can fail without a check.
static char *copyText(const char *text) {
    if (text == nullptr)
        return nullptr;
    size_t length = strlen(text);
    char *copy = allocateText(length + 1);
    if (copy == nullptr)
        return nullptr;
    memcpy(copy, text, length + 1);
    return copy;
}

// Append one owned string to a growable list bounded by BOX_ITEM_MAX.
static bool appendText(char ***items, size_t *count, char *value) {
    if (*count >= BOX_ITEM_MAX) {
        THROW("manifest declares more than %u entries", BOX_ITEM_MAX);
        return false;
    }
    size_t next = *count + 1;
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
    size_t capacity = strlen(position) + 1;
    char *text = allocateText(capacity);
    if (text == nullptr)
        return false;
    size_t length = 0;
    while (*position != '\0' && *position != '"') {
        unsigned char character = (unsigned char) *position;
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
static bool parseStringArray(const char **cursor, const char *field, char ***items, size_t *count) {
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
static bool parseInteger(const char **cursor, const char *field, int *out) {
    const char *position = skipSpace(*cursor);
    if (*position < '0' || *position > '9') {
        THROW("manifest: %s must be an integer", field);
        return false;
    }
    long value = 0;
    while (*position >= '0' && *position <= '9') {
        value = value * 10 + (*position - '0');
        if (value > 1000000) {
            THROW("manifest: %s is out of range", field);
            return false;
        }
        ++position;
    }
    *cursor = position;
    *out = (int) value;
    return true;
}

// Parse the declared schema number.
static bool parseSchemaVersion(const char **cursor, int *out) {
    return parseInteger(cursor, "schemaVersion", out);
}

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
            target = &(*manifest).identifier;
            seen = &identifierSeen;
        } else if (strcmp(key, "name") == 0) {
            field = "application.name";
            target = &(*manifest).name;
            seen = &nameSeen;
        } else if (strcmp(key, "version") == 0) {
            field = "application.version";
            target = &(*manifest).version;
            seen = &versionSeen;
        } else if (strcmp(key, "publisher") == 0) {
            field = "application.publisher";
            target = &(*manifest).publisher;
            seen = &publisherSeen;
        } else if (strcmp(key, "description") == 0) {
            field = "application.description";
            target = &(*manifest).description;
            seen = &descriptionSeen;
        } else if (strcmp(key, "license") == 0) {
            field = "application.license";
            target = &(*manifest).license;
            seen = &licenseSeen;
        } else if (strcmp(key, "homepage") == 0) {
            field = "application.homepage";
            target = &(*manifest).homepage;
            seen = &homepageSeen;
        } else if (strcmp(key, "icon") == 0) {
            field = "application.icon";
            target = &(*manifest).icon;
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
            bool parsed = parseStringArray(cursor, "application.authors", &(*manifest).authors, &(*manifest).authorCount);
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
                parsed = parseString(cursor, "payload.entry", &(*manifest).entry);
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
                        size_t sourceCount = (*manifest).fileCount;
                        size_t destinationCount = (*manifest).fileCount;
                        while (true) {
                            char *source = nullptr, *destination = nullptr;
                            if (!parseFileEntry(cursor, &source, &destination)) {
                                free(source);
                                free(destination);
                                parsed = false;
                                break;
                            }
                            if (!appendText(&(*manifest).sources, &sourceCount, source)) {
                                free(source);
                                free(destination);
                                parsed = false;
                                break;
                            }
                            if (!appendText(&(*manifest).destinations, &destinationCount, destination)) {
                                free((*manifest).sources[sourceCount - 1]);
                                (*manifest).sources[sourceCount - 1] = nullptr;
                                sourceCount -= 1;
                                free(destination);
                                parsed = false;
                                break;
                            }
                            (*manifest).fileCount = sourceCount;
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
                parsed = parseSchemaVersion(cursor, &(*manifest).schemaVersion);
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
                parsed = parseStringArray(cursor, "formats", &(*manifest).formats, &(*manifest).formatCount);
            }
        } else if (strcmp(key, "statePolicy") == 0) {
            if (stateSeen) {
                THROW("manifest: duplicate statePolicy");
                parsed = false;
            } else {
                stateSeen = true;
                parsed = parseString(cursor, "statePolicy", &(*manifest).statePolicy);
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
    size_t length = strlen(text);
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
    size_t length = strlen(text);
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
        size_t length = slash == nullptr ? strlen(position) : (size_t) (slash - position);
        if (length == 0) {
            THROW("manifest: %s has an empty path component: %s", field, text);
            return false;
        }
        if ((length == 1 && position[0] == '.') || (length == 2 && position[0] == '.' && position[1] == '.')) {
            THROW("manifest: %s must not contain '.' or '..' components: %s", field, text);
            return false;
        }
        for (size_t index = 0; index < length; ++index) {
            if ((unsigned char) position[index] < 0x20) {
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
    if ((*self).formatCount == 0) {
        THROW("manifest: formats must declare at least one export format");
        return false;
    }
    for (size_t index = 0; index < (*self).formatCount; ++index) {
        const char *format = (*self).formats[index];
        if (format == nullptr || *format == '\0' || strlen(format) > BOX_FORMAT_MAX) {
            THROW("manifest: formats contains an invalid entry");
            return false;
        }
        if (!validateCharacters(format, field, "abcdefghijklmnopqrstuvwxyz0123456789-"))
            return false;
        bool known = false;
        for (size_t candidate = 0; candidate < sizeof BOX_KNOWN_FORMATS / sizeof BOX_KNOWN_FORMATS[0]; ++candidate)
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
    const char *entryName = baseNameOf((*self).entry);
    for (size_t index = 0; index < (*self).fileCount; ++index) {
        const char *destination = (*self).destinations[index] != nullptr
            ? (*self).destinations[index] : baseNameOf((*self).sources[index]);
        if (strcmp(destination, entryName) == 0) {
            THROW("manifest: payload entry and %s share one export destination: %s", (*self).sources[index], destination);
            return false;
        }
        for (size_t other = 0; other < index; ++other) {
            const char *earlier = (*self).destinations[other] != nullptr
                ? (*self).destinations[other] : baseNameOf((*self).sources[other]);
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
    if ((*manifest).schemaVersion != 1) {
        THROW("manifest: schemaVersion must be 1, not %d", (*manifest).schemaVersion);
        return false;
    }
    if (!validateIdentifier((*manifest).identifier))
        return false;
    if ((*manifest).name == nullptr) {
        THROW("manifest: application.name is required");
        return false;
    }
    size_t nameLength = strlen((*manifest).name);
    if (nameLength == 0 || nameLength > BOX_TEXT_MAX) {
        THROW("manifest: application.name must be 1..%u characters", BOX_TEXT_MAX);
        return false;
    }
    if (!validateVersion((*manifest).version))
        return false;
    if ((*manifest).description != nullptr && strlen((*manifest).description) > BOX_DESCRIPTION_MAX) {
        THROW("manifest: application.description exceeds %u characters", BOX_DESCRIPTION_MAX);
        return false;
    }
    if ((*manifest).icon != nullptr && !validateRelativePath((*manifest).icon, "application.icon"))
        return false;
    if (!validateRelativePath((*manifest).entry, "payload.entry"))
        return false;
    for (size_t index = 0; index < (*manifest).authorCount; ++index) {
        const char *author = (*manifest).authors[index];
        if (author == nullptr || *author == '\0' || strlen(author) > BOX_TEXT_MAX) {
            THROW("manifest: application.authors entries must be 1..%u characters", BOX_TEXT_MAX);
            return false;
        }
    }
    for (size_t index = 0; index < (*manifest).fileCount; ++index) {
        const char *source = (*manifest).sources[index];
        if (!validateRelativePath(source, "payload.files source"))
            return false;
        const char *destination = (*manifest).destinations[index];
        if (destination != nullptr && !validateRelativePath(destination, "payload.files destination"))
            return false;
    }
    if ((*manifest).statePolicy == nullptr)
        (*manifest).statePolicy = copyText("clean");
    if ((*manifest).statePolicy == nullptr)
        return false;
    if (strcmp((*manifest).statePolicy, "clean") != 0) {
        THROW("manifest: statePolicy must be \"clean\"; exporting machine or user state is not implemented");
        return false;
    }
    if (!validateFormats(manifest, "formats"))
        return false;
    return validateDestinations(manifest);
}

// Release an owned string list; the count is informational only.
static void freeTextList(char **items, size_t count) {
    for (size_t index = 0; index < count; ++index)
        free(items[index]);
    free(items);
}

// Join a base path and a relative path with exactly one separator.
static char *joinPath(const char *base, const char *relative) {
    size_t baseLength = strlen(base);
    bool separator = baseLength > 0 && base[baseLength - 1] != '/';
    size_t relativeLength = strlen(relative);
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
    snprintf(suffix, sizeof suffix, ".b-box-staging-%ld", (long) getpid());
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
            if (strcmp((*entry).d_name, ".") == 0 || strcmp((*entry).d_name, "..") == 0)
                continue;
            char *child = joinPath(path, (*entry).d_name);
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
    int input = open(source, O_RDONLY);
    if (input < 0) {
        THROW("cannot read payload file %s: %s", source, strerror(errno));
        return false;
    }
    int output = open(target, O_WRONLY | O_CREAT | O_EXCL, mode & 0777);
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
            ssize_t written = write(output, buffer + offset, (size_t) (count - offset));
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
    if (copied && fchmod(output, mode & 0777) != 0) {
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
static bool copyTree(const char *source, const char *target, size_t depth) {
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
    if (mkdir(target, info.st_mode & 0777) != 0) {
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
        if (strcmp((*entry).d_name, ".") == 0 || strcmp((*entry).d_name, "..") == 0)
            continue;
        char *childSource = joinPath(source, (*entry).d_name);
        char *childTarget = childSource == nullptr ? nullptr : joinPath(target, (*entry).d_name);
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
static void emitText(char *dest, size_t capacity, size_t *used, bool *outTruncated, const char *text) {
    size_t length = strlen(text);
    if (*used + length >= capacity) {
        size_t room = capacity > 0 && *used < capacity - 1 ? capacity - 1 - *used : 0;
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
static void emitQuoted(char *dest, size_t capacity, size_t *used, bool *outTruncated, const char *text) {
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

// Append one unsigned count.
static void emitCount(char *dest, size_t capacity, size_t *used, bool *outTruncated, size_t value) {
    char text[32];
    snprintf(text, sizeof text, "%zu", value);
    emitText(dest, capacity, used, outTruncated, text);
}

// Append a bracketed list of quoted strings.
static void emitList(char *dest, size_t capacity, size_t *used, bool *outTruncated, char **items, size_t count) {
    emitText(dest, capacity, used, outTruncated, "[");
    for (size_t index = 0; index < count; ++index) {
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
    if (info.st_size <= 0 || (size_t) info.st_size > BOX_MANIFEST_MAX_BYTES) {
        THROW("manifest must be 1..%u bytes: %s", BOX_MANIFEST_MAX_BYTES, path);
        return nullptr;
    }
    int file = open(path, O_RDONLY);
    if (file < 0) {
        THROW("cannot read manifest %s: %s", path, strerror(errno));
        return nullptr;
    }
    size_t size = (size_t) info.st_size;
    char *text = allocateText(size + 1);
    if (text == nullptr) {
        close(file);
        return nullptr;
    }
    size_t offset = 0;
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
        offset += (size_t) count;
    }
    close(file);
    text[offset] = '\0';
    return text;
}

// Write the starter schema-1 form without replacing an existing path.
int BoxManifest_writeTemplate(const char *path) {
    static const char template[] =
        "{\n"
        "  \"schemaVersion\": 1,\n"
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
    int file = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) {
        THROW("cannot create manifest without replacing an existing path: %s", path);
        return EXIT_FAILURE;
    }
    size_t length = sizeof(template) - 1;
    size_t offset = 0;
    while (offset < length) {
        ssize_t written = write(file, template + offset, length - offset);
        if (written <= 0)
            break;
        offset += (size_t) written;
    }
    int closed = close(file);
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
    (*manifest).baseDirectory = parentOf(path);
    const char *cursor = text;
    bool valid = (*manifest).baseDirectory != nullptr && parseRoot(&cursor, manifest);
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
void BoxManifest_free(BoxManifest *self) {
    if (self == nullptr)
        return;
    free((*self).baseDirectory);
    free((*self).identifier);
    free((*self).name);
    free((*self).version);
    free((*self).publisher);
    free((*self).description);
    free((*self).license);
    free((*self).homepage);
    free((*self).icon);
    free((*self).entry);
    free((*self).statePolicy);
    freeTextList((*self).authors, (*self).authorCount);
    freeTextList((*self).sources, (*self).fileCount);
    freeTextList((*self).destinations, (*self).fileCount);
    freeTextList((*self).formats, (*self).formatCount);
    free(self);
}

// Export the declared payload as a portable directory payload.
int BoxManifest_export(const BoxManifest *self, const char *destination) {
    if (self == nullptr || destination == nullptr || *destination == '\0') {
        THROW("export requires a loaded manifest and a destination path");
        return EXIT_FAILURE;
    }
    for (size_t index = 0; index < (*self).formatCount; ++index) {
        if (strcmp((*self).formats[index], BOX_FORMAT_PORTABLE) != 0) {
            THROW("export format is not implemented yet: %s (only %s is available)", (*self).formats[index], BOX_FORMAT_PORTABLE);
            return EXIT_FAILURE;
        }
    }
    char *entrySource = joinPath((*self).baseDirectory, (*self).entry);
    if (entrySource == nullptr)
        return EXIT_FAILURE;
    struct stat info;
    if (lstat(entrySource, &info) != 0 || S_ISLNK(info.st_mode)) {
        THROW("payload entry is unavailable or a symbolic link: %s", entrySource);
        free(entrySource);
        return EXIT_FAILURE;
    }
    if (S_ISDIR(info.st_mode)) {
        const char *entryName = baseNameOf((*self).entry);
        size_t nameLength = strlen(entryName);
        for (size_t index = 0; index < (*self).fileCount; ++index) {
            const char *destination = (*self).destinations[index] != nullptr
                ? (*self).destinations[index] : baseNameOf((*self).sources[index]);
            if (strncmp(destination, entryName, nameLength) == 0 &&
                (destination[nameLength] == '\0' || destination[nameLength] == '/')) {
                THROW("payload file destination is inside the entry directory: %s", destination);
                free(entrySource);
                return EXIT_FAILURE;
            }
        }
    }
    for (size_t index = 0; index < (*self).fileCount; ++index) {
        char *source = joinPath((*self).baseDirectory, (*self).sources[index]);
        bool available = source != nullptr && lstat(source, &info) == 0 && !S_ISLNK(info.st_mode);
        if (!available) {
            THROW("payload file is unavailable or a symbolic link: %s", source != nullptr ? source : (*self).sources[index]);
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
    if (lstat(staging, &info) == 0 || mkdir(staging, 0755) != 0) {
        THROW("cannot create private staging directory: %s", staging);
        free(staging);
        free(parent);
        free(entrySource);
        return EXIT_FAILURE;
    }
    bool copied = true;
    char *entryTarget = joinPath(staging, baseNameOf((*self).entry));
    if (entryTarget == nullptr)
        copied = false;
    else
        copied = copyTree(entrySource, entryTarget, 0);
    for (size_t index = 0; index < (*self).fileCount && copied; ++index) {
        char *source = joinPath((*self).baseDirectory, (*self).sources[index]);
        if (source == nullptr) {
            copied = false;
            break;
        }
        const char *declared = (*self).destinations[index];
        char *target = joinPath(staging, declared != nullptr ? declared : baseNameOf((*self).sources[index]));
        if (target == nullptr)
            copied = false;
        else
            copied = copyTree(source, target, 0);
        free(target);
        free(source);
    }
    int status = EXIT_SUCCESS;
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
void BoxManifest_toString(const BoxManifest *self, char *dest, size_t capacity, bool *outTruncated) {
    if (dest == nullptr)
        return;
    if (capacity == 0) {
        *outTruncated = true;
        return;
    }
    dest[0] = '\0';
    *outTruncated = false;
    size_t used = 0;
    if (self == nullptr) {
        emitText(dest, capacity, &used, outTruncated, "nullptr");
        return;
    }
    emitText(dest, capacity, &used, outTruncated, (*self).name != nullptr ? (*self).name : "unnamed");
    emitText(dest, capacity, &used, outTruncated, " ");
    emitText(dest, capacity, &used, outTruncated, (*self).version != nullptr ? (*self).version : "unversioned");
    emitText(dest, capacity, &used, outTruncated, " [");
    emitText(dest, capacity, &used, outTruncated, (*self).identifier != nullptr ? (*self).identifier : "unidentified");
    emitText(dest, capacity, &used, outTruncated, "] entry=");
    emitText(dest, capacity, &used, outTruncated, (*self).entry != nullptr ? (*self).entry : "missing");
    emitText(dest, capacity, &used, outTruncated, " authors=");
    emitCount(dest, capacity, &used, outTruncated, (*self).authorCount);
    emitText(dest, capacity, &used, outTruncated, " files=");
    emitCount(dest, capacity, &used, outTruncated, (*self).fileCount);
    emitText(dest, capacity, &used, outTruncated, " formats=");
    emitList(dest, capacity, &used, outTruncated, (*self).formats, (*self).formatCount);
    emitText(dest, capacity, &used, outTruncated, " state=");
    emitText(dest, capacity, &used, outTruncated, (*self).statePolicy != nullptr ? (*self).statePolicy : "unset");
}

// Dump this class's own fields in declaration order; one layer, no recursion.
void BoxManifest_toStringStruct(const BoxManifest *self, char *dest, size_t capacity, bool *outTruncated) {
    if (dest == nullptr)
        return;
    if (capacity == 0) {
        *outTruncated = true;
        return;
    }
    dest[0] = '\0';
    *outTruncated = false;
    size_t used = 0;
    if (self == nullptr) {
        emitText(dest, capacity, &used, outTruncated, "nullptr");
        return;
    }
    emitText(dest, capacity, &used, outTruncated, "{baseDirectory: ");
    emitQuoted(dest, capacity, &used, outTruncated, (*self).baseDirectory != nullptr ? (*self).baseDirectory : "");
    emitText(dest, capacity, &used, outTruncated, ", schemaVersion: ");
    emitCount(dest, capacity, &used, outTruncated, (size_t) (*self).schemaVersion);
    emitText(dest, capacity, &used, outTruncated, ", identifier: ");
    emitQuoted(dest, capacity, &used, outTruncated, (*self).identifier != nullptr ? (*self).identifier : "");
    emitText(dest, capacity, &used, outTruncated, ", name: ");
    emitQuoted(dest, capacity, &used, outTruncated, (*self).name != nullptr ? (*self).name : "");
    emitText(dest, capacity, &used, outTruncated, ", version: ");
    emitQuoted(dest, capacity, &used, outTruncated, (*self).version != nullptr ? (*self).version : "");
    emitText(dest, capacity, &used, outTruncated, ", publisher: ");
    emitQuoted(dest, capacity, &used, outTruncated, (*self).publisher != nullptr ? (*self).publisher : "");
    emitText(dest, capacity, &used, outTruncated, ", description: ");
    emitQuoted(dest, capacity, &used, outTruncated, (*self).description != nullptr ? (*self).description : "");
    emitText(dest, capacity, &used, outTruncated, ", license: ");
    emitQuoted(dest, capacity, &used, outTruncated, (*self).license != nullptr ? (*self).license : "");
    emitText(dest, capacity, &used, outTruncated, ", homepage: ");
    emitQuoted(dest, capacity, &used, outTruncated, (*self).homepage != nullptr ? (*self).homepage : "");
    emitText(dest, capacity, &used, outTruncated, ", icon: ");
    emitQuoted(dest, capacity, &used, outTruncated, (*self).icon != nullptr ? (*self).icon : "");
    emitText(dest, capacity, &used, outTruncated, ", entry: ");
    emitQuoted(dest, capacity, &used, outTruncated, (*self).entry != nullptr ? (*self).entry : "");
    emitText(dest, capacity, &used, outTruncated, ", statePolicy: ");
    emitQuoted(dest, capacity, &used, outTruncated, (*self).statePolicy != nullptr ? (*self).statePolicy : "");
    emitText(dest, capacity, &used, outTruncated, ", authors: ");
    emitList(dest, capacity, &used, outTruncated, (*self).authors, (*self).authorCount);
    emitText(dest, capacity, &used, outTruncated, ", authorCount: ");
    emitCount(dest, capacity, &used, outTruncated, (*self).authorCount);
    emitText(dest, capacity, &used, outTruncated, ", sources: ");
    emitList(dest, capacity, &used, outTruncated, (*self).sources, (*self).fileCount);
    emitText(dest, capacity, &used, outTruncated, ", destinations: ");
    emitList(dest, capacity, &used, outTruncated, (*self).destinations, (*self).fileCount);
    emitText(dest, capacity, &used, outTruncated, ", fileCount: ");
    emitCount(dest, capacity, &used, outTruncated, (*self).fileCount);
    emitText(dest, capacity, &used, outTruncated, ", formats: ");
    emitList(dest, capacity, &used, outTruncated, (*self).formats, (*self).formatCount);
    emitText(dest, capacity, &used, outTruncated, ", formatCount: ");
    emitCount(dest, capacity, &used, outTruncated, (*self).formatCount);
    emitText(dest, capacity, &used, outTruncated, "}");
}
