// MODULE: C# adapter — .NET 10+ file-based applications, no owned class.
// DEFINITION: One .cs entry file builds into an out-of-tree managed program.dll.
// Exec launches that artifact with dotnet only after a successful build.
// Instance delegates to dotnet's file runner (which internally compiles); it
// does not publish an application. Multi-file/project discovery is not guessed.
// OVERVIEW: compileCsharp; buildCsharp (one entry); runCsharp; CSHARP_LANGUAGE.
#include "languages/csharp.h"
#include "b.h"

;;DEFINITION
/* .NET 10+ provides file-based C# applications without a hand-written project.
 * Build/exec request a managed program.dll outside source and preserve stdout
 * for the result path. Exec launches only after a successful build; instance
 * delegates to dotnet run --file, whose own runtime workflow compiles internally.
 * Project discovery and self-contained publishing are deliberately not guessed.
 */
;;OVERVIEW
/* MODULE: C# adapter; exported record: CSHARP_LANGUAGE (languages/csharp.h).
 * PRIVATE STATIC: compileCsharp — build one file-based managed program;
 * buildCsharp — require exactly one top-level .cs entry;
 * runCsharp — file runner or compile-before-launch, forwarding args/status.
 * RECORD FIELDS: name="csharp"; extension=".cs";
 * build=buildCsharp; run=runCsharp.
 * OWNERSHIP: temporary target paths and argument arrays are released per call.
 */

#include <stdlib.h>

static int compileCsharp(const char *project, const char *file, char **output) {
    char *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    char *target = Util_combine(directory, "/csharp");
    *output = Util_combine(target, "/program.dll");
    char *arguments[] = { "dotnet", "build", (char*) file, "--output", target,
        "--nologo", "--verbosity", "quiet", "-p:AssemblyName=program",
        "-p:UseAppHost=false", nullptr };
    int status = Util_executeBuild(arguments);
    free(target);
    free(directory);
    return status;
}

static int buildCsharp(const char *project, char **output) {
    size_t count = 0;
    bool ok = false;
    char **sources = Util_collectSources(project, "/*.cs", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    int status = EXIT_FAILURE;
    if (count != 1)
        THROW("C# directory build needs exactly one file-based .cs entry; csproj discovery is not implemented");
    else
        status = compileCsharp(project, sources[0], output);
    Util_freeSources(sources, count);
    return status;
}

static int runCsharp(const char *file, int argc, char **argv, bool buildArtifact) {
    char *project = Util_parentDirectory(file);
    char *output = nullptr;
    int status = buildArtifact ? compileCsharp(project, file, &output) : 0;
    if (status == 0) {
        char **arguments = Util_allocate(((size_t) argc + 7) * sizeof(char*));
        size_t n = 0;
        arguments[n++] = "dotnet";
        if (buildArtifact)
            arguments[n++] = output;
        else {
            arguments[n++] = "run";
            arguments[n++] = "--file";
            arguments[n++] = (char*) file;
            arguments[n++] = "--no-launch-profile";
            arguments[n++] = "--";
        }
        for (int i = 0; i < argc; ++i)
            arguments[n++] = argv[i];
        status = Util_execute(arguments);
        free(arguments);
    }
    free(output);
    free(project);
    return status;
}

const Language CSHARP_LANGUAGE = { "csharp", ".cs", buildCsharp, runCsharp };
