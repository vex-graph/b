// MODULE: graph — read and execute a serialized target graph.
// DEFINITION: A graph file is generated data with a fixed schema: top-level
// "targets" and "tests" arrays; each record carries name, kind, sources,
// includes, definitions, options, deps, a link plan and, for an external
// artifact, its output and the generator argv that produces it. The reader
// walks that schema directly; it builds no general JSON tree.
// OVERVIEW: Graph_command; parseGraph; plan; buildTarget; readFile; hashing.
#include "adapters/graph.h"
#include "b.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

;;DEFINITION
/* The graph is the only project knowledge b accepts, and it arrives as data:
 * target names, kinds, source paths, flag words and dependency names. Nothing
 * here knows a repository, a tier, a language or a layout. Objects are keyed by
 * a hash of the source bytes plus the exact flag words, so a cache hit is
 * decided by content, never by a timestamp. An external artifact is produced by
 * its recorded generator argv and is never relinked.
 */
;;OVERVIEW
/* MODULE: graph; PRIVATE STATIC: Rows and GraphTarget storage, cursor parsing,
 * plan/topo order, compile/archive/link, hashing.
 * OWNERSHIP: every allocation comes from Util_allocate and is returned with
 * Util_release; argv arrays are derived from owned string rows.
 */

// ── owned string rows ──────────────────────────────────────────────────────

typedef struct {
    char **items;
    int count;
    int cap;
} Rows;

static char *copyText(const char *text) {
    size_t size = strlen(text);
    char *copy = Util_allocate(size + 1);
    memcpy(copy, text, size + 1);
    return copy;
}

static void rowsPush(Rows *rows, const char *text) {
    if ((*rows).count == (*rows).cap) {
        int cap = (*rows).cap ? (*rows).cap * 2 : 8;
        char **grown = Util_allocate((size_t) cap * sizeof *grown);
        if ((*rows).count)
            memcpy(grown, (*rows).items, (size_t) (*rows).count * sizeof *grown);
        Util_release((*rows).items);
        (*rows).items = grown;
        (*rows).cap = cap;
    }
    (*rows).items[(*rows).count++] = copyText(text);
}

static void rowsPushf(Rows *rows, const char *format, const char *value) {
    size_t size = strlen(format) + strlen(value) + 1;
    char *text = Util_allocate(size);
    snprintf(text, size, format, value);
    rowsPush(rows, text);
    Util_release(text);
}

static void rowsFree(Rows *rows) {
    for (int i = 0; i < (*rows).count; i++)
        Util_release((*rows).items[i]);
    Util_release((*rows).items);
    (*rows).items = nullptr;
    (*rows).count = 0;
    (*rows).cap = 0;
}

// A NUL-terminated view of the rows; the rows still own the strings.
static char **rowsArgv(Rows *rows) {
    char **argv = Util_allocate(((size_t) (*rows).count + 1) * sizeof *argv);
    for (int i = 0; i < (*rows).count; i++)
        argv[i] = (*rows).items[i];
    return argv;
}

static bool runRows(Rows *rows) {
    char **argv = rowsArgv(rows);
    int status = Util_execute(argv);
    Util_release(argv);
    return status == 0;
}

// ── graph values ───────────────────────────────────────────────────────────

typedef struct {
    char *name;
    bool isTest;
    char *kind;
    char *output;
    Rows sources;
    Rows includes;
    Rows definitions;
    Rows options;
    Rows libraries;
    Rows deps;
    Rows links;
    Rows linkFlags;
    Rows generator;
} GraphTarget;

typedef struct {
    char *output;
    Rows sources;
    Rows generator;
} GenStep;

typedef struct {
    GraphTarget *items;
    int count;
    int cap;
    GenStep *gens;
    int genCount;
    int genCap;
} Graph;

static GraphTarget *targetAdd(Graph *graph) {
    if ((*graph).count == (*graph).cap) {
        int cap = (*graph).cap ? (*graph).cap * 2 : 32;
        GraphTarget *grown = Util_allocate((size_t) cap * sizeof *grown);
        if ((*graph).count)
            memcpy(grown, (*graph).items, (size_t) (*graph).count * sizeof *grown);
        Util_release((*graph).items);
        (*graph).items = grown;
        (*graph).cap = cap;
    }
    return &(*graph).items[(*graph).count++];
}

