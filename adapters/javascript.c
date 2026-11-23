#include "vex.h"
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
 * PRIVATE STATIC: buildJavascript, node --check each top-level JS module;
 * runJavascript, node file, forwarding literal arguments and status.
 */

/** Syntax-check supported top-level JavaScript module files with Node. */
static i32 buildJavascript(string project, utf8 **output) {
    utf8 *prefix[] = { "node", "--check" };
    return Util_checkSources(project, "/*.{js,mjs,cjs}", prefix, 2, output);
}

/** Run the selected JavaScript source with Node and forward program arguments. */
static i32 runJavascript(string file, i32 argc, utf8 **argv, bool buildArtifact) {
    (discard) buildArtifact;
    utf8 *prefix[] = { "node", (utf8*) file };
    return Util_runCommand(prefix, 2, argc, argv);
}

const Adapter JAVASCRIPT_ADAPTER = { "javascript", ".js", buildJavascript, runJavascript,
    "node", "syntax-check; instance/exec runtime", nullptr, nullptr };
const Adapter NODE_ADAPTER = { "node", ".mjs", buildJavascript, runJavascript,
    "node", "syntax-check; instance/exec runtime", nullptr, nullptr };
const Adapter JS_ADAPTER = { "js", ".cjs", buildJavascript, runJavascript,
    "node", "syntax-check; instance/exec runtime", nullptr, nullptr };
