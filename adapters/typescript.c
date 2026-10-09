#include "adapters/typescript.h"
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
 * TYPESCRIPT_ADAPTER {name="typescript", extension=".ts"};
 * TS_ADAPTER {name="ts", extension=".mts"}; CTS_ADAPTER {name="node-ts", extension=".cts"};
 * all build=buildTypescript, run=runTypescript.
 * METADATA (all): tools="node";
 * capabilities="native type-strip check/runtime; NOT type-checking".
 * PRIVATE STATIC: buildTypescript — Node native type-stripping parse, not type checks;
 * runTypescript — native Node type-stripping run with literal arguments.
 */

// Validate TypeScript erasability with Node's native stripping parser, not tsc.
static int buildTypescript(const char *project, char **output) {
    char *prefix[] = { "node", "-e",
        "require('node:module').stripTypeScriptTypes(require('node:fs').readFileSync(process.argv[1], 'utf8'), {mode:'strip', sourceUrl:process.argv[1]})", "--" };
    return Util_checkSources(project, "/*.{ts,mts,cts}", prefix, 4, output);
}

// Run a TypeScript source through Node's native type-stripping support.
static int runTypescript(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) buildArtifact;
    char *prefix[] = { "node", (char*) file };
    return Util_runCommand(prefix, 2, argc, argv);
}

const Adapter TYPESCRIPT_ADAPTER = { "typescript", ".ts", buildTypescript, runTypescript,
    "node", "native type-strip check/runtime; NOT type-checking" };
const Adapter TS_ADAPTER = { "ts", ".mts", buildTypescript, runTypescript,
    "node", "native type-strip check/runtime; NOT type-checking" };
const Adapter CTS_ADAPTER = { "node-ts", ".cts", buildTypescript, runTypescript,
    "node", "native type-strip check/runtime; NOT type-checking" };
