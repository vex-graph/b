// CLASS: Python language adapter.
// DEFINITION: Python is interpreted, so `run instance` and `run exec` both
// launch the interpreter on the file; there is no compiled artifact to build.
// `build` is a bytecode syntax check: py_compile runs with PYTHONPYCACHEPREFIX
// redirected to the out-of-tree output directory, so no __pycache__ is written
// into the source tree.
#include "languages/python.h"
#include "b.h"

#include <stdlib.h>

static int buildPython(const char *project, char **output) {
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, "/*.py", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    char *directory = Util_outputDirectory(project);
    if (directory == nullptr) {
        Util_freeSources(sources, count);
        return EXIT_FAILURE;
    }
    *output = Util_combine(directory, "/bytecode");
    if (!Util_makeDirectory(*output)) {
        free(directory);
        Util_freeSources(sources, count);
        return EXIT_FAILURE;
    }
    char **arguments = Util_allocate((count + 4) * sizeof(char*));
    size_t n = 0;
    arguments[n++] = "python3";
    arguments[n++] = "-m";
    arguments[n++] = "py_compile";
    for (size_t i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    char **environment = Util_environmentWith("PYTHONPYCACHEPREFIX", *output);
    int status = Util_executeWithEnvironment(arguments, environment);
    Util_freeEnvironment(environment);
    free(arguments);
    free(directory);
    Util_freeSources(sources, count);
    return status;
}

static int runPython(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) buildArtifact; // interpreted: instance and exec both run the source
    char **arguments = Util_allocate(((size_t) argc + 3) * sizeof(char*));
    size_t n = 0;
    arguments[n++] = "python3";
    arguments[n++] = (char*) file;
    for (int i = 0; i < argc; ++i)
        arguments[n++] = argv[i];
    int status = Util_execute(arguments);
    free(arguments);
    return status;
}

const Language PYTHON_LANGUAGE = { "python", ".py", buildPython, runPython };
