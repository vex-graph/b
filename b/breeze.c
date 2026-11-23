/**
 * MODULE: breeze, the run/breeze verb. No owned state.
 * DEFINITION: run and breeze are one behaviour: resolve the file, build or
 * interpret it through its adapter, forward arguments, return the child status.
 * OVERVIEW: Breeze_command; PRIVATE: regularFile; runFile.
 */

#include "vex.h"
#include "breeze.h"
#include "b.h"
#include "b/b_str.h"
#include "territory.h"
#include "adapters/lang/adapter.h"
#include "build.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>



;;DEFINITION
/* The mode word is the verb's whole decision. exec builds the artifact first;
 * instance hands the source to its runtime. The adapter rejects a mode it cannot
 * honour, so this file never guesses per language. A build failure returns
 * before any execution, and the program's status is preserved unchanged.
 */
;;OVERVIEW
/* PRIVATE: regularFile, a path that must resolve to a regular file; runFile,
 * adapter dispatch, else execute an existing native binary.
 * PUBLIC: Breeze_command, optional exec|instance, then one file.
 */

/** Return whether path resolves to a regular file. */
static bool regularFile(string path) {
    struct stat info;
    return path != nullptr && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

;;GETTER
static inline i32 (*Adapter_run(const Adapter *self))(string , i32, utf8 **, bool)
{
    return (*self).run;
}
;;GETTER
static inline string Adapter_name(const Adapter *self)
{
    return (*self).name;
}

/**
 * Dispatch a source file to its adapter or execute an existing native binary.
 * Refuses if the file lives outside a claimed territory.
 */
static i32 runFile(string input, bool buildArtifact, i32 argc, utf8 **argv) {
    if (!regularFile(input)) {
        THROW("run file does not exist or is not regular: %s", input);
        return EXIT_FAILURE;
    }
    const Adapter *adapter = Adapter_forFile(input);
    if (adapter != nullptr && Adapter_run(adapter) == nullptr) {
        THROW("adapter %s does not support running files", Adapter_name(adapter));
        return EXIT_FAILURE;
    }
    utf8 *file = realpath(input, nullptr);
    if (file == nullptr) {
        THROW("run file cannot be resolved: %s", input);
        return EXIT_FAILURE;
    }
    utf8 *directory = Util_parentDirectory(file);
    utf8 *territoryRoot = Territory_find(directory);
    free(directory);
    if (territoryRoot == nullptr) {
        THROW("run outside a claimed territory; run b.sh generate <directory> first");
        free(file);
        return EXIT_FAILURE;
    }
    free(territoryRoot);
    if (adapter != nullptr)
        return Adapter_run(adapter)(file, argc, argv, buildArtifact);
    if (access(file, X_OK) != 0) {
        THROW("file is not an executable or a supported source: %s", input);
        free(file);
        return EXIT_FAILURE;
    }
    utf8 **arguments = Util_allocate(((bytes) argc + 2) * sizeof(utf8*));
    arguments[0] = file;
    for (i32 i = 0; i < argc; ++i)
        arguments[i + 1] = argv[i];
    fflush(stdout);
    fflush(stderr);
    i32 status = Util_execute(arguments);
    free(arguments);
    free(file);
    return status;
}


/**
 * Build and execute the unit declared by the manifest in this directory.
 *
 * `b run` inside a test directory is the whole contract: the manifest says what
 * the unit is, the build produces .b/out/<name>, and the unit's own exit status
 * is returned unchanged so a suite runner can read pass, fail and skip alike.
 */
static i32 runUnit(i32 argc, utf8 **argv) {
    if (access("b.assembly.json", R_OK) != 0) {
        THROW("no b.assembly.json in this directory; name a file instead");
        return EXIT_FAILURE;
    }
    FILE *manifest = fopen("b.assembly.json", "rb");
    if (manifest == nullptr) {
        THROW("cannot read b.assembly.json");
        return EXIT_FAILURE;
    }
    static utf8 text[1 << 16];
    bytes length = fread(text, 1, sizeof(text) - 1, manifest);
    text[length] = '\0';
    fclose(manifest);

    utf8 unit[256] = "";
    string application = bStrFind(text, "\"application\"");
    string name = application != nullptr ? bStrFind(application, "\"name\"") : nullptr;
    if (name != nullptr) {
        string quote = bStrFindChar(name, ':');
        if (quote != nullptr) {
            quote = bStrFindChar(quote, '"');
            if (quote != nullptr) {
                ++quote;
                bytes used = 0;
                while (*quote != '"' && *quote != '\0' && used + 1 < sizeof(unit))
                    unit[used++] = *quote++;
                unit[used] = '\0';
            }
        }
    }
    if (unit[0] == '\0') {
        THROW("b.assembly.json declares no application.name");
        return EXIT_FAILURE;
    }

    if (!Build_file(unit, "b.assembly.json", ".b/workspace.json", ".b/out", stdout))
        return EXIT_FAILURE;

    utf8 *unitTerritory = Territory_find(".");
    utf8 binary[4096];
    snprintf(binary, sizeof(binary), "%s/.b/out/%s",
             unitTerritory != nullptr ? unitTerritory : ".", unit);
    free(unitTerritory);
    if (access(binary, X_OK) != 0) {
        THROW("build reported success but %s is not executable", binary);
        return EXIT_FAILURE;
    }
    utf8 **arguments = Util_allocate(((bytes) argc + 2) * sizeof(utf8*));
    arguments[0] = binary;
    for (i32 index = 0; index < argc; ++index)
        arguments[index + 1] = argv[index];
    fflush(stdout);
    fflush(stderr);
    i32 status = Util_execute(arguments);
    free(arguments);
    return status;
}

/**
 * Parse `breeze|run [exec|instance] <file> [--] [arguments...]` and dispatch it.
 * The mode word is optional: exec is the default, because running the artifact
 * is the behaviour that always has an answer.
 */
i32 Breeze_command(i32 argc, utf8 **argv) {
    if (argc < 2)
        return runUnit(0, nullptr);
    if (bStrEq(argv[1], "--"))
        return runUnit(argc - 2, argv + 2);
    i32 first = 1;
    bool buildArtifact = true;
    if (bStrEq(argv[first], "exec"))
        ++first;
    else if (bStrEq(argv[first], "instance")) {
        buildArtifact = false;
        ++first;
    }
    if (first >= argc) {
        THROW("usage: b.sh run|breeze [exec|instance] <file> [arguments...]");
        return EXIT_FAILURE;
    }
    string file = argv[first++];
    if (first < argc && bStrEq(argv[first], "--"))
        ++first;
    return runFile(file, buildArtifact, argc - first, argv + first);
}