static GenStep *genAdd(Graph *graph) {
    if ((*graph).genCount == (*graph).genCap) {
        int cap = (*graph).genCap ? (*graph).genCap * 2 : 16;
        GenStep *grown = Util_allocate((size_t) cap * sizeof *grown);
        if ((*graph).genCount)
            memcpy(grown, (*graph).gens, (size_t) (*graph).genCount * sizeof *grown);
        Util_release((*graph).gens);
        (*graph).gens = grown;
        (*graph).genCap = cap;
    }
    return &(*graph).gens[(*graph).genCount++];
}

// ── schema walk ────────────────────────────────────────────────────────────

typedef struct {
    const char *p;
} Cursor;

static void skipSpace(Cursor *cursor) {
    const char *p = (*cursor).p;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    (*cursor).p = p;
}

static bool take(Cursor *cursor, char wanted) {
    skipSpace(cursor);
    if (*(*cursor).p != wanted)
        return false;
    (*cursor).p++;
    return true;
}

// Read one JSON string into owned storage; nullptr on malformed input.
static char *parseString(Cursor *cursor) {
    skipSpace(cursor);
    if (*(*cursor).p != '"')
        return nullptr;
    const char *start = (*cursor).p + 1;
    const char *p = start;
    while (*p != 0 && *p != '"') {
        if (*p == '\\' && *(p + 1) != 0)
            p++;
        p++;
    }
    if (*p != '"')
        return nullptr;
    char *text = Util_allocate((size_t) (p - start) + 1);
    size_t used = 0;
    for (const char *q = start; q < p; q++) {
        if (*q == '\\' && q + 1 < p) {
            q++;
            switch (*q) {
                case 'n': text[used++] = '\n'; break;
                case 't': text[used++] = '\t'; break;
                case 'r': text[used++] = '\r'; break;
                default:  text[used++] = *q;   break;
            }
            continue;
        }
        text[used++] = *q;
    }
    text[used] = 0;
    (*cursor).p = p + 1;
    return text;
}

static bool parseRowArray(Cursor *cursor, Rows *rows) {
    if (!take(cursor, '['))
        return false;
    skipSpace(cursor);
    if (take(cursor, ']'))
        return true;
    for (;;) {
        char *item = parseString(cursor);
        if (item == nullptr)
            return false;
        rowsPush(rows, item);
        Util_release(item);
        if (take(cursor, ','))
            continue;
        return take(cursor, ']');
    }
}

// Advance past one value of any shape; the reader only trusts the keys it knows.
static bool skipValue(Cursor *cursor) {
    skipSpace(cursor);
    char kind = *(*cursor).p;
    if (kind == '"') {
        char *text = parseString(cursor);
        if (text == nullptr)
            return false;
        Util_release(text);
        return true;
    }
    if (kind == '[') {
        (*cursor).p++;
        if (take(cursor, ']'))
            return true;
        for (;;) {
            if (!skipValue(cursor))
                return false;
            if (take(cursor, ','))
                continue;
            return take(cursor, ']');
        }
    }
    if (kind == '{') {
        (*cursor).p++;
        if (take(cursor, '}'))
            return true;
        for (;;) {
            char *key = parseString(cursor);
            if (key == nullptr)
                return false;
            Util_release(key);
            if (!take(cursor, ':'))
                return false;
            if (!skipValue(cursor))
                return false;
            if (take(cursor, ','))
                continue;
            return take(cursor, '}');
        }
    }
    const char *p = (*cursor).p;
    while (*p != 0 && *p != ',' && *p != ']' && *p != '}')
        p++;
    if (p == (*cursor).p)
        return false;
    (*cursor).p = p;
    return true;
}

// Parse {"targets":[names],"flags":[words]} into the record's link plan.
static bool parseLinks(Cursor *cursor, GraphTarget *target) {
    if (!take(cursor, '{'))
        return false;
    skipSpace(cursor);
    if (take(cursor, '}'))
        return true;
    for (;;) {
        char *key = parseString(cursor);
        if (key == nullptr)
            return false;
        if (!take(cursor, ':')) {
            Util_release(key);
            return false;
        }
        bool ok;
        if (strcmp(key, "targets") == 0)
            ok = parseRowArray(cursor, &(*target).links);
        else if (strcmp(key, "flags") == 0)
            ok = parseRowArray(cursor, &(*target).linkFlags);
        else
            ok = skipValue(cursor);
        Util_release(key);
        if (!ok)
            return false;
        if (take(cursor, ','))
            continue;
        return take(cursor, '}');
    }
}

