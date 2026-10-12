// MODULE: b suite — argv validation and adapter dispatch. No owned class.
// DEFINITION: b accepts one command and hands the work to an adapter in
// adapters/. The suite owns argv, the output-path contract, and the fallback
// for an already-built executable; it never contains per-language logic. `box`
// consumes a user-authored packaging manifest; format backends are separate work.
// `b <language> <file>` is the instance-run shorthand (b java Hello.java,
// b python app.py).
// OVERVIEW: usage; regularFile; runFile (adapter, else existing executable);
// main (build | run | language shorthand | manifest-create | box-reject).
#include "b.h"
#include "adapters/adapter.h"
#include "adapters/arduino.h"
#include "adapters/workspace.h"
#include "inspect.h"

;;DEFINITION
/* The suite validates command grammar and passes work to registered adapters.
 * It owns no toolchain logic or persistent class state. Build result paths are
 * freed after printing; program arguments are borrowed and exit status is
 * preserved. Arduino upload is an explicit hardware operation; box accepts an
 * explicit manifest/destination pair but rejects until a packaging backend exists.
 */
;;OVERVIEW
/* MODULE: command suite.
 * PUBLIC ENTRY: main — help, adapters (languages alias), doctor, build, run, upload, shorthand,
 * create manifest template; box packaging rejection. Discovery delegates to
 * inspect.c, not the workspace engine.
 * PRIVATE STATIC: usage — print grammar and limits;
 * createManifest — exclusively create a starter JSON form, preserving existing paths;
 * regularFile — validate a regular source/executable input;
 * runFile — source adapter dispatch or native executable fallback.
 * CAPABILITIES: compile/check directories; execute files; compile-before-upload;
 * forward literal arguments; propagate tool/program failures without a shell.
 * Workspace assessment/build delegates to adapters/workspace.c, never runs sources.
 */

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Print the command grammar and explain the distinction between run modes.
static void usage(void) {
    puts("b run <exec|instance> <filename> [-- arguments...]\n"
         "b adapters   (alias: b languages)\n"
         "b doctor [adapter]\n"
         "b build <adapter> [directory]\n"
         "b build workspace <directory> [--plan|--confirm]\n"
         "b upload arduino <sketch> --port <port> [--fqbn <matching-board>]\n"
          "b create manifest <path>\n"
          "b box <manifest.json> <path-to-export>\n"
         "b <adapter> <filename> [-- arguments...]   (e.g. b java Hello.java)\n"
         "instance: run as-is; exec: build a source artifact then launch\n"
          "box: manifest creation is available; packaging backends are not implemented");
}

// Create a starter manifest without overwriting a file the user already owns.
static int createManifest(const char *path) {
    static const char template[] =
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"application\": {\n"
        "    \"id\": \"com.example.myapp\",\n"
        "    \"name\": \"My Application\",\n"
        "    \"version\": \"1.0.0\",\n"
        "    \"authors\": [\"Your Name\"],\n"
        "    \"publisher\": \"Your Organization\",\n"
        "    \"description\": \"Describe your application.\",\n"
        "    \"license\": \"UNLICENSED\"\n"
        "  },\n"
        "  \"payload\": {\n"
        "    \"entry\": \"dist/myapp\",\n"
        "    \"files\": []\n"
        "  },\n"
        "  \"targets\": [],\n"
        "  \"formats\": [\"zip\"],\n"
        "  \"statePolicy\": \"clean\"\n"
        "}\n";
    if (path == nullptr || *path == '\0') {
        THROW("manifest path must not be empty");
        return EXIT_FAILURE;
    }
    int file = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) {
        THROW("cannot create manifest without replacing an existing path: %s", path);
        return EXIT_FAILURE;
    }
    size_t length = sizeof(template) - 1;
    size_t offset = 0;
    while (offset < length) {
        ssize_t written = write(file, template + offset, length - offset);
        if (written <= 0)
            break;
        offset += (size_t) written;
    }
    int closeStatus = close(file);
    if (offset != length || closeStatus != 0) {
        unlink(path);
        THROW("could not completely write manifest: %s", path);
        return EXIT_FAILURE;
    }
    puts(path);
    return EXIT_SUCCESS;
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

// Parse b commands and forward build, run, inspection, and upload work.
int main(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0))) {
        usage();
        return EXIT_SUCCESS;
    }
    if ((strcmp(argv[1], "adapters") == 0 || strcmp(argv[1], "languages") == 0) && argc == 2)
        return Inspect_adapters();
    if (strcmp(argv[1], "create") == 0 && argc == 4 && strcmp(argv[2], "manifest") == 0)
        return createManifest(argv[3]);
    if (strcmp(argv[1], "box") == 0 && argc == 4) {
        if (!regularFile(argv[2])) {
            THROW("box manifest does not exist or is not a regular file: %s", argv[2]);
            return EXIT_FAILURE;
        }
        THROW("box packaging backends are not implemented; no export was created (manifest: %s, destination: %s)", argv[2], argv[3]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "doctor") == 0 && (argc == 2 || argc == 3))
        return Inspect_doctor(argc == 3 ? argv[2] : nullptr);
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
    if (strcmp(argv[1], "run") == 0 && argc >= 4) {
        bool buildArtifact = strcmp(argv[2], "exec") == 0;
        if (!buildArtifact && strcmp(argv[2], "instance") != 0) {
            THROW("unsupported run mode: %s", argv[2]);
            return EXIT_FAILURE;
        }
        int start = argc > 4 && strcmp(argv[4], "--") == 0 ? 5 : 4;
        return runFile(argv[3], buildArtifact, argc - start, argv + start);
    }
    const Adapter *shorthand = Adapter_forName(argv[1]);
    if (shorthand != nullptr && argc >= 3) {
        int start = argc > 3 && strcmp(argv[3], "--") == 0 ? 4 : 3;
        return runFile(argv[2], false, argc - start, argv + start);
    }
    if (strcmp(argv[1], "export") == 0 && argc == 5) {
        THROW("use 'b box <manifest.json> <path-to-export>'; no destination was created");
        return EXIT_FAILURE;
    }
    THROW("invalid command; run b --help");
    return EXIT_FAILURE;
}
