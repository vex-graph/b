// CLASS: C language adapter.
// DEFINITION: Compiles a directory's top-level .c files into one native
// executable with the C23 dialect and the project's build flags; a single
// source is compiled on demand for `run exec`. C has no source runtime, so
// `run instance` rejects and points at `run exec`. The compiler is $CC or cc.
#include "adapters/c.h"
#include "b.h"

;;DEFINITION
/* C compilation produces one native executable from supplied sources, using
 * the selected CC executable, strict C23 warnings and platform baseline flags.
 * Directory builds discover top-level .c inputs; exec builds one entry source
 * before launching it. Instance rejects because there is no C source runtime.
 * Child failures propagate and prevent launching previous output.
 */
;;OVERVIEW
/* MODULE: C adapter; exported record: C_ADAPTER (adapters/c.h).
 * METADATA: tools="CC=cc"; capabilities="native build/exec; source instance rejects".
 * PRIVATE STATIC: compileC — compile explicit sources into out-of-tree program;
 * buildC — collect directory sources and compile;
 * runC — reject instance or compile and execute with borrowed program args.
 * RECORD FIELDS: name="c"; extension=".c"; build=buildC; run=runC.
 * No owned class state; temporary argv/path/source lists are freed per call.
 */

#include <stdlib.h>

static int compileC(const char *project, char **sources, size_t count, char **output) {
    if (count > SIZE_MAX / sizeof(char*) - 20) {
        THROW("source argument count overflow");
        return EXIT_FAILURE;
    }
    char *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    *output = Util_combine(directory, "/program");
    // Fixed option overhead, not a ceiling on the dynamic source list.
    char **arguments = Util_allocate((count + 20) * sizeof(char*));
    size_t n = 0;
    const char *compiler = getenv("CC");
    arguments[n++] = (char*) (compiler != nullptr && *compiler ? compiler : "cc");
    arguments[n++] = "-std=gnu23";
    arguments[n++] = "-Wall";
    arguments[n++] = "-Wextra";
    arguments[n++] = "-Werror";
#ifdef __APPLE__
    arguments[n++] = "-arch";
    arguments[n++] = "arm64";
    arguments[n++] = "-mcpu=apple-m1";
    arguments[n++] = "-mmacosx-version-min=14.0";
#endif
    arguments[n++] = "-I";
    arguments[n++] = (char*) project;
    arguments[n++] = "-o";
    arguments[n++] = *output;
    for (size_t i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    int status = Util_execute(arguments);
    free(arguments);
    free(directory);
    return status;
}

static int buildC(const char *project, char **output) {
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, "/*.c", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    int status = compileC(project, sources, count, output);
    Util_freeSources(sources, count);
    return status;
}

static int runC(const char *file, int argc, char **argv, bool buildArtifact) {
    if (!buildArtifact) {
        THROW("C has no source runtime; use b run exec to compile then launch");
        return EXIT_FAILURE;
    }
    char *project = Util_parentDirectory(file);
    char *output = nullptr;
    char *sources[1] = { (char*) file };
    int status = compileC(project, sources, 1, &output);
    if (status == 0) {
        char **arguments = Util_allocate(((size_t) argc + 2) * sizeof(char*));
        arguments[0] = output;
        for (int i = 0; i < argc; ++i)
            arguments[i + 1] = argv[i];
        status = Util_execute(arguments);
        free(arguments);
    }
    free(output);
    free(project);
    return status;
}

const Adapter C_ADAPTER = { "c", ".c", buildC, runC,
    "CC=cc", "native build/exec; source instance rejects" };
