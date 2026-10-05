#include "adapters/shell.h"
#include "b.h"

;;DEFINITION
/* POSIX shell scripts run through /bin/sh in either mode. Build checks each
 * top-level .sh with -n without executing it. Arguments are passed as argv,
 * never interpolated into a command string by b; the script's own shell syntax
 * is intentionally executable and trusted. Bash/zsh dialects are not inferred.
 */
;;OVERVIEW
/* MODULE: POSIX shell adapter; PUBLIC RECORD: SHELL_ADAPTER (shell.h).
 * METADATA: tools="/bin/sh"; capabilities="POSIX syntax-check; instance/exec runtime".
 * PRIVATE STATIC: buildShell — sh -n syntax checks; runShell — sh file execution.
 * RECORD FIELDS: name="shell", extension=".sh", build=buildShell, run=runShell.
 * No persistent state; program arguments and script paths are borrowed.
 */

static int buildShell(const char *project, char **output) {
    char *prefix[] = { "/bin/sh", "-n" };
    return Util_checkSources(project, "/*.sh", prefix, 2, output);
}

static int runShell(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) buildArtifact;
    char *prefix[] = { "/bin/sh", (char*) file };
    return Util_runCommand(prefix, 2, argc, argv);
}

const Adapter SHELL_ADAPTER = { "shell", ".sh", buildShell, runShell,
    "/bin/sh", "POSIX syntax-check; instance/exec runtime" };
