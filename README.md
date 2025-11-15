# Lightweight, an ODBC SQL API for C++23

**Lightweight** is a thin and modern C++ ODBC wrapper for **easy** and **fast** raw database access.
Documentation is available at [https://lastrada-software.github.io/Lightweight/](https://lastrada-software.github.io/Lightweight/).

It supports both low-level access to the SQL API as well as provides hight level abstraction that allow easy database access.

Here you can see an example of datamapper usage (our tool for the high level abstraction)

## Supported platforms

Only ODBC is supported, so it should work on any platform that has an ODBC driver and
a modern enough C++ compiler.

- Windows (Visual Studio 2022, toolkit v143)
- Linux (GCC 14, Clang 19)

## Supported databases

- Microsoft SQL
- PostgreSQL
- SQLite3
- Oracle database (work in progress)

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

In the presented example we used rename of the columns, for more details see how-to\#rename-column-name page.
you can query the email and get access to the user record as well

```cpp
auto dm = Light::DataMapper::Create();
auto email = dm->QuerySingle<Email>(some_email_id).value_or(Email{});
auto user_name = email.user->name; // lazily loads the user record
```

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
auto dm = Light::DataMapper::Create();
auto query = dm->FromTable(RecordTableName<CustomBindingA>)
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

auto const records = dm->Query<CustomBindingA, CustomBindingB, PartOfC>(query);
for (auto const& [a, b, c]: records)
{
    // ...
}
```


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
cmake --preset linux-clang-debug -DLIGHWEIGHT_EXAMPLE=ON -B build
```

Finally, compile and run the example

``` sh
cmake --build build && ./build/src/examples/example
```

## Compile using C++26 reflection support

``` sh
docker buildx build --progress=plain -f .github/DockerReflection --load .
```
