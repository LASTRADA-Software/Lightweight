# SQL to Lightweight

This page is a side-by-side cookbook: for a given piece of SQL it shows the equivalent
**Lightweight** code. Lightweight offers three layers, and most queries can be expressed in
any of them — pick the one that fits the situation:

| Layer | Entry point | When to reach for it |
|-------|-------------|----------------------|
| **Raw SQL** | `SqlStatement` (`ExecuteDirect`, `Prepare`/`Execute`) | You already have the SQL, need full control, or are running DDL/vendor-specific statements. |
| **Query builder** | `SqlStatement::Query(...)` / `DataMapper::FromTable(...)` | You want the SQL shaped per-DBMS for you (quoting, `LIMIT`/`TOP`, `OFFSET`) but still think in tables and columns. |
| **DataMapper** | `DataMapper::Query<Record>()`, `Create`, `Update`, `Delete` | You map C++ structs to tables and want CRUD, relationships, and type-safe column references. |

All three produce the same SQL against the same database; the query builder and the `DataMapper`
route every dialect difference through `SqlQueryFormatter`, so the same C++ runs unchanged on
SQLite, PostgreSQL, and Microsoft SQL Server.

> Every C++ example on this page is compiled and executed by `src/tests/DocExampleTests.cpp`, and a
> CI check (`scripts/check-doc-snippets.py`) fails the build if the code here ever drifts from the
> tested version. See the *Keeping these examples honest* section at the end of this page.

Examples below assume:

```cpp
#include <Lightweight/Lightweight.hpp>

using namespace Lightweight;   // or qualify everything with Lightweight:: / Light::
```

## The example schema

Most snippets map onto two records and their relationship:

<!-- snippet: doc-schema -->
```cpp
struct Employee; // forward declaration for the relationship below

struct Department
{
    static constexpr std::string_view TableName = "Departments";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<40>> name {};

    HasMany<Employee> employees {}; // one department, many employees
};

struct Employee
{
    static constexpr std::string_view TableName = "Employees";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> firstName {};
    Field<SqlAnsiString<30>> lastName {};
    Field<int> salary {};
    Field<std::optional<int>> age {};

    // FK -> Departments.id. The inverse of Department::employees is matched by relationship
    // type, so the two members may be declared at any position in their records.
    BelongsTo<&Department::id, SqlRealName { "department_id" }, SqlNullable::Null> department {};
};
```

`Field<T>` declares a column; the second template argument customises it
(`PrimaryKey::ServerSideAutoIncrement` lets the database assign the id, a `SqlRealName { "..." }`
overrides the column name, etc.). `FieldNameOf<&Employee::salary>` yields the column name
(`"salary"`, or the `SqlRealName` override) and `FullyQualifiedNameOf<&Employee::salary>` yields
`"Employees"."salary"` — use these instead of hand-writing column-name strings so a rename is caught
at compile time.

---

## SELECT

### Select all rows

```sql
SELECT * FROM "Employees";
```

<!-- snippet: doc-select-all -->
```cpp
// DataMapper — materialises Employee records (relations lazily configured)
auto employees = dm.Query<Employee>().All();

// Skip relationship loading when you only need the row's own columns
auto rows = dm.Query<Employee, DataMapperOptions { .loadRelations = false }>().All();
```

<!-- snippet: doc-select-all-raw -->
```cpp
// Raw SQL
auto stmt = SqlStatement { dm.Connection() };
auto cursor = stmt.ExecuteDirect(R"(SELECT "firstName", "lastName", "salary" FROM "Employees")");
while (cursor.FetchRow())
{
    auto firstName = cursor.GetColumn<std::string>(1); // columns are 1-based
    auto lastName = cursor.GetColumn<std::string>(2);
    auto salary = cursor.GetColumn<int>(3);
    std::println("{} {} {}", firstName, lastName, salary);
}
```

### Select specific columns

```sql
SELECT "firstName", "lastName" FROM "Employees";
```

<!-- snippet: doc-select-columns -->
```cpp
// Query builder
auto query = dm.FromTable("Employees").Select().Fields("firstName", "lastName").All();

// DataMapper — project to a subset of fields; All<>() returns just those values
auto names = dm.Query<Employee, DataMapperOptions { .loadRelations = false }>()
                 .All<&Employee::firstName, &Employee::lastName>();
```

