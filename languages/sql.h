#pragma once
#include "annotation.h"
#include "languages/language.h"
;;DEFINITION
/* Explicit PostgreSQL script execution; no default database is guessed. */
;;OVERVIEW
/* PUBLIC RECORD: SQL_LANGUAGE {name="sql", extension=".sql",
 * build=buildSql, run=runSql}; implementation: sql.c. No owned state.
 */
extern const Language SQL_LANGUAGE;
