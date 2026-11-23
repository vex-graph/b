/**
 * MODULE: graph, read and execute a serialized target graph.
 * DEFINITION: A graph file is generated data with a fixed schema: top-level
 * "targets" and "tests" arrays; each record carries name, kind, sources,
 * includes, definitions, options, deps, a link plan and, for an external
 * artifact, its output and the generator argv that produces it. The reader
 * walks that schema directly; it builds no general JSON tree.
 * OVERVIEW: Graph_command; parseGraph; plan; buildTarget; readFile; hashing.
 */

#include "vex.h"
#include "adapters/lang/graph.h"
#include "adapters/lang/graph_internal.h"
#include "b.h"
#include "b/b_str.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

;;DEFINITION
/* The graph is the only project knowledge b.sh accepts, and it arrives as data:
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

/** ── owned string rows ────────────────────────────────────────────────────── */



;;GETTER
static inline i32 Rows_count(const Rows *self)
{
    return (*self).count;
}
;;GETTER
static inline utf8 **Rows_items(const Rows *self)
{
    return (*self).items;
}
;;SETTER
static inline fn Rows_setItems(Rows *self, utf8 **items)
{
    (*self).items = items;
}
;;SETTER
static inline fn Rows_setCount(Rows *self, i32 count)
{
    (*self).count = count;
}
;;GETTER
static inline i32 Rows_cap(const Rows *self)
{
    return (*self).cap;
}
;;SETTER
static inline fn Rows_setCap(Rows *self, i32 cap)
{
    (*self).cap = cap;
}

static utf8 *copyText(const utf8 *text) {
    bytes size = strlen(text);
    utf8 *copy = Util_allocate(size + 1);
    memcpy(copy, text, size + 1);
    return copy;
}

static fn rowsPush(Rows *rows, const utf8 *text) {
    i32 count = Rows_count(rows);
    i32 cap = Rows_cap(rows);
    if (count == cap) {
        i32 nextCap = cap ? cap * 2 : 8;
        utf8 **grown = Util_allocate((bytes) nextCap * sizeof *grown);
        if (count)
            memcpy(grown, Rows_items(rows), (bytes) count * sizeof *grown);
        Util_release(Rows_items(rows));
        Rows_setItems(rows, grown);
        Rows_setCap(rows, nextCap);
    }
    Rows_items(rows)[count] = copyText(text);
    Rows_setCount(rows, count + 1);
}

static fn rowsPushf(Rows *rows, const utf8 *format, const utf8 *value) {
    bytes size = strlen(format) + strlen(value) + 1;
    utf8 *text = Util_allocate(size);
    snprintf(text, size, format, value);
    rowsPush(rows, text);
    Util_release(text);
}

static fn rowsFree(Rows *rows) {
    i32 count = Rows_count(rows);
    utf8 **items = Rows_items(rows);
    for (i32 i = 0; i < count; i++)
        Util_release(items[i]);
    Util_release(items);
    Rows_setItems(rows, nullptr);
    Rows_setCount(rows, 0);
    Rows_setCap(rows, 0);
}

/** A NUL-terminated view of the rows; the rows still own the strings. */
static utf8 **rowsArgv(Rows *rows) {
    i32 count = Rows_count(rows);
    utf8 **items = Rows_items(rows);
    utf8 **argv = Util_allocate(((bytes) count + 1) * sizeof *argv);
    for (i32 i = 0; i < count; i++)
        argv[i] = items[i];
    return argv;
}

static bool runRows(Rows *rows) {
    utf8 **argv = rowsArgv(rows);
    i32 status = Util_execute(argv);
    Util_release(argv);
    return status == 0;
}

;;GETTER
static const utf8 *Rows_item(const Rows *rows, i32 index) {
    return (*rows).items[index];
}

/** ── graph values ─────────────────────────────────────────────────────────── */



;;GETTER
static const Rows *GraphTarget_sources(const GraphTarget *target)
{
    return &(*target).sources;
}
;;GETTER
static const Rows *GraphTarget_includes(const GraphTarget *target)
{
    return &(*target).includes;
}
;;GETTER
static const Rows *GraphTarget_definitions(const GraphTarget *target)
{
    return &(*target).definitions;
}
;;GETTER
static const Rows *GraphTarget_options(const GraphTarget *target)
{
    return &(*target).options;
}
;;GETTER
static const Rows *GraphTarget_links(const GraphTarget *target)
{
    return &(*target).links;
}
;;GETTER
static const Rows *GraphTarget_linkFlags(const GraphTarget *target)
{
    return &(*target).linkFlags;
}
;;GETTER
static const Rows *GraphTarget_generator(const GraphTarget *target)
{
    return &(*target).generator;
}
;;GETTER
static const Rows *GenStep_sources(const GenStep *step)
{
    return &(*step).sources;
}
;;GETTER
static const Rows *GenStep_generator(const GenStep *step)
{
    return &(*step).generator;
}