static bool parseTargetRecord(Cursor *cursor, GraphTarget *target) {
    if (!take(cursor, '{'))
        return false;
    skipSpace(cursor);
    if (take(cursor, '}'))
        return true;
    bool ok = false;
    for (;;) {
        char *key = parseString(cursor);
        if (key == nullptr)
            return false;
        if (!take(cursor, ':')) {
            Util_release(key);
            return false;
        }
        if (strcmp(key, "name") == 0) {
            (*target).name = parseString(cursor);
            ok = (*target).name != nullptr;
        } else if (strcmp(key, "kind") == 0) {
            (*target).kind = parseString(cursor);
            ok = (*target).kind != nullptr;
        } else if (strcmp(key, "output") == 0) {
            (*target).output = parseString(cursor);
            ok = (*target).output != nullptr;
        } else if (strcmp(key, "sources") == 0) {
            ok = parseRowArray(cursor, &(*target).sources);
        } else if (strcmp(key, "includes") == 0) {
            ok = parseRowArray(cursor, &(*target).includes);
        } else if (strcmp(key, "definitions") == 0) {
            ok = parseRowArray(cursor, &(*target).definitions);
        } else if (strcmp(key, "options") == 0) {
            ok = parseRowArray(cursor, &(*target).options);
        } else if (strcmp(key, "libraries") == 0) {
            ok = parseRowArray(cursor, &(*target).libraries);
        } else if (strcmp(key, "deps") == 0) {
            ok = parseRowArray(cursor, &(*target).deps);
        } else if (strcmp(key, "generator") == 0) {
            ok = parseRowArray(cursor, &(*target).generator);
        } else if (strcmp(key, "links") == 0) {
            ok = parseLinks(cursor, target);
        } else {
            ok = skipValue(cursor);
        }
        Util_release(key);
        if (!ok)
            return false;
        if (take(cursor, ','))
            continue;
        return take(cursor, '}');
    }
}

static bool parseGenRecord(Cursor *cursor, GenStep *step) {
    if (!take(cursor, '{'))
        return false;
    skipSpace(cursor);
    if (take(cursor, '}'))
        return true;
    for (;;) {
        char *key = parseString(cursor);
        if (key == nullptr)
            return false;
        if (!take(cursor, ':')) {
            Util_release(key);
            return false;
        }
        bool ok;
        if (strcmp(key, "output") == 0) {
            (*step).output = parseString(cursor);
            ok = (*step).output != nullptr;
        } else if (strcmp(key, "sources") == 0) {
            ok = parseRowArray(cursor, &(*step).sources);
        } else if (strcmp(key, "generator") == 0) {
            ok = parseRowArray(cursor, &(*step).generator);
        } else {
            ok = skipValue(cursor);
        }
        Util_release(key);
        if (!ok)
            return false;
        if (take(cursor, ','))
            continue;
        return take(cursor, '}');
    }
}

static bool parseGenArray(Cursor *cursor, Graph *graph) {
    if (!take(cursor, '['))
        return false;
    skipSpace(cursor);
    if (take(cursor, ']'))
        return true;
    for (;;) {
        GenStep *step = genAdd(graph);
        if (!parseGenRecord(cursor, step))
            return false;
        if ((*step).output == nullptr) {
            THROW("generated step without an output");
            return false;
        }
        if (take(cursor, ','))
            continue;
        return take(cursor, ']');
    }
}

static bool parseTargetArray(Cursor *cursor, Graph *graph, bool isTest) {
    if (!take(cursor, '['))
        return false;
    skipSpace(cursor);
    if (take(cursor, ']'))
        return true;
    for (;;) {
        GraphTarget *target = targetAdd(graph);
        (*target).isTest = isTest;
        if (!parseTargetRecord(cursor, target))
            return false;
        if ((*target).name == nullptr) {
            THROW("graph record without a name");
            return false;
        }
        if (take(cursor, ','))
            continue;
        return take(cursor, ']');
    }
}

static bool parseGraph(const char *json, Graph *graph) {
    Cursor cursor = {json};
    if (!take(&cursor, '{'))
        return false;
    skipSpace(&cursor);
    if (take(&cursor, '}'))
        return true;
    for (;;) {
        char *key = parseString(&cursor);
        if (key == nullptr)
            return false;
        if (!take(&cursor, ':')) {
            Util_release(key);
            return false;
        }
        bool ok;
        if (strcmp(key, "targets") == 0 || strcmp(key, "index") == 0)
            ok = parseTargetArray(&cursor, graph, false);
        else if (strcmp(key, "tests") == 0)
            ok = parseTargetArray(&cursor, graph, true);
        else if (strcmp(key, "generated") == 0)
            ok = parseGenArray(&cursor, graph);
        else
            ok = skipValue(&cursor);
        Util_release(key);
        if (!ok) {
            THROW("graph is not a valid target graph");
            return false;
        }
        if (take(&cursor, ','))
            continue;
        return take(&cursor, '}');
    }
}

