# Lightweight — Agent Guidelines

## Project Architecture

Lightweight is a thin, modern **C++23 ODBC SQL API** for raw and high-level database access. It exposes a low-level layer (`SqlConnection`, `SqlStatement`, `SqlDataBinder<T>`) and a high-level `DataMapper` (record/relationship mapping). All public symbols live in the `Lightweight` namespace (alias `Light`).

Per-DBMS differences are funneled through a single dispatch point: `SqlQueryFormatter`. Business logic must be database-agnostic and let the formatter shape the SQL. Currently supported databases:

| Database | `SqlServerType` | Test env name (`.test-env.yml`) |
|----------|-----------------|---------------------------------|
| Microsoft SQL Server  | `MICROSOFT_SQL` | `mssql2017`, `mssql2019`, `mssql2022`, `mssql` |
| PostgreSQL            | `POSTGRESQL`    | `postgres`                       |
| SQLite3               | `SQLITE`        | `sqlite3`                        |

Bundled tools (in `src/tools/`): `dbtool` (general DB CLI), `ddl2cpp` (schema → C++ records generator), `lup2dbtool` (Lightweight migration plugin → dbtool migration), `large-db-generator` (test data).

## Component Map

| Path | Purpose |
|------|---------|
| `src/Lightweight/SqlConnection.{hpp,cpp}` | ODBC connection (driver, attrs, transaction, server-type detection) |
| `src/Lightweight/SqlStatement.{hpp,cpp}`  | Prepared statement, bind/execute/fetch, batched execute |
| `src/Lightweight/SqlDataBinder.hpp`       | Generic `SqlDataBinder<T>` traits — specialize per type |
| `src/Lightweight/DataBinder/`             | Binder specializations: strings, GUID, date/time, binary, numeric, variant, optional |
| `src/Lightweight/DataMapper/`             | High-level mapper: `Field`, `BelongsTo`, `HasMany`, `HasManyThrough`, `HasOneThrough`, connection pool, query builders |
| `src/Lightweight/QueryFormatter/`         | Per-DBMS formatters: `SqlServerFormatter.hpp`, `PostgreSqlFormatter.hpp`, `SQLiteFormatter.hpp` |
| `src/Lightweight/SqlQuery/`               | DSL: `Select`, `Insert`, `Update`, `Delete`, `Migrate`, `MigrationPlan`, `Core` |
| `src/Lightweight/SqlMigration.{hpp,cpp}`  | Migration runner; `SqlMigrationLock` for cross-process safety |
| `src/Lightweight/SqlBackup/` (+ `.hpp`)   | Schema/data backup framework |
| `src/Lightweight/Tools/`                  | Shared internal tools/utilities used by `src/tools/` binaries |
| `src/Lightweight/Zip/`                    | libzip wrapper used by backup |
| `src/Lightweight/Lightweight.cppm`        | C++20 module export aggregator (when `LIGHTWEIGHT_BUILD_MODULES=ON`) |
| `src/tests/`                              | Catch2 test suite (built as `LightweightTest`); `.test-env.yml` consumed via `--test-env=<name>` |
| `src/tools/`                              | `dbtool/`, `ddl2cpp.cpp`, `lup2dbtool/`, `large-db-generator/`, `LupMigrationsPlugin/`, `test_chinook.sh` |
| `src/examples/`                           | Compilable usage samples + Chinook integration |
| `src/benchmark/`                          | Performance benchmarks |
| `cmake/`                                  | `ClangTidy.cmake`, `Coverage.cmake`, `PedanticCompiler.cmake`, `Sanitizers.cmake`, `Version.cmake`, `Lightweight-config.cmake.in` |
| `docs/`                                   | User-facing documentation (`data-binder.md`, `dbtool.md`, `how-to.md`, `sql-migrations.md`, `sqlquery.md`, `usage.md`, `best-practices.md`, `lup-migration-plugin.md`, `sql-backup-format.md`) |
| `scripts/tests/docker-databases.py`       | Docker harness for MSSQL/Postgres test containers |
| `.github/workflows/build.yml`             | Authoritative CI matrix (presets × databases × sanitizers) |
| `.agent/`                                 | Agent deep-dives (architecture, databases, testing, C++ patterns) |

