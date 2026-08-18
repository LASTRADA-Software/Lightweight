# Lightweight, a C++23 database library

**Lightweight** is a modern C++23 database library for **Microsoft SQL Server**, **PostgreSQL** and
**SQLite** over ODBC — with a data mapper and typed relationships, versioned schema migrations,
parallel backup and restore, an async coroutine API, connection pooling, and CLI/GUI tooling.

It is layered: use the thin ODBC wrapper (`SqlConnection`, `SqlStatement`) when you want raw SQL and
full control, the query builder when you want composable typed SQL, or the `DataMapper` when you
want records and relationships mapped for you. The layers interoperate — you can drop from one to
the next at any point without leaving the library.

Documentation is available at [https://lastrada-software.github.io/Lightweight/](https://lastrada-software.github.io/Lightweight/).

## Features

| Area | What it gives you | Guide |
|------|-------------------|-------|
| **Raw SQL access** | `SqlConnection`, `SqlStatement`, prepared statements, batched execution, block-prefetch | [usage.md](docs/usage.md) |
| **Query builder** | Composable typed `SELECT`/`INSERT`/`UPDATE`/`DELETE`, joins, filtering, ordering, pagination | [sqlquery.md](docs/sqlquery.md) |
| **Data mapper** | Struct-to-table mapping, CRUD, `Field<>` with primary keys and nullability | [usage.md](docs/usage.md) |
| **Relationships** | `BelongsTo`, `HasMany`, `HasOneThrough`, `HasManyThrough`, `CompositeForeignKey` | [composite-keys-design.md](docs/composite-keys-design.md) |
| **Schema migrations** | Versioned migrations, checksums, dependency ordering, plugin loading, rollback | [sql-migrations.md](docs/sql-migrations.md) |
| **Backup & restore** | Parallel chunked dump/restore, msgpack + zip + sha256, archive diffing | [sql-backup.md](docs/sql-backup.md), [sql-backup-format.md](docs/sql-backup-format.md) |
| **Async API** | C++23 coroutines: `Task<T>`, executors, strand, stdexec bridge, async `DataMapper` | [async.md](docs/async.md) |
| **Connection pooling** | Compile-time-configured pool, async-aware, recycles connections across mappers | [async.md](docs/async.md) |
| **Custom data types** | `SqlDataBinder<T>` specialization for your own types, with Unicode support | [data-binder.md](docs/data-binder.md) |
| **`dbtool` CLI** | Migrations, backup/restore, backup diffing, schema inspection — ~20 commands | [dbtool.md](docs/dbtool.md) |
| **`dbtool-gui`** | Qt/QML desktop app for migrations, backup and ad-hoc queries | [dbtool.md](docs/dbtool.md) |
| **`ddl2cpp`** | Generates C++ records from an existing schema, inferring relations automatically | [ddl2cpp-relation-generation.md](docs/ddl2cpp-relation-generation.md) |

Coming from SQL? [sql-to-lightweight.md](docs/sql-to-lightweight.md) is a side-by-side cookbook that
shows the Lightweight equivalent of a given piece of SQL in each of the three layers.
See also [best-practices.md](docs/best-practices.md) and [how-to.md](docs/how-to.md).

## Why ODBC?

ODBC is a deliberate trade-off, not an accident of history. Every other ecosystem — and within C++
every ergonomic peer (sqlpp23, sqlgen, ormpp, sqlite_orm, libpqxx, Drogon) — talks native wire
protocols per database. Lightweight targets ODBC because it buys **one API, one build, and one set
of semantics across SQL Server, PostgreSQL and SQLite**, with per-database differences funnelled
through a single dispatch point (`SqlQueryFormatter`) rather than scattered across the codebase. For
applications that must ship against more than one database, that is worth a great deal.

What it costs, stated plainly:

- **Driver deployment.** Your users need the right ODBC driver installed and registered, and driver
  quality varies. Some behaviours are driver-build-specific rather than database-specific.
- **No protocol-level bulk path.** There is no `COPY`-class fast path; bulk work goes through
  array/parameter binding, which is fast but not as fast as a native bulk loader.
- **No protocol-level async.** See the async limitation below.

## Supported platforms

Only ODBC is supported, so it should work on any platform that has an ODBC driver and
a modern enough C++ compiler.

- Windows (Visual Studio 2022, toolkit v143)
- Linux (GCC 14, Clang 19)

## Supported databases

- Microsoft SQL Server
- PostgreSQL
- SQLite3

`SqlServerType` also lists a `MYSQL` enumerator, but **MySQL is not supported**:
`SqlQueryFormatter::Get()` returns `nullptr` for it, so no query can be formatted. The enumerator is
a placeholder for possible future work — do not rely on it.

## Known limitations

Being explicit here saves you from discovering these by reading the source.

- **The query builder is not a complete SQL surface.** It has no `HAVING`, CTEs,
  `UNION`/`EXCEPT`/`INTERSECT`, `RETURNING`, upsert, window functions, or multi-row
  `INSERT ... VALUES` (bulk goes through array binding instead). The intended escape hatches are
  `SqlFieldExpression`, `WhereRaw(...)`, and `DataMapper::Query<T>(sql, ...)` — reach for them when
  you need SQL the builder does not model. For `RETURNING` specifically, `SqlStatement` can execute
  it directly, with driver caveats documented in [how-to.md](docs/how-to.md).
- **Async is thread-offload, not protocol-level non-blocking.** ODBC's own async execution is not
  portable, so each blocking ODBC call is offloaded to a worker thread and your coroutine resumes on
  a scheduler you choose. Your application thread never blocks, but *some* thread does. See
  [async.md](docs/async.md) for the full rationale.
- **No identity map or unit of work.** Loading the same row twice yields two unrelated objects.
  `BelongsTo` holds a deep copy; `HasMany` builds fresh `shared_ptr`s on every load.
- **Lazy loaders use a thread-local mapper, not the one that loaded the record.** Relation
  auto-loading goes through `DataMapper::AcquireThreadLocal()`, which is constructed from
  `SqlConnection::DefaultConnectionString()`. A lazy load therefore runs on a *different connection*,
  **outside your caller's transaction**, and fails outright if no default connection string is
  configured. If that matters to your code, load relations explicitly instead of touching them
  lazily.
- **No eager-loading / preload API.** Touching a relation across N records issues N queries.
- **Backup is online, with no snapshot.** Tables are read while writes continue, so an archive has
  no cross-table consistency guarantee. Quiesce writers if you need a consistent point-in-time dump.
  See [sql-backup.md](docs/sql-backup.md).

## Versioning and stability

Releases are calendar-versioned as `v0.YYYYMMDD.0`. The leading `0.` is part of that scheme and is
**not** a statement that the library is unfinished — it is in production use, and the test suite runs
against SQLite, PostgreSQL and SQL Server 2017/2019/2022 on every change.

That said, the project does not currently make a formal semver compatibility promise: the public API
is stable in practice, but a breaking change can land in any release. Pin an exact tag if you need
reproducible builds, and read the release notes before upgrading.

## Namespace

All functionality is placed inside a `Lightweight` namespace, we also provide an alias for this namespace `Light`, that is slightly shorter.

## High level API

High level API of the library provided by the type `DataMapper`

### Simple one record example

Example of its usage to save/load/update/delete entry in the database for one table

```cpp
#include <Lightweight/Lightweight.hpp>

// Define a person structure, mapping to a table from the database
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

    // Create a new person and create a database entry 
    auto person = Person {.name = "John Doe", .is_active = true, .age = 24};
    dm.Create(person);

    // Update the age and save to the database
    person.age = 25;
    dm.Update(person);

    // Query the person by primary key
    if (auto const po = dm.Query<Person>(person.id); po)
        std::println("Person: {} ({})", po->name, DataMapper::Inspect(*po));

    // Query all persons
    std::vector<Person> const persons = dm.Query<Person>().All(); 
    
    // Query all persons with some filter and order by name
    auto const records = dm.Query<Person>()
                             .Where(FieldNameOf<&Person::is_active>, "=", true)
                             .OrderBy(FieldNameOf<&Person::name>)
                             .All();

    // Delete the person
    dm.Delete(person);
}
```

### Foreign keys relation

Now consider the following example we have two tables `User` and `Email`, with foreign key in `Email` pointing to the `User` 
this will translate in the following structs 

```cpp
struct User
{
    Light::Field<Light::SqlGuid, Light::PrimaryKey::AutoAssign, SqlRealName { "user_id" }> id {};
    Light::Field<Light::SqlAnsiString<30>> name {};
};

struct Email
{
    Light::Field<Light::SqlGuid, Light::PrimaryKey::AutoAssign> id {};
    Light::Field<Light::SqlAnsiString<30>> address {};
    Light::BelongsTo<&User::id, Light::SqlRealName { "user_id" }> user {};
};
```

`BelongsTo` models the **many-to-one** side of a foreign key — the record that *owns* the foreign-key
column. Many `Email`s can point at one `User`. For the other relationship kinds see `HasMany`,
`HasOneThrough`, `HasManyThrough` and `CompositeForeignKey`.

In the presented example we used rename of the columns, for more details see how-to\#rename-column-name page.
you can query the email and get access to the user record as well

```cpp
auto dm = Light::DataMapper();
auto email = dm.QuerySingle<Email>(some_email_id).value_or(Email{});
auto user_name = email.user->name; // lazily loads the user record
```

> **Note:** lazy loading (`email.user->name` above) does *not* use `dm`. It uses a thread-local
> `DataMapper` built from the default connection string, so it runs on a different connection and
> outside any transaction `dm` may be in. See [Known limitations](#known-limitations).

### Mapping query results to a simple struct

If you have a SQL query that returns some values, but it does not corresponds to the existing table in the database, you can map the result to a simple struct.
The struct must have fields that match the columns in the query. The fields can be of any type that can be converted from the column type. The struct can have more fields than the columns in the query, but the fields that match the columns must be in the same order as the columns in the query.

```cpp
#include <Lightweight/Lightweight.hpp>

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
    if (auto maybeObject = dm.Query<SimpleString>(
        "SELECT A.pk, B.pk, A.c1, A.c2, B.c1, B.c2 FROM A LEFT JOIN B ON A.pk = B.pk"); maybeObject)
    ))
    {
        for (auto const& obj : *maybeObject)
            std::println("{}", DataMapper::Inspect(obj));
    }
}
```

### Mapping query to multiple struct

We also provide an API to create SQL queries, this can be usefull if you want to use information from existing structures.
The following example shows how to create a query that joins multiple tables and maps the result to multiple structs.
Consider the following structs

```cpp
struct CustomBindingA
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<int> number {};
    Field<SqlAnsiString<20>> name {};
    Field<SqlDynamicWideString<1000>> description {};
};

struct CustomBindingB
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<20>> title {};
    Field<SqlDateTime> date_time {};
    Field<uint64_t> a_id {};
    Field<uint64_t> c_id {};
};

struct CustomBindingC
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<double> value {};
    Field<SqlAnsiString<20>> comment {};
};

```

Create a query to join those tables to get in a single query
```cpp
auto dm = Light::DataMapper();
auto query = dm.FromTable(RecordTableName<CustomBindingA>)
               .Select()
               .Fields<CustomBindingA, CustomBindingB>()
               .Field(QualifiedColumnName<"C.id">)
               .Field(QualifiedColumnName<"C.comment">)
               .InnerJoin<&CustomBindingB::a_id, &CustomBindingA::id>()
               .InnerJoin<&CustomBindingC::id, &CustomBindingB::c_id>()
               .OrderBy(QualifiedColumnName<"A.id">)
               .All();
```
This create the following SQL query
```sql
SELECT "A"."id", "A"."number", "A"."name", "A"."description", "B"."id", "B"."title", "B"."date_time", "B"."a_id", "B"."c_id", ""C"."id"", "C"."comment" FROM "A"
 INNER JOIN "B" ON "B"."a_id" = "A"."id"
 INNER JOIN "C" ON "C"."id" = "B"."c_id"
 ORDER BY "A"."id" ASC
```

Now you can execute it and get the result as a `std::vector<std::tuple<CustomBindingA, CustomBindingB, ParfOfC>` like this

```cpp
struct PartOfC
{
    uint64_t id {};
    SqlAnsiString<20> comment {};
};

auto const records = dm.Query<CustomBindingA, CustomBindingB, PartOfC>(query);
for (auto const& [a, b, c]: records)
{
    // ...
}
```

## Schema migrations

Migrations are versioned, checksummed C++ definitions applied in dependency order, with rollback and
plugin loading. They can be driven from your application or from the `dbtool` CLI:

```sh
dbtool status         # what is applied, what is pending
dbtool migrate        # apply everything pending
dbtool rollback-to 20260101120000
```

See [sql-migrations.md](docs/sql-migrations.md) for writing migrations and
[dbtool.md](docs/dbtool.md) for the full command reference.

## Backup and restore

Lightweight ships a backup engine — parallel chunked dump and restore into a zip archive of msgpack
chunks with sha256 integrity, plus archive diffing:

```sh
dbtool backup --output snapshot.zip
dbtool restore --input snapshot.zip
dbtool backup-diff --left old.zip --right new.zip
```

Backups are taken **online, without a snapshot**, so there is no cross-table consistency guarantee —
see [Known limitations](#known-limitations). Details in [sql-backup.md](docs/sql-backup.md), archive
layout in [sql-backup-format.md](docs/sql-backup-format.md).

## Asynchronous API

Async entry points are added directly to the types you already use (`SqlConnection`, `DataMapper`,
`Pool`), suffixed with `Async`, and return `Async::Task<T>`. Queries go through the same fluent
builder as the synchronous API. Note that this is **thread-offload**, not protocol-level async — see
[async.md](docs/async.md), which explains why and what that means for your thread budget.

## Using SQLite for testing on Windows operating system

You need to have the SQLite3 ODBC driver for SQLite installed.

- ODBC driver download URL: http://www.ch-werner.de/sqliteodbc/
- Example connection string: `"DRIVER={SQLite3 ODBC Driver};Database=file::memory:"`

### SQLite ODBC driver installation on other operating systems

```sh
# Fedora Linux
sudo dnf install sqliteodbc

# Ubuntu Linux
sudo apt install sqliteodbc

# macOS
arch -arm64 brew install sqliteodbc
```

- sqliteODBC Documentation: http://www.ch-werner.de/sqliteodbc/html/index.html
- Example connection string: `"DRIVER=SQLite3;Database=file::memory:"`


## Generate example for the existing database

You can use `ddl2cpp` to generate header file for you database schema as well as an example file that you can compile

First, configure cmake project and compile `ddl2cpp` target

``` sh
cmake --build build --target ddl2cpp 
```

Generate header file from the existing database by providing connection string to the tool 

``` sh
 ./build/src/tools/ddl2cpp --connection-string "DRIVER=SQLite3;Database=test.db" --make-aliases --naming-convention CamelCase  --output ./src/examples/example.hpp --generate-example
```

You can also avoid all those command line arguments by creating a config file that muts be in your
current working directory or in one of its parent directories.
The config file must be named `ddl2cpp.yml` and must contain the following content:

```yaml
ConnectionString: 'DSN=YourDSN;UID=YourUser;PWD=YourSecret'
OutputDirectory: 'src/entities'
MakeAliases: true
NamingConvention: CamelCase
```

Now you can configure cmake to compile example

``` sh
cmake --preset clang-debug -DLIGHWEIGHT_EXAMPLE=ON -B build
```

Finally, compile and run the example

``` sh
cmake --build build && ./build/src/examples/example
```

`ddl2cpp` also infers relationships from the schema's foreign keys — see
[ddl2cpp-relation-generation.md](docs/ddl2cpp-relation-generation.md).

## Compile using C++26 reflection support

``` sh
docker buildx build --progress=plain -f .github/Reflection.Dockerfile --load .
```

## Building with C++20 Modules

Lightweight supports building as C++20 modules. To enable this feature, you need CMake 3.28 or higher.

Enable module support with the `LIGHTWEIGHT_BUILD_MODULES` CMake option:

```sh
cmake -B build -S . -G Ninja -DLIGHTWEIGHT_BUILD_MODULES=ON
cmake --build build
```

When modules are enabled, consumers can import the library using:

```cpp
import Lightweight;
```

**Note:** C++20 module support requires:
- CMake 3.28 or higher, with a system like Ninja
- A compiler with full C++20 module support (e.g., GCC 14+, Clang 19+, MSVC 19.36+)
