#include "languages/javascript.h"
#include "b.h"

;;DEFINITION
/* Node runs JavaScript source in either mode; build syntax-checks top-level .js/.mjs/.cjs
 * files without executing them and returns their checked directory. .mjs/.cjs
 * files can run individually with Node's normal module semantics. No bundler,
 * npm install, transpiler or package discovery is implied by file execution.
 */
;;OVERVIEW
/* MODULE: JavaScript/Node adapter; PUBLIC RECORDS (javascript.h):
 * JAVASCRIPT_LANGUAGE {name="javascript", extension=".js"};
 * NODE_LANGUAGE {name="node", extension=".mjs"}; JS_LANGUAGE {name="js", extension=".cjs"};
 * all build=buildJavascript, run=runJavascript.
 * PRIVATE STATIC: buildJavascript — node --check each top-level JS module;
 * runJavascript — node file, forwarding literal arguments and status.
 */

static int buildJavascript(const char *project, char **output) {
    char *prefix[] = { "node", "--check" };
    return Util_checkSources(project, "/*.{js,mjs,cjs}", prefix, 2, output);
}

static int runJavascript(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) buildArtifact;
    char *prefix[] = { "node", (char*) file };
    return Util_runCommand(prefix, 2, argc, argv);
}

const Language JAVASCRIPT_LANGUAGE = { "javascript", ".js", buildJavascript, runJavascript };
const Language NODE_LANGUAGE = { "node", ".mjs", buildJavascript, runJavascript };
const Language JS_LANGUAGE = { "js", ".cjs", buildJavascript, runJavascript };
