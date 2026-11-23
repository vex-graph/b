#include "vex.h"
#include "adapters/npm.h"
#include "b.h"
#include "b/b_str.h"

;;DEFINITION
/* b.sh delegates to the scripts declared in package.json, never invents a bundler.
 * Build invokes npm run build and returns the project directory, since output
 * layout belongs to the script. Instance invokes npm run start; exec runs build
 * first and starts only if it succeeds. npm owns package semantics and script
 * execution (which may itself use a shell). No install/download is automatic.
 */
;;OVERVIEW
/* MODULE: npm backend; PUBLIC RECORD: NPM_ADAPTER (npm.h).
 * PRIVATE STATIC: hasManifest, validate package.json; buildNpm, native build
 * script; runNpm, require literal package.json entry and forward start args.
 * RECORD FIELDS: name="npm", extension="package.json", build=buildNpm, run=runNpm.
 * METADATA: tools="npm,node"; capabilities="build/start scripts; exec builds then starts".
 * Return paths are caller-owned; child diagnostics/status are preserved.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/** Require a regular package.json before delegating npm script operations. */
static bool hasManifest(string project) {
    utf8 *path = Util_combine(project, "/package.json");
    struct stat info;
    bool ok = stat(path, &info) == 0 && S_ISREG(info.st_mode);
    free(path);
    if (!ok)
        THROW("npm backend requires package.json in the project directory");
    return ok;
}

/** Run the project's declared npm build script without installing dependencies. */
static i32 buildNpm(string project, utf8 **output) {
    if (!hasManifest(project))
        return EXIT_FAILURE;
    utf8 *arguments[] = { "npm", "--prefix", (utf8*) project, "run", "build", nullptr, nullptr };
    i32 status = Util_executeBuild(arguments);
    if (status == 0)
        *output = Util_combine(project, "");
    return status;
}

/** Run the declared start script, building first only for exec mode. */
static i32 runNpm(string file, i32 argc, utf8 **argv, bool buildArtifact) {
    utf8 *path = realpath(file, nullptr);
    if (path == nullptr) {
        THROW("npm manifest path is unavailable");
        return EXIT_FAILURE;
    }
    string name = strrchr(path, '/');
    if (name == nullptr || !bStrEq(name + 1, "package.json")) {
        THROW("npm run requires the project's package.json file");
        free(path);
        return EXIT_FAILURE;
    }
    utf8 *project = Util_projectDirectory(path);
    free(path);
    utf8 *output = nullptr;
    i32 status = buildArtifact ? buildNpm(project, &output) : 0;
    if (status == 0) {
        utf8 *prefix[] = { "npm", "--prefix", project, "run", "start", "--" };
        status = Util_runCommand(prefix, 6, argc, argv);
    }
    free(output);
    free(project);
    return status;
}

const Adapter NPM_ADAPTER = { "npm", "package.json", buildNpm, runNpm,
    "npm,node", "build/start scripts; exec builds then starts", nullptr, nullptr };
