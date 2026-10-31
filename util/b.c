// MODULE: b suite — argv validation and adapter dispatch. No owned class.
// DEFINITION: b claims a territory with generate, then hands file and directory
// work to an adapter in adapters/. The suite owns argv and the output-path
// contract; it never contains per-language logic. Run and breeze are one verb
// spelled twice: build the unit, box it, execute it.
// OVERVIEW: usage; regularFile; runFile (adapter, else existing executable);
// generateCommand; runCommand; main (generate | run | breeze | build | box).
#include "b.h"
#include "adapters/adapter.h"
#include "adapters/arduino.h"
#include "adapters/graph.h"
#include "adapters/territory.h"
#include "adapters/workspace.h"
#include "box.h"
#include "inspect.h"

;;DEFINITION
/* The suite validates command grammar and passes work to registered adapters.
 * It owns no toolchain logic or persistent class state. Build result paths are
 * freed after printing; program arguments are borrowed and exit status is
 * preserved. Arduino upload is an explicit hardware operation. Manifest
 * creation and portable export belong to the BoxManifest class in box.c; this
 * file only validates the command shape and reports the result path. Territory
 * resolution belongs to the Territory class in territory.c.
 */
;;OVERVIEW
/* MODULE: command suite.
 * PUBLIC ENTRY: main — generate (claim a territory), run and breeze (one
 * behaviour), build, box; transitional adapter, graph, diagnostic, upload and
 * manifest commands remain available until their replacements land.
 * PRIVATE STATIC: usage — print grammar and limits; regularFile — validate a
 * regular source/executable input; runFile — adapter/native dispatch;
 * generateCommand — claim and inventory; runCommand — strip the optional
 * separator and dispatch.
 * BOX SEAM: BoxManifest_writeTemplate (create manifest) and
 * BoxManifest_load/export/free (box), both documented in box.h.
 * TERRITORY SEAM: Territory_claim, documented in territory.h.
 * CAPABILITIES: compile/check directories; execute files; compile-before-upload;
 * forward literal arguments; propagate tool/program failures without a shell.
 * Workspace assessment/build delegates to adapters/workspace.c, never runs sources.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Print the command grammar and the distinction between claiming and building.
static void usage(void) {
    puts("Territory\n"
         "  b generate [<directory>]            claim a territory; write .b/workspace.json\n"
         "\nRun\n"
         "  b run <file> [arguments...]         build the unit, box it, execute it\n"
         "  b breeze <file> [arguments...]      identical to run\n"
         "\nBuild\n"
         "  b build <adapter> [directory]       transitional: one adapter, one directory\n"
         "  b build workspace <directory> [--plan|--confirm]\n"
         "  b graph list|build|test|run ...     transitional: target graph\n"
         "\nBox\n"
         "  b create manifest <path>            write a starter manifest\n"
         "  b box <manifest.json> <export-path> export declared portable files\n"
         "\nAdapter\n"
         "  b adapter list                      names, selectors, requirements\n"
         "\nFor adapter requirements and optional diagnostics, see docs/ADAPTERS.md.");
}

// Return whether path resolves to a regular file.
static bool regularFile(const char *path) {
    struct stat info;
    return path != nullptr && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

// Dispatch a source file to its adapter or execute an existing native binary.
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
    if (adapter != nullptr)
        return (*adapter).run(input, argc, argv, buildArtifact);
    char *file = realpath(input, nullptr);
    if (file == nullptr || access(file, X_OK) != 0) {
        THROW("file is not an executable or a supported source: %s", input);
        free(file);
        return EXIT_FAILURE;
    }
    char **arguments = Util_allocate(((size_t) argc + 2) * sizeof(char*));
    arguments[0] = file;
    for (int i = 0; i < argc; ++i)
        arguments[i + 1] = argv[i];
    int status = Util_execute(arguments);
    free(arguments);
    free(file);
    return status;
}

// Claim a territory: create the marker, inventory the tree, write the fact file.
static int generateCommand(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        THROW("usage: b generate [<directory>]");
        return EXIT_FAILURE;
    }
    return Territory_claim(argc == 3 ? argv[2] : ".") ? EXIT_SUCCESS : EXIT_FAILURE;
}

// Run one file through its adapter. An optional -- separates b's arguments
// from the program's; it is the only separator, since there is no mode word.
static int runCommand(int argc, char **argv) {
    const char *file = argv[2];
    int first = 3;
    if (argc > 3 && strcmp(argv[3], "--") == 0)
        first = 4;
    return runFile(file, true, argc - first, argv + first);
}

// Parse b commands and forward build, run, inspection, and upload work.
int main(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0))) {
        usage();
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[1], "generate") == 0)
        return generateCommand(argc, argv);
    if ((strcmp(argv[1], "adapters") == 0 || strcmp(argv[1], "languages") == 0) && argc == 2)
        return Inspect_adapters();
    if (strcmp(argv[1], "adapter") == 0 && argc == 3 && strcmp(argv[2], "list") == 0)
        return Inspect_adapters();
    if (strcmp(argv[1], "create") == 0 && argc == 4 && strcmp(argv[2], "manifest") == 0) {
        if (BoxManifest_writeTemplate(argv[3]) != 0)
            return EXIT_FAILURE;
        puts(argv[3]);
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[1], "box") == 0 && argc == 4) {
        BoxManifest *manifest = BoxManifest_load(argv[2]);
        if (manifest == nullptr)
            return EXIT_FAILURE;
        int status = BoxManifest_export(manifest, argv[3]);
        BoxManifest_free(manifest);
        if (status == 0)
            puts(argv[3]);
        return status;
    }
    if (strcmp(argv[1], "doctor") == 0 && (argc == 2 || argc == 3))
        return Inspect_doctor(argc == 3 ? argv[2] : nullptr);
    if (strcmp(argv[1], "graph") == 0)
        return Graph_command(argc - 2, argv + 2);
    if (argc >= 3 && strcmp(argv[1], "build") == 0 && strcmp(argv[2], "workspace") == 0)
        return Workspace_command(argc - 3, argv + 3);
    if (strcmp(argv[1], "build") == 0 && (argc == 3 || argc == 4)) {
        const Adapter *adapter = Adapter_forName(argv[2]);
        if (adapter == nullptr) {
            THROW("unsupported adapter: %s", argv[2]);
            return EXIT_FAILURE;
        }
        const char *path = argc == 4 ? argv[3] : ".";
        char *project = realpath(path, nullptr);
        struct stat info;
        if (project == nullptr || stat(project, &info) != 0 || !S_ISDIR(info.st_mode)) {
            THROW("build directory does not exist: %s", path);
            free(project);
            return EXIT_FAILURE;
        }
        char *output = nullptr;
        int status = (*adapter).build(project, &output);
        if (status == 0)
            puts(output);
        free(output);
        free(project);
        return status;
    }
    if (strcmp(argv[1], "upload") == 0 && argc >= 3) {
        if (strcmp(argv[2], "arduino") == 0)
            return Arduino_upload(argc - 3, argv + 3);
        THROW("unsupported upload adapter: %s", argv[2]);
        return EXIT_FAILURE;
    }
    if ((strcmp(argv[1], "breeze") == 0 || strcmp(argv[1], "run") == 0) && argc >= 3)
        return runCommand(argc, argv);
    if (strcmp(argv[1], "export") == 0 && argc == 5) {
        THROW("use 'b box <manifest.json> <path-to-export>'; no destination was created");
        return EXIT_FAILURE;
    }
    THROW("invalid command; run b --help");
    return EXIT_FAILURE;
}