### WHERE — a single condition

```sql
SELECT * FROM "Employees" WHERE "salary" >= 55000;
```

<!-- snippet: doc-where-single-datamapper -->
```cpp
// DataMapper
auto rows = dm.Query<Employee>().Where(FieldNameOf<&Employee::salary>, ">=", 55'000).All();
```

<!-- snippet: doc-where-single-raw -->
```cpp
// Raw, prepared + parameter binding (use ? placeholders, never string concatenation)
auto stmt = SqlStatement { dm.Connection() };
stmt.Prepare(R"(SELECT "firstName", "lastName", "salary" FROM "Employees" WHERE "salary" >= ?)");
auto cursor = stmt.Execute(55'000);
while (cursor.FetchRow())
{
    auto firstName = cursor.GetColumn<std::string>(1);
    std::println("{}", firstName);
}
```

> The two-argument `Where(column, value)` is shorthand for equality:
> `Where(FieldNameOf<&Employee::id>, id)` emits `WHERE "id" = ?`.

### WHERE — multiple conditions (AND / OR)

```sql
SELECT * FROM "Employees" WHERE "salary" >= 55000 AND "age" < 40;
```

<!-- snippet: doc-where-and -->
```cpp
auto rows = dm.Query<Employee>()
                .Where(FieldNameOf<&Employee::salary>, ">=", 55'000)
                .And()
                .Where(FieldNameOf<&Employee::age>, "<", 40)
                .All();
```

`And()`, `Or()`, and `Not()` apply to the *next* `Where(...)`. The query builder exposes the same
clause builder, plus `WhereRaw(...)` when you want to inject a literal predicate.

### WHERE IN

```sql
SELECT * FROM "Employees" WHERE "department_id" IN (1, 2, 3);
```

<!-- snippet: doc-where-in -->
```cpp
auto departmentIds = std::vector { 1, 2, 3 };
auto rows = dm.Query<Employee>().WhereIn(FieldNameOf<&Employee::department>, departmentIds).All();
```

`WhereIn` accepts any range (`std::vector`, `std::set`, an initializer list) or a sub-select query.

The values become bound parameters — `IN (?, ?, ?)` — whenever the query carries a bindings
vector, which is always the case for `DataMapper` queries. On the low-level `SqlQueryBuilder`
without one, they are inlined into the SQL text as escaped literals instead.

An **empty** range means "match nothing", and emits `WHERE 1 = 0` rather than omitting the condition.
This matters most for `Delete()`: `dm.FromTable("Employees").Delete().WhereIn("department_id", ids)`
with an empty `ids` deletes no rows, instead of every row in the table.

### WHERE — NULL / NOT NULL

```sql
SELECT * FROM "Employees" WHERE "age" IS NOT NULL;
```

<!-- snippet: doc-where-null -->
```cpp
auto rows = dm.Query<Employee>().WhereNotNull(FieldNameOf<&Employee::age>).All();
// WhereNull(...) emits IS NULL; WhereNotEqual(column, value) emits "<> ?"
```

### Optional / conditional filters

Search, report, and "filter form" queries usually have several criteria that each apply *only* when
the caller supplied a value. Done by hand that becomes a chain of `if (opt) query.Where(...)`
statements that mutate the builder. Lightweight expresses the same thing inline with
`If(optional).ThenWhere(column[, binaryOp])`:

- `If(opt)` guards the single `ThenWhere(...)` that immediately follows it.
- When `opt` **holds a value**, `ThenWhere(column)` appends `WHERE column = *opt`, and
  `ThenWhere(column, binaryOp)` appends `WHERE column <binaryOp> *opt` (e.g. `">="`, `"<"`, `"LIKE"`).
- When `opt` is **empty**, the call is a no-op: the predicate is omitted and the rest of the query is
  left untouched.

So one piece of code produces a different `WHERE` depending on which inputs are present — no manual
branching. In the example below (`Events` has `id`, `userId`, and `createdAt` columns) the helpers
return `userId = 42` with both timestamps absent, so only the first predicate survives:

```sql
SELECT "id" FROM "Events" WHERE "Events"."userId" = 42 ORDER BY "id";
```

