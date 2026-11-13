#include "vex.h"

#include "generate.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *COMPILABLE_EXTS[] = {
    "c", "h", "m", "rs", "sh", "java", "cpp", "cc", "hpp",
    "s", "asm", "go", "zig", nullptr
};

static bool is_compilable(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == nullptr || dot == name)
        return false;
    const char *ext = dot + 1;
    for (bytes i = 0; COMPILABLE_EXTS[i] != nullptr; i++)
        if (strcmp(COMPILABLE_EXTS[i], ext) == 0)
            return true;
    return false;
}

static bool is_ignored_dir(const char *name)
{
    if (name[0] == '.')
        return true;
    if (strcmp(name, "node_modules") == 0)
        return true;
    if (strcmp(name, "target") == 0)
        return true;
    if (strcmp(name, ".b") == 0)
        return true;
    return false;
}

static void indent(FILE *out, int depth)
{
    for (int i = 0; i < depth; i++)
        fputs("    ", out);
}

/* Collect sorted entry names for a directory; caller frees. Returns count. */
static bytes collect(const char *path, char ***out_names)
{
    DIR *d = opendir(path);
    if (d == nullptr)
        return 0;
    char **names = nullptr;
    bytes count = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr)
    {
        if (strcmp((*e).d_name, ".") == 0 || strcmp((*e).d_name, "..") == 0)
            continue;
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", path, (*e).d_name);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
        {
            if (is_ignored_dir((*e).d_name))
                continue;
        }
        else if (!S_ISREG(st.st_mode))
            continue;
        else if (!is_compilable((*e).d_name))
            continue;

        if (count == cap)
        {
            bytes next = cap == 0 ? 16 : cap * 2;
            char **tmp = realloc(names, next * sizeof(char*));
            if (tmp == nullptr)
                break;
            names = tmp;
            cap = next;
        }
        names[count] = strdup((*e).d_name);
        count++;
    }
    closedir(d);

    for (bytes i = 1; i < count; i++)
    {
        char *key = names[i];
        bytes j = i;
        while (j > 0 && strcmp(names[j - 1], key) > 0)
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
static bool has_compilable(const char *path)
{
    char **names = nullptr;
    bytes count = collect(path, &names);
    bool found = false;
    for (bytes i = 0; i < count; i++)
    {
        char full[4096];
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
static bool emit(FILE *out, const char *path, int depth)
{
    char **names = nullptr;
    bytes count = collect(path, &names);
    bool first = true;
    bool emitted = false;
    fputc('{', out);
    fputc('\n', out);
    for (bytes i = 0; i < count; i++)
    {
        char full[4096];
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
            const char *dot = strrchr(names[i], '.');
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
    /* If nothing emitted, mark empty by returning false; but we already wrote {}. 
       Caller checks has_compilable before calling, so empty dirs won't be emitted. */
    return emitted;
}

bool Generate_workspace(const char *root, FILE *out)
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