static void graphFree(Graph *graph) {
    for (int i = 0; i < (*graph).count; i++) {
        GraphTarget *t = &(*graph).items[i];
        Util_release((*t).name);
        Util_release((*t).kind);
        Util_release((*t).output);
        rowsFree(&(*t).sources);
        rowsFree(&(*t).includes);
        rowsFree(&(*t).definitions);
        rowsFree(&(*t).options);
        rowsFree(&(*t).libraries);
        rowsFree(&(*t).deps);
        rowsFree(&(*t).links);
        rowsFree(&(*t).linkFlags);
        rowsFree(&(*t).generator);
    }
    for (int i = 0; i < (*graph).genCount; i++) {
        Util_release((*graph).gens[i].output);
        rowsFree(&(*graph).gens[i].sources);
        rowsFree(&(*graph).gens[i].generator);
    }
    Util_release((*graph).gens);
    (*graph).gens = nullptr;
    (*graph).genCount = 0;
    (*graph).genCap = 0;
    Util_release((*graph).items);
    (*graph).items = nullptr;
    (*graph).count = 0;
    (*graph).cap = 0;
}

// ── file access ────────────────────────────────────────────────────────────

// Read a whole file; return a NUL-terminated buffer or nullptr after THROW.
static char *readFile(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        THROW("cannot open graph file: %s", path);
        return nullptr;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        THROW("cannot seek graph file: %s", path);
        return nullptr;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        THROW("cannot size graph file: %s", path);
        return nullptr;
    }
    rewind(file);
    char *text = Util_allocate((size_t) size + 1);
    size_t got = fread(text, 1, (size_t) size, file);
    fclose(file);
    if (got != (size_t) size) {
        THROW("short read on graph file: %s", path);
        Util_release(text);
        return nullptr;
    }
    text[got] = 0;
    *length = got;
    return text;
}

static bool loadGraph(const char *path, Graph *graph) {
    size_t length = 0;
    char *json = readFile(path, &length);
    if (json == nullptr)
        return false;
    bool ok = parseGraph(json, graph);
    Util_release(json);
    return ok;
}

// ── content hashing ────────────────────────────────────────────────────────

