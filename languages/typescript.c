#include "languages/typescript.h"
#include "b.h"

;;DEFINITION
/* Recent Node versions execute erasable TypeScript through native type stripping.
 * Both modes delegate to Node; build parses top-level .ts/.mts/.cts through Node's native
 * stripTypeScriptTypes API without execution (node --check does not strip types).
 * This is NOT tsc/type-checking, transpilation or tsconfig path mapping. Node's
 * unsupported syntax (e.g. non-erasable enum) rejects through the native tool.
 * Use an npm project script when you need the project's full TypeScript pipeline.
 */
;;OVERVIEW
/* MODULE: TypeScript/Node adapter; PUBLIC RECORDS (typescript.h):
 * TYPESCRIPT_LANGUAGE {name="typescript", extension=".ts"};
 * TS_LANGUAGE {name="ts", extension=".mts"}; CTS_LANGUAGE {name="node-ts", extension=".cts"};
 * all build=buildTypescript, run=runTypescript.
 * PRIVATE STATIC: buildTypescript — Node native type-stripping parse, not type checks;
 * runTypescript — native Node type-stripping run with literal arguments.
 */

static int buildTypescript(const char *project, char **output) {
    char *prefix[] = { "node", "-e",
        "require('node:module').stripTypeScriptTypes(require('node:fs').readFileSync(process.argv[1], 'utf8'), {mode:'strip', sourceUrl:process.argv[1]})", "--" };
    return Util_checkSources(project, "/*.{ts,mts,cts}", prefix, 4, output);
}

static int runTypescript(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) buildArtifact;
    char *prefix[] = { "node", (char*) file };
    return Util_runCommand(prefix, 2, argc, argv);
}

const Language TYPESCRIPT_LANGUAGE = { "typescript", ".ts", buildTypescript, runTypescript };
const Language TS_LANGUAGE = { "ts", ".mts", buildTypescript, runTypescript };
const Language CTS_LANGUAGE = { "node-ts", ".cts", buildTypescript, runTypescript };
