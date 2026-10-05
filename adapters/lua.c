#include "adapters/lua.h"
#include "b.h"

;;DEFINITION
/* Lua source runs through Lua in both modes. Build uses luac -p independently
 * on each top-level .lua without executing it or emitting bytecode. LUA/LUAC
 * select single executables; package installation and LuaRocks are not implicit.
 * Native interpreter status and literal argv are preserved. This is CLI Lua,
 * not a game engine's embedded dialect or Luau.
 */
;;OVERVIEW
/* MODULE: Lua adapter. PUBLIC RECORD (lua.h): LUA_ADAPTER.
 * RECORD FIELDS: name="lua", extension=".lua", build=buildLua, run=runLua,
 * tools="LUA=lua,LUAC=luac", capabilities="parse-check; instance/exec runtime".
 * PRIVATE STATIC: tool — read validated executable override/default;
 * buildLua — non-evaluating directory checks; runLua — canonical script launch.
 * Paths allocated on the cold CLI seam are freed after child completion.
 */

#include <stdlib.h>

static const char *tool(const char *variable, const char *fallback) {
    const char *value = getenv(variable);
    return value != nullptr && *value != '\0' ? value : fallback;
}

static int buildLua(const char *project, char **output) {
    char *prefix[] = { (char*) tool("LUAC", "luac"), "-p" };
    return Util_checkSources(project, "/*.lua", prefix, 2, output);
}

static int runLua(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) buildArtifact;
    char *path = realpath(file, nullptr);
    if (path == nullptr) {
        THROW("Lua script cannot be resolved");
        return EXIT_FAILURE;
    }
    char *prefix[] = { (char*) tool("LUA", "lua"), "--", path };
    int status = Util_runCommand(prefix, 3, argc, argv);
    free(path);
    return status;
}

const Adapter LUA_ADAPTER = { "lua", ".lua", buildLua, runLua,
    "LUA=lua,LUAC=luac", "parse-check; instance/exec runtime" };
