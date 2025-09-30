# "Lightweight SQL Library"

## "A Story on the evolution of a C++ SQL library"

```sql
CREATE TABLE Persons (
    ID INT PRIMARY KEY,
    FirstName VARCHAR(100),
    LastName VARCHAR(100)
);

INSERT INTO Persons (ID, FirstName, LastName) VALUES (1, 'John', 'Doe');

SELECT "ID", "FirstName", "LastName"
  FROM "Persons"
 WHERE "LastName" = 'Doe'
 LIMIT 10;
```

## Agenda

1. Who am I
2. Who we are
3. Historical approach
4. Core API & how to bring the type system together with database access
5. How to construct queries generically
6. Data modelling on top of the existing core APIs
7. Outlook

---

## Who am I

- C++ developer for more than 30 years
- My very first professional programming job: C#, ASP.NET, booking site for hotels (NOT booking.com)
- Also worked at Ruby on Rails e-shop startups ("to learn from the pros")
- Worked twice in the ads business (serving, bidding, analytics) 
  - generating terabytes of daily data stored in SQL databases
- Open source advocate and contributor (various, mostly C++)
- **|> Lifetime project <| ** Contour terminal emulator ❤️
- Now working as chief of development at **LASTRADA**, the sponsor today's C++ meetup in Helsinki 🎉

## Disclaimer

- The views expressed in this presentation are purely my own.
- The slides are made for the `slides` CLI tool. But this tool turned out to be a bit buggy 😭

---

## Who we are

- A mid-sized software company
- We develop a data centric desktop (and mobile) application

### Our main product

- Quality control software for the construction industry (Asphalt, Concrete, Soil, ...)
- also, supervise and control the lifecycle of samples
  - LIMS (Laboratory Information Management System)
- The company's software is about 30 years old with very little employee fluctuation
- The code base is almost exclusively in C++ with more than 3k cpp files
- We use ODBC to access SQL databases (primarily MS SQL Server and PostgreSQL)
- Depending on the client, the application has to handle thousands of records efficiently

---

## Database access in our code base

- Our code base uses "business objects" (BO) to represent data and logic.
- Each business object represents a table record in the database and the business logic around it.

```cpp
class DB_Table {
  public:
    Table();
    virtual ~Table();

    // Generic getters and setters for different data types
    String GetString(int column) const;
    void SetString(int column, const String& value);
    // ... other data types, e.g., bool, int, double, DateTime, etc.

    // Other generic methods...
    virtual Schema const& GetSchema() const = 0;
    virtual String GetTableName() const;
    virtual void LoadRecursive(int /*maxDepth*/) {}
    virtual void SaveData();
    // ... more methods

  private:
    Array<DB_Variant> _columns; // holds the actual data
};
```

---

## Database access... (Representation of a column value)

```cpp
class DB_Variant {
  public:
    enum Type { TYPE_NULL, TYPE_INT, TYPE_STRING, /* ... other types */ };

    Variant();
    Variant(int value);
    Variant(const String& value);
    // ... other constructors

    const bool IsNull() const;
    const Type GetType() const;       // can convert
    const int AsInt() const;          // can convert
    const String AsString() const;    // can convert
    // ... other type conversions

    void SetNull();
    void Set(int value);
    void Set(const String& value);

  private:
    // C-style data representation
};
```

---

## Database access... (How we defined a business object)

```cpp
// Way to define a business object, Person:
class Person : public DB_Table {
public:
    enum {
        ID,           // Order must match the SQL table definition
        FIRST_NAME,
        LAST_NAME,
        MAX_COLUMN,
    };

    Schema const& GetSchema() const override;

    int Id() const { return GetInt(ID); }
    void SetId(int id) { SetInt(ID, id); } // usually manually written too

    String GetFirstName() return GetString(FIRST_NAME); }
    void SetFirstName(const String& name) { SetString(FIRST_NAME, name);

    String GetLastName() return GetString(LAST_NAME); }
    void SetLastName(const String& name) { SetString(LAST_NAME, name);

    void IntroduceTo(const Person& other); // example of business logic
};
```

---

## Benefits of this design

- Business objects are easy to pass around and use in the code.
- Business logic is encapsulated within the business object.
- Easy access to all columns via generic getters and setters.
- Easy access to referenced objects via `LoadRecursive()` (think of SQL foreign keys).

---

## The problem we are facing

- Dated legacy API design decisions.
  - Minimal use of modern C++ features (e.g., no STL, no smart pointers, no move semantics, no constexpr, ...)
  - No unit tests
  - **a lot** of indirections and abstractions
  - Every time we add a new business object
  - Every time we add a new column
  - Not designed with runtime efficiency in mind
