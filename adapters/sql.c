#include "adapters/sql.h"
#include "b.h"

;;DEFINITION
/* SQL scripts execute through psql only when B_SQL_DATABASE explicitly names a
 * target database. PGHOST/PGPORT/PGUSER/PGPASSFILE stay in libpq's environment;
 * connection URIs/passwords are not accepted in the database-name field.
 * psql ignores startup files, disables interactive password prompts, stops on
 * SQL errors, and wraps ordinary statements in one transaction. This is not a
 * sandbox: explicit transaction control and psql meta-commands retain native
 * semantics. Build cannot promise a parse-only check without database context.
 */
;;OVERVIEW
/* MODULE: PostgreSQL SQL adapter; PUBLIC RECORD: SQL_ADAPTER (sql.h).
 * METADATA: tools="psql"; capabilities="explicit PostgreSQL run; no standalone build".
 * PRIVATE STATIC: buildSql — reject fake compilation; runSql — validate explicit
 * database name and invoke psql -X/ON_ERROR_STOP/--single-transaction.
 * RECORD FIELDS: name="sql", extension=".sql", build=buildSql, run=runSql.
 * Database/file names are borrowed; child status propagates. No DB is created,
 * installed or auto-selected by this adapter. Test clusters are isolated fixtures.
 */

#include <stdlib.h>
#include <string.h>

static int buildSql(const char *project, char **output) {
    (void) project;
    (void) output;
    THROW("SQL has no standalone compile here; run against an explicitly selected disposable database");
    return EXIT_FAILURE;
}

static int runSql(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) argv;
    (void) buildArtifact;
    const char *database = getenv("B_SQL_DATABASE");
    if (database == nullptr || *database == '\0') {
        THROW("SQL execution requires B_SQL_DATABASE; no existing database is auto-selected");
        return EXIT_FAILURE;
    }
    if (argc != 0) {
        THROW("SQL program arguments are not supported; use libpq environment settings for connection configuration");
        return EXIT_FAILURE;
    }
    for (const char *p = database; *p != '\0'; ++p) {
        bool valid = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_' || (*p == '-' && p != database);
        if (!valid) {
            THROW("B_SQL_DATABASE must be a database name, not a URI, credentials or SQL");
            return EXIT_FAILURE;
        }
    }
    char *arguments[] = { "psql", "-X", "--no-password", "--set=ON_ERROR_STOP=1",
        "--single-transaction", "--dbname", (char*) database, "--file", (char*) file, nullptr };
    return Util_execute(arguments);
}

const Adapter SQL_ADAPTER = { "sql", ".sql", buildSql, runSql,
    "psql", "explicit PostgreSQL run; no standalone build" };
