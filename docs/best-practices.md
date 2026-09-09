# Best Practices

## Introduction

This document provides a set of best practices for using the API.  
These best practices are based on the experience of the API team and feedback from API users, as well as learnings from
the underlying technologies.

## Common Best Practices

### Use the DataMapper API

The `DataMapper` API provides a high-level abstraction for working with database tables.  
It simplifies the process of querying, inserting, updating, and deleting data from the database while retaining
performance and flexibility.

### Keep Data Model and Business Logic Separate

Keep the data model and business logic separate to improve the maintainability and scalability of your application.

Remember to also keep frontend (e.g., GUI) and backend (e.g., API) separate.

### Use Transactions with Care

Use transactions to group multiple database operations into a single unit of work.  
This ensures that all operations are either committed or rolled back together.

However, be careful when using transactions, as they can affect performance severely if not used properly.

### Binding Output Parameters

First things first, always favor using the `DataMapper` API over manual querying and binding of columns.

However, when not using the `DataMapper` API, you need to retrieve the result manually, either via
`SqlStatement::BindOutputColumns()` or afterwards by fetching the columns individually.  
It is always highly recommended to pre-bind to avoid unnecessary memory allocations and copying.

With this, it is sufficient to call `SqlStatement::BindOutputColumns()` once, and then you can reuse the result
throughout many `SqlStatement::FetchRow()` calls.

The pitfall here is that if you are using `std::optional<T>` column types, you **MUST** rebind the result columns before
each fetch operation. If there are no nullable values, you do not have to.

```cpp
struct MixedNullRow
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id {};
    Field<std::optional<SqlAnsiString<30>>> name;
    Field<std::optional<int>> age;
};

void ForEachData(SqlStatement& stmt, auto&& onRow)
{
    stmt.ExecuteDirect(stmt.Query(RecordTableName<MixedNullRow>)
                           .Select()
                           .Fields({ "id"sv, "name"sv, "age"sv })
                           .All());

    auto currentRow = MixedNullRow {};

    // Bind output columns for the first time
    stmt.BindOutputColumns(&currentRow.id, &currentRow.name, &currentRow.age);

    while (stmt.FetchRow())
    {
        onRow(currentRow);

        // Bind output columns for the next fetch.
        // (ONLY necessary when using std::optional<T> in the columns)
        stmt.BindOutputColumns(&currentRow.id, &currentRow.name, &currentRow.age);
    }
}
```

## SQL Driver-Related Best Practices

### Query Result Row Columns in Order

When querying the result set, always access the columns in the order they are returned by the query.  
At least the MS SQL Server driver has issues when accessing columns out of order.
Carefully check the driver documentation for the specific behavior of the driver you are using.

This can be avoided when using the `DataMapper` API, which always maps the result in order and as efficiently as
possible.

## Performance Is Key

### Use Native Column Types

Use the native column types provided by the API for the columns in your tables.  
This will help to improve the performance of your application by reducing the overhead of data conversion.

The existence of `SqlVariant` in the API allows you to store any type of data in a single column, but it is recommended
to use the native column types whenever possible.

### Use Prepared Statements

Prepared statements are precompiled SQL statements that can be executed multiple times with different parameters.  
Using prepared statements can improve the performance of your application by reducing the overhead of parsing,
analyzing, and compiling SQL queries.

### Use Pagination or Infinite Scrolling

When querying large result sets, use pagination or infinite scrolling to limit the number of results returned in a
single response.  
This will help to reduce the response time and the load on the server, and improve the performance of your
application.

### Load relations for a whole result set, not per record

Touching a relation on each record of a result set issues one query per record — the N+1 problem. Name
the relation on the query instead, and it is resolved for the entire batch in a constant number of
queries:

```cpp
auto albums = dm.Query<Album>().With<&Album::tracks>().All(); // 2 queries, not 1 + N
```

A nested relation needs the whole path named — `.With<&Track::album, &Album::artist>()` — because
each record holds its own copy of the target, so one level of eager loading leaves the level below it
loading per record. `DataMapperOptions { .eagerLoadDepth = N }` loads everything reachable instead,
at the cost of fetching more than you asked for.

See [Eager loading of relations](usage.md). Two things compound with it:

- **Index your foreign keys.** `CreateTable<Record>()` emits an index for every `BelongsTo` column,
  because no supported engine indexes a foreign key implicitly. Tables created by hand, or by an older
  version of Lightweight, need that index added — without it every relation query is a full table scan.
- **Prove the absence of N+1 in tests.** A `SqlLogger` subclass counting `OnPrepare`/`OnExecuteDirect`
  turns "this endpoint issues two queries" into an assertion instead of an assumption.

