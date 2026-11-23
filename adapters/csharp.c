/**
 * MODULE: C# adapter, .NET 10+ file-based applications, no owned class.
 * DEFINITION: One .cs entry file builds into an out-of-tree managed program.dll.
 * Exec launches that artifact with dotnet only after a successful build.
 * Instance delegates to dotnet's file runner (which internally compiles); it
 * does not publish an application. Multi-file/project discovery is not guessed.
 * OVERVIEW: compileCsharp; buildCsharp (one entry); runCsharp; CSHARP_ADAPTER.
 */

#include "vex.h"
#include "adapters/csharp.h"
#include "b.h"

;;DEFINITION
/* .NET 10+ provides file-based C# applications without a hand-written project.
 * Build/exec request a managed program.dll outside source and preserve stdout
 * for the result path. Exec launches only after a successful build; instance
 * delegates to dotnet run --file, whose own runtime workflow compiles internally.
 * Project discovery and self-contained publishing are deliberately not guessed.
 */
;;OVERVIEW
/* MODULE: C# adapter; exported record: CSHARP_ADAPTER (adapters/csharp.h).
 * METADATA: tools="dotnet";
 * capabilities="SDK 10+ file build/exec; native file-run instance".
 * PRIVATE STATIC: compileCsharp, build one file-based managed program;
 * buildCsharp, require exactly one top-level .cs entry;
 * runCsharp, file runner or compile-before-launch, forwarding args/status.
 * RECORD FIELDS: name="csharp"; extension=".cs";
 * build=buildCsharp; run=runCsharp.
 * OWNERSHIP: temporary target paths and argument arrays are released per call.
 */

#include <stdlib.h>

/** Build one .NET file-based C# application into an external managed output path. */
static i32 compileCsharp(string project, string file, utf8 **output) {
    utf8 *directory = Util_outputDirectory(project);
    if (directory == nullptr)
        return EXIT_FAILURE;
    utf8 *target = Util_combine(directory, "/csharp");
    *output = Util_combine(target, "/program.dll");
    utf8 *arguments[] = { "dotnet", "build", (utf8*) file, "--output", target,
        "--nologo", "--verbosity", "quiet", "-p:AssemblyName=program",
        "-p:UseAppHost=false", nullptr, nullptr };
    i32 status = Util_executeBuild(arguments);
    free(target);
    free(directory);
    return status;
}

/** Require exactly one top-level C# source and build it as a file application. */
static i32 buildCsharp(string project, utf8 **output) {
    bytes count = 0;
    bool ok = false;
    utf8 **sources = Util_collectSources(project, "/*.cs", &count, &ok);
    if (!ok)
        return EXIT_FAILURE;
    i32 status = EXIT_FAILURE;
    if (count != 1)
        THROW("C# directory build needs exactly one file-based .cs entry; csproj discovery is not implemented");
    else
        status = compileCsharp(project, sources[0], output);
    Util_freeSources(sources, count);
    return status;
}

/** Use dotnet's file runner in instance mode or build and launch the DLL in exec mode. */
static i32 runCsharp(string file, i32 argc, utf8 **argv, bool buildArtifact) {
    utf8 *project = Util_projectDirectory(file);
    utf8 *output = nullptr;
    i32 status = buildArtifact ? compileCsharp(project, file, &output) : 0;
    if (status == 0) {
        utf8 **arguments = Util_allocate(((bytes) argc + 7) * sizeof(utf8*));
        bytes n = 0;
        arguments[n++] = "dotnet";
        if (buildArtifact)
            arguments[n++] = output;
        else {
            arguments[n++] = "run";
            arguments[n++] = "--file";
            arguments[n++] = (utf8*) file;
            arguments[n++] = "--no-launch-profile";
            arguments[n++] = "--";
        }
        for (i32 i = 0; i < argc; ++i)
            arguments[n++] = argv[i];
        status = Util_execute(arguments);
        free(arguments);
    }
    free(output);
    free(project);
    return status;
}

const Adapter CSHARP_ADAPTER = { "csharp", ".cs", buildCsharp, runCsharp,
    "dotnet", "SDK 10+ file build/exec; native file-run instance", nullptr, nullptr };
