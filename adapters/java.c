// CLASS: Java language adapter.
// DEFINITION: `run instance` uses the JDK source launcher (`java File.java`);
// `run exec` and `build` compile classes with javac into out-of-tree /classes,
// then launch the default-package main class named by the file. Project/dependency
// tooling is out of scope; this is the plain JDK path.
#include "adapters/java.h"
#include "b.h"

;;DEFINITION
/* The Java adapter preserves the difference between JDK source launching and
 * compiled-class execution. Instance delegates to java; exec first invokes
 * javac, then runs the filename's default-package main class. Directory builds
 * compile top-level .java sources into out-of-tree classes. Dependency/package
 * discovery is not inferred; allocated paths and argument lists are temporary.
 */
;;OVERVIEW
/* MODULE: Java adapter; exported record: JAVA_ADAPTER (adapters/java.h).
 * METADATA: tools="java,javac"; capabilities="class build/exec; source instance".
 * PRIVATE STATIC: compileJava — javac sources into classes directory;
 * mainClass — allocate default-package filename stem;
 * buildJava — discover directory sources and compile;
 * runJava — source-launch instance or compile and launch classes.
 * RECORD FIELDS: name="java"; extension=".java"; build=buildJava; run=runJava.
 * Capability boundary: installed JDK required; no Maven/Gradle project discovery.
 */

#include <stdlib.h>
#include <string.h>

static int compileJava(const char *project, char **sources, size_t count, char **output) {
    char *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    *output = Util_combine(directory, "/classes");
    if (!Util_makeDirectory(*output)) {
        free(directory);
        return EXIT_FAILURE;
    }
    char **arguments = Util_allocate((count + 4) * sizeof(char*));
    size_t n = 0;
    arguments[n++] = "javac";
    arguments[n++] = "-d";
    arguments[n++] = *output;
    for (size_t i = 0; i < count; ++i)
        arguments[n++] = sources[i];
    int status = Util_execute(arguments);
    free(arguments);
    free(directory);
    return status;
}

static char *mainClass(const char *file) {
    const char *base = strrchr(file, '/');
    char *name = Util_combine(base != nullptr ? base + 1 : file, "");
    char *dot = strrchr(name, '.');
    if (dot != nullptr)
        *dot = '\0';
    return name;
}

static int buildJava(const char *project, char **output) {
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, "/*.java", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    int status = compileJava(project, sources, count, output);
    Util_freeSources(sources, count);
    return status;
}

static int runJava(const char *file, int argc, char **argv, bool buildArtifact) {
    if (!buildArtifact) {
        char **arguments = Util_allocate(((size_t) argc + 3) * sizeof(char*));
        size_t n = 0;
        arguments[n++] = "java";
        arguments[n++] = (char*) file;
        for (int i = 0; i < argc; ++i)
            arguments[n++] = argv[i];
        int status = Util_execute(arguments);
        free(arguments);
        return status;
    }
    char *project = Util_parentDirectory(file);
    char *output = nullptr;
    char *sources[1] = { (char*) file };
    int status = compileJava(project, sources, 1, &output);
    if (status == 0) {
        char *main = mainClass(file);
        char **arguments = Util_allocate(((size_t) argc + 5) * sizeof(char*));
        size_t n = 0;
        arguments[n++] = "java";
        arguments[n++] = "-cp";
        arguments[n++] = output;
        arguments[n++] = main;
        for (int i = 0; i < argc; ++i)
            arguments[n++] = argv[i];
        status = Util_execute(arguments);
        free(arguments);
        free(main);
    }
    free(output);
    free(project);
    return status;
}

const Adapter JAVA_ADAPTER = { "java", ".java", buildJava, runJava,
    "java,javac", "class build/exec; source instance" };