<!-- snippet: doc-optional-filters -->
```cpp
std::optional<int> userId = MaybeUserIdFromRequest();
std::optional<SqlDateTime> since = MaybeSinceFromRequest();
std::optional<SqlDateTime> until = MaybeUntilFromRequest(); // empty -> skipped

auto query = dm.FromTable("Events")
                 .Select()
                 .Field("id")
                 .If(userId)
                 .ThenWhere(FullyQualifiedNameOf<&Events::userId>)
                 .If(since)
                 .ThenWhere(FullyQualifiedNameOf<&Events::createdAt>, ">=")
                 .If(until)
                 .ThenWhere(FullyQualifiedNameOf<&Events::createdAt>, "<")
                 .OrderBy("id")
                 .All();
```

The same builder yields different SQL as the inputs change:

| `userId` | `since` | `until` | Emitted `WHERE` |
|----------|---------|---------|-----------------|
| `42` | — | — | `WHERE "Events"."userId" = 42` |
| `42` | `2026-01-01` | — | `WHERE "Events"."userId" = 42 AND "Events"."createdAt" >= '2026-01-01T00:00:00.000'` |
| — | — | `2026-05-18` | `WHERE "Events"."createdAt" < '2026-05-18T00:00:00.000'` |
| — | — | — | *(no `WHERE` clause is emitted)* |

`If` / `ThenWhere` accept any column-name form that `Where` does (plain strings,
`SqlQualifiedTableColumnName`, `FullyQualifiedNameOf<&Record::field>`) and are available on the
`Select`, `Update`, and `Delete` builders.

### ORDER BY

```sql
SELECT * FROM "Employees" ORDER BY "lastName" DESC;
```

<!-- snippet: doc-order-by -->
```cpp
auto rows = dm.Query<Employee>().OrderBy(FieldNameOf<&Employee::lastName>, SqlResultOrdering::DESCENDING).All();
// SqlResultOrdering::ASCENDING is the default when the ordering argument is omitted.
```

### LIMIT / TOP (fetch the first row)

```sql
-- SQLite / PostgreSQL: ... ORDER BY "salary" DESC LIMIT 1
-- SQL Server:        SELECT TOP 1 ... ORDER BY "salary" DESC
```

<!-- snippet: doc-limit-first -->
```cpp
// First() returns std::optional<Employee>; the formatter emits LIMIT 1 or TOP 1 per DBMS.
auto highestPaid =
    dm.Query<Employee>().OrderBy(FieldNameOf<&Employee::salary>, SqlResultOrdering::DESCENDING).First();
if (highestPaid)
    std::println("{}", highestPaid->lastName.Value());
```

### OFFSET / LIMIT (pagination)

```sql
-- SQLite / PostgreSQL: ... ORDER BY "id" LIMIT 50 OFFSET 200
-- SQL Server:        ... ORDER BY "id" OFFSET 200 ROWS FETCH NEXT 50 ROWS ONLY
```

<!-- snippet: doc-pagination-range -->
```cpp
auto page = dm.Query<Employee>().OrderBy(FieldNameOf<&Employee::id>).Range(/*offset*/ 200, /*limit*/ 50);
```

### DISTINCT

```sql
SELECT DISTINCT "department_id" FROM "Employees";
```

<!-- snippet: doc-distinct -->
```cpp
auto query = dm.FromTable("Employees").Select().Distinct().Field("department_id").All();
```

### COUNT and aggregates

```sql
SELECT COUNT(*) FROM "Employees" WHERE "salary" >= 55000;
```

<!-- snippet: doc-count-datamapper -->
```cpp
// DataMapper — Count() is a finalizer
auto n = dm.Query<Employee>().Where(FieldNameOf<&Employee::salary>, ">=", 55'000).Count();
```

<!-- snippet: doc-count-raw -->
```cpp
// Raw — a single scalar result
auto stmt = SqlStatement { dm.Connection() };
auto total = stmt.ExecuteDirectScalar<int>(R"(SELECT COUNT(*) FROM "Employees")"); // std::optional<int>
```

```sql
SELECT MAX("salary") AS "maxSalary" FROM "Employees";
```

<!-- snippet: doc-aggregate-max -->
```cpp
// Query builder — Aggregate::{Min,Max,Sum,Avg,Count}
auto query = dm.FromTable("Employees").Select().Field(Aggregate::Max("salary")).As("maxSalary").All();
```

