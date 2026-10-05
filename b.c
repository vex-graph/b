// MODULE: b suite — argv validation and language dispatch. No owned class.
// DEFINITION: b accepts one command and hands the work to a language adapter in
// languages/. The suite owns argv, the output-path contract, and the fallback
// for an already-built executable; it never contains per-language logic. export
// stays rejected until a manifest contract exists. `b <language> <file>` is the
// instance-run shorthand (b java Hello.java, b python app.py).
// OVERVIEW: usage; regularFile; runFile (adapter, else existing executable);
// main (build | run | language shorthand | export-reject).
#include "b.h"
#include "languages/language.h"
#include "languages/arduino.h"

;;DEFINITION
/* The suite validates command grammar and passes work to registered adapters.
 * It owns no toolchain logic or persistent class state. Build result paths are
 * freed after printing; program arguments are borrowed and exit status is
 * preserved. Arduino upload is an explicit hardware operation; export remains
 * rejected until its manifest/packaging contract exists.
 */
;;OVERVIEW
/* MODULE: command suite.
 * PUBLIC ENTRY: main — help, build, run, upload, shorthand, export rejection.
 * PRIVATE STATIC: usage — print grammar and limits;
 * regularFile — validate a regular source/executable input;
 * runFile — source adapter dispatch or native executable fallback.
 * CAPABILITIES: compile/check directories; execute files; compile-before-upload;
 * forward literal arguments; propagate tool/program failures without a shell.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void usage(void) {
    puts("b run <exec|instance> <filename> [-- arguments...]\n"
         "b build <language> [directory]\n"
         "b upload arduino <sketch> --port <port> [--fqbn <matching-board>]\n"
         "b export <manifestmainfile> <destination> <exe|app|msi|iso|zip>\n"
         "b <language> <filename> [-- arguments...]   (e.g. b java Hello.java)\n"
         "instance: run as-is; exec: build a source artifact then launch\n"
         "export: planned, not implemented");
}

static bool regularFile(const char *path) {
    struct stat info;
    return path != nullptr && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static int runFile(const char *input, bool buildArtifact, int argc, char **argv) {
    if (!regularFile(input)) {
        THROW("run file does not exist or is not regular: %s", input);
        return EXIT_FAILURE;
    }
    const Language *language = Language_forFile(input);
    if (language != nullptr)
        return (*language).run(input, argc, argv, buildArtifact);
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

int main(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0))) {
        usage();
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[1], "build") == 0 && (argc == 3 || argc == 4)) {
        const Language *language = Language_forName(argv[2]);
        if (language == nullptr) {
            THROW("unsupported language: %s", argv[2]);
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
        int status = (*language).build(project, &output);
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
    if (strcmp(argv[1], "run") == 0 && argc >= 4) {
        bool buildArtifact = strcmp(argv[2], "exec") == 0;
        if (!buildArtifact && strcmp(argv[2], "instance") != 0) {
            THROW("unsupported run mode: %s", argv[2]);
            return EXIT_FAILURE;
        }
        int start = argc > 4 && strcmp(argv[4], "--") == 0 ? 5 : 4;
        return runFile(argv[3], buildArtifact, argc - start, argv + start);
    }
    const Language *shorthand = Language_forName(argv[1]);
    if (shorthand != nullptr && argc >= 3) {
        int start = argc > 3 && strcmp(argv[3], "--") == 0 ? 4 : 3;
        return runFile(argv[2], false, argc - start, argv + start);
    }
    if (strcmp(argv[1], "export") == 0 && argc == 5) {
        THROW("export is not implemented; no destination was created");
        return EXIT_FAILURE;
    }
    THROW("invalid command; run b --help");
    return EXIT_FAILURE;
}
