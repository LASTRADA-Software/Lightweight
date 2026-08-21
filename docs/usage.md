# Usage Examples

## Configure default connection information to the database

To connect to the database you need to provide connection string that library uses to establish connection and you can check if it is alive in the following way
```cpp
SqlConnection::SetDefaultConnectionString(SqlConnectionString { 
    .value = std::format("DRIVER=SQLite3;Database=test.sqlite")
});

auto sqlConnection = SqlConnection {};
if (!sqlConnection.IsAlive())
{
    std::println("Failed to connect to the database: {}",
                 SqlErrorInfo::fromConnectionHandle(sqlConnection.NativeHandle()));
    std::abort();
}
```

## Raw SQL Queries

To directly make a call to the database use `ExecuteDirect` function, for example
```cpp
auto stmt = SqlStatement {};
stmt.ExecuteDirect(R"("SELECT "a", "b", "c" FROM "That" ORDER BY "That"."b" DESC)"));
while (stmt.FetchRow())
{
   auto a = stmt.GetColumn<int>(1);
   auto b = stmt.GetColumn<int>(2);
   auto c = stmt.GetColumn<int>(3);
   std::println("{}|{}|{}", a, b,c);
}
```

## Transparent block-prefetch (fewer network round-trips)

Classic per-row fetch loops like the one above issue **one `SQLFetch` per row**, i.e. one network
round-trip per row. On TCP-backed drivers (Microsoft SQL Server, PostgreSQL) that latency dominates the
wall-clock time of large result sets.

Lightweight transparently reduces these round-trips: on the first `FetchRow()` of a result set it
inspects the columns and, when eligible, fetches whole **blocks** of rows per `SQLFetchScroll`
round-trip (ODBC row-array binding) and serves your `FetchRow()` / `GetColumn<T>()` calls from that
buffer. **No code change is required** — the loops above, `SqlRowIterator<T>`, `SqlVariantRowCursor`
and the `DataMapper` all benefit automatically.

The depth is a connection-level setting (default `Lightweight::PrefetchDepthDefault`, 1000 rows). A
value `<= 1` disables prefetch and restores one `SQLFetch` per row:

```cpp
auto conn = SqlConnection {};
conn.SetDefaultPrefetchDepth(2000); // request up to 2000 rows per SQLFetchScroll round-trip
conn.SetDefaultPrefetchDepth(1);    // disable prefetch for this connection
```

Prefetch engages only for result sets whose columns are **fixed-width numeric, temporal, or `GUID`**
types (integers, floating point, `DATE`, `TIMESTAMP`/`DATETIME`, and native `GUID`/`uniqueidentifier`/
`uuid`) on drivers that support native row-array fetching (Microsoft SQL Server, PostgreSQL, SQLite).
Result sets that contain character/text, `NUMERIC`/`DECIMAL`, `TIME`, binary or LOB columns transparently
keep the per-row path: faithful block reconstruction of those is not achievable uniformly across backends
(e.g. Microsoft SQL Server returns narrow text in the client codepage rather than UTF-8, and SQLite's
dynamic typing reports text/`NUMERIC` columns with an unreliable, unenforced size), so the dedicated
single-row binders handle them. Memory is bounded to a few MB per active cursor (the depth is auto-clamped
to that budget), and prefetch reads ahead up to one block, so a loop that stops early over-reads at most
one block.

## Prepared Statements

You can also use prepared statements to execute queries, for example
```cpp

struct Record { int a; int b; int c; };
auto conn = SqlConnection {};
auto stmt = SqlStatement { conn };
stmt.Prepare("SELECT a, b, c FROM That WHERE a = ? OR b = ?");
auto cursor = stmt.Execute(42, 43);

auto record = Record {};
cursor.BindOutputColumns(&record.a, &record.b, &record.c);
while (cursor.FetchRow())
    std::println("{}|{}|{}", record.a, record.b, record.c);
```

## Prepared-statement cache (fewer prepare round-trips)

`Prepare()` sends the SQL text to the server so it can parse and plan it — one network round-trip on
Microsoft SQL Server and PostgreSQL, paid again every time the same query text is prepared. Applications
built on `DataMapper` or the query builders re-prepare the same handful of statements constantly, because
each call site creates its own short-lived `SqlStatement`.

