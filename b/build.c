#include "build.h"
#include "territory.h"

/* Defined below; used by both the unit and the dependency compiles. */
static void appendExternalIncludes(char *command, bytes capacity, bytes *length);

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == nullptr)
        return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0)
    {
        fclose(f);
        return nullptr;
    }
    char *buf = malloc((bytes)sz + 1);
    if (buf == nullptr)
    {
        fclose(f);
        return nullptr;
    }
    bytes rd = fread(buf, 1, (bytes)sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

/* Glob matching: * (no slash), ** (any including slash). */
static bool glob_match(const char *pattern, const char *str)
{
    while (*pattern && *str)
    {
        if (pattern[0] == '*' && pattern[1] == '*')
        {
            pattern += 2;
            if (*pattern == '/')
                pattern++;
            if (*pattern == '\0')
                return true;
            const char *s = str;
            while (*s)
            {
                if (glob_match(pattern, s))
                    return true;
                s++;
            }
            return glob_match(pattern, "");
        }
        else if (*pattern == '*')
        {
            pattern++;
            const char *s = str;
            while (*s && *s != '/')
            {
                if (glob_match(pattern, s))
                    return true;
                s++;
            }
            return glob_match(pattern, "");
        }
        else if (*pattern == '?')
        {
            if (*str == '/')
                return false;
            pattern++;
            str++;
        }
        else
        {
            if (*pattern != *str)
                return false;
            pattern++;
            str++;
        }
    }
    while (*pattern == '*')
        pattern++;
    return (*pattern == '\0' && *str == '\0');
}

/* Recursive directory walk collecting files matching glob pattern. */
static void walk_glob(const char *dir, const char *pattern, const char *prefix,
                      char items[][512], bytes *n, bytes max)
{
    DIR *d = opendir(dir);
    if (d == nullptr)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr)
    {
        if (strcmp((*e).d_name, ".") == 0 || strcmp((*e).d_name, "..") == 0)
            continue;
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", dir, (*e).d_name);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
        {
            walk_glob(full, pattern, prefix, items, n, max);
        }
        else if (S_ISREG(st.st_mode))
        {
            /* Build relative path from prefix. */
            const char *rel = full + strlen(prefix);
            if (*rel == '/')
                rel++;
            if (glob_match(pattern, rel) && *n < max)
            {
                snprintf(items[*n], 512, "%s", full);
                /* store relative path instead of full? keep full for compiler */
                /* Actually store relative to cwd (prefix-stripped) */
                snprintf(items[*n], 512, "%s", rel);
                (*n)++;
            }
        }
    }
    closedir(d);
}

/* Expand a source spec (literal or glob) into file list. */
static void expand_source(const char *spec, char items[][512], bytes *n, bytes max)
{
    if (strchr(spec, '*') || strchr(spec, '?'))
    {
        /* glob: walk from "." with pattern = spec */
        walk_glob(".", spec, ".", items, n, max);
    }
    else
    {
        if (*n < max)
        {
            strncpy(items[*n], spec, 511);
            items[*n][511] = '\0';
            (*n)++;
        }
    }
}

/* Find unit name containing source_path (or matching unit_name if source is a unit name). */
static bool find_unit_name(const char *assembly, const char *key,
                          char *unit_name, bytes name_sz)
{
    /* If key is a unit name directly, check it exists. */
    char quoted[512];
    snprintf(quoted, sizeof(quoted), "\"%s\"", key);
    if (strstr(assembly, quoted) != nullptr)
    {
        /* verify it's a top-level key under assembly */
        const char *a = strstr(assembly, "\"assembly\"");
        if (a)
        {
            const char *p = strstr(a, quoted);
            if (p && p > a)
            {
                strncpy(unit_name, key, name_sz - 1);
                unit_name[name_sz - 1] = '\0';
                return true;
            }
        }
    }
    /* Otherwise search by source path. */
    const char *a = strstr(assembly, "\"assembly\"");
    if (a == nullptr)
        return false;
    const char *o = strchr(a, '{');
    if (o == nullptr)
        return false;
    const char *p = o + 1;
    int depth = 1;
    while (*p && depth >= 1)
    {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')
            p++;
        if (*p == '}')
        {
            depth--;
            p++;
            continue;
        }
        if (*p != '"')
        {
            p++;
            continue;
        }
        p++;
        const char *kstart = p;
        while (*p && *p != '"')
            p++;
        bytes klen = (bytes)(p - kstart);
        if (*p == '"')
            p++;
        while (*p && *p != ':')
            p++;
        if (*p == ':')
            p++;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        if (*p != '{')
        {
            p++;
            continue;
        }
        const char *ustart = p;
        int ud = 0;
        const char *q = p;
        while (*q)
        {
            if (*q == '{')
                ud++;
            else if (*q == '}')
            {
                ud--;
                if (ud == 0)
                    break;
            }
            else if (*q == '"')
            {
                q++;
                while (*q && *q != '"')
                {
                    if (*q == '\\')
                        q++;
                    q++;
                }
            }
            q++;
        }
        bytes blen = (bytes)(q - ustart + 1);
        char *block = malloc(blen + 1);
        memcpy(block, ustart, blen);
        block[blen] = '\0';
        if (strstr(block, key) != nullptr)
        {
            if (klen < name_sz)
            {
                memcpy(unit_name, kstart, klen);
                unit_name[klen] = '\0';
            }
            free(block);
            return true;
        }
        free(block);
        p = q + 1;
    }
    return false;
}

static bool find_unit_block(const char *assembly, const char *unit_name,
                           const char **start, const char **end)
{
    char key[512];
    snprintf(key, sizeof(key), "\"%s\"", unit_name);
    const char *p = strstr(assembly, key);
    if (p == nullptr)
        return false;
    const char *o = strchr(p, '{');
    if (o == nullptr)
        return false;
    int depth = 0;
    const char *q = o;
    while (*q)
    {
        if (*q == '{')
            depth++;
        else if (*q == '}')
        {
            depth--;
            if (depth == 0)
            {
                *start = o;
                *end = q + 1;
                return true;
            }
        }
        else if (*q == '"')
        {
            q++;
            while (*q && *q != '"')
            {
                if (*q == '\\')
                    q++;
                q++;
            }
        }
        q++;
    }
    return false;
}

