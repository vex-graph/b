/**
 * CLASS: C language adapter.
 * DEFINITION: Compiles a directory's top-level .c files into one native
 * executable with the C23 dialect and the project's build flags; a single
 * source is compiled on demand for `run exec`. C has no source runtime, so
 * `run instance` rejects and points at `run exec`. The compiler is $CC or cc.
 */

#include "vex.h"
#include "adapters/c.h"
#include "b.h"
#include "b/b_str.h"
#include "territory.h"

#include <dirent.h>
#include <sys/stat.h>

/** Forward declaration. */
static utf8 *joinPath(string directory, string name);

/** Join a directory and one child name into an owned path. */
static utf8 *joinPath(string directory, string name) {
    utf8 *slash = Util_combine(directory, "/");
    utf8 *path = Util_combine(slash, name);
    Util_release(slash);
    return path;
}

;;GETTER
static inline string dirent_d_name(const struct dirent *self)
{
    return (*self).d_name;
}

;;DEFINITION
/* C compilation produces one native executable from supplied sources, using
 * the selected CC executable, strict C23 warnings and platform baseline flags.
 * Directory builds discover top-level .c inputs; exec builds one entry source
 * before launching it. Instance rejects because there is no C source runtime.
 * Child failures propagate and prevent launching previous output. The unit's
 * declared include roots (build.includes in its assembly) resolve against the
 * territory root and reach the compiler as -I paths; the compiler, not b.sh, reads
 * the #include lines.
 */
;;OVERVIEW
/* MODULE: C adapter; exported record: C_ADAPTER (adapters/c.h).
 * METADATA: tools="CC=cc"; capabilities="native build/exec; source instance rejects".
 * PRIVATE STATIC: assemblyIncludes, read the build.includes string array from
 * the unit's b.sh.assembly.json; compileC, compile explicit sources into an
 * out-of-tree program with the declared include roots; buildC, collect
 * directory sources and compile; entryC, owned path of the file defining main;
 * runC, reject instance or compile and execute with borrowed program args.
 * RECORD FIELDS: name="c"; extension=".c"; build=buildC; run=runC; entry=entryC.
 * No owned class state; temporary argv/path/source lists are freed per call.
 */

#include <stdlib.h>
#include <string.h>

/**
 * Read the string items of the unit's build.includes array from b.sh.assembly.json.
 * The reader accepts only the array b.sh assembly writes: quoted paths, no escapes.
 * A missing file or an empty array yields no roots; a malformed array sets *ok.
 * Two passes size the list exactly, so the reader never grows storage in place.
 */
static utf8 **assemblyIncludes(string project, bytes *count, bool *ok) {
    *count = 0;
    *ok = true;
    utf8 *path = Util_combine(project, "/b.sh.assembly.json");
    utf8 *text = Util_readText(path);
    Util_release(path);
    if (text == nullptr)
        return nullptr;
    utf8 *key = bStrFindMut(text, "\"includes\"");
    utf8 *open = key != nullptr ? bStrFindCharMut(key, '[') : nullptr;
    utf8 *end = open != nullptr ? bStrFindCharMut(open, ']') : nullptr;
    if (end == nullptr) {
        Util_release(text);
        return nullptr;
    }
    bytes items = 0;
    for (utf8 *quote = bStrFindCharMut(open, '"'); quote != nullptr && quote < end; quote = bStrFindCharMut(quote + 1, '"')) {
        utf8 *close = bStrFindCharMut(quote + 1, '"');
        if (close == nullptr || close > end) {
            THROW("malformed build.includes in %s", project);
            *ok = false;
            Util_release(text);
            return nullptr;
        }
        ++items;
        quote = close;
    }
    utf8 **list = Util_allocate((items != 0 ? items : 1) * sizeof(utf8*));
    bytes filled = 0;
    for (utf8 *quote = bStrFindCharMut(open, '"'); quote != nullptr && quote < end; ) {
        utf8 *close = bStrFindCharMut(quote + 1, '"');
        bytes length = (bytes) (close - quote - 1);
        utf8 *item = Util_allocate(length + 1);
        memcpy(item, quote + 1, length);
        list[filled++] = item;
        quote = bStrFindCharMut(close + 1, '"');
    }
    *count = filled;
    Util_release(text);
    return list;
}

/**
 * Compile the supplied C23 sources into one out-of-tree native executable. Declared
 * include roots resolve against the territory root and are passed as -I paths.
 */
static utf8 *baseNameOf(string path);
static utf8 *stemOf(string path);

