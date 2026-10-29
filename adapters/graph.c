// MODULE: graph reader — list the targets in a serialized target graph.
// DEFINITION: The graph file carries top-level "targets" and "tests" arrays;
// each object begins with a "name" string. Nested arrays hold strings only.
// OVERVIEW: Graph_command; readFile; stringEnd; listNames.
#include "adapters/graph.h"
#include "b.h"

#include <stdlib.h>
#include <string.h>

;;DEFINITION
/* The reader walks the known schema instead of building a general JSON tree:
 * names are plain identifiers, nested arrays hold strings only, and a missing
 * section is a loud rejection rather than a guessed empty graph.
 */
;;OVERVIEW
/* MODULE: graph reader; PRIVATE STATIC: readFile, stringEnd, listNames.
 * OWNERSHIP: readFile returns a caller-owned buffer; Graph_command releases it
 * through Util_release, the suite's single release seam.
 */

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

// Return the closing quote of the string that opens at *p.
static const char *stringEnd(const char *p) {
    p++;
    while (*p != 0 && *p != '"') {
        if (*p == '\\' && *(p + 1) != 0)
            p++;
        p++;
    }
    return p;
}

// Return the bracket that closes the array opening at *open, skipping strings.
static const char *arrayEnd(const char *open) {
    int depth = 0;
    const char *p = open;
    while (*p != 0) {
        if (*p == '"') {
            p = stringEnd(p);
            if (*p != 0)
                p++;
            continue;
        }
        if (*p == '[')
            depth++;
        else if (*p == ']') {
            depth--;
            if (depth == 0)
                return p;
        }
        p++;
    }
    return nullptr;
}

// Print every object name in one top-level array; return its count or -1.
static int listNames(const char *json, const char *key, const char *label) {
    char token[64];
    int written = snprintf(token, sizeof token, "\"%s\"", key);
    if (written < 0 || (size_t) written >= sizeof token)
        return -1;
    const char *section = strstr(json, token);
    if (section == nullptr) {
        THROW("graph has no %s section", key);
        return -1;
    }
    const char *open = strchr(section, '[');
    const char *close = open == nullptr ? nullptr : arrayEnd(open);
    if (open == nullptr || close == nullptr) {
        THROW("graph %s section is not an array", key);
        return -1;
    }
    int count = 0;
    const char *p = open;
    while ((p = strstr(p, "\"name\"")) != nullptr && p < close) {
        p = strchr(p, ':');
        if (p == nullptr)
            return -1;
        do p++; while (*p == ' ' || *p == '\t' || *p == '\n');
        if (*p != '"') {
            THROW("graph %s entry has a non-string name", key);
            return -1;
        }
        const char *end = stringEnd(p);
        printf("%s %.*s\n", label, (int)(end - (p + 1)), p + 1);
        count++;
        p = end;
    }
    return count;
}

// List a graph file: `b graph list [--file <path>]`.
int Graph_command(int argc, char **argv) {
    const char *path = ".b/graph.json";
    if (argc > 0 && strcmp(argv[0], "list") != 0) {
        THROW("unsupported graph command: %s", argv[0]);
        return EXIT_FAILURE;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            path = argv[++i];
            continue;
        }
        THROW("unsupported graph option: %s", argv[i]);
        return EXIT_FAILURE;
    }
    size_t length = 0;
    char *json = readFile(path, &length);
    if (json == nullptr)
        return EXIT_FAILURE;
    int targets = listNames(json, "targets", "target");
    int tests = listNames(json, "tests", "test");
    Util_release(json);
    if (targets < 0 || tests < 0)
        return EXIT_FAILURE;
    fprintf(stderr, "graph: %d targets, %d tests (%s)\n", targets, tests, path);
    return EXIT_SUCCESS;
}
