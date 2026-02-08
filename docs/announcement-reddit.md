# Announcing Lightweight: Zero-overhead C++23 ODBC wrapper

> This document serves as the announcement post for Reddit r/cpp and other platforms.

---

**Title:** Lightweight: Zero-overhead C++23 ODBC wrapper with DataMapper ORM, migrations, and backup/restore

---

Hi r/cpp,

We're excited to share **Lightweight**, a modern C++23 ODBC wrapper we've been building to solve our need for high-level SQL access without runtime overhead.

**Philosophy:** We sacrifice compile time for runtime performance. Heavy use of `constexpr`, templates, and compile-time reflection techniques means the high-level API compiles down to near-raw ODBC calls.

**GitHub:** https://github.com/LASTRADA-Software/Lightweight/  
**Docs:** https://lastrada-software.github.io/Lightweight/

---

## Low-Level API: SqlStatement & SqlConnection

For those who want direct control, the core API is clean and minimal:

```cpp
auto stmt = SqlStatement {};

// Direct execution
stmt.ExecuteDirect("SELECT * FROM Users WHERE age > 21");
while (stmt.FetchRow())
    std::println("{}: {}", stmt.GetColumn<int>(1), stmt.GetColumn<std::string>(2));

// Prepared statements with type-safe binding
stmt.Prepare(R"(INSERT INTO Employees (name, department, salary) VALUES (?, ?, ?))");
stmt.Execute("Alice", "Engineering", 85'000);
stmt.Execute("Bob", "Sales", 72'000);

// Output column binding
std::string name(50, '\0');
int salary {};
stmt.Prepare("SELECT name, salary FROM Employees WHERE id = ?");
stmt.BindOutputColumns(&name, &salary);
stmt.Execute(42);
```

### Bulk Insertions

Insert thousands of rows efficiently with a single call:

```cpp
stmt.Prepare(R"(INSERT INTO Employees (name, department, salary) VALUES (?, ?, ?))");

// Works with mixed container types
auto names = std::array { "Alice"sv, "Bob"sv, "Charlie"sv };
auto depts = std::list { "Eng"sv, "Sales"sv, "Ops"sv };  // even non-contiguous!
unsigned salaries[] = { 85'000, 72'000, 68'000 };

stmt.ExecuteBatch(names, depts, salaries);  // Single ODBC batch call
```

Three batch methods for different scenarios:
- `ExecuteBatchNative()` - Fastest, requires contiguous memory
- `ExecuteBatchSoft()` - Works with any range (std::list, etc.)
- `ExecuteBatch()` - Auto-selects the best method

---

## DataMapper: High-Level ORM

Define your schema as C++ structs, and the DataMapper handles the rest:

```cpp
struct Person
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<25>> name;
    Field<bool> is_active { true };
    Field<std::optional<int>> age;
};

void Example(DataMapper& dm)
{
    dm.CreateTable<Person>();
    
    auto person = Person { .name = "John", .is_active = true, .age = 30 };
    dm.Create(person);  // INSERT - id auto-assigned
    
    person.age = 31;
    dm.Update(person);  // UPDATE
    
    // Fluent query API
    auto active = dm.Query<Person>()
        .Where(FieldNameOf<&Person::is_active>, "=", true)
        .OrderBy(FieldNameOf<&Person::name>)
        .All();
    
    dm.Delete(person);  // DELETE
}
```

---

## Relationships with Lazy Loading

```cpp
struct User
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<30>> name;
    HasMany<Email> emails;  // One-to-many
};

struct Email
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<100>> address;
    BelongsTo<&User::id, SqlRealName{"user_id"}> user;
};

// Navigate relationships naturally
auto email = dm.QuerySingle<Email>(emailId).value();
auto userName = email.user->name;  // Lazy-loaded

// Or iterate
user.emails.Each([](Email const& e) {
    std::println("Email: {}", e.address.Value());
});
```

Also supports `HasManyThrough` for many-to-many relationships via join tables.

---

## Database Migrations in Pure C++

No external tools or SQL files - define migrations as C++ code:

```cpp
LIGHTWEIGHT_SQL_MIGRATION(20240115120000, "create users table")
{
    using namespace SqlColumnTypeDefinitions;
    
    plan.CreateTable("users")
        .PrimaryKey("id", Guid())
        .RequiredColumn("name", Varchar(50)).Unique().Index()
        .RequiredColumn("email", Varchar(100)).Unique()
        .Column("password", Varchar(100))
        .Timestamps();  // created_at, updated_at
}

// Apply pending migrations
auto& manager = MigrationManager::GetInstance();
manager.CreateMigrationHistory();
size_t applied = manager.ApplyPendingMigrations();
```

Supports rollbacks, dry-run preview, checksum verification, and distributed locking for safe concurrent deployments.

---

## Backup & Restore

Full database backup/restore with progress reporting:

```cpp
#include <Lightweight/SqlBackup.hpp>

// Backup to compressed archive (multi-threaded)
SqlBackup::Backup(
    "backup.zip",
    connectionString,
    4,  // concurrent workers
    progressManager,
    "",              // schema
    "*",             // table filter (glob)
    {},              // retry settings
    { .method = CompressionMethod::Zstd, .level = 6 }
);

// Restore
SqlBackup::Restore("backup.zip", connectionString, 4, progressManager);
```

Preserves indexes, foreign keys (including composite), and supports table filtering.

---

## Supported Databases

- Microsoft SQL Server
- PostgreSQL  
- SQLite3

Works anywhere ODBC works (Windows, Linux).

---

## What's Next

We're actively developing and would love feedback. The library is production-ready for our use cases, but we're always looking to improve the API and add features.

Questions and PRs welcome!
