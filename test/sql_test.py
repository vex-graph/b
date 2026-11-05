"""PostgreSQL owner proof using a temporary, socket-only cluster.

Never connects to or mutates an existing database. All cluster processes have
bounded startup/shutdown and the fixture is retired before scratch deletion.
"""
import os
from pathlib import Path
import shutil
import subprocess
from adapter_support import AdapterCase


class SqlRejectTest(AdapterCase):
    def test_no_implicit_database_or_program_arguments(self):
        source = self.source("hello.sql", "SELECT 'Hello World';\n")
        env = dict(self.environment)
        env.pop("B_SQL_DATABASE", None)
        self.assertIn("B_SQL_DATABASE", self.invoke("sql", source, expected=1, environment=env).stderr)
        self.invoke("build", "sql", self.project, expected=1)
        for database in ("postgresql://secret@host/db", "db;DROP", "-bad", ""):
            self.invoke("sql", source, expected=1, environment=dict(env, B_SQL_DATABASE=database))
        self.invoke("sql", source, "--", "unused", expected=1,
                    environment=dict(env, B_SQL_DATABASE="b_lab"))

    def test_missing_psql_is_observable_without_contacting_a_database(self):
        source = self.source("hello.sql", "SELECT 42;\n")
        env = dict(self.missing_tool_environment(), B_SQL_DATABASE="b_lab")
        self.assertIn("cannot launch psql", self.invoke("sql", source, expected=1, environment=env).stderr)


class SqlTest(AdapterCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        for tool in ("initdb", "pg_ctl", "psql"):
            if shutil.which(tool) is None:
                import unittest
                raise unittest.SkipTest(f"{tool} unavailable; isolated PostgreSQL proof skipped")
        cls.data = cls.home / "pg-data"
        cls.socket = cls.home / "socket"
        cls.socket.mkdir(mode=0o700)
        init = subprocess.run(["initdb", "-D", str(cls.data), "--username=b_lab", "--auth=trust", "--no-locale"],
                              capture_output=True, text=True, timeout=60)
        if init.returncode != 0:
            raise RuntimeError(init.stdout + init.stderr)

        def stop():
            if (cls.data / "postmaster.pid").exists():
                subprocess.run(["pg_ctl", "-D", str(cls.data), "stop", "-m", "immediate", "-w", "-t", "15"],
                               capture_output=True, check=True, timeout=20)
        cls.addClassCleanup(stop)
        options = f"-F -k {cls.socket} -c listen_addresses='' -p 5432"
        started = subprocess.run(["pg_ctl", "-D", str(cls.data), "-l", str(cls.home / "postgres.log"),
                                  "-o", options, "start", "-w", "-t", "20"],
                                 capture_output=True, text=True, timeout=30)
        if started.returncode != 0:
            raise RuntimeError(started.stdout + started.stderr + (cls.home / "postgres.log").read_text())
        # No inherited service/connection options may redirect the fixture.
        cls.environment = {key: value for key, value in cls.environment.items() if not key.startswith("PG")}
        cls.environment.update(PGHOST=str(cls.socket), PGPORT="5432", PGUSER="b_lab",
                               PGCONNECT_TIMEOUT="5", B_SQL_DATABASE="b_lab")
        created = subprocess.run(["psql", "-X", "--no-password", "-d", "postgres", "-c", "CREATE DATABASE b_lab"],
                                 env=cls.environment, capture_output=True, text=True, timeout=20)
        if created.returncode != 0:
            raise RuntimeError(created.stdout + created.stderr)

    def query(self, sql):
        result = subprocess.run(["psql", "-X", "--no-password", "-At", "-d", "b_lab", "-c", sql],
                                env=self.environment, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def test_real_script_execution_and_committed_results(self):
        source = self.source("hello with spaces.sql", "CREATE TABLE b_numbers(value integer);\n"
                             "INSERT INTO b_numbers VALUES (40), (2); SELECT sum(value) FROM b_numbers;\n")
        result = self.invoke("run", "instance", source)
        self.assertIn("42", result.stdout)
        self.assertEqual(self.query("SELECT sum(value) FROM b_numbers"), "42")
        source.write_text("SELECT 'Hello World';\n")
        self.assertIn("Hello World", self.invoke("run", "exec", source).stdout)

    def test_error_rolls_back_ordinary_statements_and_recovers(self):
        source = self.source("failure.sql", "CREATE TABLE b_atomic(value integer);\n"
                             "INSERT INTO b_atomic VALUES (1); SELECT 1/0;\n")
        result = self.invoke("sql", source, expected=3)
        self.assertIn("division by zero", result.stderr)
        self.assertEqual(self.query("SELECT count(*) FROM pg_tables WHERE tablename='b_atomic'"), "0")
        source.write_text("CREATE TABLE b_atomic(value integer); INSERT INTO b_atomic VALUES (42);\n")
        self.invoke("sql", source)
        self.assertEqual(self.query("SELECT value FROM b_atomic"), "42")

    def test_missing_database_and_syntax_errors_are_nonzero(self):
        source = self.source("bad.sql", "not SQL\n")
        self.invoke("sql", source, expected=3)
        self.invoke("sql", source, expected=None,
                    environment=dict(self.environment, B_SQL_DATABASE="b_database_that_does_not_exist"))
