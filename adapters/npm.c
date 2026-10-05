#include "adapters/npm.h"
#include "b.h"

;;DEFINITION
/* b delegates to the scripts declared in package.json, never invents a bundler.
 * Build invokes npm run build and returns the project directory, since output
 * layout belongs to the script. Instance invokes npm run start; exec runs build
 * first and starts only if it succeeds. npm owns package semantics and script
 * execution (which may itself use a shell). No install/download is automatic.
 */
;;OVERVIEW
/* MODULE: npm backend; PUBLIC RECORD: NPM_ADAPTER (npm.h).
 * PRIVATE STATIC: hasManifest — validate package.json; buildNpm — native build
 * script; runNpm — require literal package.json entry and forward start args.
 * RECORD FIELDS: name="npm", extension="package.json", build=buildNpm, run=runNpm.
 * METADATA: tools="npm,node"; capabilities="build/start scripts; exec builds then starts".
 * Return paths are caller-owned; child diagnostics/status are preserved.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool hasManifest(const char *project) {
    char *path = Util_combine(project, "/package.json");
    struct stat info;
    bool ok = stat(path, &info) == 0 && S_ISREG(info.st_mode);
    free(path);
    if (!ok)
        THROW("npm backend requires package.json in the project directory");
    return ok;
}

static int buildNpm(const char *project, char **output) {
    if (!hasManifest(project))
        return EXIT_FAILURE;
    char *arguments[] = { "npm", "--prefix", (char*) project, "run", "build", nullptr };
    int status = Util_executeBuild(arguments);
    if (status == 0)
        *output = Util_combine(project, "");
    return status;
}

static int runNpm(const char *file, int argc, char **argv, bool buildArtifact) {
    char *path = realpath(file, nullptr);
    if (path == nullptr) {
        THROW("npm manifest path is unavailable");
        return EXIT_FAILURE;
    }
    const char *name = strrchr(path, '/');
    if (name == nullptr || strcmp(name + 1, "package.json") != 0) {
        THROW("npm run requires the project's package.json file");
        free(path);
        return EXIT_FAILURE;
    }
    char *project = Util_parentDirectory(path);
    free(path);
    char *output = nullptr;
    int status = buildArtifact ? buildNpm(project, &output) : 0;
    if (status == 0) {
        char *prefix[] = { "npm", "--prefix", project, "run", "start", "--" };
        status = Util_runCommand(prefix, 6, argc, argv);
    }
    free(output);
    free(project);
    return status;
}

const Adapter NPM_ADAPTER = { "npm", "package.json", buildNpm, runNpm,
    "npm,node", "build/start scripts; exec builds then starts" };