See `.agent/architecture.md` for a deeper structural walkthrough with canonical examples.

## Design Patterns & Principles

### Per-DBMS dispatch via `SqlQueryFormatter`
Never branch on `SqlServerType` in business logic. Instead, add a virtual method to `SqlQueryFormatter` and override it per DBMS in `QueryFormatter/{SqlServerFormatter,PostgreSqlFormatter,SQLiteFormatter}.hpp`. Canonical example: `PostgreSqlFormatter::QueryLastInsertId()` returns `"SELECT lastval();"` while the SQL Server override returns `"SELECT @@IDENTITY;"`.

### `SqlDataBinder<T>` specialization for new types
Bind/fetch/inspect for a new C++ type lives in a `SqlDataBinder<T>` specialization under `src/Lightweight/DataBinder/`. Reuse the shared helpers in `BasicStringBinder.hpp` (Unicode), `Core.hpp` (length indicators), and `UnicodeConverter.hpp` (u8/u16/wchar_t conversion) rather than re-implementing buffer growth or NULL handling.

### Error handling: `std::expected<T, SqlError>`
Prefer `std::expected<T, SqlError>` for fallible API surface. Chain monadically with `and_then`, `or_else`, `transform`, `transform_error` rather than nested `if`s. Reserve exceptions for programmer errors (precondition violation, misuse).

### Dependency injection
Tests must obtain a connection via the Catch2 fixture wired to `--test-env`, not by constructing one with hard-coded strings. Internally, anything that touches I/O, time, or randomness should be injectable so the unit tests in `src/tests/` can drive it.

### Data-driven design
Drive behaviour with descriptor tables, not scattered `switch (server)` ladders. The formatter dispatch is the canonical example; new dialect knobs should follow it.

### RAII for ODBC handles
`SQLHANDLE` resources (env, dbc, stmt) are owned by the Lightweight wrapper types and released via destructor. Do not allocate raw `SQLHANDLE` outside of those wrappers.

## C++ Coding Guidelines (self-contained — no external `cpp.md` required)

### Baseline (general C++23)
- **Data-driven design** — avoid hard-coded magic values; prefer tables/descriptors.
- **Dependency injection** — decouple components and improve testability.
- **Doxygen** on every new public function (params, return), class, struct, and member:
  ```cpp
  /// Short description.
  /// @param name Description.
  /// @return Description.
  ```
- **`const` correctness** throughout (refs, pointers, member functions).
- **C++23 features** — `constexpr`, `std::ranges`, `std::format`, `std::expected` and its monadic methods (`and_then`, `or_else`, `transform`, `transform_error`).
- **C-style loops are forbidden.** Use range-based `for`, `std::views::iota`, and other range views for generation/transformation.
- **`std::span`** for arrays and contiguous sequences.
- **`auto` type deduction** for readability; **structured bindings** for tuple-like returns.
- **`clang-format` after every change** — use the project `.clang-format`.
- **`clang-tidy` reports must be fixed at the source.** Never silence with `NOLINT` — address the underlying issue. The `linux-clang-debug` preset enables `clang-tidy` automatically.
- **All changes covered by unit tests.** Aim to **increase** coverage with every PR.
- **No raw owning pointers.** Use `std::unique_ptr` / `std::shared_ptr` for ownership; RAII for resources.
- **No new third-party dependencies** without strong justification.