### GROUP BY

```sql
SELECT "department_id", COUNT(*) FROM "Employees" GROUP BY "department_id";
```

<!-- snippet: doc-group-by -->
```cpp
auto query = dm.FromTable("Employees")
                 .Select()
                 .Field("department_id")
                 .Field(Aggregate::Count("*"))
                 .As("headcount")
                 .GroupBy("department_id")
                 .All();
```

---

## JOIN

### INNER JOIN

```sql
SELECT "Employees".*, "Departments"."name"
  FROM "Employees"
  INNER JOIN "Departments" ON "Departments"."id" = "Employees"."department_id";
```

<!-- snippet: doc-join-inner-datamapper -->
```cpp
// DataMapper — the join columns are given as member pointers: <&Parent::pk, &Child::fk>
auto rows = dm.Query<Employee, DataMapperOptions { .loadRelations = false }>()
                .InnerJoin<&Department::id, &Employee::department>()
                .All();
```

<!-- snippet: doc-join-inner-builder -->
```cpp
// Query builder — InnerJoin(otherTable, otherColumn, thisColumn)
auto query = dm.FromTable("Employees")
                 .Select()
                 .Fields({ "firstName"sv, "lastName"sv }, "Employees")
                 .Field(SqlQualifiedTableColumnName { .tableName = "Departments", .columnName = "name" })
                 .InnerJoin("Departments", "id", "department_id")
                 .All();
```

### LEFT OUTER JOIN

```sql
SELECT * FROM "Employees"
  LEFT OUTER JOIN "Departments" ON "Departments"."id" = "Employees"."department_id";
```

<!-- snippet: doc-join-left -->
```cpp
auto query = dm.FromTable("Employees")
                 .Select()
                 .Fields({ "firstName"sv, "lastName"sv }, "Employees")
                 .LeftOuterJoin("Departments", "id", "department_id")
                 .All();
```

### Multi-condition / aliased joins

```sql
SELECT ... FROM "Table_A"
  INNER JOIN "Table_B"
    ON "Table_B"."id" = "Table_A"."that_id"
   AND "Table_B"."that_foo" = "Table_A"."foo";
```

<!-- snippet: doc-join-multi -->
```cpp
auto query = dm.FromTable("Table_A")
                 .Select()
                 .Fields({ "foo"sv, "bar"sv }, "Table_A")
                 .Fields({ "that_foo"sv, "that_id"sv }, "Table_B")
                 .InnerJoin("Table_B",
                            [](SqlJoinConditionBuilder join) {
                                return join.On("id", { .tableName = "Table_A", .columnName = "that_id" })
                                    .On("that_foo", { .tableName = "Table_A", .columnName = "foo" });
                            })
                 .All();
```

Use `AliasedTableName { .tableName = "Departments", .alias = "D" }` as the join target (and
`FromTableAs("Employees", "E")`) for self-joins or when the same table appears more than once.

---

## INSERT

```sql
INSERT INTO "Employees" ("firstName", "lastName", "salary") VALUES ('Alice', 'Smith', 50000);
```

<!-- snippet: doc-insert-datamapper -->
```cpp
// DataMapper — populate a record and Create it; the auto-assigned id is written back.
auto employee = Employee { .firstName = "Alice", .lastName = "Smith", .salary = 50'000 };
dm.Create(employee);
// employee.id is now set
```

<!-- snippet: doc-insert-raw -->
```cpp
// Raw — prepare once, execute per row
auto stmt = SqlStatement { dm.Connection() };
stmt.Prepare(R"(INSERT INTO "Employees" ("firstName", "lastName", "salary") VALUES (?, ?, ?))");
std::ignore = stmt.Execute("Alice", "Smith", 50'000);
std::ignore = stmt.Execute("Bob", "Johnson", 60'000);
```

<!-- snippet: doc-insert-builder -->
```cpp
// Query builder — captures the bound values for a later prepared execution
std::vector<SqlVariant> bound;
auto query = dm.FromTable("Employees")
                 .Insert(&bound)
                 .Set("firstName", "Alice")
                 .Set("lastName", "Smith")
                 .Set("salary", 50'000);
```

### Bulk insert