- This leads to a lot of copy-paste and makes it hard to maintain the codebase.
- **Performance** is of concern, as we need to handle thousands of records efficiently.
- **Separation of concerns:** business logic should be separate from data access logic.

### Performance and static typing are issues right now for us

We needed to rewrite some part of our program to address critical performance issues.

---

## Our goals

- ❗Improve runtime performance of database operations (Ideally: zero-cost abstractions).

- **[App Devs]** Code should be more expressive and easier to read.
- **[Library Devs]** Reduce the cognitive load for developers defining and using data models.
- **[Library Devs]** Well covered unit tests.
- **[Wishful]** Have a better way to define and manage our data models

### Mission: Create a lightweight SQL Library

We need:

1. Performance: Fast and efficient database access (no needless indirections, no needless conversions)
1. Minimize C++ boilerplate code when defining business objects (data models).

---

## The core Lightweight SQL Library

- Have a `SqlConnection` to manage an SQL connection
- Have a `SqlStatement` to prepare and execute SQL statements and read results
- Have a `SqlTransaction` to manage transactions

```cpp
void main()
{
    auto conn = SqlConnection {};
    conn.Connect("DSN=mydb;UID=user;PWD=password;");

    auto stmt = SqlStatement { conn };
    stmt.Prepare("INSERT INTO Persons (FirstName, LastName) VALUES (?, ?, ?)");
    stmt.BindOutputColumn(1, 42);                   // <-- pre-binding output columns
    stmt.BindOutputColumn(2, "John"sv);
    stmt.BindOutputColumn(3, "Doe"sv);
    stmt.Execute();

    while (stmt.FetchRow())
    {
        auto const id = stmt.GetInt(1);             
        auto const firstName = stmt.GetColumn<std::string>(2);
        auto const lastName = stmt.GetColumn<std::string>(3);
        // Process the row data...
    }
}
```

---

## How does data binding work?

- similar to `std::formatter<T>`

```cpp
template <typename>
struct SqlDataBinder;

template <>
struct SqlDataBinder<int>
{
    static SQLRETURN InputParameter(...) noexcept;
    static SQLRETURN OutputColumn(...) noexcept;
    static SQLRETURN GetColumn(...) noexcept;
    static std::string Inspect(...);
};

template <>
struct SqlDataBinder<CustomType>
{
    static SQLRETURN InputParameter(...) noexcept;
    static SQLRETURN OutputColumn(...) noexcept;
    static SQLRETURN GetColumn(...) noexcept;
    static std::string Inspect(...);
};
```

---

## Custom SQL data binder

```cpp
struct CustomType { int value; };

template <>
struct SqlDataBinder<CustomType>
{
    static SQLRETURN InputParameter(auto& stmt, auto column, CustomType const& value, auto& cb) noexcept
    {
        // Imagine special logic
        // ...
        return SqlDataBinder<int>::InputParameter(hStmt, column, value.value, cb);
    }

    // ... and the others
};
```

---

## Native Batch Execution

Supported types any input_range with a fixed size element type


```cpp
void DemoMassOperations()
{
    auto stmt = SqlStatement {};
    stmt.Prepare(R"(INSERT INTO "Test" ("A", "B", "C") VALUES (?, ?, ?))");

    auto const firstColumn = ...; 
    auto const secondColumn = ...;
    auto const thirdColumn = ...;

    stmt.ExecuteBatchNative(firstColumn, secondColumn, thirdColumn);
}
```

## Example of batch input data
| firstColumn | secondColumn | thirdColumn | 
|-------------|--------------|-------------|
|      1      |      1.1     |     "1"     |
|      2      |      2.2     |     "2"     |
|      3      |      3.3     |     "3"     |
|      4      |      4.4     |     "4"     |
|      5      |      5.5     |     "5"     |
|      6      |      6.6     |     "6"     |

---

## Core (low level) API summary

| Class              | description
|--------------------|-------------------------------------------------------
| `SqlConnection`    | handling an SQL connection
| `SqlStatement`     | handling prepared, direct, and bulk statements, including data bindings
| `SqlDataBinder<T>` | non-intrusive extending column data type support
| `SqlTransaction`   | handling SQL transactions (commit, rollback)

## So what's missing?

- SQL dialect agnostic query building!

---

## SQL query builder