### Lightweight-specific additions
- Public headers must be **self-contained** (compile standalone, no PCH dependency).
- Public symbols live in the `Lightweight` namespace (alias `Light`).
- Mark builders, queries, and `std::expected`-returning APIs `[[nodiscard]]`.
- Prefer `std::expected<T, SqlError>` over throwing on the public API surface.
- Per-DBMS branching belongs only inside `SqlQueryFormatter` overrides — never in business logic.
- For Unicode-bearing data binders, follow the `BasicStringBinder.hpp` contract (u8/u16/wchar_t round-trip via `UnicodeConverter`); see `.agent/cpp-guidelines.md` for the contract.
- ODBC `SQLHANDLE` lifetimes are bound to the wrapper types; never leak them across abstraction boundaries.

## Building

CMake presets live in `CMakePresets.json`. Common entry points:

```sh
# Linux — Clang Debug with PEDANTIC + ASan + UBSan + clang-tidy (the default agent preset)
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug

# Linux — GCC Debug
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug

# Linux — Coverage (HTML in out/build/linux-clang-coverage/)
cmake --preset linux-clang-coverage
cmake --build --preset linux-clang-coverage

# Linux — sanitizer-only presets
cmake --preset linux-clang-asan-ubsan
cmake --preset linux-clang-tsan

# Windows — MSVC CL Debug (requires VCPKG_ROOT in env)
cmake --preset windows-cl-debug
cmake --build --preset windows-cl-debug

# Windows — clang-cl Debug
cmake --preset windows-clangcl-debug
cmake --build --preset windows-clangcl-debug
```

`PEDANTIC_COMPILER_WERROR=ON` is the default for Windows presets — warnings break the build, fix them at the source.

## Testing

Catch2 tests live in `src/tests/` and produce a single `LightweightTest` binary. Database selection is parameterised:

```sh
LightweightTest --test-env=<name>     # picks ODBC_CONNECTION_STRING.<name> from .test-env.yml
LightweightTest --trace-sql --trace-odbc   # for diagnosing dialect/driver issues
```

`.test-env.yml` schema:
```yaml
ODBC_CONNECTION_STRING:
  sqlite3:    "DRIVER=SQLite3;Database=test.db"
  mssql2022:  "Driver={ODBC Driver 18 for SQL Server};SERVER=localhost;PORT=1433;UID=SA;PWD=...;TrustServerCertificate=yes;DATABASE=LightweightTest"
  postgres:   "Driver={PostgreSQL Unicode};Server=localhost;Port=5432;Uid=...;Pwd=...;Database=test"
```

### Always test against every supported database before claiming done

This project's CI matrix runs the full suite against **SQLite3**, **MS SQL Server 2017/2019/2022**, and **PostgreSQL** (`.github/workflows/build.yml` → `dbms_test_matrix`). An agent that runs only `sqlite3` locally and reports green has **not** finished the work — Unicode, type, NULL, and migration semantics differ in non-obvious ways across drivers.

Required local flow (use the Docker harness for the non-SQLite DBs):

```sh
# 1. Start MSSQL 2022 + Postgres (idempotent)
python3 scripts/tests/docker-databases.py --start --wait mssql2022 postgres

# 2. Make sure .test-env.yml has entries for sqlite3, mssql2022, postgres
#    (see schema above; the docker harness uses these connection strings)

# 3. Run the full suite three times
LightweightTest --test-env=sqlite3
LightweightTest --test-env=mssql2022
LightweightTest --test-env=postgres

# 4. Run the dbtool integration suite per DB
python3 src/tests/test_dbtool.py --test-env sqlite3
python3 src/tests/test_dbtool.py --test-env mssql2022
python3 src/tests/test_dbtool.py --test-env postgres
```

If a database is **deliberately** skipped (e.g., feature genuinely doesn't apply, container can't be started in the environment), call it out in the PR summary with the reason. The `UNSUPPORTED_DATABASE(stmt, dbType)` macro in `src/tests/Utils.hpp` is the in-test escape hatch for genuinely unsupported features per DBMS — use it only when the limitation is intrinsic, not as a way to dodge a failure.