<!-- snippet: doc-bulk-insert-datamapper -->
```cpp
// DataMapper — one prepared statement for the whole batch (native ODBC array binding when possible)
auto people = std::vector<Employee> {
    Employee { .firstName = "Alice", .lastName = "Smith", .salary = 50'000 },
    Employee { .firstName = "Bob", .lastName = "Johnson", .salary = 60'000 },
};
dm.CreateAll(people);
```

<!-- snippet: doc-bulk-insert-raw -->
```cpp
// Raw — column-wise batch
auto stmt = SqlStatement { dm.Connection() };
stmt.Prepare(R"(INSERT INTO "Employees" ("firstName", "lastName", "salary") VALUES (?, ?, ?))");
auto const firstNames = std::array { "Alice"sv, "Bob"sv, "Charlie"sv };
auto const lastNames = std::array { "Smith"sv, "Johnson"sv, "Brown"sv };
auto const salaries = std::array { 50'000, 60'000, 70'000 };
std::ignore = stmt.ExecuteBatch(firstNames, lastNames, salaries);
```

---

## UPDATE

```sql
UPDATE "Employees" SET "salary" = 55000 WHERE "salary" = 50000;
```

<!-- snippet: doc-update-datamapper -->
```cpp
// DataMapper — load, mutate, write back by primary key.
// Only Field<>s you changed are written; the WHERE is the primary key.
if (auto employee = dm.QuerySingle<Employee>(id))
{
    employee->salary = 55'000;
    dm.Update(*employee);
}
```

<!-- snippet: doc-update-builder -->
```cpp
// Query builder — set/where, then prepare & execute with the captured bindings
std::vector<SqlVariant> bound;
auto query = dm.FromTable("Employees").Update(&bound).Set("salary", 55'000).Where("salary", 50'000);

auto stmt = SqlStatement { dm.Connection() };
stmt.Prepare(query);
std::ignore = stmt.ExecuteWithVariants(bound);
```

<!-- snippet: doc-update-raw -->
```cpp
// Raw
auto stmt = SqlStatement { dm.Connection() };
stmt.Prepare(R"(UPDATE "Employees" SET "salary" = ? WHERE "salary" = ?)");
auto cursor = stmt.Execute(55'000, 50'000);
auto changed = cursor.NumRowsAffected();
```

Use `dm.UpdateAll(people)` to write a whole range in one prepared statement.

---

## DELETE

```sql
DELETE FROM "Employees" WHERE "department_id" IN (1, 2, 3);
```

<!-- snippet: doc-delete-datamapper -->
```cpp
// DataMapper — bulk delete by predicate
dm.Query<Employee, DataMapperOptions { .loadRelations = false }>()
    .WhereIn(FieldNameOf<&Employee::department>, std::vector { 1, 2, 3 })
    .Delete();

// Delete a single loaded record by its primary key
dm.Delete(employee);
```

<!-- snippet: doc-delete-builder -->
```cpp
// Query builder
auto query = dm.FromTable("Employees").Delete().WhereIn("department_id", std::vector { 1, 2, 3 });
```

---

## Relationships

`BelongsTo` (many-to-one) and `HasMany` (one-to-many) replace hand-written join queries when
navigating between records. After loading a record, `ConfigureRelationAutoLoading` lets related rows
be fetched on first access:

```sql
-- Conceptually: the employee plus its department, and a department's employees
SELECT * FROM "Employees" WHERE "id" = ?;
SELECT * FROM "Departments" WHERE "id" = <employee.department_id>;
SELECT * FROM "Employees" WHERE "department_id" = <department.id>;
```

<!-- snippet: doc-relationships -->
```cpp
if (auto employee = dm.QuerySingle<Employee>(id))
{
    dm.ConfigureRelationAutoLoading(*employee);

    // BelongsTo: the parent record is fetched on demand. The FK here is nullable, so
    // Record() yields an optional; Unwrap turns the optional-reference into a value.
    if (auto const dept = employee->department.Record().transform(Unwrap))
        std::println("Department: {}", dept->name.Value());
}

// HasMany: Count() and All() on the collection
if (auto department = dm.QuerySingle<Department>(deptId))
{
    dm.ConfigureRelationAutoLoading(*department);
    std::println("{} employees", department->employees.Count());
    for (auto const& emp: department->employees.All())
        std::println("  {}", emp->lastName.Value());
}
```

