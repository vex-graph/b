#include "vex.h"
#include "build.h"
#include "b/build_internal.h"
#include "b/b_str.h"
#include "territory.h"

;;DEFINITION
/** Directory and workspace build orchestrator for b.sh compilation units. */
;;OVERVIEW
/** MODULE: build (vex/b/b/build.c). Resolves assembly manifests, compiles units, and links outputs. */

/* Defined below; used by both the unit and the dependency compiles. */
static fn appendExternalIncludes(utf8 *command, bytes capacity, bytes *length);

/* A source defines a program entry when it declares main. The ecosystem spells
 * the return type `i32`; a vendored or generated file may still say `int`. */
static bool textDefinesEntry(const utf8 *text)
{
    return bStrFind(text, "int main(") != nullptr || bStrFind(text, "i32 main(") != nullptr;
}

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static utf8 *read_file(const utf8 *path)
{
    FILE *f = fopen(path, "rb");
    if (f == nullptr)
        return nullptr;
    fseek(f, 0, SEEK_END);
    i64 sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0)
    {
        fclose(f);
        return nullptr;
    }
    utf8 *buf = malloc((bytes)sz + 1);
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
static bool glob_match(const utf8 *pattern, const utf8 *str)
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
            const utf8 *s = str;
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
            const utf8 *s = str;
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

;;GETTER
static inline const utf8 *dirent_d_name(const struct dirent *self)
{
    return (*self).d_name;
}

/* Recursive directory walk collecting files matching glob pattern. */
static fn walk_glob(const utf8 *dir, const utf8 *pattern, const utf8 *prefix,
                      utf8 items[][512], bytes *n, bytes max)
{
    DIR *d = opendir(dir);
    if (d == nullptr)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr)
    {
        const utf8 *name = dirent_d_name(e);
        if (bStrEq(name, ".") || bStrEq(name, ".."))
            continue;
        utf8 full[4096];
        snprintf(full, sizeof(full), "%s/%s", dir, name);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
        {
            walk_glob(full, pattern, prefix, items, n, max);
        }
        else if (S_ISREG(st.st_mode))
        {
            const utf8 *rel = full + strlen(prefix);
            if (*rel == '/')
                rel++;
            if (glob_match(pattern, rel) && *n < max)
            {
                snprintf(items[*n], 512, "%s", full);
                snprintf(items[*n], 512, "%s", rel);
                (*n)++;
            }
        }
    }
    closedir(d);
}

