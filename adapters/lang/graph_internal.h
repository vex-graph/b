#ifndef ADAPTERS_LANG_GRAPH_INTERNAL_H
#define ADAPTERS_LANG_GRAPH_INTERNAL_H

#include "vex.h"

/** Growable string array used by serialized graph target fields. */
typedef struct {
    utf8 **items;
    i32 count;
    i32 cap;
} Rows;

/** Deserialized build or test target record from a b graph file. */
typedef struct {
    utf8 *name;
    bool isTest;
    utf8 *kind;
    utf8 *output;
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

/** Deserialized code-generation step from a b graph file. */
typedef struct {
    utf8 *output;
    Rows sources;
    Rows generator;
} GenStep;

/** Deserialized build graph containing targets and generation steps. */
typedef struct {
    GraphTarget *items;
    i32 count;
    i32 cap;
    GenStep *gens;
    i32 genCount;
    i32 genCap;
} Graph;

/** Read cursor over a NUL-terminated JSON graph buffer. */
typedef struct {
    string p;
} Cursor;

/** Topological DFS build-order planner state over a Graph. */
typedef struct {
    Graph *graph;
    u8 *state;
    i32 *order;
    i32 count;
} Plan;

#endif