;;GETTER
static inline const utf8 *GraphTarget_name(const GraphTarget *self)
{
    return (*self).name;
}
;;SETTER
static inline fn GraphTarget_setName(GraphTarget *self, utf8 *name)
{
    (*self).name = name;
}
;;GETTER
static inline bool GraphTarget_isTest(const GraphTarget *self)
{
    return (*self).isTest;
}
;;SETTER
static inline fn GraphTarget_setIsTest(GraphTarget *self, bool isTest)
{
    (*self).isTest = isTest;
}
;;GETTER
static inline const utf8 *GraphTarget_kind(const GraphTarget *self)
{
    return (*self).kind;
}
;;SETTER
static inline fn GraphTarget_setKind(GraphTarget *self, utf8 *kind)
{
    (*self).kind = kind;
}
;;GETTER
static inline const utf8 *GraphTarget_output(const GraphTarget *self)
{
    return (*self).output;
}
;;SETTER
static inline fn GraphTarget_setOutput(GraphTarget *self, utf8 *output)
{
    (*self).output = output;
}
;;GETTER
static inline Rows *GraphTarget_sourcesMut(GraphTarget *self)
{
    return &(*self).sources;
}
;;GETTER
static inline Rows *GraphTarget_includesMut(GraphTarget *self)
{
    return &(*self).includes;
}
;;GETTER
static inline Rows *GraphTarget_definitionsMut(GraphTarget *self)
{
    return &(*self).definitions;
}
;;GETTER
static inline Rows *GraphTarget_optionsMut(GraphTarget *self)
{
    return &(*self).options;
}
;;GETTER
static inline Rows *GraphTarget_librariesMut(GraphTarget *self)
{
    return &(*self).libraries;
}
;;GETTER
static inline Rows *GraphTarget_depsMut(GraphTarget *self)
{
    return &(*self).deps;
}
;;GETTER
static inline const Rows *GraphTarget_deps(const GraphTarget *self)
{
    return &(*self).deps;
}
;;GETTER
static inline Rows *GraphTarget_linksMut(GraphTarget *self)
{
    return &(*self).links;
}
;;GETTER
static inline Rows *GraphTarget_linkFlagsMut(GraphTarget *self)
{
    return &(*self).linkFlags;
}
;;GETTER
static inline Rows *GraphTarget_generatorMut(GraphTarget *self)
{
    return &(*self).generator;
}

;;GETTER
static inline utf8 *GenStep_output(const GenStep *self)
{
    return (*self).output;
}
;;SETTER
static inline fn GenStep_setOutput(GenStep *self, utf8 *output)
{
    (*self).output = output;
}
;;GETTER
static inline Rows *GenStep_sourcesMut(GenStep *self)
{
    return &(*self).sources;
}
;;GETTER
static inline Rows *GenStep_generatorMut(GenStep *self)
{
    return &(*self).generator;
}



;;GETTER
static inline GraphTarget *Graph_items(const Graph *self)
{
    return (*self).items;
}
;;SETTER
static inline fn Graph_setItems(Graph *self, GraphTarget *items)
{
    (*self).items = items;
}
;;GETTER
static inline i32 Graph_count(const Graph *self)
{
    return (*self).count;
}
;;SETTER
static inline fn Graph_setCount(Graph *self, i32 count)
{
    (*self).count = count;
}
;;GETTER
static inline i32 Graph_cap(const Graph *self)
{
    return (*self).cap;
}
;;SETTER
static inline fn Graph_setCap(Graph *self, i32 cap)
{
    (*self).cap = cap;
}
;;GETTER
static inline GenStep *Graph_gens(const Graph *self)
{
    return (*self).gens;
}
;;SETTER
static inline fn Graph_setGens(Graph *self, GenStep *gens)
{
    (*self).gens = gens;
}
;;GETTER
static inline i32 Graph_genCount(const Graph *self)
{
    return (*self).genCount;
}
;;SETTER
static inline fn Graph_setGenCount(Graph *self, i32 genCount)
{
    (*self).genCount = genCount;
}
;;GETTER
static inline i32 Graph_genCap(const Graph *self)
{
    return (*self).genCap;
}
;;SETTER
static inline fn Graph_setGenCap(Graph *self, i32 genCap)
{
    (*self).genCap = genCap;
}

static GraphTarget *targetAdd(Graph *graph) {
    i32 count = Graph_count(graph);
    i32 cap = Graph_cap(graph);
    if (count == cap) {
        i32 nextCap = cap ? cap * 2 : 32;
        GraphTarget *grown = Util_allocate((bytes) nextCap * sizeof *grown);
        if (count)
            memcpy(grown, Graph_items(graph), (bytes) count * sizeof *grown);
        Util_release(Graph_items(graph));
        Graph_setItems(graph, grown);
        Graph_setCap(graph, nextCap);
    }
    Graph_setCount(graph, count + 1);
    return &Graph_items(graph)[count];
}

static GenStep *genAdd(Graph *graph) {
    i32 genCount = Graph_genCount(graph);
    i32 genCap = Graph_genCap(graph);
    if (genCount == genCap) {
        i32 nextCap = genCap ? genCap * 2 : 16;
        GenStep *grown = Util_allocate((bytes) nextCap * sizeof *grown);
        if (genCount)
            memcpy(grown, Graph_gens(graph), (bytes) genCount * sizeof *grown);
        Util_release(Graph_gens(graph));
        Graph_setGens(graph, grown);
        Graph_setGenCap(graph, nextCap);
    }
    Graph_setGenCount(graph, genCount + 1);
    return &Graph_gens(graph)[genCount];
}

/** ── schema walk ──────────────────────────────────────────────────────────── */



