# SQL Query

SQL Query builder class is the starting point of building sql queries to execute.

## Create or Modife database schema 
To create a database you need to use `Migration()` function provided by the `SqlQueryBuilder` class, then use API defined in `SqlMigrationQueryBuilder` to construct sql query to migrate to another schema or create a databse with the given schema. Detailed documentation can be found on separate documentation pages for each of the classes in the hirerarchy, here we present overall usage of the library.  Following options exist.

* `CreateTable(tableName)`
Following calls can be chained, for example `CreateTable("test").Column(first).Column(second)...`
Available functions:
  - `PrimaryKey(std::string columnName, SqlColumnTypeDefinition columnType)`
    + create primary key column in the database, defined by name and type. 
  - `PrimaryKeyWithAutoIncrement( std::string columnName, SqlColumnTypeDefinition columnType )`
    + create primary key column in the database with automatic indexing 
    + Second parameter has a default value `SqlColumnTypeDefinitions::Bigint`
  - `Column(std::string columnName, SqlColumnTypeDefinition columnType)`, `Column(SqlColumnDeclaration column)`
    + create a column specified by a name and type
    + for precise control on the column specification `SqlColumnDeclaration` can be used as an argument. 
  - `RequiredColumn(std::string columnName, SqlColumnTypeDefinition columnType)`
    +  create a non-nullable column defined by the name and type.
  - `Timestamps()`
    + adds the created_at and updated_at columns to the table. 
  - `ForeignKey(std::string columnName, SqlColumnTypeDefinition columnType, SqlForeignKeyReferenceDefinition foreignKey)`
    + creates a new nullable foreign key column, non-nullable version is a `RequiredForeignKey` function. 
  - Additional function that change specification of the created columns with the following usage: `CreateTable("test").Column(first).UniqueIndex()`.
    + `Unique()` enables the UNIQUE constrain on the last declared column.
    + `Index()` enables the INDEX constrain on the last declared column.
    + `UniqueIndex()` enables the UNIQUE and INDEX constrain on the last declared column.

* `AlterTable(tableName)` 
  Available functions:
  - `RenameTo(std::string_view newTableName)`
    + renames the table.
  - `RenameColumn(std::string_view oldColumnName, std::string_view newColumnName)`
    + renames a column.
  - `DropColumn(std::string_view columnName)`
    + drops a column from the table.
  - `AddIndex(std::string_view columnName)`
    + add an index to the table for the specified column.
  - `AddUniqueIndex(std::string_view columnName)`
    + add an index to the table for the specified column that is unique.
  - `DropIndex(std::string_view columnName)`
    + drop an index from the table for the specified column.

* `DropTable(tableName)`
  - Drops table with the given name, please make sure that no foreign key constrains restricts execution of this query for the given table.


### Example

```cpp
SqlMigrationQueryBuilder migration;
migration.CreateTable("Appointment").PrimaryKeyWithAutoIncrement("id", Guid{})
         .RequiredColumn("date", DateTime {})
         .Column("comment", Varchar { 80 })
         .ForeignKey("physician_id", Guid {}, 
                     SqlForeignKeyReferenceDefinition { .tableName = "Physician", .columnName = "id" })
         .ForeignKey("patient_id", Guid {}, 
                     SqlForeignKeyReferenceDefinition { .tableName = "Patient", .columnName = "id" });
```

## Insert elements

To insert elements in the database first call `FrommTable(table)` function to specify which table to use,
and then function `Insert()` to start construction of `SqlInsertQueryBuilder`

- `Set(std::string_view columnName, ColumnValue const& value)`
  - Adds a single column to the INSERT query.

## Select elements

To select some elements from the Database you first need to specify which existing table you are going to use,
for this use `FromTable(table)` function, it returns you an instance of a `SqlQueryBuilder` and then
use `Select()` function to continue constructing select query that described by `SqlSelectQueryBuilder`
interface. Here we present a compressed list of functions that can be used to create complete selection query.  

