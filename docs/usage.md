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

## Connection encryption

By default Lightweight does not touch the driver's TLS configuration — whatever the ODBC driver, the
DSN, or the connection string already says stays in force. To take explicit control, set the
`encryption` field of `SqlConnectionDataSource`:

```cpp
SqlConnection::SetDefaultDataSource(SqlConnectionDataSource {
    .datasource = "MyServerDSN",
    .username = "user",
    .password = "password",
    .encryption = SqlEncryptionMode::Enabled,
});
```

`SqlEncryptionMode` has three values:

| Value | Meaning |
|-------|---------|
| `DriverDefault` | Do not touch the setting (the default). |
| `Disabled` | Request an unencrypted connection. |
| `Enabled` | Request an encrypted connection. |

This maps onto the Microsoft SQL Server ODBC attribute
[`SQL_COPT_SS_ENCRYPT`](https://learn.microsoft.com/en-us/sql/relational-databases/native-client-odbc-api/sqlsetconnectattr),
which has to be applied to the connection handle *before* connecting. Because the server type is not
yet known at that point, the setting is applied verbatim whenever you opt in — and if the driver
rejects it, the connection **fails** rather than silently falling back to an unencrypted channel.
Leave the field at `DriverDefault` on backends that configure TLS through their own keywords
(PostgreSQL's `sslmode`, for example).

When connecting with a raw `SqlConnectionString` instead, use the driver's own `Encrypt=` keyword —
it is what `SqlConnectionDataSource::ToConnectionString()` emits, and
`SqlConnectionDataSource::FromConnectionString()` reads it back:

```cpp
auto const connectionString = SqlConnectionString {
    .value = "Driver={ODBC Driver 18 for SQL Server};SERVER=db;UID=user;PWD=password;Encrypt=yes"
};
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

Preparing a query costs a server-side parse and plan on Microsoft SQL Server and PostgreSQL, paid again
every time the same query text is prepared. Applications built on `DataMapper` or the query builders
re-prepare the same handful of statements constantly, because each call site creates its own short-lived
`SqlStatement`.

Both drivers *defer* that work rather than doing it inside `SQLPrepare`: measured, `Prepare()` on its own
costs about 1.2 µs on either backend and sends nothing. The parse is folded into the **first execute** of
a freshly prepared handle (`sp_prepexec` on the Microsoft driver, a Parse/Describe exchange on psqlODBC),
with a matching deallocate when the handle is freed. Re-executing a handle that is already prepared skips
all of it — which is what makes keeping the handle alive worth anything.

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
std::println("prepare hits={} misses={} evictions={} directReuses={}",
             stats.hits, stats.misses, stats.evictions, stats.directReuses);
```

`directReuses` counts prepares a statement served from the handle it was already holding — a repeat of
the query text it last prepared. Those cost no `SQLPrepare` either, but they never consult the pool:
parking the handle only to look that same text straight back up would be pure overhead.

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

### What it is worth, measured

`src/benchmark/prepared_statement_cache.cpp` (target `LightweightPreparedStatementCacheBenchmark`) runs
each workload below with the cache off and on, alternating the two settings so a busy database host does
not favour either, and reports the fastest of eleven repetitions:

```sh
cmake --preset clang-release -D LIGHTWEIGHT_BUILD_BENCHMARK=ON
cmake --build --preset clang-release --target LightweightPreparedStatementCacheBenchmark
./out/build/clang-release/src/benchmark/LightweightPreparedStatementCacheBenchmark 1000 "<connection string>" 11
```

Speed-up with the cache enabled, 1000 iterations, Docker-local servers (so these are *lower* bounds — the
saving is a round-trip, and a real network is slower than a loopback one):

| workload | SQLite 3 | PostgreSQL 16.4 | MS SQL Server 2022 |
|---|---|---|---|
| fresh `SqlStatement` per call, prepare + execute + fetch | 1.4x | **4.2x** | **1.7x** |
| fresh `SqlStatement` per call, prepare only | 4.1x | 1.2x | 1.1x |
| one statement, 4 query texts interleaved | 1.3x | **4.0x** | 1.0x |
| `DataMapper::Query<>().Where().All()` | 1.1x | **3.8x** | **1.3x** |
| `DataMapper::Create()` | 0.9x | **1.9x** | **1.3x** |
| `DataMapper::QuerySingle()` by primary key | 1.0x | 1.0x | 1.0x |

Reading the table:

- **The gain is concentrated in the shape the high-level API produces**: a short-lived `SqlStatement` per
  call site re-preparing a query text the connection has already seen. That is what `DataMapper`'s query
  builders and every `SqlQuery` DSL call site do.
- **PostgreSQL benefits most.** psqlODBC prepares server-side, so a re-prepare is a real round-trip.
- **`QuerySingle()` gains nothing** — it prepares through the mapper's own long-lived statement, which
  already reuses its handle for a repeat of the same text whether or not the cache is enabled.
- **`DataMapper::Create()` on SQLite is ~8% slower.** Its last-insert-id query goes through
  `ExecuteDirect()`, which parks the prepared handle and allocates a fresh one; on an in-process engine
  that costs more than the `SQLPrepare` it saves. Enable the cache for network-backed engines.

#### Under a connection pool

A prepared handle belongs to one connection's `SQLHDBC` and can never be shared with another, so every
connection a pool hands out warms up on its own. The same benchmark measures that directly: worker
threads acquire a `DataMapper` from a `Pool`, run three distinct query shapes through it
(`QuerySingle()`, a `Query<>().Where().All()` and a `Count()`) and hand it back.

Speed-up with `PoolConfig::preparedStatementCacheCapacity` set, 500 operations (1500 queries), and the
`SQLPrepare` calls the whole pool issued — on the first pass over the workload (cold) and on a later one
(warm):

| pool shape | SQLite | PostgreSQL | MS SQL Server | `SQLPrepare` cold → warm |
|---|---|---|---|---|
| 1 connection, 1 worker | 1.13x | **2.30x** | **1.21x** | 3 → 0 |
| 1 connection, 4 workers (contended) | 1.13x | **2.18x** | **1.21x** | 3 → 0 |
| 4 connections, 4 workers | 1.13x | **2.18x** | **1.40x** | 12 → 0 |
| 2 idle connections, 8 workers (`BoundedOverflow`) | 1.02x | 1.48x | 1.11x | 30 → 24 |

- **The warm-up is exactly `connections × distinct query texts`** — three texts over four connections is
  twelve `SQLPrepare` calls, not three. That is the price of the cache being per connection, and it is
  the whole of it: it is paid once, and a pool that keeps its connections converges to **zero**
  `SQLPrepare` no matter how many connections it holds.
- **Pool size does not dilute the steady-state win.** One connection and four connections reach the same
  speed-up; adding connections adds warm-up, not per-query cost.
- **Short-lived work does dilute it.** With only 6 operations per connection the cold-pass speed-up on
  PostgreSQL falls from 2.55x to 1.40x, because the twelve prepares are still being paid off. Size the
  cache for a pool whose connections live across many requests.
- **`GrowthStrategy::BoundedOverflow` past the idle set never converges.** A connection created on
  overflow is destroyed when returned, and its warmed cache with it — the warm column above still shows
  24 `SQLPrepare` calls after many passes. The win drops from 2.18x to 1.48x on PostgreSQL and vanishes
  on SQLite. If the pool overflows, raising `maxSize` to cover the real concurrency is worth more than
  any cache capacity: in the same measurement the overflow shape spent most of its time *connecting*.
- **The pooled figures are lower than the single-connection ones** (2.2x rather than 3.8x on PostgreSQL)
  because the mix includes `QuerySingle()`, which gains nothing anywhere.

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
    for (auto const& person: SqlRowIterator<Person>(dm.Connection()))
        std::println("|{}|{}|", person.name, person.age);

    // Iterate over a subset of the persons
    for (auto const& person: SqlRowIterator<Person>(dm.Connection(),
                                                    [](auto& query) { return query.Where("age", ">=", 18); }))
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

### Eager loading of relations (`With<>()`)

Accessing a relation on a query result loads it on demand — one query per record. Over a result set of
N records that is the N+1 problem: reading `album.tracks` for 1000 albums issues 1001 queries.
`With<&Record::relation>()` instead resolves the relation for the whole result set once it has been
materialized, using `WHERE <key> IN (...)`:

```cpp
// Two queries in total, whatever the number of albums: one for the albums, one for all their tracks.
auto albums = dm.Query<Album>()
                .With<&Album::tracks>()    // HasMany
                .With<&Album::artist>()    // BelongsTo
                .All();

for (auto& album: albums)
    for (auto const& track: album.tracks.All())  // already loaded, no query
        std::println("{} - {}", album.title, track->title);
```

- Supported for `BelongsTo` and `HasMany`. `HasOneThrough`, `HasManyThrough` and `CompositeForeignKey`
  still load on demand; naming one of them in `With<>()` is a compile error rather than a silent
  fallback.
- Naming several relations forms a **path**, which is what a nested relation needs — see below.
- Calls chain, one per relation to load. It applies to `All()`, `First()`, `First(n)` and `Range()`.
- The `IN` predicate is chunked (see `SqlQueryFormatter::MaxInPredicateValues`, 1000 by default), so a
  large batch costs one query per chunk — a constant number of queries per relation, never one per
  record.
- A `BelongsTo` whose foreign key is `NULL`, and an owner with no children, are handled without an
  extra query: the childless owner's relation is marked loaded-and-empty rather than left to query for
  a result already known.
- Relations that were *not* named keep their on-demand behaviour. Combining `With<>()` with
  `DataMapperOptions { .loadRelations = false }` therefore turns any unrequested relation access into a
  `SqlRequireLoadedError` instead of a silent query — useful to prove a code path issues no N+1.

#### Nested relations

Eager-loading one level is not enough for a chain. Every record holds its *own copy* of its
`BelongsTo` target, so reaching a relation of that copy runs the copy's own lazy loader — the N+1
simply moves one level down. Name the whole path instead:

```cpp
auto tracks = dm.Query<Track>()
                .With<&Track::album>()                   // 1 query for all albums
                .With<&Track::album, &Album::artist>()   // 1 query for all those albums' artists
                .All();

for (auto& track: tracks)
    std::println("{} - {}", track.album.Record().title,
                 track.album.Record().artist.Record().name);   // no queries here
```

Three queries in total, for any number of tracks. Each level is resolved for every record reached by
the level above it, at once. A path may also run through the "many" side
(`.With<&Album::tracks, &Track::genre>()`): the middle level fans out, and the level below it is
still one query rather than one per child.

Already-loaded relations are skipped, so overlapping paths (`.With<&A::b>()` next to
`.With<&A::b, &B::c>()`) do not fetch `b` twice.

#### Loading everything reachable

When a whole object graph is wanted rather than named paths, set a depth on the query instead:

```cpp
// Tracks, their albums and categories, and those albums' artists - a constant number of queries.
auto tracks = dm.Query<Track, DataMapperOptions { .eagerLoadDepth = 2 }>().All();
```

`eagerLoadDepth` batch-loads *every* `BelongsTo` and `HasMany` reachable within that many levels.
Prefer `With<>()` when only part of the graph is needed: the depth walk fetches more rows, and
instantiates the loader for the whole reachable relation graph, which costs compile time. The depth
is what bounds both — and what lets a cyclic graph (a self-referencing record, or `A → B → A`)
terminate, since the recursion is cut at a compile-time constant.

Measured on 1000 owners with 10 children each, comparing the on-demand path with `With<>()`:

| relation | queries before | queries after | SQLite3 | PostgreSQL | MS SQL Server |
|---|---:|---:|---:|---:|---:|
| `HasMany` | 1001 | 2 | 8.7x | 45x | 45x |
| `BelongsTo` | 10001 | 2 | 37x | 464x | 407x |

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

## Streaming a table with `SqlRowIterator`

`SqlRowIterator<T>` streams a table row by row, materializing one record at a time instead of
loading the whole result set into a `std::vector` as `DataMapper::Query<T>()` does. That makes it
the tool of choice for tables too large to hold in memory.

```cpp
for (auto const& person: SqlRowIterator<Person>(dm.Connection()))
    std::println("{}", DataMapper::Inspect(person));
```

Pass a callable as second argument to iterate over a **subset** of the rows. It receives the
underlying `SqlSelectQueryBuilder` with the projection for `T` already applied, so the full
`Where` / `OrWhere` / `OrderBy` / `Limit` surface of the [query builder](sqlquery.md) is available.
Whatever the callable returns is ignored, so the builder's chaining methods can be returned
directly:

```cpp
for (auto const& person: SqlRowIterator<Person>(dm.Connection(), [](auto& query) {
         return query.Where("age", ">=", 18).OrWhere([](auto& query) {
             return query.Where("age", 10).Where("name", "John");
         });
     }))
    std::println("{}", DataMapper::Inspect(person));
```

Both plain column names and `FieldNameOf<Member(Person::age)>` work as column arguments; the latter
keeps the condition in sync when a field is renamed.