;;GETTER
static inline const utf8 *Cursor_p(const Cursor *self)
{
    return (*self).p;
}
;;SETTER
static inline fn Cursor_setP(Cursor *self, const utf8 *p)
{
    (*self).p = p;
}

static fn skipSpace(Cursor *cursor) {
    const utf8 *p = Cursor_p(cursor);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    Cursor_setP(cursor, p);
}

static bool take(Cursor *cursor, utf8 wanted) {
    skipSpace(cursor);
    const utf8 *p = Cursor_p(cursor);
    if (*p != wanted)
        return false;
    Cursor_setP(cursor, p + 1);
    return true;
}

/** Read one JSON string into owned storage; nullptr on malformed input. */
static utf8 *parseString(Cursor *cursor) {
    skipSpace(cursor);
    const utf8 *cur = Cursor_p(cursor);
    if (*cur != '"')
        return nullptr;
    const utf8 *start = cur + 1;
    const utf8 *p = start;
    while (*p != 0 && *p != '"') {
        if (*p == '\\' && *(p + 1) != 0)
            p++;
        p++;
    }
    if (*p != '"')
        return nullptr;
    utf8 *text = Util_allocate((bytes) (p - start) + 1);
    bytes used = 0;
    for (const utf8 *q = start; q < p; q++) {
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
    Cursor_setP(cursor, p + 1);
    return text;
}

static bool parseRowArray(Cursor *cursor, Rows *rows) {
    if (!take(cursor, '['))
        return false;
    skipSpace(cursor);
    if (take(cursor, ']'))
        return true;
    for (;;) {
        utf8 *item = parseString(cursor);
        if (item == nullptr)
            return false;
        rowsPush(rows, item);
        Util_release(item);
        if (take(cursor, ','))
            continue;
        return take(cursor, ']');
    }
}

/** Advance past one value of any shape; the reader only trusts the keys it knows. */
static bool skipValue(Cursor *cursor) {
    skipSpace(cursor);
    const utf8 *cur = Cursor_p(cursor);
    utf8 kind = *cur;
    if (kind == '"') {
        utf8 *text = parseString(cursor);
        if (text == nullptr)
            return false;
        Util_release(text);
        return true;
    }
    if (kind == '[') {
        Cursor_setP(cursor, cur + 1);
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
        Cursor_setP(cursor, cur + 1);
        if (take(cursor, '}'))
            return true;
        for (;;) {
            utf8 *key = parseString(cursor);
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
    const utf8 *p = cur;
    while (*p != 0 && *p != ',' && *p != ']' && *p != '}')
        p++;
    if (p == cur)
        return false;
    Cursor_setP(cursor, p);
    return true;
}

/** Parse {"targets":[names],"flags":[words]} into the record's link plan. */
static bool parseLinks(Cursor *cursor, GraphTarget *target) {
    if (!take(cursor, '{'))
        return false;
    skipSpace(cursor);
    if (take(cursor, '}'))
        return true;
    for (;;) {
        utf8 *key = parseString(cursor);
        if (key == nullptr)
            return false;
        if (!take(cursor, ':')) {
            Util_release(key);
            return false;
        }
        bool ok;
        if (bStrEq(key, "targets"))
            ok = parseRowArray(cursor, GraphTarget_linksMut(target));
        else if (bStrEq(key, "flags"))
            ok = parseRowArray(cursor, GraphTarget_linkFlagsMut(target));
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
        utf8 *key = parseString(cursor);
        if (key == nullptr)
            return false;
        if (!take(cursor, ':')) {
            Util_release(key);
            return false;
        }
        if (bStrEq(key, "name")) {
            utf8 *name = parseString(cursor);
            GraphTarget_setName(target, name);
            ok = name != nullptr;
        } else if (bStrEq(key, "kind")) {
            utf8 *kind = parseString(cursor);
            GraphTarget_setKind(target, kind);
            ok = kind != nullptr;
        } else if (bStrEq(key, "output")) {
            utf8 *output = parseString(cursor);
            GraphTarget_setOutput(target, output);
            ok = output != nullptr;
        } else if (bStrEq(key, "sources")) {
            ok = parseRowArray(cursor, GraphTarget_sourcesMut(target));
        } else if (bStrEq(key, "includes")) {
            ok = parseRowArray(cursor, GraphTarget_includesMut(target));
        } else if (bStrEq(key, "definitions")) {
            ok = parseRowArray(cursor, GraphTarget_definitionsMut(target));
        } else if (bStrEq(key, "options")) {
            ok = parseRowArray(cursor, GraphTarget_optionsMut(target));
        } else if (bStrEq(key, "libraries")) {
            ok = parseRowArray(cursor, GraphTarget_librariesMut(target));
        } else if (bStrEq(key, "deps")) {
            ok = parseRowArray(cursor, GraphTarget_depsMut(target));
        } else if (bStrEq(key, "generator")) {
            ok = parseRowArray(cursor, GraphTarget_generatorMut(target));
        } else if (bStrEq(key, "links")) {
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
        utf8 *key = parseString(cursor);
        if (key == nullptr)
            return false;
        if (!take(cursor, ':')) {
            Util_release(key);
            return false;
        }
        bool ok;
        if (bStrEq(key, "output")) {
            utf8 *output = parseString(cursor);
            GenStep_setOutput(step, output);
            ok = output != nullptr;
        } else if (bStrEq(key, "sources")) {
            ok = parseRowArray(cursor, GenStep_sourcesMut(step));
        } else if (bStrEq(key, "generator")) {
            ok = parseRowArray(cursor, GenStep_generatorMut(step));
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
        if (GenStep_output(step) == nullptr) {
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
        GraphTarget_setIsTest(target, isTest);
        if (!parseTargetRecord(cursor, target))
            return false;
        if (GraphTarget_name(target) == nullptr) {
            THROW("graph record without a name");
            return false;
        }
        if (take(cursor, ','))
            continue;
        return take(cursor, ']');
    }
}

static bool parseGraph(const utf8 *json, Graph *graph) {
    Cursor cursor = {json};
    if (!take(&cursor, '{'))
        return false;
    skipSpace(&cursor);
    if (take(&cursor, '}'))
        return true;
    for (;;) {
        utf8 *key = parseString(&cursor);
        if (key == nullptr)
            return false;
        if (!take(&cursor, ':')) {
            Util_release(key);
            return false;
        }
        bool ok;
        if (bStrEq(key, "targets") || bStrEq(key, "index"))
            ok = parseTargetArray(&cursor, graph, false);
        else if (bStrEq(key, "tests"))
            ok = parseTargetArray(&cursor, graph, true);
        else if (bStrEq(key, "generated"))
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

static fn graphFree(Graph *graph) {
    i32 count = Graph_count(graph);
    GraphTarget *items = Graph_items(graph);
    for (i32 i = 0; i < count; i++) {
        GraphTarget *t = &items[i];
        Util_release((utf8*) GraphTarget_name(t));
        Util_release((utf8*) GraphTarget_kind(t));
        Util_release((utf8*) GraphTarget_output(t));
        rowsFree(GraphTarget_sourcesMut(t));
        rowsFree(GraphTarget_includesMut(t));
        rowsFree(GraphTarget_definitionsMut(t));
        rowsFree(GraphTarget_optionsMut(t));
        rowsFree(GraphTarget_librariesMut(t));
        rowsFree(GraphTarget_depsMut(t));
        rowsFree(GraphTarget_linksMut(t));
        rowsFree(GraphTarget_linkFlagsMut(t));
        rowsFree(GraphTarget_generatorMut(t));
    }
    i32 genCount = Graph_genCount(graph);
    GenStep *gens = Graph_gens(graph);
    for (i32 i = 0; i < genCount; i++) {
        GenStep *step = &gens[i];
        Util_release(GenStep_output(step));
        rowsFree(GenStep_sourcesMut(step));
        rowsFree(GenStep_generatorMut(step));
    }
    Util_release(gens);
    Graph_setGens(graph, nullptr);
    Graph_setGenCount(graph, 0);
    Graph_setGenCap(graph, 0);
    Util_release(items);
    Graph_setItems(graph, nullptr);
    Graph_setCount(graph, 0);
    Graph_setCap(graph, 0);
}

/** ── file access ──────────────────────────────────────────────────────────── */

/** Read a whole file; return a NUL-terminated buffer or nullptr after THROW. */
static utf8 *readFile(const utf8 *path, bytes *length) {
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
    i64 size = ftell(file);
    if (size < 0) {
        fclose(file);
        THROW("cannot size graph file: %s", path);
        return nullptr;
    }
    rewind(file);
    utf8 *text = Util_allocate((bytes) size + 1);
    bytes got = fread(text, 1, (bytes) size, file);
    fclose(file);
    if (got != (bytes) size) {
        THROW("short read on graph file: %s", path);
        Util_release(text);
        return nullptr;
    }
    text[got] = 0;
    *length = got;
    return text;
}

static bool loadGraph(const utf8 *path, Graph *graph) {
    bytes length = 0;
    utf8 *json = readFile(path, &length);
    if (json == nullptr)
        return false;
    bool ok = parseGraph(json, graph);
    Util_release(json);
    return ok;
}

/** ── content hashing ──────────────────────────────────────────────────────── */

static u64 hashBytes(u64 hash, const u8 *data, bytes count) {
    for (bytes i = 0; i < count; i++) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static u64 hashText(u64 hash, const utf8 *text) {
    hash = hashBytes(hash, (const u8*) text, strlen(text));
    u8 end = 0;
    return hashBytes(hash, &end, 1);
}

static bool hashFile(u64 *hash, const utf8 *path) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr)
        return false;
    u8 buffer[8192];
    bytes got;
    while ((got = fread(buffer, 1, sizeof buffer, file)) > 0)
        *hash = hashBytes(*hash, buffer, got);
    fclose(file);
    return true;
}

/** ── compile and link ─────────────────────────────────────────────────────── */

static const utf8 *compilerName() {
    const utf8 *cc = getenv("CC");
    return (cc != nullptr && *cc != 0) ? cc : "cc";
}

static i32 targetIndex(Graph *graph, const utf8 *name) {
    i32 count = Graph_count(graph);
    GraphTarget *items = Graph_items(graph);
    for (i32 i = 0; i < count; i++)
        if (bStrEq(GraphTarget_name(&items[i]), name))
            return i;
    return -1;
}

/**
 * The artifact this target produces; an external artifact keeps its own path.
 * The artifact path is declared by the graph; a builder invents no layout.
 */
static utf8 *targetOutput(const GraphTarget *target) {
    const utf8 *output = GraphTarget_output(target);
    if (output == nullptr) {
        THROW("target %s declares no output", GraphTarget_name(target));
        return nullptr;
    }
    return copyText(output);
}

/**
 * Object path: a hash of the source bytes and the exact flag words decides the
 * name, so a hit means the inputs are identical and no depfile is needed.
 */
static utf8 *objectPath(const GraphTarget *target, const utf8 *source) {
    u64 hash = 1469598103934665603ull;
    hash = hashText(hash, source);
    if (!hashFile(&hash, source)) {
        THROW("cannot read source: %s", source);
        return nullptr;
    }
    const Rows *options = GraphTarget_options(target);
    const Rows *definitions = GraphTarget_definitions(target);
    const Rows *includes = GraphTarget_includes(target);
    for (i32 i = 0; i < Rows_count(options); i++)
        hash = hashText(hash, Rows_item(options, i));
    for (i32 i = 0; i < Rows_count(definitions); i++)
        hash = hashText(hash, Rows_item(definitions, i));
    for (i32 i = 0; i < Rows_count(includes); i++)
        hash = hashText(hash, Rows_item(includes, i));
    const utf8 *slash = strrchr(source, '/');
    const utf8 *base = slash ? slash + 1 : source;
    utf8 label[256];
    snprintf(label, sizeof label, "%s", base);
    utf8 *dot = strrchr(label, '.');
    if (dot != nullptr)
        *dot = 0;
    utf8 hex[24];
    snprintf(hex, sizeof hex, "%016llx", (u64) hash);
    const utf8 *targetName = GraphTarget_name(target);
    bytes size = strlen(targetName) + strlen(label) + sizeof hex + 32;
    utf8 *path = Util_allocate(size);
    snprintf(path, size, ".b/objects/%s/%s-%s.o", targetName, label, hex);
    return path;
}

static bool compileSource(const GraphTarget *target, const utf8 *source, utf8 **objectOut) {
    utf8 *object = objectPath(target, source);
    if (object == nullptr)
        return false;
    *objectOut = object;
    if (access(object, F_OK) == 0)
        return true;
    utf8 *directory = Util_parentDirectory(object);
    bool made = Util_makeDirectory(directory);
    Util_release(directory);
    if (!made)
        return false;
    Rows argv = {0};
    rowsPush(&argv, compilerName());
    const Rows *options = GraphTarget_options(target);
    const Rows *definitions = GraphTarget_definitions(target);
    const Rows *includes = GraphTarget_includes(target);
    for (i32 i = 0; i < Rows_count(options); i++)
        rowsPush(&argv, Rows_item(options, i));
    for (i32 i = 0; i < Rows_count(definitions); i++)
        rowsPushf(&argv, "-D%s", Rows_item(definitions, i));
    for (i32 i = 0; i < Rows_count(includes); i++)
        rowsPushf(&argv, "-I%s", Rows_item(includes, i));
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

/** Run the recorded generator for an external artifact and confirm it appeared. */
static bool generateExternal(const GraphTarget *target, const utf8 *output) {
    if (access(output, F_OK) == 0)
        return true;
    const Rows *generator = GraphTarget_generator(target);
    if (Rows_count(generator) == 0) {
        THROW("external artifact is missing and has no generator: %s", output);
        return false;
    }
    utf8 *directory = Util_parentDirectory(output);
    bool made = Util_makeDirectory(directory);
    Util_release(directory);
    if (!made)
        return false;
    Rows argv = {0};
    for (i32 i = 0; i < Rows_count(generator); i++)
        rowsPush(&argv, Rows_item(generator, i));
    bool ok = runRows(&argv);
    rowsFree(&argv);
    if (!ok) {
        THROW("generator failed for %s", GraphTarget_name(target));
        return false;
    }
    if (access(output, F_OK) != 0) {
        THROW("generator produced no artifact for %s", GraphTarget_name(target));
        return false;
    }
    return true;
}

static bool linkTarget(Graph *graph, GraphTarget *target, const Rows *objects, const utf8 *output) {
    const utf8 *kind = GraphTarget_kind(target) ? GraphTarget_kind(target) : "exe";
    utf8 *directory = Util_parentDirectory(output);
    bool made = Util_makeDirectory(directory);
    Util_release(directory);
    if (!made)
        return false;
    Rows argv = {0};
    bool ok;
    i32 objectCount = Rows_count(objects);
    if (bStrEq(kind, "lib")) {
        rowsPush(&argv, "ar");
        rowsPush(&argv, "rcs");
        rowsPush(&argv, output);
        for (i32 i = 0; i < objectCount; i++)
            rowsPush(&argv, Rows_item(objects, i));
        unlink(output);
        ok = runRows(&argv);
    } else {
        rowsPush(&argv, compilerName());
        for (i32 i = 0; i < objectCount; i++)
            rowsPush(&argv, Rows_item(objects, i));
        if (bStrEq(kind, "mod")) {
            rowsPush(&argv, "-bundle");
            rowsPush(&argv, "-Wl,-undefined,dynamic_lookup");
        }
        rowsPush(&argv, "-o");
        rowsPush(&argv, output);
        const Rows *links = GraphTarget_links(target);
        const Rows *linkFlags = GraphTarget_linkFlags(target);
        GraphTarget *items = Graph_items(graph);
        for (i32 i = 0; i < Rows_count(links); i++) {
            const utf8 *linkName = Rows_item(links, i);
            i32 index = targetIndex(graph, linkName);
            if (index < 0) {
                THROW("link plan names an unknown target: %s", linkName);
                rowsFree(&argv);
                return false;
            }
            utf8 *library = targetOutput(&items[index]);
            rowsPush(&argv, library);
            Util_release(library);
        }
        for (i32 i = 0; i < Rows_count(linkFlags); i++)
            rowsPush(&argv, Rows_item(linkFlags, i));
        ok = runRows(&argv);
    }
    rowsFree(&argv);
    return ok;
}

/** Read a small text file into a caller buffer; false when it is absent. */
static bool readSmallText(const utf8 *path, utf8 *text, bytes size) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr)
        return false;
    bytes got = fread(text, 1, size - 1, file);
    fclose(file);
    text[got] = 0;
    return true;
}

static utf8 *signaturePath(const utf8 *name) {
    bytes size = strlen(name) + 32;
    utf8 *path = Util_allocate(size);
    snprintf(path, size, ".b/meta/%s.sig", name);
    return path;
}

/**
 * A link signature is the object names (already content-derived), the link
 * words, and every dependency's own signature. The values chain, so freshness
 * is decided by content, never by a clock and never by an equal timestamp.
 */
static u64 linkSignature(Graph *graph, GraphTarget *target, const Rows *objects) {
    u64 hash = 1469598103934665603ull;
    i32 objectCount = Rows_count(objects);
    for (i32 i = 0; i < objectCount; i++)
        hash = hashText(hash, Rows_item(objects, i));
    const Rows *linkFlags = GraphTarget_linkFlags(target);
    const Rows *links = GraphTarget_links(target);
    for (i32 i = 0; i < Rows_count(linkFlags); i++)
        hash = hashText(hash, Rows_item(linkFlags, i));
    GraphTarget *items = Graph_items(graph);
    for (i32 i = 0; i < Rows_count(links); i++) {
        i32 index = targetIndex(graph, Rows_item(links, i));
        if (index < 0)
            continue;
        GraphTarget *dep = &items[index];
        const utf8 *kind = GraphTarget_kind(dep) ? GraphTarget_kind(dep) : "exe";
        if (bStrEq(kind, "ext")) {
            utf8 *output = targetOutput(dep);
            struct stat info;
            utf8 token[64] = "absent";
            if (stat(output, &info) == 0)
                snprintf(token, sizeof token, "%lld:%lld",
                         (i64) info.st_size, (i64) info.st_mtime);
            hash = hashText(hash, token);
            Util_release(output);
            continue;
        }
        utf8 *path = signaturePath(GraphTarget_name(dep));
        utf8 text[64] = "";
        readSmallText(path, text, sizeof text);
        hash = hashText(hash, text);
        Util_release(path);
    }
    return hash;
}

static fn writeSignature(const utf8 *name, u64 signature) {
    utf8 *path = signaturePath(name);
    utf8 *directory = Util_parentDirectory(path);
    if (Util_makeDirectory(directory)) {
        utf8 text[32];
        snprintf(text, sizeof text, "%016llx\n", (u64) signature);
        FILE *file = fopen(path, "wb");
        if (file != nullptr) {
            fwrite(text, 1, strlen(text), file);
            fclose(file);
        }
    }
    Util_release(directory);
    Util_release(path);
}

/** True when the artifact exists and its recorded signature still matches. */
static bool artifactFresh(const utf8 *name, const utf8 *output, u64 signature) {
    if (access(output, F_OK) != 0)
        return false;
    utf8 *path = signaturePath(name);
    utf8 text[64] = "";
    bool read = readSmallText(path, text, sizeof text);
    Util_release(path);
    if (!read)
        return false;
    utf8 expected[32];
    snprintf(expected, sizeof expected, "%016llx", (u64) signature);
    return bStrPrefixN(text, expected, 16);
}

/**
 * Produce every generated input whose sources or command changed since last time.
 * The generator argv is data: glslang, python or cargo, the builder does not care.
 */
static bool runGenerated(Graph *graph) {
    i32 genCount = Graph_genCount(graph);
    GenStep *gens = Graph_gens(graph);
    for (i32 i = 0; i < genCount; i++) {
        GenStep *step = &gens[i];
        u64 hash = 1469598103934665603ull;
        const Rows *sources = GenStep_sources(step);
        const Rows *generator = GenStep_generator(step);
        for (i32 s = 0; s < Rows_count(sources); s++) {
            const utf8 *source = Rows_item(sources, s);
            hash = hashText(hash, source);
            if (!hashFile(&hash, source)) {
                THROW("cannot read generator input: %s", source);
                return false;
            }
        }
        for (i32 g = 0; g < Rows_count(generator); g++)
            hash = hashText(hash, Rows_item(generator, g));
        const utf8 *stepOutput = GenStep_output(step);
        u64 key = hashText(1469598103934665603ull, stepOutput);
        utf8 name[64];
        snprintf(name, sizeof name, "gen-%016llx", (u64) key);
        utf8 expected[32];
        snprintf(expected, sizeof expected, "%016llx", (u64) hash);
        utf8 *sigPath = signaturePath(name);
        utf8 recorded[64] = "";
        bool fresh = access(stepOutput, F_OK) == 0 &&
                     readSmallText(sigPath, recorded, sizeof recorded) &&
                     bStrPrefixN(recorded, expected, 16);
        Util_release(sigPath);
        if (fresh)
            continue;
        utf8 *directory = Util_parentDirectory(stepOutput);
        bool made = Util_makeDirectory(directory);
        Util_release(directory);
        if (!made)
            return false;
        Rows argv = {0};
        for (i32 g = 0; g < Rows_count(generator); g++)
            rowsPush(&argv, Rows_item(generator, g));
        bool ok = runRows(&argv);
        rowsFree(&argv);
        if (!ok || access(stepOutput, F_OK) != 0) {
            THROW("generator failed for %s", stepOutput);
            return false;
        }
        writeSignature(name, hash);
        fprintf(stderr, "b.sh: generated %s\n", stepOutput);
    }
    return true;
}

static bool buildTarget(Graph *graph, GraphTarget *target) {
    utf8 *output = targetOutput(target);
    if (output == nullptr)
        return false;
    const utf8 *kind = GraphTarget_kind(target) ? GraphTarget_kind(target) : "exe";
    if (bStrEq(kind, "ext")) {
        bool ok = generateExternal(target, output);
        if (ok)
            fprintf(stderr, "b.sh: artifact %s\n", output);
        Util_release(output);
        return ok;
    }
    Rows objects = {0};
    bool ok = true;
    const Rows *sources = GraphTarget_sources(target);
    for (i32 i = 0; i < Rows_count(sources) && ok; i++) {
        utf8 *object = nullptr;
        ok = compileSource(target, Rows_item(sources, i), &object);
        if (ok)
            rowsPush(&objects, object);
        Util_release(object);
    }
    const utf8 *targetName = GraphTarget_name(target);
    if (ok && Rows_count(sources) == 0 && !bStrEq(kind, "lib")) {
        THROW("target %s has no sources", targetName);
        ok = false;
    }
    if (ok) {
        u64 signature = linkSignature(graph, target, &objects);
        if (artifactFresh(targetName, output, signature)) {
            fprintf(stderr, "b.sh: up to date %s\n", targetName);
            rowsFree(&objects);
            Util_release(output);
            return true;
        }
    }
    if (ok)
        ok = linkTarget(graph, target, &objects, output);
    if (ok) {
        writeSignature(targetName, linkSignature(graph, target, &objects));
        fprintf(stderr, "b.sh: built %s -> %s\n", targetName, output);
    } else {
        THROW("build failed for %s", targetName);
    }
    rowsFree(&objects);
    Util_release(output);
    return ok;
}

/** ── planning ─────────────────────────────────────────────────────────────── */



;;GETTER
static inline Graph *Plan_graph(const Plan *self)
{
    return (*self).graph;
}
;;GETTER
static inline u8 *Plan_state(const Plan *self)
{
    return (*self).state;
}
;;GETTER
static inline i32 *Plan_order(const Plan *self)
{
    return (*self).order;
}
;;GETTER
static inline i32 Plan_count(const Plan *self)
{
    return (*self).count;
}
;;SETTER
static inline fn Plan_setCount(Plan *self, i32 count)
{
    (*self).count = count;
}

static bool planVisit(Plan *plan, i32 index) {
    u8 *state = Plan_state(plan);
    if (state[index] == 2)
        return true;
    Graph *graph = Plan_graph(plan);
    GraphTarget *items = Graph_items(graph);
    if (state[index] == 1) {
        THROW("dependency cycle at %s", GraphTarget_name(&items[index]));
        return false;
    }
    state[index] = 1;
    GraphTarget *target = &items[index];
    const Rows *edges = GraphTarget_deps(target);
    for (i32 pass = 0; pass < 2; pass++) {
        if (pass == 1)
            edges = GraphTarget_links(target);
        i32 edgeCount = Rows_count(edges);
        for (i32 i = 0; i < edgeCount; i++) {
            const utf8 *edgeName = Rows_item(edges, i);
            i32 next = targetIndex(graph, edgeName);
            if (next < 0) {
                THROW("%s depends on unknown target %s",
                      GraphTarget_name(target), edgeName);
                return false;
            }
            if (!planVisit(plan, next))
                return false;
        }
    }
    state[index] = 2;
    i32 count = Plan_count(plan);
    Plan_order(plan)[count] = index;
    Plan_setCount(plan, count + 1);
    return true;
}

/** ── commands ─────────────────────────────────────────────────────────────── */

static i32 listCommand(Graph *graph, const utf8 *path) {
    i32 targets = 0, tests = 0;
    i32 count = Graph_count(graph);
    GraphTarget *items = Graph_items(graph);
    for (i32 i = 0; i < count; i++) {
        GraphTarget *target = &items[i];
        bool isTest = GraphTarget_isTest(target);
        printf("%s %s\n", isTest ? "test" : "target", GraphTarget_name(target));
        if (isTest)
            tests++;
        else
            targets++;
    }
    fprintf(stderr, "graph: %d targets, %d tests, %d generated (%s)\n",
            targets, tests, Graph_genCount(graph), path);
    return EXIT_SUCCESS;
}

static i32 buildCommand(Graph *graph, i32 argc, utf8 **argv) {
    Plan plan = {graph, nullptr, nullptr, 0};
    i32 graphCount = Graph_count(graph);
    plan.state = Util_allocate((bytes) graphCount);
    plan.order = Util_allocate(((bytes) graphCount + 1) * sizeof(i32));
    i32 status = EXIT_SUCCESS;
    bool all = argc == 0;
    for (i32 i = 0; i < argc; i++) {
        if (bStrEq(argv[i], "--all")) {
            all = true;
            continue;
        }
        i32 index = targetIndex(graph, argv[i]);
        if (index < 0) {
            THROW("graph has no target %s", argv[i]);
            status = EXIT_FAILURE;
            continue;
        }
        if (!planVisit(&plan, index))
            status = EXIT_FAILURE;
    }
    if (all) {
        for (i32 i = 0; i < graphCount; i++)
            if (!planVisit(&plan, i))
                status = EXIT_FAILURE;
    }
    if (status == EXIT_SUCCESS && !runGenerated(graph))
        status = EXIT_FAILURE;
    if (status == EXIT_SUCCESS) {
        GraphTarget *items = Graph_items(graph);
        for (i32 i = 0; i < plan.count; i++)
            if (!buildTarget(graph, &items[plan.order[i]]))
                status = EXIT_FAILURE;
    }
    Util_release(plan.state);
    Util_release(plan.order);
    if (status == EXIT_SUCCESS)
        fprintf(stderr, "graph: %d target(s) built\n", plan.count);
    return status;
}

/** Build then run one target, forwarding program arguments to it. */
static i32 runCommand(Graph *graph, const utf8 *name, const Rows *program) {
    utf8 *requested[1] = {(utf8*) name};
    i32 status = buildCommand(graph, 1, requested);
    if (status != EXIT_SUCCESS)
        return status;
    i32 index = targetIndex(graph, name);
    if (index < 0) {
        THROW("graph has no target %s", name);
        return EXIT_FAILURE;
    }
    utf8 *output = targetOutput(&Graph_items(graph)[index]);
    Rows argv = {0};
    rowsPush(&argv, output);
    i32 progCount = Rows_count(program);
    for (i32 i = 0; i < progCount; i++)
        rowsPush(&argv, Rows_item(program, i));
    utf8 **list = rowsArgv(&argv);
    i32 code = Util_execute(list);
    Util_release(list);
    rowsFree(&argv);
    Util_release(output);
    return code == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/** Build the matching tests, then run each binary; 77 is a declared skip. */
static i32 testCommand(Graph *graph, const utf8 *filter) {
    Rows names = {0};
    i32 count = Graph_count(graph);
    GraphTarget *items = Graph_items(graph);
    for (i32 i = 0; i < count; i++) {
        GraphTarget *target = &items[i];
        if (!GraphTarget_isTest(target))
            continue;
        const utf8 *name = GraphTarget_name(target);
        if (filter != nullptr && bStrFind(name, filter) == nullptr)
            continue;
        rowsPush(&names, name);
    }
    if (names.count == 0) {
        THROW("no test matches %s", filter == nullptr ? "*" : filter);
        rowsFree(&names);
        return EXIT_FAILURE;
    }
    i32 status = buildCommand(graph, names.count, names.items);
    if (status != EXIT_SUCCESS) {
        rowsFree(&names);
        return status;
    }
    i32 passed = 0, skipped = 0, failed = 0;
    for (i32 i = 0; i < names.count; i++) {
        i32 index = targetIndex(graph, names.items[i]);
        utf8 *binary = index < 0 ? nullptr : targetOutput(&items[index]);
        if (binary == nullptr) {
            failed++;
            continue;
        }
        utf8 *argv[2] = {binary, nullptr};
        i32 code = Util_execute(argv);
        if (code == 0) {
            passed++;
        } else if (code == 77) {
            skipped++;
        } else {
            fprintf(stderr, "b.sh: FAIL %s (%d)\n", names.items[i], code);
            failed++;
        }
        Util_release(binary);
    }
    fprintf(stderr, "tests: %d passed, %d skipped, %d failed\n", passed, skipped, failed);
    rowsFree(&names);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/** Run a graph command: `b.sh graph list|build|test|run [target...] [--file <path>]`. */
i32 Graph_command(i32 argc, utf8 **argv) {
    const utf8 *path = ".b/graph.json";
    const utf8 *command = "list";
    i32 first = 0;
    if (argc > 0 && argv[0][0] != '-') {
        command = argv[0];
        first = 1;
    }
    Rows names = {0};
    Rows program = {0};
    for (i32 i = first; i < argc; i++) {
        if (bStrEq(argv[i], "--file") && i + 1 < argc) {
            path = argv[++i];
            continue;
        }
        if (bStrEq(command, "run") && names.count >= 1) {
            rowsPush(&program, argv[i]);
            continue;
        }
        if (argv[i][0] == '-' && !bStrEq(argv[i], "--all")) {
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
    i32 status;
    if (bStrEq(command, "list")) {
        status = listCommand(&graph, path);
    } else if (bStrEq(command, "build")) {
        status = buildCommand(&graph, names.count, names.items);
    } else if (bStrEq(command, "test")) {
        status = testCommand(&graph, names.count ? names.items[0] : nullptr);
    } else if (bStrEq(command, "run")) {
        if (names.count == 0) {
            THROW("usage: b.sh graph run <target> [arguments...]");
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