A connection can keep the already-prepared handles alive in a bounded LRU pool, so re-preparing a query
it has seen before skips `SQLPrepare` entirely:

```cpp
auto conn = SqlConnection {};
conn.SetPreparedStatementCacheCapacity(Lightweight::PreparedStatementCacheCapacitySuggested); // 64
```

The cache is **opt-in** (default capacity `Lightweight::PreparedStatementCacheCapacityDefault`, i.e. `0`
= disabled) but, once enabled, **transparent**: every `SqlStatement` on that connection participates, so
`DataMapper`, the `SqlQuery` DSL, and raw `Prepare()` call sites all benefit without a code change. It
can also be requested up-front via `SqlConnectionDataSource::preparedStatementCacheCapacity`.

For pooled applications, configure it on the pool rather than on each acquired connection.
`PoolConfig::preparedStatementCacheCapacity` is applied to every connection the pool creates, so no
call site has to remember to enable it:

```cpp
constexpr auto MyPoolConfig = Lightweight::PoolConfig {
    .initialSize = 4,
    .maxSize = 16,
    .growthStrategy = Lightweight::GrowthStrategy::BoundedOverflow,
    .preparedStatementCacheCapacity = Lightweight::PreparedStatementCacheCapacitySuggested,
};
auto pool = Lightweight::Pool<MyPoolConfig> {};
```

The global pool returned by `GlobalDataMapperPool()` takes the same setting from the CMake option
`LIGHTWEIGHT_POOL_PREPARED_STATEMENT_CACHE_CAPACITY` (default `0`, i.e. disabled), alongside the
existing `LIGHTWEIGHT_POOL_INITIAL_SIZE`, `LIGHTWEIGHT_POOL_MAX_SIZE` and
`LIGHTWEIGHT_POOL_GROWTH_STRATEGY`.

A pooled connection keeps its warmed handles across acquires, since the pool hands back the same live
connection rather than reconnecting it. Note that the capacity is **per connection**: the cache is a set
of ODBC statement handles owned by one connection's `SQLHDBC` and can never be shared with another
connection, so each pooled connection warms up separately and a fully warmed pool holds up to
`maxSize * preparedStatementCacheCapacity` prepared statements on the server.

How it works: a handle is *checked out* while a statement uses it and returned to the pool when that
statement is re-prepared or destroyed. Two statements preparing the same text at the same time therefore
each get their own handle. When the pool exceeds its capacity the least recently returned handle is
freed — a bound that matters because several backends cap the number of live prepared statements per
session. Statistics are available for diagnostics:

```cpp
auto const& stats = conn.PreparedStatementCache().Stats();
std::println("prepare hits={} misses={} evictions={}", stats.hits, stats.misses, stats.evictions);
```

**Schema changes invalidate cached plans.** A pooled handle carries the plan the driver derived from the
schema as it was at preparation time, so DDL must drop it:

```cpp
conn.ClearPreparedStatementCache();
```

Lightweight does this for you where it owns the DDL — `SqlStatement::MigrateDirect()` and the
`MigrationManager` executor clear the cache after applying a script — and disconnecting or reconnecting a
connection clears it as well. Raw DDL you send through `ExecuteDirect()` is your responsibility. A single
statement that must never reuse a plan opts out:

```cpp
auto stmt = SqlStatement { conn };
stmt.SetPreparedStatementCaching(SqlPreparedStatementCaching::Disabled);
```

The cache is active on Microsoft SQL Server, PostgreSQL and SQLite. On any other backend
`SqlConnection::SupportsPreparedStatementReuse()` is false and the requested capacity stays inactive, so
the same setup code is safe to run everywhere.

## SQL Query Builder

Or construct statement using `SqlQueryBuilder`
```cpp

auto stmt = SqlStatement { };
auto const sqlQuery =  stmt.Query("That")
                .Select()
                .Fields("a", "b")
                .Field("c")
                .OrderBy(SqlQualifiedTableColumnName { .tableName = "That", .columnName = "b" },
                         SqlResultOrdering::DESCENDING)
                .All()
stmt.Prepare(sqlQuery);
stmt.Execute();

while(stmt.FetchRow())
{
    auto a = stmt.GetColumn<int>(1);
    auto b = stmt.GetColumn<int>(2);
    auto c = stmt.GetColumn<int>(3);
}


```