```cpp
void main()
{
    auto conn = SqlConnection {};
    conn.Connect("DSN=mydb;UID=user;PWD=password;");

    // SQL queries may differ based on SQL dialects (e.g., LIMIT vs TOP)
    auto stmt = SqlStatement { conn };
    stmt.Prepare(conn.FromTable("Person")          // <-- query builder constructed from connection
                     .Select()
                     .Fields({"ID", "FirstName", "LastName"})
                     .Where("LastName", "Doe")
                     .OrderBy("FirstName", SqlResultOrdering::ASCENDING)
                     .Limit(10));                  // MS SQL & Oracle != PostgreSQL & MySQL
    stmt.Execute();

    while (stmt.FetchRow())
    {
        auto const id = stmt.GetColumn<int>(1);
        auto const firstName = stmt.GetColumn<std::string>(2);
        auto const lastName = stmt.GetColumn<std::string>(3);
        // Process the row data...
    }
}
```

---

## Examples of a more complex SQL query with joins

```SQL
SELECT "JoinTestA"."id", "JoinTestA"."value_a_first", "JoinTestA"."value_a_second", "JoinTestA"."value_a_third", "JoinTestC"."id", "JoinTestC"."value_c_first", "JoinTestC"."value_c_second", "JoinTestC"."value_c_third" 
    FROM "JoinTestA"
    INNER JOIN "JoinTestB" ON "JoinTestB"."a_id" = "JoinTestA"."id"
    INNER JOIN "JoinTestC" ON "JoinTestC"."id" = "JoinTestB"."c_id"
```


```cpp
conn.FromTable(RecordTableName<JoinTestA>)
                    .Select()
                    .Fields<JoinTestA, JoinTestC>()
                    .InnerJoin<&JoinTestB::a_id, &JoinTestA::id>()
                    .InnerJoin<&JoinTestC::id, &JoinTestB::c_id>()
                    .All()
```

- Library provides helper templates to extract table and column names from types and members.
- For example: `RecordTableName<T>`, `FieldNameOf<&T::member>` and `Fields<T...>`.

---

## SQL query builder, pre-binding

```cpp
void main()
{
    // ... same as before
    stmt.Execute();

    auto id = 0;                stmt.BindColumn(1, &id); // Coding style purely for presentation
    auto firstName = String {}; stmt.BindColumn(2, &firstName);
    auto lastName = String {};  stmt.BindColumn(3, &lastName);

    while (stmt.FetchRow())
    {
        // Process the row data...
        std::println("{}: {}, {}", id, lastName, firstName);
    }
}
```

### Example output could be like

Id | FirstName | LastName
---|-----------|--------
1  | John      | Doe
2  | Jane      | Doe
3  | Jim       | Johnson

---

## Modeling data

- Business objects combine data **and** logic in one
- No separation of concerns

### Ideal solution

```cpp
struct Person
{
    int Id;
    std::string FirstName;
    std::string LastName;
}
```

### Active record pattern

```cpp
void main()
{
    Person person;

    person.Id = 1;
    person.FirstName = "Jeff";
    person.LastName = "Clintfield";

    person.Create(); // <-- Where does this come from?
}
```

---

## Active record pattern (How to define a data model)

- A data modeling API on top of the core API from 
- `Field<T>` poor-man's C++ annotation system (also to track modification state)

```cpp
template <typename Record>                                 | template <typename T>
struct ActiveRecord                                        | class Field
{                                                          | {
    void Create();                                         |   public:
    void Update();                                         |     // ...
    void Delete();                                         |     Field& operator=(T&& value);
                                                           |     Field& operator=(T const& value);
    static Record Find(auto id);                           | 
    static std::vector<Record> FindAll(auto whereClause);  |     bool IsModified() const;
    // and more functions to operate on the database ...   |     void SetModified(bool value);
};                                                         | 
                                                           |   private:
                                                           |     T _value;
                                                           |     bool _modified;
                                                           | };
```

---

## Active record pattern (How to define a data model)

Example

```cpp
struct Person: ActiveRecord<Person>
{
    Person();                                  // <-| problem | TODO: show one impl to demo boilerplate
    Person(Person&& moveFrom);                 // <-| problem
    Person(Person const& copyFrom);            // <-| problem
    Person& operator=(Person const& copyFrom); // <-| problem
    Person& operator=(Person&& moveFrom);      // <-| problem
    ~Person();                                 // <-| problem

    Field<int> Id;
    Field<std::string> FirstName;
    Field<std::string> LastName;
};
```

- Implementation of these create a huge boilerplate
- But needed for CRUD functions to work

---

## Sidestepping into the Dapper framework

