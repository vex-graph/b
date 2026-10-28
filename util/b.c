// MODULE: b suite — argv validation and adapter dispatch. No owned class.
// DEFINITION: b accepts a project/run/build/box command and hands work to an
// adapter in adapters/. The suite owns argv, the output-path contract, and the fallback
// for an already-built executable; it never contains per-language logic. `box`
// consumes a user-authored packaging manifest; format backends are separate work.
// OVERVIEW: usage; regularFile; runFile (adapter, else existing executable);
// main (project | refresh | run | build | box | optional utilities).
#include "b.h"
#include "../adapters/adapter.h"
#include "../adapters/arduino.h"
#include "../adapters/workspace.h"
#include "box.h"
#include "inspect.h"
#include "../project/project.h"

;;DEFINITION
/* The suite validates command grammar and passes work to registered adapters.
 * It owns no toolchain logic or persistent class state. Build result paths are
 * freed after printing; program arguments are borrowed and exit status is
 * preserved. Arduino upload is an explicit hardware operation. Manifest
 * creation and portable export belong to the BoxManifest class in box.c; this
 * file only validates the command shape and reports the result path.
 */
;;OVERVIEW
/* MODULE: command suite.
 * PUBLIC ENTRY: main — project lifecycle, breeze (run alias), build, box;
 * optional adapter, diagnostic, upload and manifest commands remain available.
 * Discovery delegates to inspect.c, not the workspace engine.
 * PRIVATE STATIC: usage — print grammar and limits; regularFile — validate a
 * regular source/executable input; runFile — adapter/native dispatch;
 * stateDirectory — read the launcher state root; projectCommand — register/query;
 * refreshCommand — rebuild the generic project inventory; enterProject — set run context;
 * runCommand — parse run options and dispatch.
 * BOX SEAM: BoxManifest_writeTemplate (create manifest) and
 * BoxManifest_load/export/free (box), both documented in box.h.
 * CAPABILITIES: compile/check directories; execute files; compile-before-upload;
 * forward literal arguments; propagate tool/program failures without a shell.
 * Workspace assessment/build delegates to adapters/workspace.c, never runs sources.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Print the command grammar and explain the distinction between run modes.
static void usage(void) {
    puts("Project\n"
         "  b project <name> <project-path>     cook/register a project\n"
         "  b project <name>                    show its root\n"
         "  b project remove <name>             remove its B registration/index\n"
         "  b refresh <name>                    refresh its adapter file index\n"
         "\nBreeze\n"
         "  b breeze exec <file> [--in <name>] [-- arguments...]\n"
         "  b breeze instance <file> [--in <name>] [-- arguments...]\n"
         "  b run <exec|instance> <file> [--in <name>] [-- arguments...] (alias)\n"
         "\nBuild\n"
         "  b build <adapter> [directory]\n"
         "  b build workspace <directory> [--plan|--confirm]\n"
         "\nBox\n"
         "  b create manifest <path>             write a starter manifest\n"
         "  b box <manifest.json> <export-path>  export declared portable files\n"
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

// Resolve one named project from the same external state root used by the launcher.
static const char *stateDirectory(void) {
    const char *state = getenv("B_HOME");
    if (state == nullptr || *state == '\0') {
        THROW("B_HOME is unset; invoke b through its bootstrap launcher");
        return nullptr;
    }
    return state;
}

// Save a project name/root mapping or print the root already registered under it.
static int projectCommand(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[2], "remove") == 0) {
        const char *state = stateDirectory();
        if (state == nullptr || !Project_remove(state, argv[3]))
            return EXIT_FAILURE;
        printf("removed project %s\n", argv[3]);
        return EXIT_SUCCESS;
    }
    if (argc != 3 && argc != 4) {
        THROW("usage: b project <name> [project-path] | b project remove <name>");
        return EXIT_FAILURE;
    }
    const char *state = stateDirectory();
    if (state == nullptr)
        return EXIT_FAILURE;
    if (argc == 4) {
        if (!Project_register(state, argv[2], argv[3]))
            return EXIT_FAILURE;
    }
    char *root = Project_root(state, argv[2]);
    if (root == nullptr)
        return EXIT_FAILURE;
    puts(root);
    free(root);
    return EXIT_SUCCESS;
}

// Refresh one registered project's adapter-recognized file inventory.
static int refreshCommand(int argc, char **argv) {
    if (argc != 3) {
        THROW("usage: b refresh <project-name>");
        return EXIT_FAILURE;
    }
    const char *state = stateDirectory();
    if (state == nullptr)
        return EXIT_FAILURE;
    size_t fileCount = 0;
    char *index = Project_refresh(state, argv[2], &fileCount);
    if (index == nullptr)
        return EXIT_FAILURE;
    printf("refreshed %s: %zu adapter files indexed at %s\n", argv[2], fileCount, index);
    free(index);
    return EXIT_SUCCESS;
}

// Set process-local execution context so children inherit the selected project.
static bool enterProject(const char *root) {
#ifdef _WIN32
    if (_putenv_s("B_PROJECT_ROOT", root) != 0 || chdir(root) != 0) {
#else
    if (setenv("B_PROJECT_ROOT", root, 1) != 0 || chdir(root) != 0) {
#endif
        THROW("cannot enter project directory: %s", root);
        return false;
    }
    return true;
}

// Parse b breeze/run options, resolve --in from the user-local registry, then dispatch.
static int runCommand(int argc, char **argv) {
    bool buildArtifact = strcmp(argv[2], "exec") == 0;
    if (!buildArtifact && strcmp(argv[2], "instance") != 0) {
        THROW("unsupported run mode: %s", argv[2]);
        return EXIT_FAILURE;
    }
    const char *projectName = nullptr;
    char **programArguments = Util_allocate((size_t) argc * sizeof(char*));
    int programArgumentCount = 0;
    bool afterSeparator = false;
    for (int index = 4; index < argc; ++index) {
        if (!afterSeparator && strcmp(argv[index], "--") == 0) {
            afterSeparator = true;
            continue;
        }
        if (!afterSeparator && strcmp(argv[index], "--in") == 0) {
            if (projectName != nullptr || index + 1 >= argc ||
                strcmp(argv[index + 1], "--") == 0) {
                THROW("--in requires one project name");
                free(programArguments);
                return EXIT_FAILURE;
            }
            projectName = argv[++index];
            continue;
        }
        programArguments[programArgumentCount++] = argv[index];
    }

    char *root = nullptr;
    char *file = nullptr;
    int status = EXIT_FAILURE;
    if (projectName != nullptr) {
        const char *state = stateDirectory();
        if (state != nullptr)
            root = Project_root(state, projectName);
        if (root != nullptr)
            file = Project_file(root, argv[3]);
        if (root != nullptr && file != nullptr && enterProject(root))
            status = runFile(file, buildArtifact, programArgumentCount, programArguments);
    } else
        status = runFile(argv[3], buildArtifact, programArgumentCount, programArguments);
    free(file);
    free(root);
    free(programArguments);
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
    if (strcmp(argv[1], "project") == 0)
        return projectCommand(argc, argv);
    if (strcmp(argv[1], "refresh") == 0)
        return refreshCommand(argc, argv);
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
    if ((strcmp(argv[1], "breeze") == 0 || strcmp(argv[1], "run") == 0) && argc >= 4)
        return runCommand(argc, argv);
    if (strcmp(argv[1], "export") == 0 && argc == 5) {
        THROW("use 'b box <manifest.json> <path-to-export>'; no destination was created");
        return EXIT_FAILURE;
    }
    THROW("invalid command; run b --help");
    return EXIT_FAILURE;
}