When the `BelongsTo` is **mandatory** (omit `SqlNullable::Null`), the parent is reached with the
cleaner `employee.department->name` / `*employee.department`. Query with
`DataMapperOptions { .loadRelations = false }` when you do not want relations populated; accessing an
unloaded relation then throws rather than issuing a query. `HasManyThrough<Other, Through>` and
`HasOneThrough<...>` model many-to-many / one-through relationships across a junction table.

### Several foreign keys into the same table

A relation finds its counterpart by matching the relationship *type*, so the two members may sit at
any index in their records. That leaves one case undecidable: a record holding **more than one**
foreign key into the same table - a meeting referencing the person table both as its organizer and
as whoever writes the minutes. Which of the two a `HasMany<Meeting>` means cannot be guessed, and
guessing wrong returns wrong rows silently, so it is a compile error.

Name the foreign key column to resolve it. Take a schema with both shapes at once: two direct roles
on the meeting itself, and any number of attendees through a join table.

```sql
CREATE TABLE "Humans" (
    "id"   BIGINT PRIMARY KEY AUTOINCREMENT,
    "name" VARCHAR(30) NOT NULL
);

CREATE TABLE "Meetings" (
    "id"              BIGINT PRIMARY KEY AUTOINCREMENT,
    "topic"           VARCHAR(40) NOT NULL,
    "organizer_id"    BIGINT NOT NULL REFERENCES "Humans"("id"),
    "minute_taker_id" BIGINT          REFERENCES "Humans"("id")
);

-- One row per person per meeting: the many-to-many that gives a meeting many attendees.
CREATE TABLE "Attendances" (
    "id"         BIGINT PRIMARY KEY AUTOINCREMENT,
    "meeting_id" BIGINT NOT NULL REFERENCES "Meetings"("id"),
    "human_id"   BIGINT NOT NULL REFERENCES "Humans"("id")
);
```

<!-- snippet: doc-meeting-schema -->
```cpp
struct Meeting;
struct Attendance;

struct Human
{
    static constexpr std::string_view TableName = "Humans";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};

    // Two columns of Meetings point back here, so each relation names the one it means.
    HasMany<Meeting, SqlRealName { "organizer_id" }> organizedMeetings {};
    HasMany<Meeting, SqlRealName { "minute_taker_id" }> minutedMeetings {};

    // Attendance is a plain many-to-many, so no selector is needed here.
    HasManyThrough<Meeting, Attendance> attendedMeetings {};
};

struct Meeting
{
    static constexpr std::string_view TableName = "Meetings";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<40>> topic {};

    // Each foreign key names its own column - exactly as it would without the second one.
    BelongsTo<&Human::id, SqlRealName { "organizer_id" }> organizer {};
    BelongsTo<&Human::id, SqlRealName { "minute_taker_id" }, SqlNullable::Null> minuteTaker {};

    // Any number of attendees, through the join record below.
    HasManyThrough<Human, Attendance> attendees {};
};

struct Attendance
{
    static constexpr std::string_view TableName = "Attendances";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    BelongsTo<&Meeting::id, SqlRealName { "meeting_id" }> meeting {};
    BelongsTo<&Human::id, SqlRealName { "human_id" }> human {};
};
```

Only the two ambiguous relations carry a selector; `attendedMeetings` and `attendees` resolve on
their own, because `Attendance` holds exactly one foreign key into each table. The `BelongsTo` side
never changes - it already names its own column.

The selector is the *SQL column name*, not a pointer to the member: the two records reference each
other, so neither type is complete where the other is declared. A name that matches no `BelongsTo`
into that table is a compile error, so a typo cannot go unnoticed.

#### Writing the rows

Assigning a record to a `BelongsTo` copies its primary key, so relationships are set by handing over
the record itself. Attendees are rows in the join table:

<!-- snippet: doc-meeting-create -->
```cpp
dm.CreateTables<Human, Meeting, Attendance>();

auto alice = Human { .name = "Alice" };
auto bob = Human { .name = "Bob" };
auto carol = Human { .name = "Carol" };
for (auto* human: { &alice, &bob, &carol })
    dm.Create(*human);

// Alice calls the planning meeting; Bob writes the minutes.
auto planning = Meeting { .topic = "Planning", .organizer = alice, .minuteTaker = bob };
dm.Create(planning);

// All three of them attend it - one join row per attendee.
for (auto const& attendee: { alice, bob, carol })
    dm.CreateExplicit(Attendance { .meeting = planning, .human = attendee });

// Carol runs the retrospective, nobody takes minutes, and only Alice joins her.
auto retro = Meeting { .topic = "Retrospective", .organizer = carol };
dm.Create(retro);
for (auto const& attendee: { carol, alice })
    dm.CreateExplicit(Attendance { .meeting = retro, .human = attendee });
```

`dm.Create()` writes the record and fills its generated primary key back in, which is what makes
`planning` usable as a foreign key on the very next line. Use `dm.CreateExplicit()` for rows you do
not need to keep, such as the join rows above.

#### Reading them back

<!-- snippet: doc-meeting-read -->
```cpp
// Read a meeting with everyone involved in it.
if (auto meeting = dm.QuerySingle<Meeting>(planningId))
{
    dm.ConfigureRelationAutoLoading(*meeting);

    // A mandatory BelongsTo dereferences straight through.
    std::println("{} - organized by {}", meeting->topic.Value(), meeting->organizer->name.Value());

    // A nullable one yields an optional instead.
    if (auto const scribe = meeting->minuteTaker.Record().transform(Unwrap))
        std::println("  minutes by {}", scribe->name.Value());

    std::println("  {} attendees:", meeting->attendees.Count());
    for (auto const& attendee: meeting->attendees.All())
        std::println("    {}", attendee->name.Value());
}

// And the same relationships read from the other side.
if (auto human = dm.QuerySingle<Human>(aliceId))
{
    dm.ConfigureRelationAutoLoading(*human);
    std::println("{} organized {}, minuted {} and attended {} meeting(s)",
                 human->name.Value(),
                 human->organizedMeetings.Count(),
                 human->minutedMeetings.Count(),
                 human->attendedMeetings.Count());
}
```

which prints:

```
Planning - organized by Alice
  minutes by Bob
  3 attendees:
    Alice
    Bob
    Carol
Alice organized 1, minuted 0 and attended 2 meeting(s)
```

Each relation queries only its own foreign key: `organizedMeetings` filters on `organizer_id`,
`minutedMeetings` on `minute_taker_id`, and `attendees` joins through `Attendances`. `Count()` costs
a `SELECT COUNT(*)` without materialising the rows, and `Each()` streams them when the full set
would be too large to hold.

#### Self-referential relationships

When both foreign keys of a join record point at the *same* table - people who know other people -
neither end can be resolved automatically, so `HasManyThrough` takes both column names: first the one
pointing back at the record owning the relation, then the one pointing at the record it reaches.

```cpp
struct Friendship;

struct Person
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> name;

    //                                          this person -.    .- the friend
    HasManyThrough<Person, Friendship, SqlRealName { "a_id" }, SqlRealName { "b_id" }> friends;
};

struct Friendship
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
    BelongsTo<&Person::id, SqlRealName { "a_id" }> a;
    BelongsTo<&Person::id, SqlRealName { "b_id" }> b;
};
```

Swapping the two selectors reverses the direction the relation reads: with `"b_id"` first and
`"a_id"` second, `friends` walks the friendships from the other end.

`HasOneThrough` also takes two selectors, but they name columns on *different* records: the first one
is the column on the join record pointing back at the owner (same as above), the second is the column
on the *referenced* record pointing at the join record. Passing two join-record column names there is
a compile error, not a silently reversed relation.

Records with a single foreign key per relationship need no selector at all - resolution stays
automatic, and every schema that compiled before this feature existed still does.

---

## CREATE TABLE

```sql
CREATE TABLE "Appointment" (
    "id"           BIGINT       PRIMARY KEY AUTOINCREMENT,
    "date"         DATETIME     NOT NULL,
    "comment"      VARCHAR(80),
    "physician_id" GUID         REFERENCES "Physician"("id"),
    "patient_id"   GUID         REFERENCES "Patient"("id")
);
```

<!-- snippet: doc-create-table-datamapper -->
```cpp
// Derive the schema from the record definitions, in dependency order.
dm.CreateTables<Department, Employee>();
```