static uint64_t hashBytes(uint64_t hash, const void *bytes, size_t count) {
    const unsigned char *p = bytes;
    for (size_t i = 0; i < count; i++) {
        hash ^= p[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t hashText(uint64_t hash, const char *text) {
    hash = hashBytes(hash, text, strlen(text));
    unsigned char end = 0;
    return hashBytes(hash, &end, 1);
}

static bool hashFile(uint64_t *hash, const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr)
        return false;
    unsigned char buffer[8192];
    size_t got;
    while ((got = fread(buffer, 1, sizeof buffer, file)) > 0)
        *hash = hashBytes(*hash, buffer, got);
    fclose(file);
    return true;
}

// ── compile and link ───────────────────────────────────────────────────────

static const char *compilerName(void) {
    const char *cc = getenv("CC");
    return (cc != nullptr && *cc != 0) ? cc : "cc";
}

static int targetIndex(Graph *graph, const char *name) {
    for (int i = 0; i < (*graph).count; i++)
        if (strcmp((*graph).items[i].name, name) == 0)
            return i;
    return -1;
}

// The artifact this target produces; an external artifact keeps its own path.
// The artifact path is declared by the graph; a builder invents no layout.
static char *targetOutput(const GraphTarget *target) {
    if ((*target).output == nullptr) {
        THROW("target %s declares no output", (*target).name);
        return nullptr;
    }
    return copyText((*target).output);
}

// Object path: a hash of the source bytes and the exact flag words decides the
// name, so a hit means the inputs are identical and no depfile is needed.
static char *objectPath(const GraphTarget *target, const char *source) {
    uint64_t hash = 1469598103934665603ull;
    hash = hashText(hash, source);
    if (!hashFile(&hash, source)) {
        THROW("cannot read source: %s", source);
        return nullptr;
    }
    for (int i = 0; i < (*target).options.count; i++)
        hash = hashText(hash, (*target).options.items[i]);
    for (int i = 0; i < (*target).definitions.count; i++)
        hash = hashText(hash, (*target).definitions.items[i]);
    for (int i = 0; i < (*target).includes.count; i++)
        hash = hashText(hash, (*target).includes.items[i]);
    const char *slash = strrchr(source, '/');
    const char *base = slash ? slash + 1 : source;
    char label[256];
    snprintf(label, sizeof label, "%s", base);
    char *dot = strrchr(label, '.');
    if (dot != nullptr)
        *dot = 0;
    char hex[24];
    snprintf(hex, sizeof hex, "%016llx", (unsigned long long) hash);
    size_t size = strlen((*target).name) + strlen(label) + sizeof hex + 32;
    char *path = Util_allocate(size);
    snprintf(path, size, ".b/objects/%s/%s-%s.o", (*target).name, label, hex);
    return path;
}

static bool compileSource(const GraphTarget *target, const char *source, char **objectOut) {
    char *object = objectPath(target, source);
    if (object == nullptr)
        return false;
    *objectOut = object;
    if (access(object, F_OK) == 0)
        return true;
    char *directory = Util_parentDirectory(object);
    bool made = Util_makeDirectory(directory);
    Util_release(directory);
    if (!made)
        return false;
    Rows argv = {0};
    rowsPush(&argv, compilerName());
    for (int i = 0; i < (*target).options.count; i++)
        rowsPush(&argv, (*target).options.items[i]);
    for (int i = 0; i < (*target).definitions.count; i++)
        rowsPushf(&argv, "-D%s", (*target).definitions.items[i]);
    for (int i = 0; i < (*target).includes.count; i++)
        rowsPushf(&argv, "-I%s", (*target).includes.items[i]);
    if (Util_endsWith(source, ".m"))
        rowsPush(&argv, "-fobjc-arc");
    rowsPush(&argv, "-c");
    rowsPush(&argv, source);
    rowsPush(&argv, "-o");
    rowsPush(&argv, object);
    bool ok = runRows(&argv);
    rowsFree(&argv);
    return ok;
}

// Run the recorded generator for an external artifact and confirm it appeared.
static bool generateExternal(const GraphTarget *target, const char *output) {
    if (access(output, F_OK) == 0)
        return true;
    if ((*target).generator.count == 0) {
        THROW("external artifact is missing and has no generator: %s", output);
        return false;
    }
    char *directory = Util_parentDirectory(output);
    bool made = Util_makeDirectory(directory);
    Util_release(directory);
    if (!made)
        return false;
    Rows argv = {0};
    for (int i = 0; i < (*target).generator.count; i++)
        rowsPush(&argv, (*target).generator.items[i]);
    bool ok = runRows(&argv);
    rowsFree(&argv);
    if (!ok) {
        THROW("generator failed for %s", (*target).name);
        return false;
    }
    if (access(output, F_OK) != 0) {
        THROW("generator produced no artifact for %s", (*target).name);
        return false;
    }
    return true;
}

static bool linkTarget(Graph *graph, GraphTarget *target, const Rows *objects, const char *output) {
    const char *kind = (*target).kind ? (*target).kind : "exe";
    char *directory = Util_parentDirectory(output);
    bool made = Util_makeDirectory(directory);
    Util_release(directory);
    if (!made)
        return false;
    Rows argv = {0};
    bool ok;
    if (strcmp(kind, "lib") == 0) {
        rowsPush(&argv, "ar");
        rowsPush(&argv, "rcs");
        rowsPush(&argv, output);
        for (int i = 0; i < (*objects).count; i++)
            rowsPush(&argv, (*objects).items[i]);
        unlink(output);
        ok = runRows(&argv);
    } else {
        rowsPush(&argv, compilerName());
        for (int i = 0; i < (*objects).count; i++)
            rowsPush(&argv, (*objects).items[i]);
        if (strcmp(kind, "mod") == 0) {
            rowsPush(&argv, "-bundle");
            rowsPush(&argv, "-Wl,-undefined,dynamic_lookup");
        }
        rowsPush(&argv, "-o");
        rowsPush(&argv, output);
        for (int i = 0; i < (*target).links.count; i++) {
            int index = targetIndex(graph, (*target).links.items[i]);
            if (index < 0) {
                THROW("link plan names an unknown target: %s", (*target).links.items[i]);
                rowsFree(&argv);
                return false;
            }
            char *library = targetOutput(&(*graph).items[index]);
            rowsPush(&argv, library);
            Util_release(library);
        }
        for (int i = 0; i < (*target).linkFlags.count; i++)
            rowsPush(&argv, (*target).linkFlags.items[i]);
        ok = runRows(&argv);
    }
    rowsFree(&argv);
    return ok;
}

// Read a small text file into a caller buffer; false when it is absent.
static bool readSmallText(const char *path, char *text, size_t size) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr)
        return false;
    size_t got = fread(text, 1, size - 1, file);
    fclose(file);
    text[got] = 0;
    return true;
}