/* Expand a source spec (literal or glob) into file list. */
static fn expand_source(const utf8 *spec, utf8 items[][512], bytes *n, bytes max)
{
    if (bStrFindChar(spec, '*') || bStrFindChar(spec, '?'))
    {
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
static bool find_unit_name(const utf8 *assembly, const utf8 *key,
                          utf8 *unit_name, bytes name_sz)
{
    utf8 quoted[512];
    snprintf(quoted, sizeof(quoted), "\"%s\"", key);
    if (bStrFind(assembly, quoted) != nullptr)
    {
        const utf8 *a = bStrFind(assembly, "\"assembly\"");
        if (a)
        {
            const utf8 *p = bStrFind(a, quoted);
            if (p && p > a)
            {
                strncpy(unit_name, key, name_sz - 1);
                unit_name[name_sz - 1] = '\0';
                return true;
            }
        }
    }
    const utf8 *a = bStrFind(assembly, "\"assembly\"");
    if (a == nullptr)
        return false;
    const utf8 *o = bStrFindChar(a, '{');
    if (o == nullptr)
        return false;
    const utf8 *p = o + 1;
    i32 depth = 1;
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
        const utf8 *kstart = p;
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
        const utf8 *ustart = p;
        i32 ud = 0;
        const utf8 *q = p;
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
        utf8 *block = malloc(blen + 1);
        memcpy(block, ustart, blen);
        block[blen] = '\0';
        if (bStrFind(block, key) != nullptr)
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

static bool find_unit_block(const utf8 *assembly, const utf8 *unit_name,
                           const utf8 **start, const utf8 **end)
{
    utf8 key[512];
    snprintf(key, sizeof(key), "\"%s\"", unit_name);
    const utf8 *p = bStrFind(assembly, key);
    if (p == nullptr)
        return false;
    const utf8 *o = bStrFindChar(p, '{');
    if (o == nullptr)
        return false;
    i32 depth = 0;
    const utf8 *q = o;
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

static fn collect_strings(const utf8 *block, const utf8 *field,
                           utf8 items[][512], bytes *n, bytes max)
{
    utf8 key[128];
    snprintf(key, sizeof(key), "\"%s\"", field);
    const utf8 *p = bStrFind(block, key);
    if (p == nullptr)
        return;
    p = bStrFindChar(p, '[');
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
static fn appendDependencyObjects(utf8 *objects, bytes capacity, bytes *used,
                                    const utf8 *territory, const utf8 *dependency,
                                    const utf8 *objectBase, const utf8 *includeFlags,
                                    utf8 amalgamatedList[][256], bytes amalgamatedCount)
{
    utf8 listing[1024];
    snprintf(listing, sizeof(listing), "%s/deps.list", objectBase);
    utf8 command[4096];
    snprintf(command, sizeof(command),
             "find '%s/%s' \\( -name '*.c' -o -name '*.m' \\) -not -path '*/.git/*' -not -path '*/.b/*' 2>/dev/null | sort > '%s'",
             territory, dependency, listing);
    if (system(command) != 0)
        return;
    utf8 here[4096];
    bytes hereLength = 0;
    if (getcwd(here, sizeof(here)) != nullptr)
        hereLength = strlen(here);

    FILE *file = fopen(listing, "r");
    if (file == nullptr)
        return;
    utf8 line[4096];
    while (fgets(line, sizeof(line), file) != nullptr)
    {
        bytes length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = '\0';
        if (length == 0)
            continue;
        if (hereLength > 0 && bStrPrefixN(line, here, hereLength)
            && line[hereLength] == '/')
            continue;

        FILE *source = fopen(line, "rb");
        if (source == nullptr)
            continue;
        static utf8 contents[1 << 16];
        bytes read = fread(contents, 1, sizeof(contents) - 1, source);
        contents[read] = '\0';
        fclose(source);
        if (textDefinesEntry(contents))
            continue;

        const utf8 *relative = line;
        bytes prefix = strlen(territory);
        if (bStrPrefixN(line, territory, prefix))
            relative = line + prefix + 1;

        const utf8 *leaf = strrchr(line, '/');
        leaf = leaf != nullptr ? leaf + 1 : line;

        bool alreadyPasted = false;
        for (bytes a = 0; a < amalgamatedCount; ++a)
            if (bStrEq(leaf, amalgamatedList[a]))
                alreadyPasted = true;
        if (alreadyPasted)
            continue;

        utf8 directoryOf[4096];
        snprintf(directoryOf, sizeof(directoryOf), "%s", line);
        utf8 *lastSlash = strrchr(directoryOf, '/');
        if (lastSlash != nullptr)
            *lastSlash = '\0';

        utf8 manifestPath[4096];
        snprintf(manifestPath, sizeof(manifestPath), "%s/b.assembly.json", directoryOf);
        FILE *manifest = fopen(manifestPath, "rb");
        if (manifest != nullptr)
        {
            static utf8 manifestText[1 << 16];
            bytes manifestLength = fread(manifestText, 1, sizeof(manifestText) - 1, manifest);
            manifestText[manifestLength] = '\0';
            fclose(manifest);
            const utf8 *sourcesKey = bStrFind(manifestText, "\"sources\"");
            if (sourcesKey != nullptr && bStrFind(sourcesKey, leaf) == nullptr)
                continue;
        }
        else
        {
            const utf8 *leafDot = strrchr(leaf, '.');
            if (leafDot != nullptr && bStrEq(leafDot, ".c"))
            {
                static const utf8 *shims[] = { "_cocoa.m", "_apple.m", "_macos.m", "_cocoa.c" };
                bool replaced = false;
                for (bytes s = 0; s < sizeof(shims) / sizeof(shims[0]) && !replaced; ++s)
                {
                    utf8 candidate[4096];
                    snprintf(candidate, sizeof(candidate), "%s/%.*s%s", directoryOf,
                             (i32) (leafDot - leaf), leaf, shims[s]);
                    struct stat probe;
                    replaced = stat(candidate, &probe) == 0;
                }
                if (replaced)
                    continue;
            }
        }

        utf8 object[4096];
        snprintf(object, sizeof(object), "%s/deps/%.*s.o", objectBase,
                 (i32) (strlen(relative) - 2), relative);
        utf8 directory[4096];
        snprintf(directory, sizeof(directory), "%s", object);
        utf8 *slash = strrchr(directory, '/');
        if (slash != nullptr)
            *slash = '\0';
        utf8 make[8192];
        snprintf(make, sizeof(make), "mkdir -p '%s'", directory);
        if (system(make) != 0)
            continue;

        struct stat sourceInfo, objectInfo;
        bool fresh = stat(object, &objectInfo) == 0
                  && stat(line, &sourceInfo) == 0
                  && objectInfo.st_mtime >= sourceInfo.st_mtime;
        if (!fresh)
        {
            utf8 compile[16384];
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
static fn noteAmalgamation(const utf8 *path, utf8 names[][256], bytes *count, bytes capacity)
{
    FILE *file = fopen(path, "rb");
    if (file == nullptr)
        return;
    static utf8 text[1 << 18];
    bytes length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    const utf8 *cursor = text;
    while ((cursor = bStrFind(cursor, "#include \"")) != nullptr)
    {
        cursor += 10;
        const utf8 *end = bStrFindChar(cursor, '"');
        if (end == nullptr)
            break;
        bytes span = (bytes) (end - cursor);
        if (span > 2 && (bStrPrefix(end - 2, ".c") || bStrPrefix(end - 2, ".m")))
        {
            const utf8 *leaf = end;
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

bool Build_file(const utf8 *source_path, const utf8 *assembly_path,
                const utf8 *workspace_path, const utf8 *out_dir, FILE *out)
{
    (discard) workspace_path;
    utf8 *asm_buf = read_file(assembly_path);
    if (asm_buf == nullptr)
    {
        fprintf(out, "build: cannot read %s\n", assembly_path);
        return false;
    }
    utf8 *territory = Territory_find(".");
    const utf8 *base = territory != nullptr ? territory : "";

    utf8 resolvedOut[4096];
    if (out_dir != nullptr && out_dir[0] == '/')
        snprintf(resolvedOut, sizeof(resolvedOut), "%s", out_dir);
    else
        snprintf(resolvedOut, sizeof(resolvedOut), "%s/%s", base,
                 out_dir != nullptr ? out_dir : ".b/out");
    out_dir = resolvedOut;

    utf8 unit_name[256];
    const utf8 *blk_start = nullptr, *blk_end = nullptr;
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
    else if (bStrFind(asm_buf, "\"application\"") != nullptr)
    {
        const utf8 *nameKey = bStrFind(asm_buf, "\"name\"");
        const utf8 *nameValue = nameKey != nullptr ? bStrFindChar(nameKey, ':') : nullptr;
        bytes written = 0;
        if (nameValue != nullptr)
        {
            nameValue = bStrFindChar(nameValue, '"');
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
    utf8 *block = malloc(blk_len + 1);
    memcpy(block, blk_start, blk_len);
    block[blk_len] = '\0';

    fprintf(out, "build: unit '%s'\n", unit_name);

    utf8 unit_type[64] = "executable";
    const utf8 *tp = bStrFind(block, "\"type\"");
    if (tp != nullptr)
    {
        tp = bStrFindChar(tp, ':');
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

    utf8 raw_sources[64][512];
    bytes nraw = 0;
    collect_strings(block, "sources", raw_sources, &nraw, 64);
    if (nraw == 0)
        collect_strings(block, "entry", raw_sources, &nraw, 64);

    utf8 entry_name[512] = "";
    {
        utf8 declared[4][512];
        bytes declared_count = 0;
        collect_strings(block, "entry", declared, &declared_count, 4);
        if (declared_count > 0)
        {
            const utf8 *slash = strrchr(declared[0], '/');
            snprintf(entry_name, sizeof(entry_name), "%s", slash != nullptr ? slash + 1 : declared[0]);
        }
    }
    if (!bStrEq(unit_type, "lib") && !bStrEq(unit_type, "module") && entry_name[0] != '\0')
    {
        bytes kept = 0;
        for (bytes i = 0; i < nraw; ++i)
        {
            const utf8 *slash = strrchr(raw_sources[i], '/');
            const utf8 *leaf = slash != nullptr ? slash + 1 : raw_sources[i];
            bool isEntry = bStrEq(leaf, entry_name);
            bool definesMain = false;
            FILE *probe = fopen(raw_sources[i], "rb");
            if (probe != nullptr)
            {
                static utf8 text[1 << 16];
                bytes length = fread(text, 1, sizeof(text) - 1, probe);
                text[length] = '\0';
                fclose(probe);
                definesMain = textDefinesEntry(text);
            }
            if (isEntry || !definesMain)
            {
                memmove(raw_sources[kept], raw_sources[i], sizeof(raw_sources[i]));
                kept += 1;
            }
        }
        nraw = kept;
    }
    bool has_rust = false;
    for (bytes i = 0; i < nraw; i++)
    {
        const utf8 *d = strrchr(raw_sources[i], '.');
        if (d && bStrEq(d + 1, "rs"))
            has_rust = true;
    }
    if (has_rust)
    {
        for (bytes i = 0; i < nraw; i++)
        {
            const utf8 *d = strrchr(raw_sources[i], '.');
            if (d && bStrEq(d + 1, "rs"))
            {
                utf8 tmp[1024];
                strcpy(tmp, raw_sources[i]);
                utf8 *sl = strrchr(tmp, '/');
                if (sl)
                {
                    *sl = '\0';
                    utf8 dir[1024];
                    strcpy(dir, tmp);
                    while (dir[0] != '\0')
                    {
                        utf8 cargo[1024];
                        snprintf(cargo, sizeof(cargo), "%s/Cargo.toml", dir);
                        FILE *cf = fopen(cargo, "r");
                        if (cf)
                        {
                            fclose(cf);
                            utf8 cmd[2048];
                            snprintf(cmd, sizeof(cmd), "cd %s && cargo build 2>&1", dir);
                            fprintf(out, "build: %s\n", cmd);
                            system(cmd);
                            break;
                        }
                        utf8 *s2 = strrchr(dir, '/');
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



    utf8 sources[256][512];
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

    utf8 amalgamated[32][256];
    bytes namalgamated = 0;
    for (bytes i = 0; i < nsrc; i++)
        noteAmalgamation(sources[i], amalgamated, &namalgamated, 32);
    for (bytes i = 0; i < namalgamated; i++)
        fprintf(out, "build: amalgamating %s (included, not compiled separately)\n", amalgamated[i]);

    utf8 include_flags[32768] = "";
    bytes if_used = 0;

    if_used += (bytes)snprintf(include_flags + if_used, sizeof(include_flags) - if_used,
                                " -I%s", base);
    utf8 roots[64][512];
    bytes nroots = 0;
    collect_strings(block, "include_roots", roots, &nroots, 64);
    for (bytes i = 0; i < nroots; i++)
        if_used += (bytes)snprintf(include_flags + if_used, sizeof(include_flags) - if_used,
                                    " -I%s/%s", base, roots[i]);
    collect_strings(block, "includes", roots, &nroots, 64);
    for (bytes i = 0; i < nroots; i++)
        if_used += (bytes)snprintf(include_flags + if_used, sizeof(include_flags) - if_used,
                                    " -I%s/%s", base, roots[i]);
    if (singleUnit)
    {
        utf8 declared[64][512];
        bytes ndeclared = 0;
        collect_strings(block, "dependencies", declared, &ndeclared, 64);
        for (bytes i = 0; i < ndeclared; i++)
            if_used += (bytes)snprintf(include_flags + if_used, sizeof(include_flags) - if_used,
                                        " -I%s/%s", base, declared[i]);
    }

    utf8 deps[64][512];
    bytes ndeps = 0;
    collect_strings(block, "deps", deps, &ndeps, 64);
    if (singleUnit)
        collect_strings(block, "dependencies", deps, &ndeps, 64);
    for (bytes d = 0; d < ndeps; d++)
    {
        const utf8 *ds = nullptr, *de = nullptr;
        if (find_unit_block(asm_buf, deps[d], &ds, &de))
        {
            bytes dl = (bytes)(de - ds);
            utf8 *dblock = malloc(dl + 1);
            memcpy(dblock, ds, dl);
            dblock[dl] = '\0';
            utf8 droots[64][512];
            bytes ndr = 0;
            collect_strings(dblock, "include_roots", droots, &ndr, 64);
            for (bytes r = 0; r < ndr; r++)
                if_used += (bytes)snprintf(include_flags + if_used, sizeof(include_flags) - if_used,
                                            " -I%s", droots[r]);
            free(dblock);
        }
    }

    appendExternalIncludes(include_flags, sizeof(include_flags), &if_used);

    utf8 shaders[64][512];
    bytes nshaders = 0;
    collect_strings(block, "shaders", shaders, &nshaders, 64);
    if (nshaders > 0)
    {
        utf8 gen_dir[1024];
        snprintf(gen_dir, sizeof(gen_dir), "%s/../gen", out_dir);
        utf8 base_dir[1024];
        strncpy(base_dir, out_dir, sizeof(base_dir) - 1);
        base_dir[sizeof(base_dir) - 1] = '\0';
        utf8 *sl = strrchr(base_dir, '/');
        if (sl)
            *sl = '\0';
        snprintf(gen_dir, sizeof(gen_dir), "%s/gen", base_dir);
        utf8 mk[2048];
        snprintf(mk, sizeof(mk), "mkdir -p %s", gen_dir);
        system(mk);

        for (bytes i = 0; i < nshaders; i++)
        {
            utf8 shader_files[256][512];
            bytes nsf = 0;
            expand_source(shaders[i], shader_files, &nsf, 256);
            for (bytes j = 0; j < nsf; j++)
            {
                const utf8 *src = shader_files[j];
                const utf8 *base = strrchr(src, '/');
                base = base ? base + 1 : src;
                utf8 name[256];
                strncpy(name, base, sizeof(name) - 1);
                name[sizeof(name) - 1] = '\0';
                for (utf8 *c = name; *c; c++)
                    if (*c == '.')
                        *c = '_';
                utf8 stem[256];
                strncpy(stem, base, sizeof(stem) - 1);
                stem[sizeof(stem) - 1] = '\0';
                for (utf8 *c = stem; *c; c++)
                    if (*c == '.')
                        *c = '_';

                utf8 spv[1024], hdr[1024];
                utf8 hdrstem[256];
                strcpy(hdrstem, stem);
                utf8 hname[256];
                strcpy(hname, base);
                utf8 *hdot = bStrFindCharMut(hname, '.');
                if (hdot)
                    *hdot = '\0';
                snprintf(spv, sizeof(spv), "%s/%s.spv", gen_dir, stem);
                snprintf(hdr, sizeof(hdr), "%s/%s_spv.h", gen_dir, hname);

                utf8 cmd[4096];
                snprintf(cmd, sizeof(cmd), "glslc -I vex-lib/graphvex/src %s -o %s 2>&1", src, spv);
                fprintf(out, "shader: %s\n", cmd);
                system(cmd);
                snprintf(cmd, sizeof(cmd), "cd %s && xxd -i %s.spv > %s_spv.h 2>&1", gen_dir, stem, name);
                fprintf(out, "shader: %s\n", cmd);
                system(cmd);
            }
        }
        bytes gl = strlen(include_flags);
        snprintf(include_flags + gl, sizeof(include_flags) - gl, " -I%s", gen_dir);
    }

    utf8 base_dir[1024];
    strncpy(base_dir, out_dir, sizeof(base_dir) - 1);
    base_dir[sizeof(base_dir) - 1] = '\0';
    utf8 *slash = strrchr(base_dir, '/');
    if (slash)
        *slash = '\0';
    utf8 obj_unit_dir[1024];
    snprintf(obj_unit_dir, sizeof(obj_unit_dir), "%s/obj/%s", base_dir, unit_name);
    utf8 mkdir_cmd[2048];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", obj_unit_dir);
    system(mkdir_cmd);

    utf8 options[64][512];
    bytes noptions = 0;
    collect_strings(block, "options", options, &noptions, 64);
    utf8 option_flags[2048] = "";
    bytes option_used = 0;
    for (bytes i = 0; i < noptions; ++i)
        option_used += (bytes)snprintf(option_flags + option_used, sizeof(option_flags) - option_used,
                                        " %s", options[i]);

    utf8 obj_files[262144] = "";
    bytes of_used = 0;
    for (bytes i = 0; i < nsrc; i++)
    {
        const utf8 *src = sources[i];
        const utf8 *ed = strrchr(src, '.');
        const utf8 *ext2 = ed ? ed + 1 : "";
        if (bStrEq(ext2, "rs"))
            continue;
        const utf8 *sourceLeaf = strrchr(src, '/');
        sourceLeaf = sourceLeaf != nullptr ? sourceLeaf + 1 : src;
        bool amalgamatedSource = false;
        for (bytes a = 0; a < namalgamated; ++a)
            if (bStrEq(sourceLeaf, amalgamated[a]))
                amalgamatedSource = true;
        if (amalgamatedSource)
            continue;
        const utf8 *base = strrchr(src, '/');
        base = base ? base + 1 : src;
        utf8 obj_name[256];
        strncpy(obj_name, base, sizeof(obj_name) - 1);
        obj_name[sizeof(obj_name) - 1] = '\0';
        utf8 *dot = strrchr(obj_name, '.');
        if (dot)
            strcpy(dot, ".o");
        else
            strcat(obj_name, ".o");

        utf8 obj_path[1024];
        snprintf(obj_path, sizeof(obj_path), "%s/%s", obj_unit_dir, obj_name);

        utf8 cmd[65536];
        const utf8 *edot = strrchr(src, '.');
        const utf8 *ext = edot ? edot + 1 : "";
        const utf8 *debug_flags = getenv("B_CFLAGS");
        if (!debug_flags || !*debug_flags)
            debug_flags = "-g -O0";
        if (bStrEq(ext, "m"))
        {
            snprintf(cmd, sizeof(cmd),
                     "clang -std=gnu23 -Wall -Wextra -Werror %s -c -x objective-c -fobjc-arc %s%s %s -o %s",
                     debug_flags, include_flags, option_flags, src, obj_path);
        }
        else if (bStrEq(ext, "rs"))
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
        i32 rc = system(cmd);
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

    if (!bStrEq(unit_type, "lib") && !bStrEq(unit_type, "module") && ndeps > 0)
    {
        for (bytes d = 0; d < ndeps; d++)
            appendDependencyObjects(obj_files, sizeof(obj_files), &of_used,
                                    territory, deps[d], obj_unit_dir, include_flags,
                                    amalgamated, namalgamated);
    }

    utf8 out_path[1024];
    i32 rc = 0;
    if (bStrEq(unit_type, "lib"))
    {
        snprintf(out_path, sizeof(out_path), "%s/lib%s.a", out_dir, unit_name);
        utf8 ar_cmd[524288];
        snprintf(ar_cmd, sizeof(ar_cmd), "ar rcs %s %s", out_path, obj_files);
        fprintf(out, "build: %s\n", ar_cmd);
        rc = system(ar_cmd);
    }
    else if (bStrEq(unit_type, "module"))
    {
        snprintf(out_path, sizeof(out_path), "%s/%s.dylib", out_dir, unit_name);
        utf8 link_cmd[524288];
        snprintf(link_cmd, sizeof(link_cmd),
                 "clang -g -dynamiclib -fPIC -undefined dynamic_lookup %s -o %s",
                 obj_files, out_path);
        fprintf(out, "build: %s\n", link_cmd);
        rc = system(link_cmd);
    }
    else
    {
        snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, unit_name);
        utf8 dep_libs[4096] = "";
        bytes dl_used = 0;
        if (!singleUnit)
        {
            for (bytes d = 0; d < ndeps; d++)
            {
                dl_used += (bytes)snprintf(dep_libs + dl_used, sizeof(dep_libs) - dl_used,
                                            " -L%s -l%s", out_dir, deps[d]);
            }
        }
        utf8 link_cmd[524288];
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

    if (bStrEq(unit_type, "app"))
    {
        utf8 app_dir[1024];
        snprintf(app_dir, sizeof(app_dir), "%s/%s.app", out_dir, unit_name);
        utf8 macos_dir[1024];
        snprintf(macos_dir, sizeof(macos_dir), "%s/Contents/MacOS", app_dir);
        utf8 mkdir_cmd[2048];
        snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", macos_dir);
        system(mkdir_cmd);

        utf8 cp_cmd[2048];
        snprintf(cp_cmd, sizeof(cp_cmd), "cp %s %s/%s", out_path, macos_dir, unit_name);
        system(cp_cmd);
        chmod(macos_dir, 0755);

        utf8 plist[2048];
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



/* Call fn(context, directory, name) for every file the inventory records. */
static bool walkWorkspace(const utf8 *text, bool (*fn)(any , const utf8 *, const utf8 *), any context)
{
    const utf8 *cursor = bStrFind(text, "\"directories\"");
    if (cursor == nullptr)
        return false;
    cursor = bStrFindChar(cursor, '{');
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
        utf8 directory[512];
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
            utf8 name[256];
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

;;GETTER
static inline bytes WorkspaceBuild_headerCount(const WorkspaceBuild *self)
{
    return (*self).headerCount;
}
;;SETTER
static inline fn WorkspaceBuild_setHeaderCount(WorkspaceBuild *self, bytes count)
{
    (*self).headerCount = count;
}
;;GETTER
static inline utf8 **WorkspaceBuild_headerDirectories(const WorkspaceBuild *self)
{
    return (*self).headerDirectories;
}
;;SETTER
static inline fn WorkspaceBuild_setHeaderDirectories(WorkspaceBuild *self, utf8 **dirs)
{
    (*self).headerDirectories = dirs;
}
;;GETTER
static inline const utf8 *WorkspaceBuild_root(const WorkspaceBuild *self)
{
    return (*self).root;
}
;;GETTER
static inline bytes WorkspaceBuild_compiled(const WorkspaceBuild *self)
{
    return (*self).compiled;
}
;;SETTER
static inline fn WorkspaceBuild_setCompiled(WorkspaceBuild *self, bytes compiled)
{
    (*self).compiled = compiled;
}
;;GETTER
static inline bytes WorkspaceBuild_failed(const WorkspaceBuild *self)
{
    return (*self).failed;
}
;;SETTER
static inline fn WorkspaceBuild_setFailed(WorkspaceBuild *self, bytes failed)
{
    (*self).failed = failed;
}

static bool rememberHeaderDirectory(WorkspaceBuild *build, const utf8 *directory)
{
    bytes count = WorkspaceBuild_headerCount(build);
    utf8 **dirs = WorkspaceBuild_headerDirectories(build);
    for (bytes index = 0; index < count; ++index)
        if (bStrEq(dirs[index], directory))
            return true;
    any grown = realloc(dirs, (count + 1) * sizeof(utf8*));
    if (grown == nullptr)
        return false;
    WorkspaceBuild_setHeaderDirectories(build, grown);
    dirs = WorkspaceBuild_headerDirectories(build);
    utf8 *copy = strdup(directory);
    if (copy == nullptr)
        return false;
    dirs[count] = copy;
    WorkspaceBuild_setHeaderCount(build, count + 1);
    return true;
}

/* Recursively mark every directory holding a header an include root.
 *
 * The inventory lists runnable files only, so headers are discovered from the
 * filesystem instead. This is what makes `#include "net/websocket.h"` resolve
 * without any source spelling a parent hop. */
static bool scanHeaderDirectories(WorkspaceBuild *build, const utf8 *absolute, const utf8 *relativePath)
{
    DIR *directory = opendir(absolute);
    if (directory == nullptr)
        return true;
    bool hasHeader = false;
    struct dirent *entry;
    while ((entry = readdir(directory)) != nullptr)
    {
        const utf8 *name = dirent_d_name(entry);
        if (bStrEq(name, ".") || bStrEq(name, ".."))
            continue;
        if (bStrEq(name, ".git") || bStrEq(name, ".b") || bStrEq(name, "node_modules"))
            continue;
        utf8 child[4096];
        snprintf(child, sizeof(child), "%s/%s", absolute, name);
        struct stat information;
        if (lstat(child, &information) != 0)
            continue;
        if (S_ISDIR(information.st_mode))
        {
            utf8 childRelative[4096];
            snprintf(childRelative, sizeof(childRelative), "%s/%s", relativePath, name);
            if (!scanHeaderDirectories(build, child, childRelative))
            {
                closedir(directory);
                return false;
            }
        }
        else if (S_ISREG(information.st_mode))
        {
            const utf8 *dot = strrchr(name, '.');
            if (dot != nullptr && (bStrEq(dot, ".h") || bStrEq(dot, ".hpp")))
                hasHeader = true;
        }
    }
    closedir(directory);
    if (hasHeader && !rememberHeaderDirectory(build, relativePath))
        return false;
    return true;
}

/* Single-quote a path into a growing command line. */
static fn appendQuoted(utf8 *command, bytes capacity, bytes *length, const utf8 *path)
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
 * absent is skipped, never a failure, an image that needs Vulkan and cannot
 * find it fails at its own #include, with its own words. */
static fn appendExternalIncludes(utf8 *command, bytes capacity, bytes *length)
{
    utf8 sdkInclude[4096];
    const utf8 *candidates[4];
    bytes count = 0;
    const utf8 *extra = getenv("B_INCLUDE");
    if (extra != nullptr && *extra != '\0')
        candidates[count++] = extra;
    const utf8 *sdk = getenv("VULKAN_SDK");
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

static bool compileSource(any context, const utf8 *directory, const utf8 *name)
{
    WorkspaceBuild *build = context;
    const utf8 *dot = strrchr(name, '.');
    if (dot == nullptr || !bStrEq(dot, ".c"))
        return true;

    utf8 source[4096];
    utf8 objectDirectory[4096];
    utf8 object[4096];
    const utf8 *root = WorkspaceBuild_root(build);
    snprintf(source, sizeof(source), "%s/%s/%s", root,
             bStrEq(directory, ".") ? "" : directory, name);
    snprintf(objectDirectory, sizeof(objectDirectory), "%s/.b/obj/%s", root,
             bStrEq(directory, ".") ? "root" : directory);
    snprintf(object, sizeof(object), "%s/%.*s.o", objectDirectory,
             (i32) (dot - name), name);

    utf8 makeDirectory[8192];
    bytes makeLength = 0;
    makeDirectory[0] = '\0';
    bytes prefixLength = (bytes) snprintf(makeDirectory, sizeof(makeDirectory), "mkdir -p ");
    makeLength = prefixLength;
    appendQuoted(makeDirectory, sizeof(makeDirectory), &makeLength, objectDirectory);
    makeDirectory[makeLength] = '\0';
    if (system(makeDirectory) != 0)
        return true;

    static utf8 command[262144];
    bytes length = 0;
    command[0] = '\0';
    length = (bytes) snprintf(command, sizeof(command),
                               "%s -std=gnu23 -c -Wall -Wextra -o ",
                               getenv("CC") != nullptr ? getenv("CC") : "cc");
    appendQuoted(command, sizeof(command), &length, object);
    length += (bytes) snprintf(command + length, sizeof(command) - length, " ");
    appendQuoted(command, sizeof(command), &length, source);

    appendExternalIncludes(command, sizeof(command), &length);

    utf8 walked[4096];
    snprintf(walked, sizeof(walked), "%s", bStrEq(directory, ".") ? "" : directory);
    while (walked[0] != '\0')
    {
        utf8 ancestor[4096];
        snprintf(ancestor, sizeof(ancestor), "%s/%s", root, walked);
        length += (bytes) snprintf(command + length, sizeof(command) - length, " -I");
        appendQuoted(command, sizeof(command), &length, ancestor);
        utf8 *cut = strrchr(walked, '/');
        if (cut == nullptr)
            break;
        *cut = '\0';
    }
    utf8 localDirectory[4096];
    snprintf(localDirectory, sizeof(localDirectory), "%s", root);
    length += (bytes) snprintf(command + length, sizeof(command) - length, " -I");
    appendQuoted(command, sizeof(command), &length, localDirectory);

    utf8 rootInclude[4096];
    snprintf(rootInclude, sizeof(rootInclude), "%s", root);
    length += (bytes) snprintf(command + length, sizeof(command) - length, " -I");
    appendQuoted(command, sizeof(command), &length, rootInclude);
    bytes headerCount = WorkspaceBuild_headerCount(build);
    utf8 *const *headerDirectories = WorkspaceBuild_headerDirectories(build);
    for (bytes index = 0; index < headerCount; ++index)
    {
        utf8 include[4096];
        snprintf(include, sizeof(include), "%s/%s", root, headerDirectories[index]);
        length += (bytes) snprintf(command + length, sizeof(command) - length, " -I");
        appendQuoted(command, sizeof(command), &length, include);
    }

    if (system(command) == 0)
        WorkspaceBuild_setCompiled(build, WorkspaceBuild_compiled(build) + 1);
    else
        WorkspaceBuild_setFailed(build, WorkspaceBuild_failed(build) + 1);
    return true;
}

bool Build_workspace(const utf8 *workspacePath, FILE *out);
bool Build_workspace(const utf8 *workspacePath, FILE *out)
{
    utf8 *text = read_file(workspacePath);
    if (text == nullptr)
    {
        fprintf(out, "build: cannot read %s, run `b.sh generate` first\n", workspacePath);
        return false;
    }

    WorkspaceBuild build;
    build.root = nullptr;
    build.headerDirectories = nullptr;
    build.headerCount = 0;
    build.compiled = 0;
    build.failed = 0;

    const utf8 *rootKey = bStrFind(text, "\"root\"");
    utf8 root[4096] = ".";
    if (rootKey != nullptr)
    {
        const utf8 *cursor = bStrFindChar(rootKey, ':');
        if (cursor != nullptr)
        {
            cursor = bStrFindChar(cursor, '"');
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

    DIR *top = opendir(root);
    if (top != nullptr)
    {
        struct dirent *entry;
        while ((entry = readdir(top)) != nullptr)
        {
            const utf8 *name = dirent_d_name(entry);
            if (bStrEq(name, ".") || bStrEq(name, ".."))
                continue;
            if (bStrEq(name, ".git") || bStrEq(name, ".b") || bStrEq(name, "node_modules"))
                continue;
            utf8 child[4096];
            snprintf(child, sizeof(child), "%s/%s", root, name);
            struct stat information;
            if (lstat(child, &information) == 0 && S_ISDIR(information.st_mode))
            {
                rememberHeaderDirectory(&build, name);
                static const utf8 *nested[] = { "engine" };
                for (bytes level = 0; level < sizeof(nested) / sizeof(nested[0]); ++level)
                {
                    utf8 inner[4096];
                    snprintf(inner, sizeof(inner), "%s/%s/%s", root, name, nested[level]);
                    struct stat nestedInfo;
                    if (lstat(inner, &nestedInfo) == 0 && S_ISDIR(nestedInfo.st_mode))
                    {
                        utf8 nestedRelative[4096];
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

    bool ok = walkWorkspace(text, compileSource, &build);
    fprintf(out, "build: %zu compiled, %zu failed (%zu include roots)\n",
            build.compiled, build.failed, build.headerCount);
    for (bytes index = 0; index < build.headerCount; ++index)
        free(build.headerDirectories[index]);
    free(build.headerDirectories);
    free(text);
    return ok && build.failed == 0;
}

i32 Build_command(i32 argc, utf8 **argv)
{
    const utf8 *key = argc > 1 ? argv[1] : nullptr;
    const utf8 *asm_path = argc > 2 ? argv[2] : "b.assembly.json";
    const utf8 *ws_path = argc > 3 ? argv[3] : ".b/workspace.json";
    const utf8 *out_dir = argc > 4 ? argv[4] : ".b/out";
    if (key == nullptr)
        return Build_workspace(ws_path, stdout) ? 0 : 1;
    utf8 mkdir_cmd[1024];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", out_dir);
    system(mkdir_cmd);
    FILE *out = stdout;
    return Build_file(key, asm_path, ws_path, out_dir, out) ? 0 : 1;
}
