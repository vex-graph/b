#include "languages/html.h"
#include "b.h"

;;DEFINITION
/* HTML runs by opening the original local document in a browser; index.html is
 * not a native executable. Both modes launch the same document. B_BROWSER may
 * select one executable; otherwise use the host opener. Success means the
 * opener accepted the request, not that JavaScript or rendering passed tests.
 * No HTTP server, bundling, asset copy or browser lifetime supervision is implied.
 */
;;OVERVIEW
/* MODULE: local HTML launcher; PUBLIC RECORDS (html.h): HTML_LANGUAGE
 * {name="html", extension=".html"}, HTM_LANGUAGE {name="web", extension=".htm"};
 * both build=buildHtml, run=runHtml.
 * PRIVATE STATIC: buildHtml — reject guessed builds; runHtml — canonical-path
 * browser launch, rejecting unused program arguments.
 * Allocated canonical path is freed after opener status; browser remains OS-owned.
 */

#include <stdlib.h>

static int buildHtml(const char *project, char **output) {
    (void) project;
    (void) output;
    THROW("HTML has no guessed build pipeline; use b build npm for a declared web build script");
    return EXIT_FAILURE;
}

static int runHtml(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) argv;
    (void) buildArtifact;
    if (argc != 0) {
        THROW("local HTML browser launch does not accept program arguments");
        return EXIT_FAILURE;
    }
    char *path = realpath(file, nullptr);
    if (path == nullptr) {
        THROW("HTML file cannot be resolved");
        return EXIT_FAILURE;
    }
    const char *browser = getenv("B_BROWSER");
    if (browser == nullptr || *browser == '\0') {
#ifdef __APPLE__
        browser = "open";
#else
        browser = "xdg-open";
#endif
    }
    char *prefix[] = { (char*) browser, path };
    int status = Util_runCommand(prefix, 2, 0, nullptr);
    free(path);
    return status;
}

const Language HTML_LANGUAGE = { "html", ".html", buildHtml, runHtml };
const Language HTM_LANGUAGE = { "web", ".htm", buildHtml, runHtml };
