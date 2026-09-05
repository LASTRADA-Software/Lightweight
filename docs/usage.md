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
