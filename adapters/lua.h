#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* Lua delegates execution and parse-only validation to installed native tools. */
;;OVERVIEW
/* MODULE: Lua adapter contract. PUBLIC RECORD: LUA_ADAPTER (lua.c).
 * FIELDS: name="lua", extension=".lua", build=buildLua, run=runLua,
 * tools="LUA=lua,LUAC=luac", capabilities="parse-check; instance/exec runtime".
 * No owned state; returned paths belong to the caller.
 */
extern const Adapter LUA_ADAPTER;