static void collect_strings(const char *block, const char *field,
                           char items[][512], bytes *n, bytes max)
{
    char key[128];
    snprintf(key, sizeof(key), "\"%s\"", field);
    const char *p = strstr(block, key);
    if (p == nullptr)
        return;
    p = strchr(p, '[');
    if (p == nullptr)
        return;
    p++;
    while (*p && *p != ']' && *n < max)
    {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')
            p++;
        if (*p == '"')
        {
            p++;
            bytes i = 0;
            while (*p && *p != '"' && i < 511)
            {
                items[*n][i++] = *p;
                p++;
            }
            items[*n][i] = '\0';
            if (i > 0)
                (*n)++;
            if (*p == '"')
                p++;
        }
        else
            break;
    }
}


/* Compile a dependency repository's sources once and append the objects.
 *
 * A manifest names its sibling repositories in `dependencies` (paths, not unit
 * names). Those sources are compiled into .b/obj/deps/<path>/ and linked into
 * the unit, so a test links the R0 and R2 code it exercises. A source that
 * defines main() is skipped: the unit owns the entry point. */
static void appendDependencyObjects(char *objects, bytes capacity, bytes *used,
                                    const char *territory, const char *dependency,
                                    const char *objectBase, const char *includeFlags,
                                    char amalgamatedList[][256], bytes amalgamatedCount)
{
    char listing[1024];
    snprintf(listing, sizeof(listing), "%s/deps.list", objectBase);
    char command[4096];
    snprintf(command, sizeof(command),
             "find '%s/%s' \\( -name '*.c' -o -name '*.m' \\) -not -path '*/.git/*' -not -path '*/.b/*' 2>/dev/null | sort > '%s'",
             territory, dependency, listing);
    if (system(command) != 0)
        return;
    // The unit already compiles its own directory; the dependency sweep must
    // stop at it or every symbol in a shared source is defined twice.
    char here[4096];
    bytes hereLength = 0;
    if (getcwd(here, sizeof(here)) != nullptr)
        hereLength = strlen(here);

    FILE *file = fopen(listing, "r");
    if (file == nullptr)
        return;
    char line[4096];
    while (fgets(line, sizeof(line), file) != nullptr)
    {
        bytes length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = '\0';
        if (length == 0)
            continue;
        if (hereLength > 0 && strncmp(line, here, hereLength) == 0
            && line[hereLength] == '/')
            continue;

        FILE *source = fopen(line, "rb");
        if (source == nullptr)
            continue;
        static char contents[1 << 16];
        bytes read = fread(contents, 1, sizeof(contents) - 1, source);
        contents[read] = '\0';
        fclose(source);
        if (strstr(contents, "int main(") != nullptr)
            continue;

        const char *relative = line;
        bytes prefix = strlen(territory);
        if (strncmp(line, territory, prefix) == 0)
            relative = line + prefix + 1;

        const char *leaf = strrchr(line, '/');
        leaf = leaf != nullptr ? leaf + 1 : line;

        // A unit source may paste this file in with #include "x.c". Compiling
        // it again beside the unit would define every symbol twice.
        bool alreadyPasted = false;
        for (bytes a = 0; a < amalgamatedCount; ++a)
            if (strcmp(leaf, amalgamatedList[a]) == 0)
                alreadyPasted = true;
        if (alreadyPasted)
            continue;

        // One directory, one feature: a shim and its C file live together, and
        // the build picks one. A unit manifest that lists its sources is the
        // authority for its own directory; a directory without one falls back
        // to the platform-shim convention.
        char directoryOf[4096];
        snprintf(directoryOf, sizeof(directoryOf), "%s", line);
        char *lastSlash = strrchr(directoryOf, '/');
        if (lastSlash != nullptr)
            *lastSlash = '\0';

        char manifestPath[4096];
        snprintf(manifestPath, sizeof(manifestPath), "%s/b.assembly.json", directoryOf);
        FILE *manifest = fopen(manifestPath, "rb");
        if (manifest != nullptr)
        {
            static char manifestText[1 << 16];
            bytes manifestLength = fread(manifestText, 1, sizeof(manifestText) - 1, manifest);
            manifestText[manifestLength] = '\0';
            fclose(manifest);
            const char *sourcesKey = strstr(manifestText, "\"sources\"");
            if (sourcesKey != nullptr && strstr(sourcesKey, leaf) == nullptr)
                continue;   // this unit does not claim the file
        }
        else
        {
            // A portable .c the platform shim replaces: touchid.c beside
            // touchid_cocoa.m compiles as the shim only on Apple.
            const char *leafDot = strrchr(leaf, '.');
            if (leafDot != nullptr && strcmp(leafDot, ".c") == 0)
            {
                static const char *shims[] = { "_cocoa.m", "_apple.m", "_macos.m", "_cocoa.c" };
                bool replaced = false;
                for (bytes s = 0; s < sizeof(shims) / sizeof(shims[0]) && !replaced; ++s)
                {
                    char candidate[4096];
                    snprintf(candidate, sizeof(candidate), "%s/%.*s%s", directoryOf,
                             (int) (leafDot - leaf), leaf, shims[s]);
                    struct stat probe;
                    replaced = stat(candidate, &probe) == 0;
                }
                if (replaced)
                    continue;
            }
        }

        char object[4096];
        snprintf(object, sizeof(object), "%s/deps/%.*s.o", objectBase,
                 (int) (strlen(relative) - 2), relative);
        char directory[4096];
        snprintf(directory, sizeof(directory), "%s", object);
        char *slash = strrchr(directory, '/');
        if (slash != nullptr)
            *slash = '\0';
        char make[8192];
        snprintf(make, sizeof(make), "mkdir -p '%s'", directory);
        if (system(make) != 0)
            continue;

        struct stat sourceInfo, objectInfo;
        bool fresh = stat(object, &objectInfo) == 0
                  && stat(line, &sourceInfo) == 0
                  && objectInfo.st_mtime >= sourceInfo.st_mtime;
        if (!fresh)
        {
            char compile[16384];
            snprintf(compile, sizeof(compile),
                     "clang -std=gnu23 -g -O0 -w %s -c '%s' -o '%s' 2>/dev/null",
                     includeFlags, line, object);
            if (system(compile) != 0)
                continue;
        }

        *used += (bytes) snprintf(objects + *used, capacity - *used, " '%s'", object);
    }
    fclose(file);
    remove(listing);
}