When touching anything in `DataBinder/`, also exercise both `u8`/`u16`/`wchar_t` paths (the `WideChar` typedef in `Utils.hpp` selects the platform-appropriate UTF-16 code unit). See `.agent/testing.md` and `.agent/databases.md` for deeper guidance.

## Adding Features

### New `SqlDataBinder<T>` specialization
1. Create `src/Lightweight/DataBinder/<Type>.hpp` with `template <> struct SqlDataBinder<T> { … }`.
2. Implement `InputParameter`, `OutputColumn`, `GetColumn`, and (if needed) `Inspect`.
3. Reuse helpers from `BasicStringBinder.hpp` / `Core.hpp` / `UnicodeConverter.hpp`.
4. Add tests in `src/tests/DataBinderTests.cpp` covering bind, fetch, NULL, and (for strings) Unicode round-trip.
5. **Run the suite against all three databases.**

### New SQL surface (DSL / migration / query primitive)
1. Extend the relevant header in `src/Lightweight/SqlQuery/` (`Select`, `Insert`, `Update`, `Delete`, `Migrate`, `MigrationPlan`, `Core`).
2. If the SQL text differs by DBMS, add a virtual hook to `SqlQueryFormatter` and override per-DBMS in `QueryFormatter/`.
3. Update or add tests in `src/tests/QueryBuilderTests.cpp` / `MigrationTests.cpp`.
4. If user-visible: update the matching `docs/*.md` page.
5. **Run the suite against all three databases.**

### New tool or `dbtool` subcommand
1. Add the source under `src/tools/<tool>/` and register in `src/tools/CMakeLists.txt`.
2. Drive it by descriptors / config tables, not hard-coded option ladders.
3. Add coverage in `src/tests/test_dbtool.py` (or its peers).
4. Document in `docs/dbtool.md` (or a new file).

### New per-DBMS feature gate
Add the capability check to `SqlQueryFormatter` (e.g., `bool SupportsX() const`); use it in callers. Do **not** use `if (server == SqlServerType::X)` in non-formatter code.

## Workflow (post-implementation checklist)

1. Run `clang-format` on every changed `.hpp` / `.cpp` (project `.clang-format`).
2. Build the relevant preset (`linux-clang-debug` for full coverage of warnings + clang-tidy + ASan/UBSan; `windows-clangcl-debug` if Windows-side changes). Resolve all warnings — `PEDANTIC_COMPILER_WERROR` is on.
3. **Run the full test suite against every supported database.** At minimum: `sqlite3`, `mssql2022`, `postgres`. Use `scripts/tests/docker-databases.py --start --wait mssql2022 postgres` to spin up the non-SQLite containers. Document any DB skipped with the reason.
4. Run `clang-tidy` (it runs automatically under `linux-clang-debug`); fix every finding at the source — never `NOLINT`.
5. If touching public headers or user-visible behaviour, update `docs/` (`data-binder.md`, `sql-migrations.md`, `dbtool.md`, `usage.md`, `how-to.md`, `best-practices.md`, etc.).
6. Run the sanitizer presets when relevant (`linux-clang-asan-ubsan`, `linux-clang-tsan`) — required for changes touching pools, threading, or raw buffer arithmetic.
7. Execute `/simplify` to reduce duplication and code-quality issues. If the simplify pass surfaces issues out of scope, **ask** the user whether to address them in this PR; if yes, include them; if no, ignore and move on.
8. In the PR / commit summary include:
   - **Performance impact** — with measurements where possible (use `src/benchmark/` if applicable).
   - **Risk assessment** — what could break per DBMS, threading, ABI, ODBC version compatibility.
   - **Code coverage** — results from `linux-clang-coverage` if coverage was a goal.
   - **Databases tested** — explicit list with versions (`sqlite3`, `mssql2022 (Docker)`, `postgres (Docker 16.4)`).