static i32 compileC(string project, utf8 **sources, bytes count, string name, utf8 **output) {
    if (count > SIZE_MAX / sizeof(utf8*) - 20) {
        THROW("source argument count overflow");
        return EXIT_FAILURE;
    }
    bytes includeCount = 0;
    bool includesOk = true;
    utf8 **includes = assemblyIncludes(project, &includeCount, &includesOk);
    if (!includesOk) {
        Util_freeSources(includes, includeCount);
        return EXIT_FAILURE;
    }
    utf8 *root = Territory_find(project);
    bytes depCount = 0;
    utf8 **deps = nullptr;
    bytes depCapacity = 0;
    bytes includeCapacity = includeCount == 0 ? 16 : includeCount;
    if (includes == nullptr)
        includes = Util_allocate(includeCapacity * sizeof(utf8*));
    if (includeCount == 0 && root != nullptr) {
        utf8 **stack = Util_allocate(4096 * sizeof(utf8*));
        bytes stackTop = 0;
        stack[stackTop++] = Util_combine(root, "");
        while (stackTop > 0) {
            utf8 *cur = stack[--stackTop];
            DIR *dir = opendir(cur);
            if (dir == nullptr) { free(cur); continue; }
            bool hasHeader = false;
            bool hasC = false;
            struct dirent *e;
            utf8 **subdirs = Util_allocate(256 * sizeof(utf8*));
            bytes subCount = 0;
            rewinddir(dir);
            while ((e = readdir(dir)) != nullptr) {
                string name = dirent_d_name(e);
                if (bStrEq(name, ".") || bStrEq(name, ".."))
                    continue;
                utf8 *full = joinPath(cur, name);
                struct stat st;
                if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
                    if (subCount < 256)
                        subdirs[subCount++] = full;
                    else
                        free(full);
                } else if (stat(full, &st) == 0 && S_ISREG(st.st_mode)) {
                    utf8 *ext = strrchr(name, '.');
                    if (ext && bStrEq(ext, ".h"))
                        hasHeader = true;
                    if (ext && bStrEq(ext, ".c"))
                        hasC = true;
                    free(full);
                } else {
                    free(full);
                }
            }
            closedir(dir);

            if (hasHeader && !bStrEq(cur, root)) {
                bytes rootLen = strlen(root);
                string rel = cur;
                if (bStrPrefixN(cur, root, rootLen)) {
                    rel = cur + rootLen;
                    while (*rel == '/'
                       ) ++rel;
                }
                if (*rel != '\0') {
                    utf8 *relDir = Util_combine(rel, "");
                    bool found = false;
                    for (bytes i = 0; i < includeCount; ++i)
                        if (bStrEq(includes[i], relDir)) { found = true; break; }
                    if (!found) {
                        if (includeCount == includeCapacity) {
                            includeCapacity *= 2;
                            utf8 **grown = Util_allocate(includeCapacity * sizeof(utf8*));
                            for (bytes j = 0; j < includeCount; ++j)
                               grown[j] = includes[j];
                            free(includes);
                            includes = grown;
                        }
                        includes[includeCount++] = relDir;
                    } else {
                        free(relDir);
                    }
                    if (hasC) {
                        bool depFound = false;
                        for (bytes i = 0; i < depCount; ++i)
                            if (bStrEq(deps[i], rel)) { depFound = true; break; }
                        if (!depFound) {
                            if (depCount == depCapacity) {
                                depCapacity = depCapacity == 0 ? 8 : depCapacity * 2;
                                utf8 **grown = Util_allocate(depCapacity * sizeof(utf8*));
                                for (bytes j = 0; j < depCount; ++j)
                                   grown[j] = deps[j];
                                free(deps);
                                deps = grown;
                            }
                            deps[depCount++] = Util_combine(rel, "");
                        }
                    }
                }
            }

            for (bytes i = 0; i < subCount; ++i) {
                if (stackTop < 4095)
                    stack[stackTop++] = subdirs[i];
                else
                    free(subdirs[i]);
            }
            free(subdirs);
            free(cur);
        }
        free(stack);
    }

    bytes depSrcTotal = 0;
    utf8 **depSources = nullptr;
    bytes depSrcCapacity = 0;
    if (depCount > 0 && root != nullptr) {
        for (bytes di = 0; di < depCount; ++di) {
            utf8 *depDir = joinPath(root, deps[di]);
            bytes depSrcCount = 0;
            bool depOk = false;
            utf8 **depSrcs = Util_collectSources(depDir, "/*.c", &depSrcCount, &depOk);
            if (depOk) {
                for (bytes si = 0; si < depSrcCount; ++si) {
                    if (depSrcTotal == depSrcCapacity) {
                        depSrcCapacity = depSrcCapacity == 0 ? 8 : depSrcCapacity * 2;
                        utf8 **grown = Util_allocate(depSrcCapacity * sizeof(utf8*));
                        for (bytes j = 0; j < depSrcTotal; ++j)
                           grown[j] = depSources[j];
                        free(depSources);
                        depSources = grown;
                    }
                    utf8 *bn = baseNameOf(depSrcs[si]);
                    bool dup = false;
                    for (bytes k = 0; k < depSrcTotal; ++k) {
                        utf8 *obn = baseNameOf(depSources[k]);
                        if (bStrEq(obn, bn)) { dup = true; free(obn); break; }
                        free(obn);
                    }
                    free(bn);
                    if (!dup)
                        depSources[depSrcTotal++] = Util_combine(depSrcs[si], "");
                }
            }
            Util_freeSources(depSrcs, depSrcCount);
            free(depDir);
        }
    }

    utf8 *directory = Util_outputDirectory(project);
    if (directory == nullptr) {
        free(root);
        Util_freeSources(includes, includeCount);
        return EXIT_FAILURE;
    }
    *output = joinPath(directory, name);
    utf8 **paths = Util_allocate((includeCount != 0 ? includeCount : 1) * sizeof(utf8*));
    utf8 **arguments = Util_allocate((count + 20 + 2 * includeCount) * sizeof(utf8*));
    bytes n = 0;
    string compiler = getenv("CC");
    arguments[n++] = (utf8*) (compiler != nullptr && *compiler ? compiler : "cc");
    arguments[n++] = "-std=gnu2x";
    arguments[n++] = "-Wall";
    arguments[n++] = "-Wextra";
    arguments[n++] = "-Werror";
    arguments[n++] = "-Wno-unused-but-set-variable";
    arguments[n++] = "-ffunction-sections";
    arguments[n++] = "-fdata-sections";