Dapper is a simple object mapper for .NET, written in C#.

```csharp
public class Dog
{
    public int? Age { get; set; }
    public Guid Id { get; set; }
    public string Name { get; set; }
    public float? Weight { get; set; }

    public int IgnoredProperty { get { return 1; } }
}

var guid = Guid.NewGuid();
var dog = connection.Query<Dog>("select Age = @Age, Id = @Id", new { Age = (int?)null, Id = guid });
```

Wouldn't that be nice for C++?

---

## SQL Data Mapper API (Declaration and Creation)

Back to basics (or almost):

```cpp
struct Person
{
    Field<int, SqlPrimaryKey::AUTO_INCREMENT> Id;
    Field<std::string> FirstName;
    Field<std::string, SqlRealName{"LAST_NAME2"}> LastName;
};

void CreateDemo()
{
    auto dm = DataMapper {};

    dm.CreateTable<Person>();
    auto person = Person {};
    person.FirstName = "Jeff";
    person.LastName = "Johnson";

    dm.Create(person);
    std::println("New person spawned with primary key {}.", person.Id);
}
```

Example output: `New person spawned with primary key 42.`

---

## SQL Data Mapper API

Back to basics (or almost):

```cpp
struct Person
{
    Field<int, SqlPrimaryKey::AUTO_INCREMENT> Id;
    Field<std::string> FirstName;
    Field<std::string, SqlRealName{"LAST_NAME2"}> LastName;
};

void PersonGotMarried(DataMapper& dm, Person& person)
{
    person.LastName = "Johnson, the Married";

    dm.Update(person);
    std::println("Married person's record: {}", DataMapper::Inspect(person));
}
```

Example Output: `Married person's record: <Id: 42, FirstName: "Jeff", LAST_NAME_2: "Johnson the Married.">`

---

## BelongsTo relationship 

Foreign keys modeled using `BelongsTo<MemberPtr>` member

```cpp
struct User
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id {};
    Field<SqlAnsiString<30>> name {};
};

struct Email
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id {};
    Field<SqlAnsiString<30>> address {};
    BelongsTo<&User::id, SqlRealName { "user_id" }> user {}; // foreign key to User
};


auto dm = Light::DataMapper{};
auto email = dm.QuerySingle<Email>(some_email_id).value_or(Email{});
auto user_name = email.user->name; // lazily loads the user record
```

---

## HasMany relationship

Inverse relationship to BelongsTo is modeled using `HasMany<T, MemberPtr>`

```cpp

struct User
{
    Light::Field<Light::SqlGuid, Light::PrimaryKey::AutoAssign> id {};
    Light::Field<Light::SqlAnsiString<30>> name {};

    Light::HasMany<Email> emails {};
};


auto johnDoe = dm.QuerySingle<User>(some_user_id).value_or(User{});
for (auto const& email: johnDoe.emails)
{
    // process email
}
```

---

## Using DataMapper to retrieve data

```cpp
auto const records = dm.Query<Person>()
                       .Where(FieldNameOf<&Person::is_active>, "=", true)
                       .All<&Person::name, &Person::age>();

for (auto const& person : records)
{
    // only person.name and person.age are populated 
}
```

```cpp
struct PartOfC
{
    uint64_t id {};
    SqlAnsiString<20> comment {};

    static constexpr std::string_view TableName = "C"; // optional, defaults to struct name
};

auto const records = dm.Query<CustomBindingA, CustomBindingB, PartOfC>()
                       .InnerJoin<&CustomBindingB::a_id, &CustomBindingA::id>()
                       .InnerJoin<&CustomBindingC::id, &CustomBindingB::c_id>()
                       .OrderBy(QualifiedColumnName<"A.id">)
                       .All();

for (auto const& [a, b, c]: records)
{
    // a is CustomBindingA, b is CustomBindingB, c is PartOfC
}
```

---

## Generate structures from database schema

- We can generate C++ structures from an existing database schema using ddl2cpp tool

```bash
Shell⚡ ddl2cpp --help

Usage: ddl2cpp [options] [database] [schema]
Options:
  --trace-sql             Enable SQL tracing
  --connection-string STR ODBC connection string
  --database STR          Database name
  --schema STR            Schema name
  --create-test-tables    Create test tables
  --output STR            Output directory, for every table separate header file will be created
  --generate-example      Generate usage example code
                          using generated header and database connection
  --make-aliases          Create aliases for the tables and members
  --naming-convention STR Naming convention for aliases
                          [none, snake_case, camelCase]
  --no-warnings           Suppresses warnings
  --help, -h              Display this information
```