static char *signaturePath(const char *name) {
    size_t size = strlen(name) + 32;
    char *path = Util_allocate(size);
    snprintf(path, size, ".b/meta/%s.sig", name);
    return path;
}

// A link signature is the object names (already content-derived), the link
// words, and every dependency's own signature. The values chain, so freshness
// is decided by content, never by a clock and never by an equal timestamp.
static uint64_t linkSignature(Graph *graph, GraphTarget *target, const Rows *objects) {
    uint64_t hash = 1469598103934665603ull;
    for (int i = 0; i < (*objects).count; i++)
        hash = hashText(hash, (*objects).items[i]);
    for (int i = 0; i < (*target).linkFlags.count; i++)
        hash = hashText(hash, (*target).linkFlags.items[i]);
    for (int i = 0; i < (*target).links.count; i++) {
        int index = targetIndex(graph, (*target).links.items[i]);
        if (index < 0)
            continue;
        GraphTarget *dep = &(*graph).items[index];
        const char *kind = (*dep).kind ? (*dep).kind : "exe";
        if (strcmp(kind, "ext") == 0) {
            // An external artifact's bytes are not ours to hash; its size and
            // modification stamp are the only change token it offers.
            char *output = targetOutput(dep);
            struct stat info;
            char token[64] = "absent";
            if (stat(output, &info) == 0)
                snprintf(token, sizeof token, "%lld:%lld",
                         (long long) info.st_size, (long long) info.st_mtime);
            hash = hashText(hash, token);
            Util_release(output);
            continue;
        }
        char *path = signaturePath((*dep).name);
        char text[64] = "";
        readSmallText(path, text, sizeof text);
        hash = hashText(hash, text);
        Util_release(path);
    }
    return hash;
}

static void writeSignature(const char *name, uint64_t signature) {
    char *path = signaturePath(name);
    char *directory = Util_parentDirectory(path);
    if (Util_makeDirectory(directory)) {
        char text[32];
        snprintf(text, sizeof text, "%016llx\n", (unsigned long long) signature);
        FILE *file = fopen(path, "wb");
        if (file != nullptr) {
            fwrite(text, 1, strlen(text), file);
            fclose(file);
        }
    }
    Util_release(directory);
    Util_release(path);
}

// True when the artifact exists and its recorded signature still matches.
static bool artifactFresh(const char *name, const char *output, uint64_t signature) {
    if (access(output, F_OK) != 0)
        return false;
    char *path = signaturePath(name);
    char text[64] = "";
    bool read = readSmallText(path, text, sizeof text);
    Util_release(path);
    if (!read)
        return false;
    char expected[32];
    snprintf(expected, sizeof expected, "%016llx", (unsigned long long) signature);
    return strncmp(text, expected, 16) == 0;
}

// Produce every generated input whose sources or command changed since last time.
// The generator argv is data: glslang, python or cargo, the builder does not care.
static bool runGenerated(Graph *graph) {
    for (int i = 0; i < (*graph).genCount; i++) {
        GenStep *step = &(*graph).gens[i];
        uint64_t hash = 1469598103934665603ull;
        for (int s = 0; s < (*step).sources.count; s++) {
            hash = hashText(hash, (*step).sources.items[s]);
            if (!hashFile(&hash, (*step).sources.items[s])) {
                THROW("cannot read generator input: %s", (*step).sources.items[s]);
                return false;
            }
        }
        for (int g = 0; g < (*step).generator.count; g++)
            hash = hashText(hash, (*step).generator.items[g]);
        uint64_t key = hashText(1469598103934665603ull, (*step).output);
        char name[64];
        snprintf(name, sizeof name, "gen-%016llx", (unsigned long long) key);
        char expected[32];
        snprintf(expected, sizeof expected, "%016llx", (unsigned long long) hash);
        char *sigPath = signaturePath(name);
        char recorded[64] = "";
        bool fresh = access((*step).output, F_OK) == 0 &&
                     readSmallText(sigPath, recorded, sizeof recorded) &&
                     strncmp(recorded, expected, 16) == 0;
        Util_release(sigPath);
        if (fresh)
            continue;
        char *directory = Util_parentDirectory((*step).output);
        bool made = Util_makeDirectory(directory);
        Util_release(directory);
        if (!made)
            return false;
        Rows argv = {0};
        for (int g = 0; g < (*step).generator.count; g++)
            rowsPush(&argv, (*step).generator.items[g]);
        bool ok = runRows(&argv);
        rowsFree(&argv);
        if (!ok || access((*step).output, F_OK) != 0) {
            THROW("generator failed for %s", (*step).output);
            return false;
        }
        writeSignature(name, hash);
        fprintf(stderr, "b: generated %s\n", (*step).output);
    }
    return true;
}

