# Testing deep-dive

## Layout

| Path | Purpose |
|------|---------|
| `src/tests/CoreTests.cpp` | Core SQL API |
| `src/tests/DataBinderTests.cpp` | `SqlDataBinder<T>` specializations (start here for new types) |
| `src/tests/DataMapper/` | High-level mapper, relations, query builders |
| `src/tests/QueryBuilderTests.cpp` | DSL surface |
| `src/tests/MigrationTests.cpp`, `MigrationReflectionTests.cpp` | Migrations & reflection |
| `src/tests/CxxModelPrinterTests.cpp` | `ddl2cpp` output shape |
| `src/tests/Lup2DbtoolTests.cpp` | `lup2dbtool` |
| `src/tests/UnicodeConverterTests.cpp` | u8/u16/wchar_t conversion |
| `src/tests/UtilsTests.cpp` | Internal utilities |
| `src/tests/SqlBackup/` | Backup framework |
| `src/tests/Zip/` | libzip wrapper |
| `src/tests/dbtool/` + `test_dbtool.py` | `dbtool` integration (Python driver) |
| `src/tests/Utils.{hpp,cpp}` | Shared fixture, `UNSUPPORTED_DATABASE` macro, `WideChar` typedef |
| `src/tests/LargeTestDatabase/` | Generators for large-DB perf tests |
| `src/tests/dummy_migration_plugin*` | Plugin integration fixtures |

The test binary is `LightweightTest` (Catch2 single-binary). Filter with Catch2 tags / patterns:

```sh
LightweightTest "[model]"                    # all model tests
LightweightTest "SqlVariant: SqlGuid"        # one test
LightweightTest --reporter compact           # condensed output
LightweightTest --trace-sql --trace-odbc     # diagnose driver behaviour
```

## `.test-env.yml`

The connection-string registry consumed by `--test-env=<name>`:

```yaml
ODBC_CONNECTION_STRING:
  sqlite3:    "DRIVER=SQLite3;Database=test.db"
  mssql2017:  "Driver={ODBC Driver 17 for SQL Server};SERVER=localhost;PORT=1432;UID=SA;PWD=Lightweight!Test42;TrustServerCertificate=yes;DATABASE=LightweightTest"
  mssql2019:  "Driver={ODBC Driver 18 for SQL Server};SERVER=localhost;PORT=1434;UID=SA;PWD=Lightweight!Test42;TrustServerCertificate=yes;DATABASE=LightweightTest"
  mssql2022:  "Driver={ODBC Driver 18 for SQL Server};SERVER=localhost;PORT=1433;UID=SA;PWD=Lightweight!Test42;TrustServerCertificate=yes;DATABASE=LightweightTest"
  postgres:   "Driver={PostgreSQL Unicode};Server=localhost;Port=5432;Uid=postgres;Pwd=Lightweight!Test42;Database=test"
```

Add new test-env entries here — never hard-code connection strings inside test sources.

## Required local flow

```sh
# 1. Start non-SQLite DBs
python3 scripts/tests/docker-databases.py --start --wait mssql2022 postgres

# 2. Run Catch2 tests against each
LightweightTest --test-env=sqlite3
LightweightTest --test-env=mssql2022
LightweightTest --test-env=postgres

# 3. Run dbtool integration tests against each
python3 src/tests/test_dbtool.py --dbtool ./out/build/<preset>/target/dbtool \
    --plugins-dir ./out/build/<preset>/target/plugins \
    --test-env sqlite3
python3 src/tests/test_dbtool.py --dbtool … --test-env mssql2022
python3 src/tests/test_dbtool.py --dbtool … --test-env postgres
```

## Sanitizers & valgrind

| Preset | What it gives you |
|--------|-------------------|
| `linux-clang-debug` | ASan + UBSan + clang-tidy + pedantic warnings |
| `linux-clang-asan-ubsan` | ASan + UBSan, no tidy (faster build for repro) |
| `linux-clang-tsan` | ThreadSanitizer (use for pool/migration-lock changes) |
| `linux-clang-coverage` / `linux-gcc-coverage` | LCOV / gcov |

CI runs valgrind on the SQLite3 matrix entry only:

```sh
valgrind --error-exitcode=1 --leak-check=full --leak-resolution=high --num-callers=64 \
    LightweightTest --test-env=sqlite3
```

ASan/UBSan environment used in CI:

```sh
ASAN_OPTIONS="detect_leaks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1"
UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1:suppressions=.github/ubsan.suppressions"
TSAN_OPTIONS="second_deadlock_stack=1"
```

## Catch2 conventions

- Test files end in `*Tests.cpp` (note the trailing `s`). New test files must be added to `src/tests/CMakeLists.txt`.
- Tag conventions used in this repo: `[model]`, `[TableFilter]`, `[migration]`, `[binder]`, `[unicode]`. Reuse existing tags before inventing new ones.
- The `WideChar` typedef in `src/tests/Utils.hpp` selects the platform-appropriate UTF-16 code unit (`wchar_t` on Windows, `char16_t` on Linux). Use it (and the `WTEXT(x)` literal macro) for any portable UTF-16 test data.
- The `Member(x)` macro abstracts over C++20 member pointers vs. C++26 reflection (`^^x`). Use it in test code that should compile under both reflection modes.
- The `UNSUPPORTED_DATABASE(stmt, dbType)` macro is the only sanctioned way to skip a test for a specific DBMS. Use it sparingly and only for genuinely unsupported features.

## When tests fail on Windows

The harness suppresses Windows blocking error dialogs (commit `bd5cd8ae`). If a test crash brings up a Watson dialog, set `MSVC_DISABLE_WATSON=1 _NO_DEBUG_HEAP=1` in the env. The `test_dbtool.py` runner decodes stdout as UTF-8 and disables Python output buffering (commit `6d8c360f`) so `dbtool` log output remains intact across the bridge.