`dm.CreateTable<Employee>()` creates a single table. For explicit column-by-column DDL use the
migration query builder:

<!-- snippet: doc-create-table-migration -->
```cpp
using namespace Lightweight::SqlColumnTypeDefinitions;

auto migration = dm.Connection().Migration();
migration.CreateTable("Appointment")
    .PrimaryKeyWithAutoIncrement("id")
    .RequiredColumn("date", DateTime {})
    .Column("comment", Varchar { 80 })
    .ForeignKey(
        "physician_id", Guid {}, SqlForeignKeyReferenceDefinition { .tableName = "Physician", .columnName = "id" })
    .ForeignKey(
        "patient_id", Guid {}, SqlForeignKeyReferenceDefinition { .tableName = "Patient", .columnName = "id" });
auto const plan = migration.GetPlan();
```

See [sql-migrations.md](sql-migrations.md) and [sqlquery.md](sqlquery.md) for the full DDL surface
(`AlterTable`, `Index`, `UniqueIndex`, `Timestamps`, `DropTable`, ...).

---

## Transactions

```sql
BEGIN;
  INSERT INTO "Employees" (...) VALUES (...);
COMMIT;   -- or ROLLBACK
```

<!-- snippet: doc-transaction -->
```cpp
auto stmt = SqlStatement { dm.Connection() };
{
    // SqlTransactionMode::COMMIT auto-commits on scope exit; ROLLBACK auto-rolls back.
    auto tx = SqlTransaction { stmt.Connection(), SqlTransactionMode::COMMIT };
    stmt.Prepare(R"(INSERT INTO "Employees" ("firstName", "lastName", "salary") VALUES (?, ?, ?))");
    std::ignore = stmt.Execute("Eve", "Stone", 70'000);

    tx.Commit(); // or tx.Rollback(); explicit calls are also available
}
```

For an asynchronous transaction (`AsyncSqlTransaction`) over the coroutine layer, see
[async.md](async.md).

---

## Mapping a custom result shape

When a query's columns don't match a full record — joins, projections, aggregates — define a plain
struct (no `Field<>` wrapper needed for read-only rows) whose members line up, in order, with the
selected columns:

<!-- snippet: doc-custom-result-struct -->
```cpp
struct DepartmentHeadcount
{
    SqlAnsiString<40> name;
    int headcount = 0;
};
```

then pass the query to `Query<T>`:

<!-- snippet: doc-custom-result-usage -->
```cpp
auto query = dm.FromTable("Employees")
                 .Select()
                 .Field(SqlQualifiedTableColumnName { .tableName = "Departments", .columnName = "name" })
                 .Field(Aggregate::Count("*"))
                 .As("headcount")
                 .InnerJoin("Departments", "id", "department_id")
                 .GroupBy(SqlQualifiedTableColumnName { .tableName = "Departments", .columnName = "name" })
                 .All();

for (auto const& row: dm.Query<DepartmentHeadcount>(query))
    std::println("{}: {}", row.name, row.headcount);
```

The same struct trick works with `SqlRowIterator<T>` for streaming large result sets one row at a
time.

---

## Keeping these examples honest

Each C++ block above is mirrored by a region in `src/tests/DocExampleTests.cpp`, delimited by
`//! [id]` markers, and tagged here with `<!-- snippet: id -->`. The test file compiles and runs
every example against a real database (SQLite locally, plus PostgreSQL and SQL Server in CI), and
`scripts/check-doc-snippets.py` asserts the doc text and the tested code are identical (modulo
indentation). To change an example:

1. Edit the `//! [id]` region in `src/tests/DocExampleTests.cpp`, keeping it passing.
2. Copy the same lines into the matching `<!-- snippet: id -->` block here.
3. Run `python3 scripts/check-doc-snippets.py` — it prints a diff for any block that drifted.

---

## See also

- [usage.md](usage.md) — getting started: connections, prepared statements, the `DataMapper` CRUD loop.
- [sqlquery.md](sqlquery.md) — the query-builder DSL in depth.
- [sql-migrations.md](sql-migrations.md) — schema creation and migrations.
- [best-practices.md](best-practices.md) — choosing between the layers, performance notes.
- [async.md](async.md) — the coroutine / async API.