static bool buildTarget(Graph *graph, GraphTarget *target) {
    char *output = targetOutput(target);
    if (output == nullptr)
        return false;
    const char *kind = (*target).kind ? (*target).kind : "exe";
    if (strcmp(kind, "ext") == 0) {
        bool ok = generateExternal(target, output);
        if (ok)
            fprintf(stderr, "b: artifact %s\n", output);
        Util_release(output);
        return ok;
    }
    Rows objects = {0};
    bool ok = true;
    for (int i = 0; i < (*target).sources.count && ok; i++) {
        char *object = nullptr;
        ok = compileSource(target, (*target).sources.items[i], &object);
        if (ok)
            rowsPush(&objects, object);
        Util_release(object);
    }
    if (ok && (*target).sources.count == 0 && strcmp(kind, "lib") != 0) {
        THROW("target %s has no sources", (*target).name);
        ok = false;
    }
    if (ok) {
        uint64_t signature = linkSignature(graph, target, &objects);
        if (artifactFresh((*target).name, output, signature)) {
            fprintf(stderr, "b: up to date %s\n", (*target).name);
            rowsFree(&objects);
            Util_release(output);
            return true;
        }
    }
    if (ok)
        ok = linkTarget(graph, target, &objects, output);
    if (ok) {
        writeSignature((*target).name, linkSignature(graph, target, &objects));
        fprintf(stderr, "b: built %s -> %s\n", (*target).name, output);
    } else {
        THROW("build failed for %s", (*target).name);
    }
    rowsFree(&objects);
    Util_release(output);
    return ok;
}

// ── planning ───────────────────────────────────────────────────────────────

typedef struct {
    Graph *graph;
    unsigned char *state;   // 0 unvisited, 1 visiting, 2 planned
    int *order;
    int count;
} Plan;

static bool planVisit(Plan *plan, int index) {
    if ((*plan).state[index] == 2)
        return true;
    Graph *graph = (*plan).graph;
    if ((*plan).state[index] == 1) {
        THROW("dependency cycle at %s", (*graph).items[index].name);
        return false;
    }
    (*plan).state[index] = 1;
    GraphTarget *target = &(*graph).items[index];
    const Rows *edges = &(*target).deps;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1)
            edges = &(*target).links;
        for (int i = 0; i < (*edges).count; i++) {
            int next = targetIndex(graph, (*edges).items[i]);
            if (next < 0) {
                THROW("%s depends on unknown target %s",
                      (*target).name, (*edges).items[i]);
                return false;
            }
            if (!planVisit(plan, next))
                return false;
        }
    }
    (*plan).state[index] = 2;
    (*plan).order[(*plan).count++] = index;
    return true;
}

// ── commands ───────────────────────────────────────────────────────────────

static int listCommand(Graph *graph, const char *path) {
    int targets = 0, tests = 0;
    for (int i = 0; i < (*graph).count; i++) {
        GraphTarget *target = &(*graph).items[i];
        printf("%s %s\n", (*target).isTest ? "test" : "target", (*target).name);
        if ((*target).isTest)
            tests++;
        else
            targets++;
    }
    fprintf(stderr, "graph: %d targets, %d tests, %d generated (%s)\n",
            targets, tests, (*graph).genCount, path);
    return EXIT_SUCCESS;
}

static int buildCommand(Graph *graph, int argc, char **argv) {
    Plan plan = {graph, nullptr, nullptr, 0};
    plan.state = Util_allocate((size_t) (*graph).count);
    plan.order = Util_allocate(((size_t) (*graph).count + 1) * sizeof(int));
    int status = EXIT_SUCCESS;
    bool all = argc == 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--all") == 0) {
            all = true;
            continue;
        }
        int index = targetIndex(graph, argv[i]);
        if (index < 0) {
            THROW("graph has no target %s", argv[i]);
            status = EXIT_FAILURE;
            continue;
        }
        if (!planVisit(&plan, index))
            status = EXIT_FAILURE;
    }
    if (all) {
        for (int i = 0; i < (*graph).count; i++)
            if (!planVisit(&plan, i))
                status = EXIT_FAILURE;
    }
    if (status == EXIT_SUCCESS && !runGenerated(graph))
        status = EXIT_FAILURE;
    if (status == EXIT_SUCCESS) {
        for (int i = 0; i < plan.count; i++)
            if (!buildTarget(graph, &(*graph).items[plan.order[i]]))
                status = EXIT_FAILURE;
    }
    Util_release(plan.state);
    Util_release(plan.order);
    if (status == EXIT_SUCCESS)
        fprintf(stderr, "graph: %d target(s) built\n", plan.count);
    return status;
}