/* A C source may amalgamate another by #include-ing the .c itself.
 *
 * That is how a white-box test reaches the statics of a program file: the
 * preprocessor pastes the whole translation unit in, so the test and the code
 * share one scope. The pattern is legitimate, but the included file must not
 * ALSO be compiled into the unit or every symbol it defines is a duplicate.
 * Record the basenames here so the compile loop can skip them and say so. */
static void noteAmalgamation(const char *path, char names[][256], bytes *count, bytes capacity)
{
    FILE *file = fopen(path, "rb");
    if (file == nullptr)
        return;
    static char text[1 << 18];
    bytes length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    const char *cursor = text;
    while ((cursor = strstr(cursor, "#include \"")) != nullptr)
    {
        cursor += 10;
        const char *end = strchr(cursor, '"');
        if (end == nullptr)
            break;
        bytes span = (bytes) (end - cursor);
        if (span > 2 && (strncmp(end - 2, ".c", 2) == 0 || strncmp(end - 2, ".m", 2) == 0))
        {
            const char *leaf = end;
            while (leaf > cursor && *(leaf - 1) != '/')
                --leaf;
            bytes leafLength = (bytes) (end - leaf);
            if (leafLength > 0 && leafLength < 256 && *count < capacity)
            {
                memcpy(names[*count], leaf, leafLength);
                names[*count][leafLength] = '\0';
                *count += 1;
            }
        }
        cursor = end + 1;
    }
}

