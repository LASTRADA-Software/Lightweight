# Schema introspection

`SqlSchema` reads an existing database's structure back out into plain C++ structures: tables,
columns, primary keys, foreign keys in both directions, and indexes. It is the layer `ddl2cpp` is
built on, and it is public API — reach for it when you need to inspect a schema you did not define
in C++, or generate something from it.

Everything lives in the `Lightweight::SqlSchema` namespace and works through an ordinary
`SqlStatement`, so it obeys the same connection and transaction rules as the rest of the library.

## Reading every table

```cpp
#include <Lightweight/Lightweight.hpp>

auto stmt = Light::SqlStatement {};

std::vector<Light::SqlSchema::Table> const tables =
    Light::SqlSchema::ReadAllTables(stmt, /* database */ "MyDatabase");
```

The `schema` parameter is optional and defaults to the driver's notion of the default schema. For
SQL Server that is typically `dbo`; SQLite has no schemas at all.

Reading a large schema is not instant, so `ReadAllTables` accepts a progress callback that fires
per table:

```cpp
auto const tables = Light::SqlSchema::ReadAllTables(
    stmt,
    "MyDatabase",
    /* schema */ {},
    [](std::string_view tableName, size_t current, size_t total) {
        std::println("[{}/{}] {}", current, total, tableName);
    });
```

Two further optional parameters follow: a `TableReadyCallback`, invoked as soon as one table's
schema is complete so you can stream results instead of waiting for the whole database, and a
`TableFilterPredicate`, which skips reading columns and constraints for tables you do not care
about. Filtering at that level is much cheaper than reading everything and discarding it.

## What you get back

`Table` describes one table:

| Field | Meaning |
|-------|---------|
| `schema`, `name` | Where the table lives and what it is called. |
| `columns` | Ordered `Column` definitions. |
| `primaryKeys` | Column names forming the primary key — more than one for a composite key. |
| `foreignKeys` | Foreign keys declared *on* this table. |
| `externalForeignKeys` | Foreign keys on *other* tables pointing *at* this one. |
| `indexes` | Indexes excluding the primary-key index. |

`externalForeignKeys` is what makes relation inference possible: `foreignKeys` gives you the
`BelongsTo` side, and `externalForeignKeys` gives you the `HasMany` side of the same relationship.

Each `Column` carries its name, a portable `SqlColumnTypeDefinition`, the raw
`dialectDependantTypeString` as the driver reported it, plus `isNullable`, `isUnique`, `size`,
`decimalDigits`, `isAutoIncrement`, `isPrimaryKey`, `isForeignKey`, an optional
`foreignKeyConstraint`, and `defaultValue`.

Keep both type representations in mind: `type` is the normalized form to compare or map against,
while `dialectDependantTypeString` is what the database actually said and is the better thing to
show a human when a type does not round-trip as expected.

## Following relationships directly

When you only need the edges and not whole tables:

```cpp
auto const table = Light::SqlSchema::FullyQualifiedTableName {
    .catalog = {}, .schema = {}, .table = "Orders"
};

auto const incoming = Light::SqlSchema::AllForeignKeysTo(stmt, table);   // who points at Orders
auto const outgoing = Light::SqlSchema::AllForeignKeysFrom(stmt, table); // what Orders points at
```

## Turning a description back into DDL

`MakeCreateTablePlan` converts a `Table` (or a whole `TableList`) into a `SqlCreateTablePlan`, which
the query formatter renders as `CREATE TABLE` for the target DBMS. Because the plan goes through
`SqlQueryFormatter`, a schema read from PostgreSQL can be emitted as SQL Server DDL without you
writing per-dialect code:

```cpp
auto const plan = Light::SqlSchema::MakeCreateTablePlan(tables);
```

The `TableList` overload maps each table independently and **preserves the order you pass in** — it
does not topologically sort by foreign-key dependency. If the target DBMS enforces that a referenced
table exists before the referencing one, order the list yourself before calling it.

## Event-driven reading

For very large schemas, or when you want to react as the schema is read rather than materialize all
of it, implement the `EventHandler` interface and pass it to the `ReadAllTables` overload that takes
one. It is a SAX-style callback interface: `OnTables`, `OnTable` (return `false` to skip a table),
`OnPrimaryKeys`, `OnForeignKey`, `OnColumn`, `OnExternalForeignKey`, `OnIndexes`, and `OnTableEnd`.

## See also

- [ddl2cpp-relation-generation.md](ddl2cpp-relation-generation.md) — how `ddl2cpp` turns this data into records and relations.
- [sql-migrations.md](sql-migrations.md) — defining schema from C++ rather than reading it.
- [sqlquery.md](sqlquery.md) — the DDL surface of the query builder.