For more info see `SqlQuery` and `SqlQueryFormatter` documentation

## High level Data Mapping

The `DataMapper` provides a higher-level abstraction for interacting with databases. It simplifies operations by automatically creating tables based on the specified type and enabling data retrieval through straightforward method calls.
For more info see `DataMapper` documentation
```cpp
// Define a person structure, mapping to a table
// The field members are mapped to the columns in the table,
// and the Field<> template parameter specifies the type of the column.
// Field<> is also used to track what fields are modified and need to be updated.
struct Person
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<25>> name;
    Field<bool> is_active { true };
    Field<std::optional<int>> age;
};

void CRUD(DataMapper& dm)
{
    // Creates the table if it does not exist
    dm.CreateTable<Person>();

    // Create a new person
    auto person = Person {};
    person.name = "John Doe";
    person.is_active = true;
    dm.Create(person);

    // Update the person
    person.age = 25;
    dm.Update(person);

    // Query the person
    if (auto const po = dm.QuerySingle<Person>(person.id); po)
        std::println("Person: {} ({})", po->name, DataMapper::Inspect(*po));

    // Query all persons
    auto const persons = dm.Query<Person>(); 

    // Iterate over all persons 
    auto stmt = SqlStatement { dm.Connection() };
    for(const auto& person: SqlRowIterator<Person>(stmt))
        std::println("|{}|{}|", person.name, person.age);

    // Delete the person
    dm.Delete(person);
}
```

### Batched insert and update

To insert or update many records efficiently, use `CreateAll` and `UpdateAll`. They prepare a single
statement once and submit the whole batch, preferring native ODBC row-wise array binding (one
`SQLExecute`, zero-copy) when every column is a fixed-width type — primitives, `SqlDate`/`SqlTime`/
`SqlDateTime`, `SqlNumeric`, inline fixed-capacity strings (`SqlAnsiString`/`SqlFixedString`), or
`std::optional` of a fixed non-numeric type (including nullable fixed-capacity strings) — and the driver
supports parameter arrays. Records with variable-length columns (e.g. `std::string`) transparently fall
back to a prepare-once + per-row execute, which is still far cheaper than calling `Create`/`CreateExplicit`
in a loop (those re-prepare per row).

```cpp
void BulkInsert(DataMapper& dm, std::vector<Person> const& people)
{
    dm.CreateTable<Person>();

    // Inserts all records with a single prepared statement (native batch when possible).
    dm.CreateAll(people); // accepts any contiguous range: std::vector, std::array, std::span, C array

    // UpdateAll writes all storable non-primary-key columns, matched on the primary key.
    dm.UpdateAll(people);
}
```

> Note: `CreateAll`/`UpdateAll` do not write primary keys, relations, or modified-state back onto the
> records (treat them as write-only inputs), and `UpdateAll` writes a uniform set of columns for every
> row rather than only the per-record modified ones. The range must be contiguous.

## Simple row retrieval via structs

When only read access is needed, you can use a simple `struct` to represent the row,
and also do not need to wrap the fields into `Field<>` template.
The `struct` must have fields that match the columns in the query. The fields can be of any type that can be converted from the column type. The struct can have more fields than the columns in the query, but the fields that match the columns must be in the same order as the columns in the query.

```cpp
struct SimpleStruct
{
    uint64_t pkFromA;
    uint64_t pkFromB;
    SqlAnsiString<30> c1FromA;
    SqlAnsiString<30> c2FromA;
    SqlAnsiString<30> c1FromB;
    SqlAnsiString<30> c2FromB;
};

void SimpleStructExample(DataMapper& dm)
{
    auto const records = dm.Query<SimpleStruct>(
        "SELECT A.pk, B.pk, A.c1, A.c2, B.c1, B.c2 FROM A LEFT JOIN B ON A.pk = B.pk");

    for (auto const& obj: records)
        std::println("{}", DataMapper::Inspect(obj));
}
```
