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

```
CreateTable("Appointment").PrimaryKeyWithAutoIncrement("id", Guid{})
                  .RequiredColumn("date", DateTime {} )
                  .Column("comment", Varchar{80})
                  .ForeignKey("physician_id", Guid{}, SqlForeignKeyReferenceDefinition {
                                                          .tableName = "Physician",
                                                          .columnName = "id",
                                                      } )
                  .ForeignKey("patient_id", Guid{}, SqlForeignKeyReferenceDefinition {
                                                          .tableName = "Patient",
                                                          .columnName = "id",
                                                      } );
```

## Insert elements
To insert elements in the database first call `FrommTable(table)` function to specify which table to use, and then function `Insert()` to start construction of `SqlInsertQueryBuilder`
* `Set(std::string_view columnName, ColumnValue const& value)`
  -  Adds a single column to the INSERT query.

## Select elements
To select some elements from the Database you first need to specify which existing table you are going to use, for this use `FromTable(table)` function, it returns you an instance of a `SqlQueryBuilder` and then use `Select()` function to continue constructing select query that described by `SqlSelectQueryBuilder` interface. Here we present a compressed list of functions that can be used to create complete selection query.  

* Select field
  - `Distinct()`
    + Adds a DISTINCT clause to the SELECT query.
  - `Field()`
    + Simple usage `Field("field")` 
    + With table name specification as `Field(SqlQualifiedTableColumnName { "Table", "field" })`
  - `Fields()`
    + Simple usage `Fields({"a", "b", "c"})`
    + Fields from another table `Fields({"a", "b", "c"}, "Table_B")`
    + Choose all fields of a structure that represents table `Field<TableType>()`. Note: can pass more than one type
* (optional) Order and Group
  - `OrderBy`
  - `GroupBy`
* (optional) Additional option to build WHERE clause. See documentation for `SqlWhereClauseBuilder` 
  - `Where`
    + `Where("a",1)` specify simple condition that is equivalent to the sql query `WHERE "a" = 1`
    + `Where(SqlQualifiedTableColumnName{ .tableName = "Table_A", .columnName = "a"}, 1)` such call translated into `WHERE "Table_A".a" = 1`
  - `Or()`, `And()` and `Not()` logical functions to apply to the next call
    + Example of usage `Where("a",1).Or().Where("b",1)`
  -  `Inner`|`LeftOuter`|`RightOuter`|`FullOuter` + `Join`
    + See documentation for `SqlJoinConditionBuilder` for details 
* End
  - `First()`
   + Specify number of elements to fetch, by default only one element will be fetched.
  - `All()`


### Example

```
q.FromTable("Table_A")
                .Select()
                .Fields({ "foo"sv, "bar"sv }, "Table_A")
                .Fields({ "that_foo"sv, "that_id"sv }, "Table_B")
                .LeftOuterJoin("Table_B",
                               [](SqlJoinConditionBuilder q) {
                               return q.On("id", { .tableName = "Table_A", .columnName = "that_id" })
                                       .On("that_foo", { .tableName = "Table_A", .columnName = "foo" });
                               })
                .Where(SqlQualifiedTableColumnName { .tableName = "Table_A", .columnName = "foo" }, 42)
                .All()
```
