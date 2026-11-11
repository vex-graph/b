// MODULE: breeze — the run/breeze verb. No owned state.
// DEFINITION: run and breeze are one behaviour: resolve the file, build or
// interpret it through its adapter, forward arguments, return the child status.
// OVERVIEW: Breeze_command; PRIVATE: regularFile; runFile.
#include "breeze.h"
#include "b.h"
#include "territory.h"
#include "adapters/lang/adapter.h"
#include "build.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


#include "vex.h"

;;DEFINITION
/* The mode word is the verb's whole decision. exec builds the artifact first;
 * instance hands the source to its runtime. The adapter rejects a mode it cannot
 * honour, so this file never guesses per language. A build failure returns
 * before any execution, and the program's status is preserved unchanged.
 */
;;OVERVIEW
/* PRIVATE: regularFile — a path that must resolve to a regular file; runFile —
 * adapter dispatch, else execute an existing native binary.
 * PUBLIC: Breeze_command — optional exec|instance, then one file.
 */

// Return whether path resolves to a regular file.
static bool regularFile(const char *path) {
    struct stat info;
    return path != nullptr && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

// Dispatch a source file to its adapter or execute an existing native binary.
// Refuses if the file lives outside a claimed territory.
static int runFile(const char *input, bool buildArtifact, int argc, char **argv) {
    if (!regularFile(input)) {
        THROW("run file does not exist or is not regular: %s", input);
        return EXIT_FAILURE;
    }
    const Adapter *adapter = Adapter_forFile(input);
    if (adapter != nullptr && (*adapter).run == nullptr) {
        THROW("adapter %s does not support running files", (*adapter).name);
        return EXIT_FAILURE;
    }
    char *file = realpath(input, nullptr);
    if (file == nullptr) {
        THROW("run file cannot be resolved: %s", input);
        return EXIT_FAILURE;
    }
    // Refuse if the file lives outside a claimed territory.
    char *directory = Util_parentDirectory(file);
    char *territoryRoot = Territory_find(directory);
    free(directory);
    if (territoryRoot == nullptr) {
        THROW("run outside a claimed territory; run b.sh generate <directory> first");
        free(file);
        return EXIT_FAILURE;
    }
    free(territoryRoot);
    if (adapter != nullptr)
        return (*adapter).run(file, argc, argv, buildArtifact);
    if (access(file, X_OK) != 0) {
        THROW("file is not an executable or a supported source: %s", input);
        free(file);
        return EXIT_FAILURE;
    }
    char **arguments = Util_allocate(((bytes) argc + 2) * sizeof(char*));
    arguments[0] = file;
    for (int i = 0; i < argc; ++i)
        arguments[i + 1] = argv[i];
    fflush(stdout);
    fflush(stderr);
    int status = Util_execute(arguments);
    free(arguments);
    free(file);
    return status;
}


// Build and execute the unit declared by the manifest in this directory.
//
// `b run` inside a test directory is the whole contract: the manifest says what
// the unit is, the build produces .b/out/<name>, and the unit's own exit status
// is returned unchanged so a suite runner can read pass, fail and skip alike.
static int runUnit(int argc, char **argv) {
    if (access("b.assembly.json", R_OK) != 0) {
        THROW("no b.assembly.json in this directory; name a file instead");
        return EXIT_FAILURE;
    }
    FILE *manifest = fopen("b.assembly.json", "rb");
    if (manifest == nullptr) {
        THROW("cannot read b.assembly.json");
        return EXIT_FAILURE;
    }
    static char text[1 << 16];
    bytes length = fread(text, 1, sizeof(text) - 1, manifest);
    text[length] = '\0';
    fclose(manifest);

    // application.name is the unit name, and the name of the built artifact.
    char unit[256] = "";
    const char *application = strstr(text, "\"application\"");
    const char *name = application != nullptr ? strstr(application, "\"name\"") : nullptr;
    if (name != nullptr) {
        const char *quote = strchr(name, ':');
        if (quote != nullptr) {
            quote = strchr(quote, '"');
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

    // Output is territory-grounded: <territory>/.b/out/<name>.
    char *unitTerritory = Territory_find(".");
    char binary[4096];
    snprintf(binary, sizeof(binary), "%s/.b/out/%s",
             unitTerritory != nullptr ? unitTerritory : ".", unit);
    free(unitTerritory);
    if (access(binary, X_OK) != 0) {
        THROW("build reported success but %s is not executable", binary);
        return EXIT_FAILURE;
    }
    char **arguments = Util_allocate(((bytes) argc + 2) * sizeof(char*));
    arguments[0] = binary;
    for (int index = 0; index < argc; ++index)
        arguments[index + 1] = argv[index];
    fflush(stdout);
    fflush(stderr);
    int status = Util_execute(arguments);
    free(arguments);
    return status;
}

// Parse `breeze|run [exec|instance] <file> [--] [arguments...]` and dispatch it.
// The mode word is optional: exec is the default, because running the artifact
// is the behaviour that always has an answer.
int Breeze_command(int argc, char **argv) {
    // `b run` with nothing after it runs the unit this directory declares.
    if (argc < 2)
        return runUnit(0, nullptr);
    if (strcmp(argv[1], "--") == 0)
        return runUnit(argc - 2, argv + 2);
    int first = 1;
    bool buildArtifact = true;
    if (strcmp(argv[first], "exec") == 0)
        ++first;
    else if (strcmp(argv[first], "instance") == 0) {
        buildArtifact = false;
        ++first;
    }
    if (first >= argc) {
        THROW("usage: b.sh run|breeze [exec|instance] <file> [arguments...]");
        return EXIT_FAILURE;
    }
    const char *file = argv[first++];
    if (first < argc && strcmp(argv[first], "--") == 0)
        ++first;
    return runFile(file, buildArtifact, argc - first, argv + first);
}