// Build then run one target, forwarding program arguments to it.
static int runCommand(Graph *graph, const char *name, const Rows *program) {
    char *requested[1] = {(char *) name};
    int status = buildCommand(graph, 1, requested);
    if (status != EXIT_SUCCESS)
        return status;
    int index = targetIndex(graph, name);
    if (index < 0) {
        THROW("graph has no target %s", name);
        return EXIT_FAILURE;
    }
    char *output = targetOutput(&(*graph).items[index]);
    Rows argv = {0};
    rowsPush(&argv, output);
    for (int i = 0; i < (*program).count; i++)
        rowsPush(&argv, (*program).items[i]);
    char **list = rowsArgv(&argv);
    int code = Util_execute(list);
    Util_release(list);
    rowsFree(&argv);
    Util_release(output);
    return code == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

// Build the matching tests, then run each binary; 77 is a declared skip.
static int testCommand(Graph *graph, const char *filter) {
    Rows names = {0};
    for (int i = 0; i < (*graph).count; i++) {
        if (!(*graph).items[i].isTest)
            continue;
        if (filter != nullptr && strstr((*graph).items[i].name, filter) == nullptr)
            continue;
        rowsPush(&names, (*graph).items[i].name);
    }
    if (names.count == 0) {
        THROW("no test matches %s", filter == nullptr ? "*" : filter);
        rowsFree(&names);
        return EXIT_FAILURE;
    }
    int status = buildCommand(graph, names.count, names.items);
    if (status != EXIT_SUCCESS) {
        rowsFree(&names);
        return status;
    }
    int passed = 0, skipped = 0, failed = 0;
    for (int i = 0; i < names.count; i++) {
        int index = targetIndex(graph, names.items[i]);
        char *binary = index < 0 ? nullptr : targetOutput(&(*graph).items[index]);
        if (binary == nullptr) {
            failed++;
            continue;
        }
        char *argv[2] = {binary, nullptr};
        int code = Util_execute(argv);
        if (code == 0) {
            passed++;
        } else if (code == 77) {
            skipped++;
        } else {
            fprintf(stderr, "b: FAIL %s (%d)\n", names.items[i], code);
            failed++;
        }
        Util_release(binary);
    }
    fprintf(stderr, "tests: %d passed, %d skipped, %d failed\n", passed, skipped, failed);
    rowsFree(&names);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

// Run a graph command: `b graph list|build|test|run [target...] [--file <path>]`.
int Graph_command(int argc, char **argv) {
    const char *path = ".b/graph.json";
    const char *command = "list";
    int first = 0;
    if (argc > 0 && argv[0][0] != '-') {
        command = argv[0];
        first = 1;
    }
    Rows names = {0};
    Rows program = {0};
    for (int i = first; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            path = argv[++i];
            continue;
        }
        // Everything after `run <target>` belongs to the program, not to b.
        if (strcmp(command, "run") == 0 && names.count >= 1) {
            rowsPush(&program, argv[i]);
            continue;
        }
        if (argv[i][0] == '-' && strcmp(argv[i], "--all") != 0) {
            THROW("unsupported graph option: %s", argv[i]);
            rowsFree(&names);
            rowsFree(&program);
            return EXIT_FAILURE;
        }
        rowsPush(&names, argv[i]);
    }
    Graph graph = {0};
    if (!loadGraph(path, &graph)) {
        graphFree(&graph);
        rowsFree(&names);
        rowsFree(&program);
        return EXIT_FAILURE;
    }
    int status;
    if (strcmp(command, "list") == 0) {
        status = listCommand(&graph, path);
    } else if (strcmp(command, "build") == 0) {
        status = buildCommand(&graph, names.count, names.items);
    } else if (strcmp(command, "test") == 0) {
        status = testCommand(&graph, names.count ? names.items[0] : nullptr);
    } else if (strcmp(command, "run") == 0) {
        if (names.count == 0) {
            THROW("usage: b graph run <target> [arguments...]");
            status = EXIT_FAILURE;
        } else {
            status = runCommand(&graph, names.items[0], &program);
        }
    } else {
        THROW("unsupported graph command: %s", command);
        status = EXIT_FAILURE;
    }
    graphFree(&graph);
    rowsFree(&names);
    rowsFree(&program);
    return status;
}
