#pragma once
#include "annotation.h"
#include "adapters/adapter.h"
;;DEFINITION
/* Explicit PostgreSQL script execution; no default database is guessed. */
;;OVERVIEW
/* PUBLIC RECORD: SQL_ADAPTER {name="sql", extension=".sql",
 * build=buildSql, run=runSql}; implementation: sql.c. No owned state.
 */
extern const Adapter SQL_ADAPTER;