- Select field
  - `Distinct()`
    - Adds a DISTINCT clause to the SELECT query.
  - `Field()`
    - Simple usage `Field("field")` 
    - With table name specification as `Field(SqlQualifiedTableColumnName { "Table", "field" })`
    - Helper function to construct `SqlQualifiedTableColumnName` from a string `QualifiedColumnName<"Table.field">`
  - `Fields()`
    - Simple usage `Fields({"a", "b", "c"})`
    - Fields from another table `Fields({"a", "b", "c"}, "Table_B")`
    - Choose all fields of a structure that represents table `Field<TableType>()`. Note: can pass more than one type
- (optional) Order and Group
  - `OrderBy`
  - `GroupBy`
- (optional) Additional option to build WHERE clause. See documentation for `SqlWhereClauseBuilder`
  - `Where`
    - `Where("a", 42)` specify simple condition that is equivalent to the sql query `WHERE "a" = 42`
    - `Where(SqlQualifiedTableColumnName { .tableName = "Table_A", .columnName = "a" }, 42)` such call translated into `WHERE "Table_A"."a" = 42`
  - `Or()`, `And()` and `Not()` logical functions to apply to the next call
    - Example of usage `Where("a",1).Or().Where("b",1)`
  - `If(optional).ThenWhere(column[, binaryOp])` — conditional WHERE driven by a `std::optional`
    - `ThenWhere(column)` appends `WHERE column = *value` only when the optional holds a value; when the optional is empty the call is a no-op and the underlying query is left untouched. Returns the underlying builder, so it can be chained between other clauses.
    - `ThenWhere(column, binaryOp)` mirrors `Where(column, binaryOp, value)` — emits `WHERE column <binaryOp> *value` (e.g. `">="`, `"<"`, `"!="`, `"LIKE"`) under the same empty/populated rules. Useful for range-style filters over `SqlDateTime`, numeric columns, etc.
    - Works with any column-name overload accepted by `Where` — plain strings, `SqlQualifiedTableColumnName`, and `FullyQualifiedNameOf<&Record::field>`.
    - Available on every builder that derives from `SqlWhereClauseBuilder`: `Select`, `Update`, and `Delete`.
    - Example — combining equality and range filters, any of which can be absent:
      ```cpp
      std::optional<int>         userId = MaybeUserIdFromRequest();
      std::optional<SqlDateTime> since  = MaybeSinceFromRequest();
      std::optional<SqlDateTime> until  = MaybeUntilFromRequest();

      auto query = q.FromTable("Events")
                    .Select()
                    .Field("id")
                    .If(userId).ThenWhere(FullyQualifiedNameOf<&Events::userId>)
                    .If(since).ThenWhere(FullyQualifiedNameOf<&Events::createdAt>, ">=")
                    .If(until).ThenWhere(FullyQualifiedNameOf<&Events::createdAt>, "<")
                    .OrderBy("id")
                    .All();
      // userId={42}, since={2026-01-01}, until={2026-05-18T12:30:45}
      //   -> WHERE "Events"."userId" = 42
      //      AND "Events"."createdAt" >= '2026-01-01T00:00:00.000'
      //      AND "Events"."createdAt" <  '2026-05-18T12:30:45.000'
      // userId={}, since={}, until={2026-05-18T12:30:45}
      //   -> WHERE "Events"."createdAt" < '2026-05-18T12:30:45.000'
      // userId={}, since={}, until={}
      //   -> no WHERE clause is emitted
      ```
  -  `Inner`|`LeftOuter`|`RightOuter`|`FullOuter` + `Join`
    - See documentation for `SqlJoinConditionBuilder` for details 
- End (finalizers — only reachable after at least one projection has been added)
  - `Count()`
    - Emits `SELECT COUNT(*) FROM ...`. Exposed on the starter directly — `SELECT COUNT(*)` is well-formed without an explicit column list.
  - `First()`
    - Specify number of elements to fetch, by default only one element will be fetched.
  - `All()`
  - `Range(offset, limit)`