#ifdef __APPLE__
#if defined(__arm64__) || defined(__aarch64__)
    arguments[n++] = "-arch";
    arguments[n++] = "arm64";
    arguments[n++] = "-mcpu=apple-m1";
    arguments[n++] = "-mmacosx-version-min=14.0";
#elif defined(__x86_64__)
    arguments[n++] = "-arch";
    arguments[n++] = "x86_64";
    arguments[n++] = "-mmacosx-version-min=13.0";
#endif
#endif
    arguments[n++] = "-I";
    arguments[n++] = (utf8*) project;
    for (bytes i = 0; i < includeCount; ++i) {
        utf8 *prefix = Util_combine(root, "/");
        paths[i] = Util_combine(prefix, includes[i]);
        free(prefix);
        arguments[n++] = "-I";
        arguments[n++] = paths[i];
    }

#ifdef __APPLE__
    arguments[n++] = "-Wl,-dead_strip";
#elif defined(_WIN32)
    arguments[n++] = "/OPT:REF";
    arguments[n++] = "/OPT:ICF";
#else
    arguments[n++] = "-Wl,--gc-sections";
#endif
    arguments[n++] = "-o";
    arguments[n++] = *output;
    for (bytes i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    for (bytes i = 0; i < depSrcTotal; ++i)
        arguments[n++] = depSources[i];
    i32 status = Util_execute(arguments);
    for (bytes i = 0; i < includeCount; ++i)
        free(paths[i]);
    free(paths);
    free(arguments);
    free(directory);
    free(root);
    Util_freeSources(includes, includeCount);
    return status;
}

/**
 * Discover top-level C sources and compile them as one project executable.
 * Return the last path component of a path; caller-owned.
 */
static utf8 *baseNameOf(string path) {
    string name = path;
    for (string cursor = path; *cursor != '\0'; ++cursor)
        if (*cursor == '/' || *cursor == '\\')
            name = cursor + 1;
    return Util_combine(name, "");
}

/** Return the file's name without directory or extension; caller-owned. */
static utf8 *stemOf(string path) {
    utf8 *name = baseNameOf(path);
    utf8 *dot = strrchr(name, '.');
    if (dot != nullptr && dot != name)
        *dot = '\0';
    return name;
}

/**
 * A source declares a program entry when it defines main.
 * The ecosystem spells the return type `i32`; a vendored or generated file may
 * still say `int`, so both spellings count.
 */
static bool hasEntry(string source) {
    return Util_fileContains(source, "int main(") || Util_fileContains(source, "i32 main(");
}

