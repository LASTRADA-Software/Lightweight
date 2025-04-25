# Lightweight, an ODBC SQL API for C++23

**Lightweight** is a thin and modern C++ ODBC wrapper for **easy** and **fast** raw database access.

Documentation is available at [https://lastrada-software.github.io/Lightweight/](https://lastrada-software.github.io/Lightweight/).

## Goals

- **Easy to use** - simple, expressive and intuitive API
- **Production ready** - targeting production grade systems
- **Performance** - do as little as possible, and as much as necessary - **Zero overhead abstraction** is a key design requirement.
- **Extensible** - support for custom data types for writing to and reading from columns
- **Resource aware** - efficient resource management and exception safety

## Example: CRUD-style High level Data Mapping

```cpp
#include <Lightweight/Lightweight.hpp>

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
    name.age = 25;
    dm.Update(person);

    // Query the person
    if (auto const po = dm.QuerySingle<Person>(person.id); po)
        std::println("Person: {} ({})", po->name, DataMapper::Inspect(*po));

    // Query all persons
    auto const persons = dm.Query<Person>(); 

    // Delete the person
    dm.Delete(person);
}
```

## Example: Simple row retrieval via structs

When only read access is needed, you can use a simple struct to represent the row,
and also do not need to wrap the fields into `Field<>` template.
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

## Using SQLite for testing on Windows operating system

You need to have the SQLite3 ODBC driver for SQLite installed.

- ODBC driver download URL: http://www.ch-werner.de/sqliteodbc/
- Example connection string: `DRIVER={SQLite3 ODBC Driver};Database=file::memory:`

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
- Example connection string: `DRIVER=SQLite3;Database=file::memory:`


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

Now you can configure cmake to compile example

``` sh
cmake --preset linux-clang-debug -DLIGHWEIGHT_EXAMPLE=ON -B build
```

Finally, compile and run the example

``` sh
cmake --build build  && ./build/src/examples/example
```
