#include "vex.h"
#include "generate.h"
#include "b/b_str.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

;;DEFINITION
/** Directory scanner and b.sh workspace generator for compilable source trees. */
;;OVERVIEW
/** MODULE: generate (vex/b/b/generate.c). Walks source folders, filters build dirs, and sorts entries. */

static const utf8 *COMPILABLE_EXTS[] = {
    "c", "h", "m", "rs", "sh", "java", "cpp", "cc", "hpp",
    "s", "asm", "go", "zig", nullptr
};

static bool is_compilable(const utf8 *name)
{
    const utf8 *dot = strrchr(name, '.');
    if (dot == nullptr || dot == name)
        return false;
    const utf8 *ext = dot + 1;
    for (bytes i = 0; COMPILABLE_EXTS[i] != nullptr; i++)
        if (bStrEq(COMPILABLE_EXTS[i], ext))
            return true;
    return false;
}

static bool is_ignored_dir(const utf8 *name)
{
    if (name[0] == '.')
        return true;
    if (bStrEq(name, "node_modules"))
        return true;
    if (bStrEq(name, "target"))
        return true;
    if (bStrEq(name, ".b"))
        return true;
    return false;
}

static fn indent(FILE *out, i32 depth)
{
    for (i32 i = 0; i < depth; i++)
        fputs("    ", out);
}

;;GETTER
static inline const utf8 *dirent_d_name(const struct dirent *self)
{
    return (*self).d_name;
}

/* Collect sorted entry names for a directory; caller frees. Returns count. */
static bytes collect(const utf8 *path, utf8 ***out_names)
{
    DIR *d = opendir(path);
    if (d == nullptr)
        return 0;
    utf8 **names = nullptr;
    bytes count = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr)
    {
        const utf8 *name = dirent_d_name(e);
        if (bStrEq(name, ".") || bStrEq(name, ".."))
            continue;
        utf8 full[4096];
        snprintf(full, sizeof(full), "%s/%s", path, name);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
        {
            if (is_ignored_dir(name))
                continue;
        }
        else if (!S_ISREG(st.st_mode))
            continue;
        else if (!is_compilable(name))
            continue;

        if (count == cap)
        {
            bytes next = cap == 0 ? 16 : cap * 2;
            utf8 **tmp = realloc(names, next * sizeof(utf8*));
            if (tmp == nullptr)
                break;
            names = tmp;
            cap = next;
        }
        names[count] = strdup(name);
        count++;
    }
    closedir(d);

    for (bytes i = 1; i < count; i++)
    {
        utf8 *key = names[i];
        bytes j = i;
        while (j > 0 && bStrCmp(names[j - 1], key) > 0)
        {
            names[j] = names[j - 1];
            j--;
        }
        names[j] = key;
    }
    *out_names = names;
    return count;
}

/* Returns true if directory contains at least one compilable file (recursively). */
static bool has_compilable(const utf8 *path)
{
    utf8 **names = nullptr;
    bytes count = collect(path, &names);
    bool found = false;
    for (bytes i = 0; i < count; i++)
    {
        utf8 full[4096];
        snprintf(full, sizeof(full), "%s/%s", path, names[i]);
        struct stat st;
        stat(full, &st);
        if (S_ISDIR(st.st_mode))
        {
            if (has_compilable(full)) { found = true; }
        }
        else
        {
            found = true;
        }
        if (found && !S_ISDIR(st.st_mode))
            break;
    }
    for (bytes i = 0; i < count; i++)
        free(names[i]);
    free(names);
    return found;
}

/* Emit directory contents. Returns true if anything was emitted. */
static bool emit(FILE *out, const utf8 *path, i32 depth)
{
    utf8 **names = nullptr;
    bytes count = collect(path, &names);
    bool first = true;
    bool emitted = false;
    fputc('{', out);
    fputc('\n', out);
    for (bytes i = 0; i < count; i++)
    {
        utf8 full[4096];
        snprintf(full, sizeof(full), "%s/%s", path, names[i]);
        struct stat st;
        stat(full, &st);

        if (S_ISDIR(st.st_mode))
        {
            if (!has_compilable(full))
            {
                free(names[i]);
                continue;
            }
            if (!first) { fputc(',', out); fputc('\n', out); }
            first = false;
            emitted = true;
            indent(out, depth + 1);
            fprintf(out, "\"%s\": ", names[i]);
            emit(out, full, depth + 1);
        }
        else
        {
            const utf8 *dot = strrchr(names[i], '.');
            if (!first) { fputc(',', out); fputc('\n', out); }
            first = false;
            emitted = true;
            indent(out, depth + 1);
            fprintf(out, "\"%s\": [\"%s\"]", names[i], dot + 1);
        }
        free(names[i]);
    }
    free(names);
    if (count > 0 && emitted)
        fputc('\n', out);
    indent(out, depth);
    fputc('}', out);
    return emitted;
}

bool Generate_workspace(const utf8 *root, FILE *out)
{
    fprintf(out, "{\n");
    indent(out, 1);
    fprintf(out, "\"version\": 1,\n");
    indent(out, 1);
    fprintf(out, "\"root\": \"%s\",\n", root);
    indent(out, 1);
    fprintf(out, "\"tree\": ");
    bool ok = emit(out, root, 1);
    fprintf(out, "\n}\n");
    return ok;
}