/** Find the unit's program entries. Returns the first one, owned, and counts all. */
static utf8 *programEntry(string project, bytes *count) {
    bytes total = 0;
    bool ok = false;
    utf8 **sources = Util_collectSources(project, "/*.c", &total, &ok);
    utf8 *first = nullptr;
    bytes programs = 0;
    for (bytes i = 0; ok && i < total; ++i) {
        if (!hasEntry(sources[i]))
            continue;
        ++programs;
        if (first == nullptr)
            first = Util_combine(sources[i], "");
    }
    Util_freeSources(sources, total);
    *count = programs;
    return first;
}

/**
 * Compile one program: the named entry plus every sibling that is not itself an
 * entry. A unit's shared helpers therefore link into each of its programs.
 */
static i32 compileEntry(string project, string entry, utf8 **output) {
    bytes count = 0;
    bool ok = false;
    utf8 **sources = Util_collectSources(project, "/*.c", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    utf8 *entryName = baseNameOf(entry);
    utf8 **chosen = Util_allocate((count != 0 ? count : 1) * sizeof(utf8*));
    bytes chosenCount = 0;
    for (bytes i = 0; i < count; ++i) {
        utf8 *name = baseNameOf(sources[i]);
        bool same = bStrEq(name, entryName);
        free(name);
        if (same || !hasEntry(sources[i]))
            chosen[chosenCount++] = sources[i];
    }
    utf8 *stem = stemOf(entry);
    i32 status = EXIT_FAILURE;
    if (chosenCount == 0)
        THROW("no C sources to compile in %s", project);
    else
        status = compileC(project, chosen, chosenCount, stem, output);
    free(stem);
    free(chosen);
    free(entryName);
    Util_freeSources(sources, count);
    return status;
}

static i32 buildC(string project, utf8 **output) {
    bytes count = 0;
    bool ok = false;
    utf8 **sources = Util_collectSources(project, "/*.c", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    bytes programs = 0;
    for (bytes i = 0; i < count; ++i)
        if (hasEntry(sources[i]))
            ++programs;
    i32 status = EXIT_SUCCESS;
    if (programs == 0) {
        THROW("unit %s has no program entry; it is a library", project);
        status = EXIT_FAILURE;
    }
    utf8 *artifact = nullptr;
    bytes built = 0;
    for (bytes i = 0; i < count; ++i) {
        if (!hasEntry(sources[i]))
            continue;
        utf8 *one = nullptr;
        i32 result = compileEntry(project, sources[i], &one);
        if (result != 0) {
            status = result;
            free(one);
            continue;
        }
        ++built;
        if (built == 1)
            artifact = one;
        else {
            free(one);
            free(artifact);
            artifact = nullptr;
        }
    }
    if (status == EXIT_SUCCESS) {
        if (artifact != nullptr)
            *output = artifact;
        else
            *output = Util_outputDirectory(project);
    } else {
        free(artifact);
    }
    Util_freeSources(sources, count);
    return status;
}

/** Compile a source entry for exec mode and launch it with the caller's arguments. */
static i32 runC(string file, i32 argc, utf8 **argv, bool buildArtifact) {
    if (!buildArtifact) {
        THROW("C has no source runtime; use b.sh run exec to compile then launch");
        return EXIT_FAILURE;
    }
    utf8 *project = Util_projectDirectory(file);
    utf8 *entry = nullptr;
    if (hasEntry(file)) {
        entry = Util_combine(file, "");
    } else {
        bytes programs = 0;
        utf8 *first = programEntry(project, &programs);
        if (programs == 1)
            entry = first;
        else {
            free(first);
            if (programs == 0)
                THROW("%s holds no program entry", project);
            else
                THROW("%s holds %zu programs; name the one to run", project, programs);
        }
    }
    utf8 *output = nullptr;
    i32 status = EXIT_FAILURE;
    if (entry != nullptr)
        status = compileEntry(project, entry, &output);
    if (status == 0) {
        utf8 **arguments = Util_allocate(((bytes) argc + 2) * sizeof(utf8*));
        arguments[0] = output;
        for (i32 i = 0; i < argc; ++i)
            arguments[i + 1] = argv[i];
        status = Util_execute(arguments);
        free(arguments);
    }
    free(output);
    free(entry);
    free(project);
    return status;
}

/** Return an owned path to the top-level C source that defines main(), or nullptr. */
static utf8 *entryC(string project) {
    bytes count = 0;
    bool ok = false;
    utf8 **sources = Util_collectSources(project, "/*.c", &count, &ok);
    utf8 *found = nullptr;
    for (bytes i = 0; ok && i < count && found == nullptr; ++i)
        if (hasEntry(sources[i]))
            found = Util_combine(sources[i], "");
    Util_freeSources(sources, count);
    return found;
}

const Adapter C_ADAPTER = { "c", ".c", buildC, runC,
    "CC=cc", "native build/exec; source instance rejects", entryC, nullptr };