> **Compile-time guard against empty projections.** `Select()` returns a
> `SqlSelectQueryStarter` — a distinct type that intentionally does **not** expose
> `All()`, `First()`, or `Range()`. Both of these patterns are therefore compile
> errors:
>
> ```cpp
> auto bad1 = q.FromTable("T").Select().All();           // chain — compile error
>
> auto q2 = q.FromTable("T").Select();
> auto bad2 = q2.All();                                  // named lvalue — compile error
> ```
>
> Adding a projection (`Field`, `Fields`, `FieldAs`, `Build`) returns a
> `SqlSelectQueryBuilder&` aliasing the starter's storage. That reference exposes
> the finalizers:
>
> ```cpp
> // Chain — Field returns Builder&, finalizer bound to that reference:
> auto good = q.FromTable("T").Select().Field("*").All();
>
> // Imperative — capture the first projection as auto&, continue from there:
> auto starter = q.FromTable("T").Select();
> auto& query = starter.Field(columns[0].name);
> for (size_t i = 1; i < columns.size(); ++i) query.Field(columns[i].name);
> auto result = query.All();
> ```
>
> To select all columns, use `.Field("*")` — the single-`Field` overload special-cases
> the wildcard. `Fields("*")` and `Fields({"*"})` quote the literal and produce
> `SELECT "*" FROM ...`, which is not what you want.
>
> `Distinct()` and `Varying()` are exposed on the starter as state-preserving
> overrides (they return `SqlSelectQueryStarter&`), so chains like
> `Select().Distinct().Fields(...).All()` keep working while
> `Select().Distinct().All()` is still a compile error. `Where`, `OrderBy`,
> `GroupBy`, and the join family (`InnerJoin`, `LeftOuterJoin`, etc.) are
> re-exposed via `using` declarations and *promote* the chain — they return
> `SqlSelectQueryBuilder&`. A chain like `Select().WhereNotNull("x").All()` will
> therefore compile (a small leak in the gate, in exchange for keeping the
> common `Select().WhereNotNull("x").Count()` pattern working unchanged).


### Example

```cpp
auto query = q.FromTable("Table_A")
              .Select()
              .Fields({ "foo"sv, "bar"sv }, "Table_A")
              .Fields({ "that_foo"sv, "that_id"sv }, "Table_B")
              .LeftOuterJoin("Table_B",
                  [](SqlJoinConditionBuilder q) {
                      return q.On("id", { .tableName = "Table_A", .columnName = "that_id" })
                              .On("that_foo", { .tableName = "Table_A", .columnName = "foo" });
                  })
              .Where(SqlQualifiedTableColumnName { .tableName = "Table_A", .columnName = "foo" }, 42)
              .All();
```


### Examples of SQL to DataMapper mappings


```cpp

dm->Query<Employee>().All();    // SELECT "Employee"."EmployeeId", "Employee"."LastName", "Employee"."FirstName", "Employee"."Title",
                                //        "Employee"."ReportsTo", "Employee"."BirthDate", "Employee"."HireDate", "Employee"."Address",
                                //        "Employee"."City", "Employee"."State", "Employee"."Country", "Employee"."PostalCode",
                                //        "Employee"."Phone", "Employee"."Fax", "Employee"."Email"
                                //        FROM "Employee"
                                // SELECT "AlbumId", "Title", "ArtistId" FROM "Album"




dm->Query<Album>()                                                        // TOP 1 "Album"."AlbumId", "Album"."Title", "Album"."ArtistId" 
    .Where(FieldNameOf<&Album::Title>, "=", "Mozart Gala: Famous Arias")  // FROM "Album"
    .First()                                                              // WHERE "Title" = 'Mozart Gala: Famous Arias'
    .value();



dm->Query<Track>()                                   // SELECT "Track"."TrackId", "Track"."Name", "Track"."AlbumId",
    .WhereIn(FieldNameOf<&Track::AlbumId>, albumIds) // "Track"."MediaTypeId", "Track"."GenreId", "Track"."Composer",
    .All();                                          // "Track"."Milliseconds", "Track"."Bytes", "Track"."UnitPrice"
                                                     // FROM "Track"
                                                     // WHERE "AlbumId" IN (193, 194, 195)



dm->Query<Customer, Employee>()                                  // SELECT "Customer"."CustomerId", "Customer"."FirstName", .... ,
    .InnerJoin<&Employee::EmployeeId, &Customer::SupportRepId>() // "Employee"."EmployeeId", "Employee"."LastName", ...., "Employee"."Email"
                                                                 // FROM "Customer"
                                                                 // INNER JOIN "Employee" ON "Employee"."EmployeeId" = "Customer"."SupportRepId"

```