---

## Example of generated structure

```bash
Shell⚡ bat ./src/examples/test_chinook/entities/Album.hpp
```

```cpp
// File is automatically generated using ddl2cpp.
#pragma once

#include "Artist.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Album final
{
    static constexpr std::string_view TableName = "Album";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "AlbumId" }> AlbumId;
    Light::Field<Light::SqlDynamicUtf16String<160>, Light::SqlRealName { "Title" }> Title;
    Light::BelongsTo<&Artist::ArtistId, Light::SqlRealName { "ArtistId" }> ArtistId;
};
```

---

## Outlook (C++26 annotations)

List of changes that we are considering as an outlook and C++26 reflection support in mind 

* Use annotations instead of `Field<T>` wrapper
```cpp
struct Field
{

    [[=PrimaryKey::ServerSideAutoIncrement, =SqlRealName("TrackId")]] 
    int32_t TrackId;
    std::string Name;
    [[=SqlRealName("OVERWRITE_BYTES")]] 
    std::optional<int32_t> bytes;
    [[=SqlRealName("AlbumId"), =SqlNullable::Null]] 
    BelongsTo<^^Album::AlbumId> AlbumId;
};
```

---

## Outlook (DB migrations)

* Generate migration script from the data model definition

```cpp
struct Field
{
    // ... other fields ...

    [[=SqlRealName("NewColumn"), =SqlNullable::Null, =Version("2025-10-07-03:14:15")]] 
    std::optional<int32_t> NewColumn;
};

// This collects all version changes and generates migration scripts
dm.MigrateToVersion("2.0"); 

```

- Find shortest path through relationships at compile time to generate required joins
- Generate C++ implementations that use the query builder directly to improve compilation times (inspired by [2025 CppCon Sutter])


---


## Get in touch

Repository: https://github.com/LASTRADA-Software/Lightweight 

```
██████████████████████████████████████████████████████████████████████
██              ████      ██  ████    ████  ████  ████              ██
██  ██████████  ██████    ██          ████    ████████  ██████████  ██
██  ██      ██  ██      ██    ██████    ████    ██████  ██      ██  ██
██  ██      ██  ██    ██    ██      ██████  ██  ██  ██  ██      ██  ██
██  ██      ██  ██        ██  ██  ████  ████  ██  ████  ██      ██  ██
██  ██████████  ██  ██████              ██  ██████████  ██████████  ██
██              ██  ██  ██  ██  ██  ██  ██  ██  ██  ██              ██
██████████████████  ██  ██████    ██████████  ████████████████████████
██  ██          ████  ████          ██████████████████          ██████
████    ██  ██████  ████    ████    ██████    ██  ████    ██    ██  ██
████  ██          ████    ██████    ██  ██████  ██  ██  ██  ██    ████
██  ████████  ████  ████  ████      ██████      ██    ████          ██
████  ████████            ████████████    ████        ██      ██  ████
████  ████    ██      ██            ██████  ████  ████  ██████      ██
██    ██    ██  ██  ████    ████████████  ████  ████      ██      ████
██    ████████████████      ██  ██  ██        ██  ██    ██      ██████
██  ████████    ██████████    ██████████  ████  ██    ██      ████  ██
████  ██    ████    ██    ██████████  ████  ████  ████    ██        ██
██      ██████            ████        ████    ████    ██    ██    ████
██    ██    ████  ██    ██    ██  ██          ██████            ██████
██  ██████████  ████  ████████      ████    ██  ████  ██  ██  ██  ████
██  ████████  ████  ████  ██  ██  ████  ████      ████  ██████  ██  ██
██  ██      ██      ██      ██          ██  ██████    ████  ████  ████
██  ██  ████  ██████    ██  ████  ████  ████    ████    ██      ██████
██  ████  ████              ████  ████████████  ██          ██████████
██████████████████  ██████████      ██    ██  ██    ██████  ██      ██
██              ████        ████  ████  ██          ██  ██  ██    ████
██  ██████████  ██    ██  ██  ██    ██████    ██    ██████      ██  ██
██  ██      ██  ██      ████  ██  ████    ██  ████            ██  ████
██  ██      ██  ██    ██████        ██████  ████      ████          ██
██  ██      ██  ██  ██    ██    ██████    ████  ██████    ██    ██████
██  ██████████  ██████████  ██████  ████          ████  ██  ██  ██████
██              ██  ██  ████  ██████████  ████      ████  ██████  ████
██████████████████████████████████████████████████████████████████████
```
