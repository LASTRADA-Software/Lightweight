# Per-DBMS notes

Lightweight is exercised against three databases in CI (`.github/workflows/build.yml` → `dbms_test_matrix`). Every change must be verified against all three before being marked done — driver semantics differ in non-obvious ways.

## SQLite3

- **`SqlServerType::SQLITE`**, test-env name `sqlite3`.
- **Driver**: `sqliteodbc` (`libsqliteodbc` on Linux, `sqliteodbc` MSI on Windows).
- **Connection string**: `DRIVER=SQLite3;Database=test.db` (Linux) or `DRIVER={SQLite3 ODBC Driver};Database=file::memory:` (Windows).
- **Strengths**: fast local tests, no daemon needed.
- **Footguns**:
  - Type affinity instead of strict types — column types are advisory.
  - No real `BOOLEAN` (stored as INTEGER).
  - Limited migration support (no native `ALTER COLUMN` semantics; the migration layer emulates).
  - Valgrind is run *only* against SQLite3 in CI to catch driver-leaked allocations (`build.yml` adds `valgrind --leak-check=full ... --error-exitcode=1` for the sqlite3 matrix entry).

## Microsoft SQL Server

- **`SqlServerType::MICROSOFT_SQL`**, test-env names `mssql2017`, `mssql2019`, `mssql2022`, `mssql` (Windows LocalDB).
- **Driver**: `ODBC Driver 17/18 for SQL Server` (`mssql-tools18` on Linux installs ODBC Driver 18; the Windows CI uses LocalDB + Driver 17).
- **Connection strings**:
  - Linux Docker: `Driver={ODBC Driver 18 for SQL Server};SERVER=localhost;PORT=1433;UID=SA;PWD=Lightweight!Test42;TrustServerCertificate=yes;DATABASE=LightweightTest`
  - Windows LocalDB: `Driver={ODBC Driver 17 for SQL Server};Server=(LocalDB)\MSSQLLocalDB;Database=TestDB;Trusted_Connection=Yes;`
- **Footguns**:
  - `wchar_t` is 16-bit on Windows; on Linux the driver still expects UTF-16 even though `wchar_t` is 32-bit. Use the `WideChar` typedef from `src/tests/Utils.hpp` (or `char16_t` directly) for portable UTF-16 in tests.
  - `IDENTITY` columns are queried via `SELECT @@IDENTITY` (note the formatter override).
  - Schema-qualified identifiers use `[schema].[name]` quoting.
  - 2017 `mssql-tools` (no `-C` flag); 2018+ `mssql-tools18` (requires `-C` for self-signed cert).
  - 2025 image is currently disabled in CI (image broken).

## PostgreSQL

- **`SqlServerType::POSTGRESQL`**, test-env name `postgres`.
- **Driver**: `psqlODBC` — pick `PostgreSQL Unicode` (NOT `PostgreSQL ANSI`) for any Unicode-bearing workload.
- **Connection string**: `Driver={PostgreSQL Unicode};Server=localhost;Port=5432;Uid=postgres;Pwd=Lightweight!Test42;Database=test`
- **Footguns**:
  - **Newline translation**: `psqlODBC` defaults to LF↔CRLF translation. The project's test connection strings disable it explicitly (commit `f311cb9f`); preserve that in any new test-env entry, or character-data round-trips with embedded newlines will silently mutate.
  - **Unicode round-trip**: `SQL_C_WCHAR` (UTF-16) is the binding type that round-trips reliably across all DataBinder paths (commit `894c67c7`); avoid `SQL_C_CHAR` for non-ASCII strings unless you've validated the driver actually transcodes.
  - `SERIAL` columns: detect via `nextval(` in the captured default value (see `PostgreSqlFormatter::BuildColumnDefinition`); restored backups carry the sequence default rather than `AUTO_INCREMENT`.
  - Identifier quoting uses `"`. Folding to lowercase happens for unquoted identifiers — the formatter quotes everything to keep behaviour consistent.
  - `SELECT lastval();` retrieves the last inserted id (formatter override). Race-prone if multiple clients insert into different sequences simultaneously — call it inside the same session right after the insert.

## Docker harness

`scripts/tests/docker-databases.py` is the supported way to spin up MSSQL/Postgres locally:

```sh
python3 scripts/tests/docker-databases.py --start --wait                # all
python3 scripts/tests/docker-databases.py --start --wait mssql2022 postgres
python3 scripts/tests/docker-databases.py --status
python3 scripts/tests/docker-databases.py --stop
python3 scripts/tests/docker-databases.py --remove
python3 scripts/tests/docker-databases.py --load-sql Chinook.sql mssql2022
```

The script is **idempotent** and waits through four phases (internal health → external port → stability → post-ready stabilisation) before reporting ready. Container ports: MSSQL 2017 → 1432, 2019 → 1434, 2022 → 1433, 2025 → 1435, Postgres → 5432. The shared password is `Lightweight!Test42`.

## When can you legitimately skip a DB?

Only when the test exercises a feature that the database *intrinsically* does not support — e.g., a SQL Server-only construct on SQLite. Use the `UNSUPPORTED_DATABASE(stmt, dbType)` macro from `src/tests/Utils.hpp`:

```cpp
TEST_CASE("merge upsert", "[migration]") {
    auto stmt = ConnectTestEnv();
    UNSUPPORTED_DATABASE(stmt, SqlServerType::SQLITE);
    // … MSSQL/Postgres-only path …
}
```

Do **not** use it to dodge a flaky failure or a Unicode bug — that's a real bug, fix it.
