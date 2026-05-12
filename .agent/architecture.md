# Architecture deep-dive

## Layers

```
                ┌─────────────────────────────────────────────┐
 user code ──▶  │  DataMapper (Field, BelongsTo, HasMany,…)   │  high-level
                ├─────────────────────────────────────────────┤
                │  SqlQuery DSL (Select/Insert/Update/Delete) │
                │  SqlMigration / SqlScopedLock               │
                ├─────────────────────────────────────────────┤
                │  SqlStatement / SqlConnection               │  low-level
                │  SqlDataBinder<T> (per-type bind/fetch)     │
                ├─────────────────────────────────────────────┤
                │  SqlQueryFormatter — per-DBMS dispatch      │
                │   ├── SqlServerFormatter                    │
                │   ├── PostgreSqlFormatter                   │
                │   └── SQLiteFormatter                       │
                ├─────────────────────────────────────────────┤
                │  ODBC (unixODBC / Windows ODBC)             │  driver
                └─────────────────────────────────────────────┘
```

Anything that varies between DBMSes belongs in `SqlQueryFormatter`. Business logic above must be database-agnostic.

## Canonical examples

### Per-DBMS formatter override
File: `src/Lightweight/QueryFormatter/PostgreSqlFormatter.hpp`

```cpp
class PostgreSqlFormatter final: public SQLiteQueryFormatter
{
  public:
    [[nodiscard]] std::string QueryLastInsertId(std::string_view) const override
    {
        return "SELECT lastval();";
    }

    [[nodiscard]] std::string_view DateFunction() const noexcept override
    {
        return "CURRENT_DATE";
    }
    // … BinaryLiteral, BuildColumnDefinition, DropTable (CASCADE) overrides …
};
```

To add a new dialect-sensitive primitive:
1. Add a `[[nodiscard]] virtual` method in `SqlQueryFormatter.hpp` with the most reasonable default.
2. Override it in any formatter where the default is wrong.
3. Have the caller request the SQL fragment from the formatter — never inline a `switch` on `SqlServerType`.

### `SqlDataBinder<T>` specialization shape
Files: `src/Lightweight/SqlDataBinder.hpp` (primary template), `src/Lightweight/DataBinder/*.hpp` (specializations).

Each specialization typically provides:
- `static SQLRETURN InputParameter(SQLHSTMT, SQLUSMALLINT, T const&, SqlDataBinderCallback&)`
- `static SQLRETURN OutputColumn(SQLHSTMT, SQLUSMALLINT, T*, SQLLEN*, SqlDataBinderCallback&)`
- `static SQLRETURN GetColumn(SQLHSTMT, SQLUSMALLINT, T*, SQLLEN*, SqlDataBinderCallback const&)`
- `static std::string Inspect(T const&)` (optional — used by tracing and `DataMapper::Inspect`)

For Unicode-bearing strings, reuse `BasicStringBinder.hpp` helpers (`GetRawColumnArrayData`, `GetColumnUtf16`, `BindOutputColumnNonUtf16Unicode`) and `UnicodeConverter.hpp` rather than re-implementing buffer growth, NULL handling, or transcoding.

### `DataMapper` field & relationship primitives
- `Field<T, …Modifiers>` — one column with optional `PrimaryKey`, `SqlRealName`, default value.
- `BelongsTo<&Other::id, SqlRealName{"fk_col"}>` — many-to-one (lazy-loads via dereference).
- `HasMany<&Child::parent_id>` — one-to-many.
- `HasManyThrough<&Through::a_id, &Through::b_id>` — many-to-many via join table.
- `HasOneThrough<…>` — one-to-one via intermediate.

Records are plain aggregates of these field/relation types — there is no required base class. Reflection (C++20 member pointers, or C++26 reflection when `LIGHTWEIGHT_CXX26_REFLECTION`) drives column enumeration.

### Connection pool
File: `src/Lightweight/DataMapper/Pool.{hpp,cpp}`. The pool is the canonical way to share connections across threads — never store a `SqlConnection` as a long-lived bare member.

### Migrations
- DSL: `src/Lightweight/SqlQuery/Migrate.{hpp,cpp}`, `MigrationPlan.{hpp,cpp}`.
- Runner: `src/Lightweight/SqlMigration.{hpp,cpp}`.
- Cross-process lock: `src/Lightweight/SqlScopedLock.{hpp,cpp}` (the
  generic distributed-lock RAII wrapper) plus
  `src/Lightweight/SqlAdvisoryLock.hpp` (the per-dialect handler interface
  selected via `SqlQueryFormatter::AdvisoryLockOps()`).
- The `dbtool` subcommand and the `lup2dbtool` adapter live in `src/tools/`.

## Where reflection lives

`Lightweight.cppm` exports the public surface as a C++20 module when `LIGHTWEIGHT_BUILD_MODULES=ON`. The `Member(x)` macro in `src/tests/Utils.hpp` (and similar in production headers) selects between C++20 member pointers (`&x`) and C++26 reflection (`^^x`) depending on `LIGHTWEIGHT_CXX26_REFLECTION`.

## Key types to recognise

| Type | Role |
|------|------|
| `SqlServerType` | Enum: `MICROSOFT_SQL`, `POSTGRESQL`, `SQLITE`, `MYSQL`, `UNKNOWN`. Derived from the connected driver |
| `SqlConnectInfo` | Parsed connection-string descriptor |
| `SqlError` / `SqlErrorDetection` | Error reporting; pair with `std::expected<T, SqlError>` in new APIs |
| `SqlScopedTraceLogger` / `SqlLogger` | Tracing hooks (driven by `--trace-sql --trace-odbc`) |
| `ThreadSafeQueue` | Internal MPMC queue used by the pool |