### Let block-prefetch cut network round-trips

Per-row fetch loops issue one `SQLFetch` (one network round-trip) per row. Lightweight transparently
fetches rows in blocks (ODBC row-array binding) so a large result set costs `ceil(rows / depth)`
round-trips instead of one per row — see [Transparent block-prefetch](usage.md). It is on by default
(`Lightweight::PrefetchDepthDefault`, 1000 rows) and tuned per connection:

```cpp
connection.SetDefaultPrefetchDepth(1000); // rows per SQLFetchScroll round-trip; <= 1 disables
```

Keep in mind:

- It engages only for **fixed-width numeric/temporal** result sets; result sets with character,
  `GUID`, `NUMERIC`, `TIME`, binary or LOB columns transparently stay on the per-row path.
- An active cursor reads ahead up to one block and holds a few MB of buffers, so set the depth to `1`
  on a connection used for cursors you intend to abandon early or where memory is tight.
- It does not change results — values are identical to the per-row path.

### Enable the prepared-statement cache for recurring queries

Every fresh prepare costs a server-side parse and plan on Microsoft SQL Server and PostgreSQL — charged
at the statement's first execute, not inside `SQLPrepare` — and the `DataMapper` / query-builder layers
re-prepare the same handful of statements on every call because each call site builds a fresh
`SqlStatement`. A connection can pool the already-prepared handles so repeats re-execute one instead —
see [Prepared-statement cache](usage.md):

```cpp
connection.SetPreparedStatementCacheCapacity(Lightweight::PreparedStatementCacheCapacitySuggested);
```

Measured against Docker-local servers, this is worth **4.2x** on PostgreSQL, **1.7x** on MS SQL Server and
**1.4x** on SQLite for a repeatedly re-prepared single-row read, and **3.8x / 1.3x / 1.1x** for the same
query driven through `DataMapper::Query<>()`. What it removes is network round-trips — about **2.8 per
query** on PostgreSQL and **exactly one** on MS SQL Server — so the further away the server, the more it
is worth: across a 50 ms link the same PostgreSQL read goes from 202 ms to 61 ms. The full tables, the
latency sweep and the workloads that gain nothing are in [usage.md](usage.md).

Keep in mind:

- It is **opt-in** (default capacity `0`), and transparent once enabled — no call-site changes.
- It pays off where a **short-lived statement re-prepares a text the connection has seen** — the shape the
  query builders produce. Code that already drives one long-lived `SqlStatement` through one query text
  (as `DataMapper::QuerySingle()` does) reuses its own handle regardless and gains nothing.
- On **SQLite it is roughly break-even**: with no network there is no round-trip to save, and
  `DataMapper::Create()` measures ~8% slower because its last-insert-id `ExecuteDirect()` has to park the
  prepared handle. Enable it for the network-backed engines.
- Size it to your working set of distinct query texts. Too small and the LRU thrashes; too large and you
  risk the server-side cap on live prepared statements per session.
- With a connection pool, set it once via `PoolConfig::preparedStatementCacheCapacity` (or the
  `LIGHTWEIGHT_POOL_PREPARED_STATEMENT_CACHE_CAPACITY` CMake option for `GlobalDataMapperPool()`) instead
  of per acquired connection. Budget for the whole pool: handles cannot be shared between connections, so
  a warmed pool holds up to `maxSize * preparedStatementCacheCapacity` of them, and every connection pays
  its own warm-up — measured, exactly `connections * distinct query texts` prepares, paid once. Pool size
  does not dilute the steady-state win (one connection and four reach the same speed-up), but short-lived
  work does: at six operations per connection the PostgreSQL gain fell from 2.55x to 1.40x.
- **Make sure the pool does not overflow.** Under `GrowthStrategy::BoundedOverflow`, a connection created
  past the idle set is destroyed when returned, taking its warmed cache with it, so the pool never stops
  re-preparing: the PostgreSQL gain drops from 2.18x to 1.48x and SQLite's to nothing. Raising `maxSize`
  to cover the real concurrency is worth more than any cache capacity.
- A pooled handle carries the plan derived from the schema at preparation time. Call
  `ClearPreparedStatementCache()` after raw DDL; migrations and `MigrateDirect()` already do.
- Statements whose plan must be re-derived opt out via
  `SqlStatement::SetPreparedStatementCaching(SqlPreparedStatementCaching::Disabled)`.

## SQL Server Variation Challenges

### 64-bit Integer Handling in Oracle Database

Oracle database does not support 64-bit integers natively.  
When working with 64-bit integers in Oracle database, you need to use the `SqlNumeric` column types.