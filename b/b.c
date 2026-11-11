// MODULE: b.sh suite — argv grammar and dispatch. No owned class.
// DEFINITION: b.sh claims a territory with generate, then hands file work to the
// breeze verb, directory work to the build verb, and packaging to box. This
// file owns only the command grammar; it contains no language logic and no
// build policy.
// OVERVIEW: usage; generateCommand; main (generate | run | breeze | build |
// assembly | box | adapter).
#include "b.h"
#include "breeze.h"
#include "assembly.h"
#include "build.h"
#include "box.h"
#include "inspect.h"
#include "territory.h"
#include "adapters/arduino.h"
#include "adapters/lang/graph.h"
#include "vex.h"

;;DEFINITION
/* The suite validates command grammar and passes work to the verb that owns it.
 * It owns no toolchain logic and no persistent state: result paths are printed
 * and released, program arguments are borrowed, and exit status is preserved.
 * Manifest creation and portable export belong to BoxManifest in box.c; the run
 * verb belongs to breeze.c; units and declarations belong to build.c; territory
 * resolution belongs to Territory in territory.c.
 */
;;OVERVIEW
/* MODULE: command suite.
 * PUBLIC ENTRY: main — generate (claim a territory), run and breeze (one
 * behaviour), build, assembly, box; the transitional adapter, graph, diagnostic,
 * upload and manifest commands remain until their replacements land.
 * PRIVATE STATIC: usage — print grammar and limits; generateCommand — claim and
 * inventory.
 * SEAMS: Breeze_command (breeze.h), Build_command and Build_assembly (build.h),
 * Territory_claim (territory.h), BoxManifest_* (box.h), Inspect_* (inspect.h).
 */

#include <stdlib.h>
#include <string.h>

// Print the command grammar and the distinction between claiming and building.
static void usage() {
    puts("Territory\n"
         "  b.sh generate [<directory>]            claim a territory; write .b/workspace.json\n"
         "\nRun\n"
         "  b.sh run [exec|instance] <file> [arguments...]   build, then execute\n"
         "  b.sh run                             build and run the unit this directory declares\n"
         "  b.sh run -- [arguments...]           the same, forwarding arguments\n"
         "  b.sh breeze [exec|instance] <file> [arguments...] the same verb, second spelling\n"
         "\nBuild\n"
         "  b.sh build [<unit>...] [--explain]     build units; no argument builds descendants\n"
         "  b.sh assembly [<unit>] [--version <v>] [--include <dir>]... [--depends <dir>]... [--force]\n"
         "  b.sh graph list|build|test|run ...     transitional: target graph\n"
         "\nBox\n"
         "  b.sh create manifest <path>            write a starter manifest\n"
         "  b.sh box <manifest.json> <export-path> export declared portable files\n"
         "\nAdapter\n"
         "  b.sh adapter list                      names, selectors, requirements\n"
         "\nFor adapter requirements and optional diagnostics, see docs/ADAPTERS.md.");
}

// Claim a territory: create the marker, inventory the tree, write the fact file.
static int generateCommand(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        THROW("usage: b.sh generate [<directory>]");
        return EXIT_FAILURE;
    }
    return Territory_claim(argc == 3 ? argv[2] : ".") ? EXIT_SUCCESS : EXIT_FAILURE;
}

// Parse b.sh commands and forward build, run, inspection, and upload work.
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
    if (strcmp(argv[1], "assembly") == 0)
        return Build_assembly(argc - 1, argv + 1);
    if (strcmp(argv[1], "graph") == 0)
        return Graph_command(argc - 2, argv + 2);
    if (strcmp(argv[1], "build") == 0)
        return Build_command(argc - 1, argv + 1);
    if (strcmp(argv[1], "upload") == 0 && argc >= 3) {
        if (strcmp(argv[2], "arduino") == 0)
            return Arduino_upload(argc - 3, argv + 3);
        THROW("unsupported upload adapter: %s", argv[2]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "breeze") == 0 || strcmp(argv[1], "run") == 0)
        return Breeze_command(argc - 1, argv + 1);
    if (strcmp(argv[1], "export") == 0 && argc == 5) {
        THROW("use 'b.sh box <manifest.json> <path-to-export>'; no destination was created");
        return EXIT_FAILURE;
    }
    THROW("invalid command; run b.sh --help");
    return EXIT_FAILURE;
}
