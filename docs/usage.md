# Usage Examples

## Configure default connection information to the database

To connect to the database you need to provide connection string that library uses to establish connection and you can check if it is alive in the following way
```cpp
SqlConnection::SetDefaultConnectionString(SqlConnectionString { 
    .value = std::format("DRIVER=SQLite3;Database=test.sqlite")
});

auto sqlConnection = SqlConnection();
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

## Prepared Statements

You can also use prepared statements to execute queries, for example
```cpp

struct Record { int a; int b; int c; };
auto conn = SqlConnection {};
auto stmt = SqlStatement { conn };
stmt.Prepare("SELECT a, b, c FROM That WHERE a = ? OR b = ?");
stmt.Execute(42, 43);

SqlResultCursor cursor = stmt.GetResultCursor();
auto record = Record {};
cursor.BindOutputColumns<Record>(&record.a, &rcord.b, &record.c);
while (cursor.FetchRow())
    std::println("{}|{}|{}", a, b, c);
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
    auto stmt = SqlStatement { dm.Connection() };
    for(const auto& person: SqlRowIterator<Person>(stmt))
        std::println("|{}|{}|", person.name, person.age);

    // Delete the person
    dm.Delete(person);
}
```

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
    if (auto maybeObject = dm.Query<SimpleString>(
        "SELECT A.pk, B.pk, A.c1, A.c2, B.c1, B.c2 FROM A LEFT JOIN B ON A.pk = B.pk"); maybeObject)
    ))
    {
        for (auto const& obj : *maybeObject)
            std::println("{}", DataMapper::Inspect(obj));
    }
}
```
