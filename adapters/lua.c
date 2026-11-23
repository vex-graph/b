#include "vex.h"
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
 * PRIVATE STATIC: tool, read validated executable override/default;
 * buildLua, non-evaluating directory checks; runLua, canonical script launch.
 * Paths allocated on the cold CLI seam are freed after child completion.
 */

#include <stdlib.h>

/** Return a nonempty environment override or the supplied executable default. */
static const utf8 *tool(const utf8 *variable, const utf8 *fallback) {
    const utf8 *value = getenv(variable);
    return value != nullptr && *value != '\0' ? value : fallback;
}

/** Parse-check each top-level Lua file without evaluating it. */
static i32 buildLua(const utf8 *project, utf8 **output) {
    utf8 *prefix[] = { (utf8*) tool("LUAC", "luac"), "-p" };
    return Util_checkSources(project, "/*.lua", prefix, 2, output);
}

/** Run a canonicalized Lua script and forward its arguments to the interpreter. */
static i32 runLua(const utf8 *file, i32 argc, utf8 **argv, bool buildArtifact) {
    (discard) buildArtifact;
    utf8 *path = realpath(file, nullptr);
    if (path == nullptr) {
        THROW("Lua script cannot be resolved");
        return EXIT_FAILURE;
    }
    utf8 *prefix[] = { (utf8*) tool("LUA", "lua"), "--", path };
    i32 status = Util_runCommand(prefix, 3, argc, argv);
    free(path);
    return status;
}

const Adapter LUA_ADAPTER = { "lua", ".lua", buildLua, runLua,
    "LUA=lua,LUAC=luac", "parse-check; instance/exec runtime", nullptr, nullptr };
