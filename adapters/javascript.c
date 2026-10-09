#include "adapters/javascript.h"
#include "b.h"

;;DEFINITION
/* Node runs JavaScript source in either mode; build syntax-checks top-level .js/.mjs/.cjs
 * files without executing them and returns their checked directory. .mjs/.cjs
 * files can run individually with Node's normal module semantics. No bundler,
 * npm install, transpiler or package discovery is implied by file execution.
 */
;;OVERVIEW
/* MODULE: JavaScript/Node adapter; PUBLIC RECORDS (javascript.h):
 * JAVASCRIPT_ADAPTER {name="javascript", extension=".js"};
 * NODE_ADAPTER {name="node", extension=".mjs"}; JS_ADAPTER {name="js", extension=".cjs"};
 * all build=buildJavascript, run=runJavascript.
 * METADATA (all): tools="node"; capabilities="syntax-check; instance/exec runtime".
 * PRIVATE STATIC: buildJavascript — node --check each top-level JS module;
 * runJavascript — node file, forwarding literal arguments and status.
 */

// Syntax-check supported top-level JavaScript module files with Node.
static int buildJavascript(const char *project, char **output) {
    char *prefix[] = { "node", "--check" };
    return Util_checkSources(project, "/*.{js,mjs,cjs}", prefix, 2, output);
}

// Run the selected JavaScript source with Node and forward program arguments.
static int runJavascript(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) buildArtifact;
    char *prefix[] = { "node", (char*) file };
    return Util_runCommand(prefix, 2, argc, argv);
}

const Adapter JAVASCRIPT_ADAPTER = { "javascript", ".js", buildJavascript, runJavascript,
    "node", "syntax-check; instance/exec runtime" };
const Adapter NODE_ADAPTER = { "node", ".mjs", buildJavascript, runJavascript,
    "node", "syntax-check; instance/exec runtime" };
const Adapter JS_ADAPTER = { "js", ".cjs", buildJavascript, runJavascript,
    "node", "syntax-check; instance/exec runtime" };
