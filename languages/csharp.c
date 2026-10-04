// CLASS: C# language adapter (reserved).
// DEFINITION: C# needs the .NET SDK and a project (or file-based-app support);
// neither is wired yet. This adapter claims the ".cs" extension so a .cs file
// reports a specific, honest rejection instead of a generic one. It creates no
// output and launches nothing. Wired once the .NET path is implemented + tested.
#include "languages/csharp.h"
#include "b.h"

#include <stdlib.h>

static int buildCSharp(const char *project, char **output) {
    (void) project;
    (void) output;
    THROW("C# is not implemented; it needs the .NET SDK and a project");
    return EXIT_FAILURE;
}

static int runCSharp(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) file;
    (void) argc;
    (void) argv;
    (void) buildArtifact;
    THROW("C# is not implemented; it needs the .NET SDK and a project");
    return EXIT_FAILURE;
}

const Language CSHARP_LANGUAGE = { "csharp", ".cs", buildCSharp, runCSharp };
