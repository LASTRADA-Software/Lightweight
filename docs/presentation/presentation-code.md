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

---

## Who am I

- C++ developer for more than 30 years
- My very first professional programming job: C#, ASP.NET, booking site for hotels (NOT booking.com)
- Also worked at Ruby on Rails e-shop startups ("to learn from the pros")
- Worked twice in the ads business (serving, bidding, analytics) 
  - generating terabytes of data daily (into SQL databases)
- Open source advocate and contributor (various, mostly C++)
- **|> Lifetime project <| ** Contour terminal emulator ❤️
- Now working at as chief of development at **LASTRADA**, the sponsor today's C++ meetup in Helsinki 🎉

## Disclaimer

- The views expressed in this presentation are purely my own.
- The slides are made for the `slides` CLI tool. But this tool turned out to be a bit buggy 😭

---

## Who we are

- Not too small software company, not too big
- We develop a data centric desktop (and mobile) application

### Our main product

- Quality control software for construction industry (Asphalt, Concrete, Soil, ...)
- also, supervise and control the lifecycle of probes and samples
  - LIMS (Laboratory Information Management System)
- The company's software is about 30 years old with very little employee fluctuation
- The code base is almost exclusively in C++ with more than 3 million lines of code
- We use ODBC to access SQL databases (for MS SQL Server, PostgreSQL, ...)
- Depending on the client, the application creates thousands of records per day

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
// Classic way to define a business object, Person:
class Person : public DB_Table {
public:
    enum {
        ID,           // Order must match the SQL table definition
        FIRST_NAME,
        LAST_NAME,
        MAX_COLUMN,
    };

    Schema const& GetSchema() const override; // <-- interesting part

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
  - Little to no use of standard C++ features (e.g., no STL, no smart pointers, no move semantics, no constexpr, ...)
  - No unit tests -> hard to refactor
  - **a lot** of indirections and abstractions -> increases runtime overhead and cognitive load
  - Every time we add a new business object -> we have to write repetitive code.
  - Every time we add a new column -> we have to add too much boilerplate code.
  - Not designed with runtime efficiency in mind. -> crucial!
- This leads to a lot of copy-paste and makes it hard to maintain the codebase.
- **Performance** is of concern, as we need to handle thousands of records efficiently.
- **Separation of concerns:** business logic should be separate from data access logic.

### Performance is an issue right now for us

We needed to rewrite some part of our program to address critical performance issues.

### Our goals

- ❗Improve runtime performance of database operations (Ideally: zero-cost abstractions).

- **[App Devs]** Code should be more expressive and easier to read.
- **[Library Devs]** Reduce the cognitive load for developers defining and using data models.
- **[Wishful]** Have a better way to define and manage our data models <- without too much boilerplate.

---

## Mission: Create a lightweight SQL Library

We need:

1. performance: Fast and efficient database access (no needless indirections, no needless conversions)

and

1. Minimize C++ boilerplate code when defining business objects (data models).

and

1. We only use ODBC as the database access layer, so only support ODBC.

---

## Generation 1: The core Lightweight SQL Library

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
        auto const id = stmt.GetInt(1);             // XXX did you get the indices right?
        auto const firstName = stmt.GetString(2);
        auto const lastName = stmt.GetString(3);
        // Process the row data...
    }
}
```

---

## Generation 1: How does data binding work?

- similar to `std::formatter<T>`

```cpp
template <>
struct SqlDataBinder;

template <>
struct SqlDataBinder<int>
{
    static SQLRETURN InputParameter(SQLHSTMT stmt, SQLUSMALLINT column, int value, SqlDataBinderCallback& cb) noexcept;
    static SQLRETURN OutputColumn(SQLHSTMT stmt, SQLUSMALLINT column, int* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept;
    static SQLRETURN GetColumn(SQLHSTMT stmt, SQLUSMALLINT column, int* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept;
    static std::string Inspect(int value);
};
```

---

## Generation 1: Custom SQL data binder

```cpp
struct CustomType { int value; };

template <>
struct SqlDataBinder<CustomType>
{
    static SQLRETURN InputParameter(SQLHSTMT stmt, SQLUSMALLINT column, CustomType const& value, SqlDataBinderCallback& cb) noexcept
    {
        // Example special logic
        if (cb.ServerType() == SqlServerType::MICROSOFT_SQL)
            return SqlDataBinder<int>::InputParameter(hStmt, column, value.value * -1, cb);

        return SqlDataBinder<int>::InputParameter(hStmt, column, value.value, cb);
    }

    // ... and the others
};
```

---

## Generation 1: Custom SQL data binder (special-cases API)

```cpp
class LIGHTWEIGHT_API SqlDataBinderCallback
{
  public:
    // ...

    virtual SqlServerType ServerType() const noexcept = 0;
    virtual std::string const& DriverName() const noexcept = 0;

    // called after SqlStatement.Execute() has executed
    virtual void PlanPostExecuteCallback(std::function<void()>&&) = 0;

    // called after each fetched row
    virtual void PlanPostProcessOutputColumn(std::function<void()>&&) = 0;
};
```

---

## Generation 1: Native Batch Execution

```cpp
void DemoMassOperations()
{
    auto stmt = SqlStatement {};
    stmt.Prepare(R"(INSERT INTO "Test" ("A", "B", "C") VALUES (?, ?, ?))");

    auto const first = std::array<Lightweight::SqlFixedString<8>, 3> { "Hello", "World", "!" };
    auto const second = std::vector { 1.3, 2.3, 3.3 };
    unsigned const third[3] = { 50'000u, 60'000u, 70'000u };

    stmt.ExecuteBatchNative(first, second, third); // <-- insert all rows
}
```

## Generation 1: low level API summary

| Class              | description
|--------------------|-------------------------------------------------------
| `SqlConnection`    | handling an SQL connection
| `SqlStatement`     | handling prepared, direct, and bulk statements, including data bindings
| `SqlDataBinder<T>` | non-intrusive extending column data type support
| `SqlTransaction`   | handling SQL transactions (commit, rollback)

## So what's missing?

- SQL dialect agnostic query building!

---

## Generation 1: SQL query builder

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
        auto const id = stmt.GetInt(1);
        auto const firstName = stmt.GetString(2);
        auto const lastName = stmt.GetString(3);
        // Process the row data...
    }
}
```

---

## Generation 1: (Continued): SQL query builder, pre-binding

```cpp
void main()
{
    auto conn = SqlConnection {};
    conn.Connect("DSN=mydb;UID=user;PWD=password;");

    auto stmt = SqlStatement { conn };
    stmt.Prepare(conn.FromTable("Person") // <-- query builder constructed from connection
                     .Select()
                     .Fields({"ID", "FirstName", "LastName"})
                     .Where("LastName = ?")
                     .OrderBy("FirstName", SqlResultOrdering::ASCENDING)
                     .Limit(10));
    stmt.Bind(1, "Doe");
    stmt.Execute();

    auto id = 0;                stmt.BindColumn(1, &id); // Coding style prely for presentation
    auto firstName = String {}; stmt.BindColumn(2, &firstName);
    auto lastName = String {};  stmt.BindColumn(3, &lastName);

    while (stmt.FetchRow())
    {
        // Process the row data...
        std::println("{}: {}, {}", id, lastName, firstName);
    }
}
```