bool Build_file(const char *source_path, const char *assembly_path,
                const char *workspace_path, const char *out_dir, FILE *out)
{
    (void)workspace_path;
    char *asm_buf = read_file(assembly_path);
    if (asm_buf == nullptr)
    {
        fprintf(out, "build: cannot read %s\n", assembly_path);
        return false;
    }
    // Manifest paths are workspace-relative, not shell-relative: the same
    // manifest must build from the repository root or from its own directory.
    char *territory = Territory_find(".");
    const char *base = territory != nullptr ? territory : "";

    // Output is territory-grounded too: every unit lands in <territory>/.b/out
    // beside the application it builds for, never in a stray .b/ under a test
    // directory.
    char resolvedOut[4096];
    if (out_dir != nullptr && out_dir[0] == '/')
        snprintf(resolvedOut, sizeof(resolvedOut), "%s", out_dir);
    else
        snprintf(resolvedOut, sizeof(resolvedOut), "%s/%s", base,
                 out_dir != nullptr ? out_dir : ".b/out");
    out_dir = resolvedOut;

    char unit_name[256];
    const char *blk_start = nullptr, *blk_end = nullptr;
    bool singleUnit = false;
    if (find_unit_name(asm_buf, source_path, unit_name, sizeof(unit_name)))
    {
        if (!find_unit_block(asm_buf, unit_name, &blk_start, &blk_end))
        {
            fprintf(out, "build: cannot locate unit block for %s\n", unit_name);
            free(asm_buf);
            return false;
        }
    }
    else if (strstr(asm_buf, "\"application\"") != nullptr)
    {
        // A manifest with no "assembly" wrapper declares exactly one unit:
        // the whole file is the block. This is the schema the repositories
        // use, so `b build <unit>` must read it, not only the legacy wrapper.
        const char *nameKey = strstr(asm_buf, "\"name\"");
        const char *nameValue = nameKey != nullptr ? strchr(nameKey, ':') : nullptr;
        bytes written = 0;
        if (nameValue != nullptr)
        {
            nameValue = strchr(nameValue, '"');
            if (nameValue != nullptr)
            {
                ++nameValue;
                while (*nameValue != '"' && *nameValue != '\0' && written + 1 < sizeof(unit_name))
                    unit_name[written++] = *nameValue++;
            }
        }
        if (written == 0)
            snprintf(unit_name, sizeof(unit_name), "unit");
        unit_name[written] = '\0';
        blk_start = asm_buf;
        blk_end = asm_buf + strlen(asm_buf);
        singleUnit = true;
    }
    else
    {
        fprintf(out, "build: no assembly unit found for %s\n", source_path);
        free(asm_buf);
        return false;
    }
    bytes blk_len = (bytes)(blk_end - blk_start);
    char *block = malloc(blk_len + 1);
    memcpy(block, blk_start, blk_len);
    block[blk_len] = '\0';

    fprintf(out, "build: unit '%s'\n", unit_name);

    /* Determine unit type. */
    char unit_type[64] = "executable";
    const char *tp = strstr(block, "\"type\"");
    if (tp != nullptr)
    {
        tp = strchr(tp, ':');
        if (tp != nullptr)
        {
            tp++;
            while (*tp == ' ' || *tp == '\t' || *tp == '\n' || *tp == '\r')
                tp++;
            if (*tp == '"')
            {
                tp++;
                bytes ti = 0;
                while (*tp && *tp != '"' && ti < sizeof(unit_type) - 1)
                    unit_type[ti++] = *tp++;
                unit_type[ti] = '\0';
            }
        }
    }

    /* Collect sources (with glob expansion). */
    char raw_sources[64][512];
    bytes nraw = 0;
    collect_strings(block, "sources", raw_sources, &nraw, 64);
    if (nraw == 0)
        collect_strings(block, "entry", raw_sources, &nraw, 64);

    // One executable, one entry point. A directory may hold several standalone
    // test programs; the unit builds only the one it names as `entry`, plus any
    // source that does not define main().
    char entry_name[512] = "";
    {
        char declared[4][512];
        bytes declared_count = 0;
        collect_strings(block, "entry", declared, &declared_count, 4);
        if (declared_count > 0)
        {
            const char *slash = strrchr(declared[0], '/');
            snprintf(entry_name, sizeof(entry_name), "%s", slash != nullptr ? slash + 1 : declared[0]);
        }
    }
    // "cli", "executable" and any other non-library type all link as programs.
    if (strcmp(unit_type, "lib") != 0 && strcmp(unit_type, "module") != 0 && entry_name[0] != '\0')
    {
        bytes kept = 0;
        for (bytes i = 0; i < nraw; ++i)
        {
            const char *slash = strrchr(raw_sources[i], '/');
            const char *leaf = slash != nullptr ? slash + 1 : raw_sources[i];
            bool isEntry = strcmp(leaf, entry_name) == 0;
            bool definesMain = false;
            FILE *probe = fopen(raw_sources[i], "rb");
            if (probe != nullptr)
            {
                static char text[1 << 16];
                bytes length = fread(text, 1, sizeof(text) - 1, probe);
                text[length] = '\0';
                fclose(probe);
                definesMain = strstr(text, "int main(") != nullptr;
            }
            if (isEntry || !definesMain)
            {
                memmove(raw_sources[kept], raw_sources[i], sizeof(raw_sources[i]));
                kept += 1;
            }
        }
        nraw = kept;
    }
    /* If unit has .rs sources, build via cargo. */
    bool has_rust = false;
    for (bytes i = 0; i < nraw; i++)
    {
        const char *d = strrchr(raw_sources[i], '.');
        if (d && strcmp(d + 1, "rs") == 0)
            has_rust = true;
    }
    if (has_rust)
    {
        /* Find Cargo.toml from first .rs source. */
        for (bytes i = 0; i < nraw; i++)
        {
            const char *d = strrchr(raw_sources[i], '.');
            if (d && strcmp(d + 1, "rs") == 0)
            {
                char tmp[1024];
                strcpy(tmp, raw_sources[i]);
                char *sl = strrchr(tmp, '/');
                if (sl)
                {
                    *sl = '\0';
                    char dir[1024];
                    strcpy(dir, tmp);
                    while (dir[0] != '\0')
                    {
                        char cargo[1024];
                        snprintf(cargo, sizeof(cargo), "%s/Cargo.toml", dir);
                        FILE *cf = fopen(cargo, "r");
                        if (cf)
                        {
                            fclose(cf);
                            char cmd[2048];
                            snprintf(cmd, sizeof(cmd), "cd %s && cargo build 2>&1", dir);
                            fprintf(out, "build: %s\n", cmd);
                            system(cmd);
                            break;
                        }
                        char *s2 = strrchr(dir, '/');
                        if (s2)
                            *s2 = '\0';
                        else
                            break;
                    }
                }
                break;
            }
        }
    }



    char sources[256][512];
    bytes nsrc = 0;
    for (bytes i = 0; i < nraw; i++)
    {
        expand_source(raw_sources[i], sources, &nsrc, 256);
    }
    if (nsrc == 0)
    {
        fprintf(out, "build: no sources in unit\n");
        free(block);
        free(asm_buf);
        return false;
    }

    /* Amalgamated sources: #include "x.c" is legal, being compiled twice is not. */
    char amalgamated[32][256];
    bytes namalgamated = 0;
    for (bytes i = 0; i < nsrc; i++)
        noteAmalgamation(sources[i], amalgamated, &namalgamated, 32);
    for (bytes i = 0; i < namalgamated; i++)
        fprintf(out, "build: amalgamating %s (included, not compiled separately)\n", amalgamated[i]);

    /* Collect include roots (unit + deps). */
    char include_flags[32768] = "";
    bytes if_used = 0;

    // The territory root is always an include root: repository directories are
    // spelled from there, and the floor adds `lib` for the `vex.h` door.
    if_used += (bytes)snprintf(include_flags + if_used, sizeof(include_flags) - if_used,
                                " -I%s", base);
    char roots[64][512];
    bytes nroots = 0;
    collect_strings(block, "include_roots", roots, &nroots, 64);
    for (bytes i = 0; i < nroots; i++)
        if_used += (bytes)snprintf(include_flags + if_used, sizeof(include_flags) - if_used,
                                    " -I%s/%s", base, roots[i]);
    // A repository manifest spells include roots `includes`, and names its
    // sibling repositories in `dependencies`. Both are plain include roots
    // now that every repository is flat.
    collect_strings(block, "includes", roots, &nroots, 64);
    for (bytes i = 0; i < nroots; i++)
        if_used += (bytes)snprintf(include_flags + if_used, sizeof(include_flags) - if_used,
                                    " -I%s/%s", base, roots[i]);
    if (singleUnit)
    {
        char declared[64][512];
        bytes ndeclared = 0;
        collect_strings(block, "dependencies", declared, &ndeclared, 64);
        for (bytes i = 0; i < ndeclared; i++)
            if_used += (bytes)snprintf(include_flags + if_used, sizeof(include_flags) - if_used,
                                        " -I%s/%s", base, declared[i]);
    }

    char deps[64][512];
    bytes ndeps = 0;
    collect_strings(block, "deps", deps, &ndeps, 64);
    if (singleUnit)
        collect_strings(block, "dependencies", deps, &ndeps, 64);
    for (bytes d = 0; d < ndeps; d++)
    {
        const char *ds = nullptr, *de = nullptr;
        if (find_unit_block(asm_buf, deps[d], &ds, &de))
        {
            bytes dl = (bytes)(de - ds);
            char *dblock = malloc(dl + 1);
            memcpy(dblock, ds, dl);
            dblock[dl] = '\0';
            char droots[64][512];
            bytes ndr = 0;
            collect_strings(dblock, "include_roots", droots, &ndr, 64);
            for (bytes r = 0; r < ndr; r++)
                if_used += (bytes)snprintf(include_flags + if_used, sizeof(include_flags) - if_used,
                                            " -I%s", droots[r]);
            free(dblock);
        }
    }

    // External SDK headers (Vulkan, Homebrew) apply to dependency sources too,
    // not only to the workspace build.
    appendExternalIncludes(include_flags, sizeof(include_flags), &if_used);

    /* Shader codegen: if "shaders" declared, generate headers into .b/gen. */
    char shaders[64][512];
    bytes nshaders = 0;
    collect_strings(block, "shaders", shaders, &nshaders, 64);
    if (nshaders > 0)
    {
        char gen_dir[1024];
        snprintf(gen_dir, sizeof(gen_dir), "%s/../gen", out_dir);
        /* resolve .b/gen */
        char base_dir[1024];
        strncpy(base_dir, out_dir, sizeof(base_dir) - 1);
        base_dir[sizeof(base_dir) - 1] = '\0';
        char *sl = strrchr(base_dir, '/');
        if (sl) *sl = '\0';
        snprintf(gen_dir, sizeof(gen_dir), "%s/gen", base_dir);
        char mk[2048];
        snprintf(mk, sizeof(mk), "mkdir -p %s", gen_dir);
        system(mk);

        for (bytes i = 0; i < nshaders; i++)
        {
            /* Expand glob if needed. */
            char shader_files[256][512];
            bytes nsf = 0;
            expand_source(shaders[i], shader_files, &nsf, 256);
            for (bytes j = 0; j < nsf; j++)
            {
                const char *src = shader_files[j];
                const char *base = strrchr(src, '/');
                base = base ? base + 1 : src;
                char name[256];
                strncpy(name, base, sizeof(name) - 1);
                name[sizeof(name) - 1] = '\0';
                /* replace dots with underscores for variable name */
                for (char *c = name; *c; c++)
                    if (*c == '.') *c = '_';
                char stem[256];
                strncpy(stem, base, sizeof(stem) - 1);
                stem[sizeof(stem) - 1] = '\0';
                for (char *c = stem; *c; c++)
                    if (*c == '.') *c = '_';

                char spv[1024], hdr[1024];
                /* header uses stem-without-extension + _spv.h; spv uses full stem */
                char hdrstem[256];
                strcpy(hdrstem, stem);
                /* Derive header name from original base name's first component */
                char hname[256];
                strcpy(hname, base);
                char *hdot = strchr(hname, '.');
                if (hdot) *hdot = '\0';
                /* hname now = "quad" (for quad.vert) */
                snprintf(spv, sizeof(spv), "%s/%s.spv", gen_dir, stem);
                snprintf(hdr, sizeof(hdr), "%s/%s_spv.h", gen_dir, hname);

                char cmd[4096];
                /* glslc -> SPIR-V */
                snprintf(cmd, sizeof(cmd), "glslc -I vex-lib/graphvex/src %s -o %s 2>&1", src, spv);
                fprintf(out, "shader: %s\n", cmd);
                system(cmd);
                /* xxd -i -> C header */
                snprintf(cmd, sizeof(cmd), "cd %s && xxd -i %s.spv > %s_spv.h 2>&1", gen_dir, stem, name);
                fprintf(out, "shader: %s\n", cmd);
                system(cmd);
            }
        }
        /* Add .b/gen to include roots. */
        bytes gl = strlen(include_flags);
        snprintf(include_flags + gl, sizeof(include_flags) - gl, " -I%s", gen_dir);
    }

    /* Create object dir. */
    char base_dir[1024];
    strncpy(base_dir, out_dir, sizeof(base_dir) - 1);
    base_dir[sizeof(base_dir) - 1] = '\0';
    char *slash = strrchr(base_dir, '/');
    if (slash)
        *slash = '\0';
    char obj_unit_dir[1024];
    snprintf(obj_unit_dir, sizeof(obj_unit_dir), "%s/obj/%s", base_dir, unit_name);
    char mkdir_cmd[2048];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", obj_unit_dir);
    system(mkdir_cmd);

    // The manifest's own compiler options, applied after the defaults so a
    // unit can relax a warning it knowingly carries (deprecations in a legacy
    // program file), instead of the build relaxing it for everyone.
    char options[64][512];
    bytes noptions = 0;
    collect_strings(block, "options", options, &noptions, 64);
    char option_flags[2048] = "";
    bytes option_used = 0;
    for (bytes i = 0; i < noptions; ++i)
        option_used += (bytes)snprintf(option_flags + option_used, sizeof(option_flags) - option_used,
                                        " %s", options[i]);

    /* Compile each source. */
    char obj_files[262144] = "";
    bytes of_used = 0;
    for (bytes i = 0; i < nsrc; i++)
    {
        const char *src = sources[i];
        const char *ed = strrchr(src, '.');
        const char *ext2 = ed ? ed + 1 : "";
        if (strcmp(ext2, "rs") == 0)
            continue; /* Rust handled by cargo separately */
        const char *sourceLeaf = strrchr(src, '/');
        sourceLeaf = sourceLeaf != nullptr ? sourceLeaf + 1 : src;
        bool amalgamatedSource = false;
        for (bytes a = 0; a < namalgamated; ++a)
            if (strcmp(sourceLeaf, amalgamated[a]) == 0)
                amalgamatedSource = true;
        if (amalgamatedSource)
            continue; /* pasted into the unit that includes it */
        const char *base = strrchr(src, '/');
        base = base ? base + 1 : src;
        char obj_name[256];
        strncpy(obj_name, base, sizeof(obj_name) - 1);
        obj_name[sizeof(obj_name) - 1] = '\0';
        char *dot = strrchr(obj_name, '.');
        if (dot)
            strcpy(dot, ".o");
        else
            strcat(obj_name, ".o");

        char obj_path[1024];
        snprintf(obj_path, sizeof(obj_path), "%s/%s", obj_unit_dir, obj_name);

        char cmd[65536];
        const char *edot = strrchr(src, '.');
        const char *ext = edot ? edot + 1 : "";
        /* Debuggable by default: CLion/LLDB reads these symbols, and the hot
           module swap keeps them per image. B_CFLAGS overrides the default. */
        const char *debug_flags = getenv("B_CFLAGS");
        if (!debug_flags || !*debug_flags)
            debug_flags = "-g -O0";
        if (strcmp(ext, "m") == 0)
        {
            snprintf(cmd, sizeof(cmd),
                     "clang -std=gnu23 -Wall -Wextra -Werror %s -c -x objective-c -fobjc-arc %s%s %s -o %s",
                     debug_flags, include_flags, option_flags, src, obj_path);
        }
        else if (strcmp(ext, "rs") == 0)
        {
            snprintf(cmd, sizeof(cmd),
                     "rustc --crate-type staticlib -O %s -o %s",
                     src, obj_path);
        }
        else
        {
            snprintf(cmd, sizeof(cmd),
                     "clang -std=gnu23 -Wall -Wextra -Werror %s -c %s%s %s -o %s",
                     debug_flags, include_flags, option_flags, src, obj_path);
        }
        fprintf(out, "build: %s\n", cmd);
        int rc = system(cmd);
        if (rc != 0)
        {
            fprintf(out, "build: FAILED compiling %s (exit %d)\n", src, rc);
            free(block);
            free(asm_buf);
            return false;
        }
        if (of_used > 0)
            of_used += (bytes)snprintf(obj_files + of_used, sizeof(obj_files) - of_used, " ");
        of_used += (bytes)snprintf(obj_files + of_used, sizeof(obj_files) - of_used, "%s", obj_path);
    }

    /* Link dependency objects for an executable unit. */
    if (strcmp(unit_type, "lib") != 0 && strcmp(unit_type, "module") != 0 && ndeps > 0)
    {
        for (bytes d = 0; d < ndeps; d++)
            appendDependencyObjects(obj_files, sizeof(obj_files), &of_used,
                                    territory, deps[d], obj_unit_dir, include_flags,
                                    amalgamated, namalgamated);
    }

    /* Link or archive. */
    char out_path[1024];
    int rc = 0;
    if (strcmp(unit_type, "lib") == 0)
    {
        snprintf(out_path, sizeof(out_path), "%s/lib%s.a", out_dir, unit_name);
        char ar_cmd[524288];
        snprintf(ar_cmd, sizeof(ar_cmd), "ar rcs %s %s", out_path, obj_files);
        fprintf(out, "build: %s\n", ar_cmd);
        rc = system(ar_cmd);
    }
    else if (strcmp(unit_type, "module") == 0)
    {
        /* A reloadable application image. It links NO dep archives on purpose:
           the host process already holds R1-R4, and -undefined dynamic_lookup
           resolves the Frame and Panel symbols against it at load time. Linking
           a second copy would be a shadow owner (Compose down, never rebuild). */
        snprintf(out_path, sizeof(out_path), "%s/%s.dylib", out_dir, unit_name);
        char link_cmd[524288];
        snprintf(link_cmd, sizeof(link_cmd),
                 "clang -g -dynamiclib -fPIC -undefined dynamic_lookup %s -o %s",
                 obj_files, out_path);
        fprintf(out, "build: %s\n", link_cmd);
        rc = system(link_cmd);
    }
    else
    {
        snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, unit_name);
        /* Build dep library link flags. */
        char dep_libs[4096] = "";
        bytes dl_used = 0;
        // A single-application manifest names dependencies as source roots;
        // their objects are already on the link line. Asking for -l<name>
        // would look for archives this build never produces.
        if (!singleUnit)
        {
            for (bytes d = 0; d < ndeps; d++)
            {
                dl_used += (bytes)snprintf(dep_libs + dl_used, sizeof(dep_libs) - dl_used,
                                            " -L%s -l%s", out_dir, deps[d]);
            }
        }
        char link_cmd[524288];
        // -dead_strip drops the dependency code this unit never reaches: every
        // repository is compiled whole, and an executable that uses a handful
        // of its symbols should not carry the rest. Deliberately NOT applied to
        // "module" units: their entry points are found by dlsym, so the linker
        // sees them as unreachable and would strip the swap surface away.
        snprintf(link_cmd, sizeof(link_cmd),
                 "clang -g -dead_strip %s %s -o %s -framework Cocoa -framework Foundation -framework Metal -framework QuartzCore -framework IOKit -framework CoreVideo -framework CoreGraphics -framework IOSurface -framework Network -framework Security -framework AppKit -framework AVFoundation -framework CoreLocation -framework Contacts -framework EventKit -framework LocalAuthentication -framework Photos -framework UserNotifications -framework CoreMedia -framework CoreServices -framework SystemConfiguration -framework CoreFoundation -L/opt/homebrew/lib -lvulkan -lncurses -lcurl -lsqlite3 -lz -lm -lpthread", obj_files, dep_libs, out_path);
        fprintf(out, "build: %s\n", link_cmd);
        rc = system(link_cmd);
    }
    free(block);
    free(asm_buf);

    if (rc != 0)
    {
        fprintf(out, "build: FAILED (exit %d)\n", rc);
        return false;
    }

    /* If type is "app", wrap binary in a macOS .app bundle. */
    if (strcmp(unit_type, "app") == 0)
    {
        char app_dir[1024];
        snprintf(app_dir, sizeof(app_dir), "%s/%s.app", out_dir, unit_name);
        char macos_dir[1024];
        snprintf(macos_dir, sizeof(macos_dir), "%s/Contents/MacOS", app_dir);
        char mkdir_cmd[2048];
        snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", macos_dir);
        system(mkdir_cmd);

        /* Copy binary. */
        char cp_cmd[2048];
        snprintf(cp_cmd, sizeof(cp_cmd), "cp %s %s/%s", out_path, macos_dir, unit_name);
        system(cp_cmd);
        chmod(macos_dir, 0755);

        /* Generate Info.plist. */
        char plist[2048];
        snprintf(plist, sizeof(plist), "%s/Contents/Info.plist", app_dir);
        FILE *pf = fopen(plist, "w");
        if (pf)
        {
            fprintf(pf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
            fprintf(pf, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
            fprintf(pf, "<plist version=\"1.0\">\n");
            fprintf(pf, "<dict>\n");
            fprintf(pf, "  <key>CFBundleExecutable</key>\n");
            fprintf(pf, "  <string>%s</string>\n", unit_name);
            fprintf(pf, "  <key>CFBundleIdentifier</key>\n");
            fprintf(pf, "  <string>com.vexgraph.%s</string>\n", unit_name);
            fprintf(pf, "  <key>CFBundlePackageType</key>\n");
            fprintf(pf, "  <string>APPL</string>\n");
            fprintf(pf, "  <key>CFBundleSignature</key>\n");
            fprintf(pf, "  <string>vexg</string>\n");
            fprintf(pf, "  <key>CFBundleName</key>\n");
            fprintf(pf, "  <string>%s</string>\n", unit_name);
            fprintf(pf, "  <key>CFBundleVersion</key>\n");
            fprintf(pf, "  <string>1.0</string>\n");
            fprintf(pf, "  <key>LSMinimumSystemVersion</key>\n");
            fprintf(pf, "  <string>14.0</string>\n");
            fprintf(pf, "  <key>NSHighResolutionCapable</key>\n");
            fprintf(pf, "  <true/>\n");
            fprintf(pf, "</dict>\n");
            fprintf(pf, "</plist>\n");
            fclose(pf);
        }
        fprintf(out, "build: OK -> %s.app\n", app_dir);
    }
    else
    {
        fprintf(out, "build: OK -> %s\n", out_path);
    }
    return true;
}


/* ------------------------------------------------------------------ *
 * Workspace build: compile every runnable source .b/workspace.json lists.
 *
 * The inventory is the contract: `b generate` wrote it, `b build` with no
 * unit argument consumes it. No assembly file, no --plan, no --confirm.
 * ------------------------------------------------------------------ */

typedef struct
{
    const char *root;
    char **headerDirectories;
    bytes headerCount;
    bytes compiled;
    bytes failed;
} WorkspaceBuild;

/* Call fn(context, directory, name) for every file the inventory records. */
static bool walkWorkspace(const char *text, bool (*fn)(void *, const char *, const char *), void *context)
{
    const char *cursor = strstr(text, "\"directories\"");
    if (cursor == nullptr)
        return false;
    cursor = strchr(cursor, '{');
    if (cursor == nullptr)
        return false;
    ++cursor;
    while (*cursor != '\0' && *cursor != '}')
    {
        while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t' || *cursor == ',')
            ++cursor;
        if (*cursor != '"')
            break;
        ++cursor;
        char directory[512];
        bytes length = 0;
        while (*cursor != '"' && *cursor != '\0' && length + 1 < sizeof(directory))
            directory[length++] = *cursor++;
        directory[length] = '\0';
        if (*cursor == '"')
            ++cursor;
        while (*cursor != '[' && *cursor != '\0')
            ++cursor;
        if (*cursor != '[')
            return false;
        ++cursor;
        while (*cursor != ']' && *cursor != '\0')
        {
            while (*cursor == ' ' || *cursor == ',' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t')
                ++cursor;
            if (*cursor != '"')
                break;
            ++cursor;
            char name[256];
            bytes nameLength = 0;
            while (*cursor != '"' && *cursor != '\0' && nameLength + 1 < sizeof(name))
                name[nameLength++] = *cursor++;
            name[nameLength] = '\0';
            if (*cursor == '"')
                ++cursor;
            if (!fn(context, directory, name))
                return false;
        }
        if (*cursor == ']')
            ++cursor;
    }
    return true;
}

static bool rememberHeaderDirectory(WorkspaceBuild *build, const char *directory)
{
    for (bytes index = 0; index < (*build).headerCount; ++index)
        if (strcmp((*build).headerDirectories[index], directory) == 0)
            return true;
    void *grown = realloc((*build).headerDirectories, ((*build).headerCount + 1) * sizeof(char*));
    if (grown == nullptr)
        return false;
    (*build).headerDirectories = grown;
    (*build).headerDirectories[(*build).headerCount] = strdup(directory);
    if ((*build).headerDirectories[(*build).headerCount] == nullptr)
        return false;
    (*build).headerCount += 1;
    return true;
}

/* Recursively mark every directory holding a header an include root.
 *
 * The inventory lists runnable files only, so headers are discovered from the
 * filesystem instead. This is what makes `#include "net/websocket.h"` resolve
 * without any source spelling a parent hop. */
static bool scanHeaderDirectories(WorkspaceBuild *build, const char *absolute, const char *relativePath)
{
    DIR *directory = opendir(absolute);
    if (directory == nullptr)
        return true;
    bool hasHeader = false;
    struct dirent *entry;
    while ((entry = readdir(directory)) != nullptr)
    {
        const char *name = (*entry).d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        if (strcmp(name, ".git") == 0 || strcmp(name, ".b") == 0 || strcmp(name, "node_modules") == 0)
            continue;
        char child[4096];
        snprintf(child, sizeof(child), "%s/%s", absolute, name);
        struct stat information;
        if (lstat(child, &information) != 0)
            continue;
        if (S_ISDIR(information.st_mode))
        {
            char childRelative[4096];
            snprintf(childRelative, sizeof(childRelative), "%s/%s", relativePath, name);
            if (!scanHeaderDirectories(build, child, childRelative))
            {
                closedir(directory);
                return false;
            }
        }
        else if (S_ISREG(information.st_mode))
        {
            const char *dot = strrchr(name, '.');
            if (dot != nullptr && (strcmp(dot, ".h") == 0 || strcmp(dot, ".hpp") == 0))
                hasHeader = true;
        }
    }
    closedir(directory);
    if (hasHeader && !rememberHeaderDirectory(build, relativePath))
        return false;
    return true;
}

/* Single-quote a path into a growing command line. */
static void appendQuoted(char *command, bytes capacity, bytes *length, const char *path)
{
    bytes pathLength = strlen(path);
    if (*length + pathLength + 4 >= capacity)
        return;
    command[(*length)++] = '\'';
    memcpy(command + *length, path, pathLength);
    *length += pathLength;
    command[(*length)++] = '\'';
    command[*length] = '\0';
}


/* External SDK include roots, probed once per compile.
 *
 * The ecosystem links against a few SDKs that never live inside the tree.
 * Honoured in order: an explicit B_INCLUDE directory, the LunarG convention
 * ($VULKAN_SDK/include), then the two Homebrew prefixes. A directory that is
 * absent is skipped, never a failure — an image that needs Vulkan and cannot
 * find it fails at its own #include, with its own words. */
static void appendExternalIncludes(char *command, bytes capacity, bytes *length)
{
    char sdkInclude[4096];
    const char *candidates[4];
    bytes count = 0;
    const char *extra = getenv("B_INCLUDE");
    if (extra != nullptr && *extra != '\0')
        candidates[count++] = extra;
    const char *sdk = getenv("VULKAN_SDK");
    if (sdk != nullptr && *sdk != '\0')
    {
        snprintf(sdkInclude, sizeof(sdkInclude), "%s/include", sdk);
        candidates[count++] = sdkInclude;
    }
    candidates[count++] = "/opt/homebrew/include";
    candidates[count++] = "/usr/local/include";

    for (bytes index = 0; index < count; ++index)
    {
        struct stat information;
        if (stat(candidates[index], &information) != 0 || !S_ISDIR(information.st_mode))
            continue;
        *length += (bytes) snprintf(command + *length, capacity - *length, " -I");
        appendQuoted(command, capacity, length, candidates[index]);
    }
}

static bool compileSource(void *context, const char *directory, const char *name)
{
    WorkspaceBuild *build = context;
    const char *dot = strrchr(name, '.');
    if (dot == nullptr || strcmp(dot, ".c") != 0)
        return true;

    char source[4096];
    char objectDirectory[4096];
    char object[4096];
    snprintf(source, sizeof(source), "%s/%s/%s", (*build).root,
             strcmp(directory, ".") == 0 ? "" : directory, name);
    snprintf(objectDirectory, sizeof(objectDirectory), "%s/.b/obj/%s", (*build).root,
             strcmp(directory, ".") == 0 ? "root" : directory);
    snprintf(object, sizeof(object), "%s/%.*s.o", objectDirectory,
             (int) (dot - name), name);

    char makeDirectory[8192];
    bytes makeLength = 0;
    makeDirectory[0] = '\0';
    bytes prefixLength = (bytes) snprintf(makeDirectory, sizeof(makeDirectory), "mkdir -p ");
    makeLength = prefixLength;
    appendQuoted(makeDirectory, sizeof(makeDirectory), &makeLength, objectDirectory);
    makeDirectory[makeLength] = '\0';
    if (system(makeDirectory) != 0)
        return true;

    static char command[262144];
    bytes length = 0;
    command[0] = '\0';
    length = (bytes) snprintf(command, sizeof(command),
                               "%s -std=gnu23 -c -Wall -Wextra -o ",
                               getenv("CC") != nullptr ? getenv("CC") : "cc");
    appendQuoted(command, sizeof(command), &length, object);
    length += (bytes) snprintf(command + length, sizeof(command) - length, " ");
    appendQuoted(command, sizeof(command), &length, source);

    appendExternalIncludes(command, sizeof(command), &length);

    // Nearest include root first, or a sibling repository with the same
    // relative path shadows this one: graphvex's "compositor/compositor.h"
    // must beat darling-framework's, and a source's own directory must beat
    // both. This is what keeps a repository building against itself.
    char walked[4096];
    snprintf(walked, sizeof(walked), "%s", strcmp(directory, ".") == 0 ? "" : directory);
    while (walked[0] != '\0')
    {
        char ancestor[4096];
        snprintf(ancestor, sizeof(ancestor), "%s/%s", (*build).root, walked);
        length += (bytes) snprintf(command + length, sizeof(command) - length, " -I");
        appendQuoted(command, sizeof(command), &length, ancestor);
        char *cut = strrchr(walked, '/');
        if (cut == nullptr)
            break;
        *cut = '\0';
    }
    char localDirectory[4096];
    snprintf(localDirectory, sizeof(localDirectory), "%s", (*build).root);
    length += (bytes) snprintf(command + length, sizeof(command) - length, " -I");
    appendQuoted(command, sizeof(command), &length, localDirectory);

    char rootInclude[4096];
    snprintf(rootInclude, sizeof(rootInclude), "%s", (*build).root);
    length += (bytes) snprintf(command + length, sizeof(command) - length, " -I");
    appendQuoted(command, sizeof(command), &length, rootInclude);
    for (bytes index = 0; index < (*build).headerCount; ++index)
    {
        char include[4096];
        snprintf(include, sizeof(include), "%s/%s", (*build).root, (*build).headerDirectories[index]);
        length += (bytes) snprintf(command + length, sizeof(command) - length, " -I");
        appendQuoted(command, sizeof(command), &length, include);
    }

    if (system(command) == 0)
        (*build).compiled += 1;
    else
        (*build).failed += 1;
    return true;
}

bool Build_workspace(const char *workspacePath, FILE *out);
bool Build_workspace(const char *workspacePath, FILE *out)
{
    char *text = read_file(workspacePath);
    if (text == nullptr)
    {
        fprintf(out, "build: cannot read %s — run `b.sh generate` first\n", workspacePath);
        return false;
    }

    WorkspaceBuild build;
    build.root = nullptr;
    build.headerDirectories = nullptr;
    build.headerCount = 0;
    build.compiled = 0;
    build.failed = 0;

    const char *rootKey = strstr(text, "\"root\"");
    char root[4096] = ".";
    if (rootKey != nullptr)
    {
        const char *cursor = strchr(rootKey, ':');
        if (cursor != nullptr)
        {
            cursor = strchr(cursor, '"');
            if (cursor != nullptr)
            {
                ++cursor;
                bytes length = 0;
                while (*cursor != '"' && *cursor != '\0' && length + 1 < sizeof(root))
                    root[length++] = *cursor++;
                root[length] = '\0';
            }
        }
    }
    build.root = root;

    /* Pass 1: include roots.
     *
     * Two levels, because a repository's sources are rooted at the repository,
     * not at the header's own directory:
     *   - every top-level directory is a root, so `c23/constructor.h`,
     *     `lifetime/platform.h` and `nio/mem.h` resolve;
     *   - every directory holding a header is also a root, for sources that
     *     include a sibling by name.
     */
    DIR *top = opendir(root);
    if (top != nullptr)
    {
        struct dirent *entry;
        while ((entry = readdir(top)) != nullptr)
        {
            const char *name = (*entry).d_name;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            if (strcmp(name, ".git") == 0 || strcmp(name, ".b") == 0 || strcmp(name, "node_modules") == 0)
                continue;
            char child[4096];
            snprintf(child, sizeof(child), "%s/%s", root, name);
            struct stat information;
            if (lstat(child, &information) == 0 && S_ISDIR(information.st_mode))
            {
                rememberHeaderDirectory(&build, name);
                // A repository may keep one grouping level below its root
                // (darkbase/engine). Its includes are rooted there, so that
                // level is a root too. No repository keeps src/ any more.
                static const char *nested[] = { "engine" };
                for (bytes level = 0; level < sizeof(nested) / sizeof(nested[0]); ++level)
                {
                    char inner[4096];
                    snprintf(inner, sizeof(inner), "%s/%s/%s", root, name, nested[level]);
                    struct stat nestedInfo;
                    if (lstat(inner, &nestedInfo) == 0 && S_ISDIR(nestedInfo.st_mode))
                    {
                        char nestedRelative[4096];
                        snprintf(nestedRelative, sizeof(nestedRelative), "%s/%s", name, nested[level]);
                        rememberHeaderDirectory(&build, nestedRelative);
                    }
                }
            }
        }
        closedir(top);
    }
    if (!scanHeaderDirectories(&build, root, "."))
    {
        free(text);
        return false;
    }

    /* Pass 2: compile. */
    bool ok = walkWorkspace(text, compileSource, &build);
    fprintf(out, "build: %zu compiled, %zu failed (%zu include roots)\n",
            build.compiled, build.failed, build.headerCount);
    for (bytes index = 0; index < build.headerCount; ++index)
        free(build.headerDirectories[index]);
    free(build.headerDirectories);
    free(text);
    return ok && build.failed == 0;
}

int Build_command(int argc, char **argv)
{
    const char *key = argc > 1 ? argv[1] : nullptr;
    const char *asm_path = argc > 2 ? argv[2] : "b.assembly.json";
    const char *ws_path = argc > 3 ? argv[3] : ".b/workspace.json";
    const char *out_dir = argc > 4 ? argv[4] : ".b/out";
    if (key == nullptr)
        return Build_workspace(ws_path, stdout) ? 0 : 1;
    char mkdir_cmd[1024];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", out_dir);
    system(mkdir_cmd);
    FILE *out = stdout;
    return Build_file(key, asm_path, ws_path, out_dir, out) ? 0 : 1;
}
