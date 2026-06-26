// SPDX-License-Identifier: Apache-2.0
//
// Tests backing the code examples in docs/sql-to-lightweight.md.
//
// Every fenced ```cpp block in that document is tagged with an
// `<!-- snippet: <id> -->` marker and mirrored here by a matching
// `//! [<id>]` ... `//! [<id>]` region. The script scripts/check-doc-snippets.py
// compares the two at CI time and fails on any drift, so the documentation can
// never show code that does not compile and pass here.
//
// Guidelines for editing:
//   * Keep the lines *between* the `//! [id]` markers byte-identical (modulo
//     common indentation) to the corresponding doc block.
//   * Put any setup the example relies on *before* the opening marker and any
//     assertions *after* the closing marker, so they stay out of the doc.

#include "Utils.hpp"

#include <Lightweight/DataMapper/QueryBuilders.hpp>
#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlQuery/Core.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <print>
#include <string_view>
#include <tuple>
#include <vector>

using namespace Lightweight;
using namespace std::string_view_literals;

// Record types must have linkage (not in an anonymous namespace) for reflection-cpp,
// so the documentation entities live in a named namespace.
namespace docex
{

//! [doc-schema]
struct Employee; // forward declaration for the relationship below

struct Department
{
    static constexpr std::string_view TableName = "Departments";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<40>> name;

    HasMany<Employee> employees; // one department, many employees
};

struct Employee
{
    static constexpr std::string_view TableName = "Employees";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> firstName;

    // FK -> Departments.id. A HasMany and its inverse BelongsTo are matched by field
    // position, so this member sits at the same index as Department::employees above.
    BelongsTo<&Department::id, SqlRealName { "department_id" }, SqlNullable::Null> department;

    Field<SqlAnsiString<30>> lastName;
    Field<int> salary;
    Field<std::optional<int>> age;
};
//! [doc-schema]

//! [doc-custom-result-struct]
struct DepartmentHeadcount
{
    SqlAnsiString<40> name;
    int headcount;
};
//! [doc-custom-result-struct]

// Record used by the conditional-WHERE example.
struct Events
{
    static constexpr std::string_view TableName = "Events";

    Field<int> id;
    Field<int> userId;
    Field<SqlDateTime> createdAt;
};

inline std::optional<int> MaybeUserIdFromRequest()
{
    return 42;
}
inline std::optional<SqlDateTime> MaybeSinceFromRequest()
{
    return std::nullopt;
}
inline std::optional<SqlDateTime> MaybeUntilFromRequest()
{
    return std::nullopt;
}

// Seeds two departments and five employees and returns the departments (ids populated).
struct SeededCompany
{
    Department engineering;
    Department sales;
};

inline SeededCompany SeedCompany(DataMapper& dm)
{
    dm.CreateTables<Department, Employee>();

    auto engineering = Department { .name = "Engineering" };
    dm.Create(engineering);
    auto sales = Department { .name = "Sales" };
    dm.Create(sales);

    auto alice =
        Employee { .firstName = "Alice", .department = engineering, .lastName = "Anders", .salary = 50'000, .age = 30 };
    dm.Create(alice);
    auto bob = Employee { .firstName = "Bob", .department = engineering, .lastName = "Brown", .salary = 60'000, .age = 40 };
    dm.Create(bob);
    auto carol = Employee { .firstName = "Carol", .department = sales, .lastName = "Clark", .salary = 55'000, .age = 35 };
    dm.Create(carol);
    auto dave = Employee { .firstName = "Dave", .department = sales, .lastName = "Davis", .salary = 70'000, .age = 50 };
    dm.Create(dave);
    auto erin = Employee { .firstName = "Erin", .department = engineering, .lastName = "Evans", .salary = 45'000 };
    dm.Create(erin);

    return { engineering, sales };
}

} // namespace docex

using namespace docex;

TEST_CASE_METHOD(SqlTestFixture, "Doc.Select", "[DocExample]")
{
    auto dm = DataMapper();
    SeedCompany(dm);

    SECTION("select all")
    {
        //! [doc-select-all]
        // DataMapper — materialises Employee records (relations lazily configured)
        auto employees = dm.Query<Employee>().All();

        // Skip relationship loading when you only need the row's own columns
        auto rows = dm.Query<Employee, DataMapperOptions { .loadRelations = false }>().All();
        //! [doc-select-all]
        CHECK(employees.size() == 5);
        CHECK(rows.size() == 5);
    }

    SECTION("select columns")
    {
        //! [doc-select-columns]
        // Query builder
        auto query = dm.FromTable("Employees").Select().Fields("firstName", "lastName").All();

        // DataMapper — project to a subset of fields; All<>() returns just those values
        auto names = dm.Query<Employee, DataMapperOptions { .loadRelations = false }>()
                         .All<&Employee::firstName, &Employee::lastName>();
        //! [doc-select-columns]
        CHECK(query.ToSql().contains(R"("firstName")"));
        CHECK(names.size() == 5);
    }

    SECTION("where single")
    {
        //! [doc-where-single-datamapper]
        // DataMapper
        auto rows = dm.Query<Employee>().Where(FieldNameOf<&Employee::salary>, ">=", 55'000).All();
        //! [doc-where-single-datamapper]
        CHECK(rows.size() == 3);
    }

    SECTION("where and")
    {
        //! [doc-where-and]
        auto rows = dm.Query<Employee>()
                        .Where(FieldNameOf<&Employee::salary>, ">=", 55'000)
                        .And()
                        .Where(FieldNameOf<&Employee::age>, "<", 40)
                        .All();
        //! [doc-where-and]
        CHECK(rows.size() == 1);
    }

    SECTION("where in")
    {
        //! [doc-where-in]
        auto departmentIds = std::vector { 1, 2, 3 };
        auto rows = dm.Query<Employee>().WhereIn(FieldNameOf<&Employee::department>, departmentIds).All();
        //! [doc-where-in]
        CHECK(rows.size() == 5);
    }

    SECTION("where not null")
    {
        //! [doc-where-null]
        auto rows = dm.Query<Employee>().WhereNotNull(FieldNameOf<&Employee::age>).All();
        // WhereNull(...) emits IS NULL; WhereNotEqual(column, value) emits "<> ?"
        //! [doc-where-null]
        CHECK(rows.size() == 4);
    }

    SECTION("order by")
    {
        //! [doc-order-by]
        auto rows = dm.Query<Employee>().OrderBy(FieldNameOf<&Employee::lastName>, SqlResultOrdering::DESCENDING).All();
        // SqlResultOrdering::ASCENDING is the default when the ordering argument is omitted.
        //! [doc-order-by]
        CHECK(rows.size() == 5);
    }

    SECTION("limit first")
    {
        //! [doc-limit-first]
        // First() returns std::optional<Employee>; the formatter emits LIMIT 1 or TOP 1 per DBMS.
        auto highestPaid =
            dm.Query<Employee>().OrderBy(FieldNameOf<&Employee::salary>, SqlResultOrdering::DESCENDING).First();
        if (highestPaid)
            std::println("{}", highestPaid->lastName.Value());
        //! [doc-limit-first]
        REQUIRE(highestPaid.has_value());
        CHECK(highestPaid->salary.Value() == 70'000);
    }

    SECTION("pagination")
    {
        //! [doc-pagination-range]
        auto page = dm.Query<Employee>().OrderBy(FieldNameOf<&Employee::id>).Range(/*offset*/ 200, /*limit*/ 50);
        //! [doc-pagination-range]
        CHECK(page.size() <= 50);
    }

    SECTION("distinct")
    {
        //! [doc-distinct]
        auto query = dm.FromTable("Employees").Select().Distinct().Field("department_id").All();
        //! [doc-distinct]
        CHECK(query.ToSql().contains("DISTINCT"));
    }

    SECTION("count")
    {
        //! [doc-count-datamapper]
        // DataMapper — Count() is a finalizer
        auto n = dm.Query<Employee>().Where(FieldNameOf<&Employee::salary>, ">=", 55'000).Count();
        //! [doc-count-datamapper]
        CHECK(n == 3);

        //! [doc-count-raw]
        // Raw — a single scalar result
        auto stmt = SqlStatement { dm.Connection() };
        auto total = stmt.ExecuteDirectScalar<int>(R"(SELECT COUNT(*) FROM "Employees")"); // std::optional<int>
        //! [doc-count-raw]
        REQUIRE(total.has_value());
        CHECK(total.value() == 5);
    }

    SECTION("aggregate")
    {
        //! [doc-aggregate-max]
        // Query builder — Aggregate::{Min,Max,Sum,Avg,Count}
        auto query = dm.FromTable("Employees").Select().Field(Aggregate::Max("salary")).As("maxSalary").All();
        //! [doc-aggregate-max]
        CHECK(query.ToSql().contains(R"(MAX("salary"))"));
    }

    SECTION("group by")
    {
        //! [doc-group-by]
        auto query = dm.FromTable("Employees")
                         .Select()
                         .Field("department_id")
                         .Field(Aggregate::Count("*"))
                         .As("headcount")
                         .GroupBy("department_id")
                         .All();
        //! [doc-group-by]
        CHECK(query.ToSql().contains("GROUP BY"));
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Doc.Select.Raw", "[DocExample]")
{
    auto dm = DataMapper();
    SeedCompany(dm);

    SECTION("select all raw")
    {
        //! [doc-select-all-raw]
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
        //! [doc-select-all-raw]
    }

    SECTION("where single raw")
    {
        //! [doc-where-single-raw]
        // Raw, prepared + parameter binding (use ? placeholders, never string concatenation)
        auto stmt = SqlStatement { dm.Connection() };
        stmt.Prepare(R"(SELECT "firstName", "lastName", "salary" FROM "Employees" WHERE "salary" >= ?)");
        auto cursor = stmt.Execute(55'000);
        while (cursor.FetchRow())
        {
            auto firstName = cursor.GetColumn<std::string>(1);
            std::println("{}", firstName);
        }
        //! [doc-where-single-raw]
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Doc.OptionalFilters", "[DocExample]")
{
    auto dm = DataMapper();

    //! [doc-optional-filters]
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
    //! [doc-optional-filters]
    CHECK(query.ToSql().contains(R"("Events"."userId")"));
    CHECK_FALSE(query.ToSql().contains(R"("Events"."createdAt")"));
}

TEST_CASE_METHOD(SqlTestFixture, "Doc.Join", "[DocExample]")
{
    auto dm = DataMapper();
    SeedCompany(dm);

    SECTION("inner join datamapper")
    {
        //! [doc-join-inner-datamapper]
        // DataMapper — the join columns are given as member pointers: <&Parent::pk, &Child::fk>
        auto rows = dm.Query<Employee, DataMapperOptions { .loadRelations = false }>()
                        .InnerJoin<&Department::id, &Employee::department>()
                        .All();
        //! [doc-join-inner-datamapper]
        CHECK(rows.size() == 5);
    }

    SECTION("inner join builder")
    {
        //! [doc-join-inner-builder]
        // Query builder — InnerJoin(otherTable, otherColumn, thisColumn)
        auto query = dm.FromTable("Employees")
                         .Select()
                         .Fields({ "firstName"sv, "lastName"sv }, "Employees")
                         .Field(SqlQualifiedTableColumnName { .tableName = "Departments", .columnName = "name" })
                         .InnerJoin("Departments", "id", "department_id")
                         .All();
        //! [doc-join-inner-builder]
        CHECK(query.ToSql().contains("INNER JOIN"));
    }

    SECTION("left outer join")
    {
        //! [doc-join-left]
        auto query = dm.FromTable("Employees")
                         .Select()
                         .Fields({ "firstName"sv, "lastName"sv }, "Employees")
                         .LeftOuterJoin("Departments", "id", "department_id")
                         .All();
        //! [doc-join-left]
        CHECK(query.ToSql().contains("LEFT OUTER JOIN"));
    }

    SECTION("multi-condition join")
    {
        //! [doc-join-multi]
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
        //! [doc-join-multi]
        CHECK(query.ToSql().contains("INNER JOIN"));
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Doc.Insert", "[DocExample]")
{
    auto dm = DataMapper();
    dm.CreateTables<Department, Employee>();

    SECTION("insert datamapper")
    {
        //! [doc-insert-datamapper]
        // DataMapper — populate a record and Create it; the auto-assigned id is written back.
        auto employee = Employee { .firstName = "Alice", .lastName = "Smith", .salary = 50'000 };
        dm.Create(employee);
        // employee.id is now set
        //! [doc-insert-datamapper]
        CHECK(employee.id.Value() != 0);
    }

    SECTION("insert raw")
    {
        //! [doc-insert-raw]
        // Raw — prepare once, execute per row
        auto stmt = SqlStatement { dm.Connection() };
        stmt.Prepare(R"(INSERT INTO "Employees" ("firstName", "lastName", "salary") VALUES (?, ?, ?))");
        std::ignore = stmt.Execute("Alice", "Smith", 50'000);
        std::ignore = stmt.Execute("Bob", "Johnson", 60'000);
        //! [doc-insert-raw]
        CHECK(dm.Query<Employee>().Count() == 2);
    }

    SECTION("insert builder")
    {
        //! [doc-insert-builder]
        // Query builder — captures the bound values for a later prepared execution
        std::vector<SqlVariant> bound;
        auto query = dm.FromTable("Employees")
                         .Insert(&bound)
                         .Set("firstName", "Alice")
                         .Set("lastName", "Smith")
                         .Set("salary", 50'000);
        //! [doc-insert-builder]
        CHECK(query.ToSql().contains("INSERT INTO"));
    }

    SECTION("bulk insert datamapper")
    {
        //! [doc-bulk-insert-datamapper]
        // DataMapper — one prepared statement for the whole batch (native ODBC array binding when possible)
        auto people = std::vector<Employee> {
            Employee { .firstName = "Alice", .lastName = "Smith", .salary = 50'000 },
            Employee { .firstName = "Bob", .lastName = "Johnson", .salary = 60'000 },
        };
        dm.CreateAll(people);
        //! [doc-bulk-insert-datamapper]
        CHECK(dm.Query<Employee>().Count() == 2);
    }

    SECTION("bulk insert raw")
    {
        //! [doc-bulk-insert-raw]
        // Raw — column-wise batch
        auto stmt = SqlStatement { dm.Connection() };
        stmt.Prepare(R"(INSERT INTO "Employees" ("firstName", "lastName", "salary") VALUES (?, ?, ?))");
        auto const firstNames = std::array { "Alice"sv, "Bob"sv, "Charlie"sv };
        auto const lastNames = std::array { "Smith"sv, "Johnson"sv, "Brown"sv };
        auto const salaries = std::array { 50'000, 60'000, 70'000 };
        stmt.ExecuteBatch(firstNames, lastNames, salaries);
        //! [doc-bulk-insert-raw]
        CHECK(dm.Query<Employee>().Count() == 3);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Doc.Update", "[DocExample]")
{
    auto dm = DataMapper();
    SeedCompany(dm);
    auto const id = dm.Query<Employee>().First().value().id.Value();

    SECTION("update datamapper")
    {
        //! [doc-update-datamapper]
        // DataMapper — load, mutate, write back by primary key.
        // Only Field<>s you changed are written; the WHERE is the primary key.
        auto employee = dm.QuerySingle<Employee>(id).value();
        employee.salary = 55'000;
        dm.Update(employee);
        //! [doc-update-datamapper]
        CHECK(dm.QuerySingle<Employee>(id).value().salary.Value() == 55'000);
    }

    SECTION("update builder")
    {
        //! [doc-update-builder]
        // Query builder — set/where, then prepare & execute with the captured bindings
        std::vector<SqlVariant> bound;
        auto query = dm.FromTable("Employees").Update(&bound).Set("salary", 55'000).Where("salary", 50'000);

        auto stmt = SqlStatement { dm.Connection() };
        stmt.Prepare(query);
        std::ignore = stmt.ExecuteWithVariants(bound);
        //! [doc-update-builder]
        CHECK(dm.Query<Employee>().Where(FieldNameOf<&Employee::salary>, 55'000).Count() >= 1);
    }

    SECTION("update raw")
    {
        //! [doc-update-raw]
        // Raw
        auto stmt = SqlStatement { dm.Connection() };
        stmt.Prepare(R"(UPDATE "Employees" SET "salary" = ? WHERE "salary" = ?)");
        auto cursor = stmt.Execute(55'000, 50'000);
        auto changed = cursor.NumRowsAffected();
        //! [doc-update-raw]
        CHECK(changed >= 1);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Doc.Delete", "[DocExample]")
{
    auto dm = DataMapper();
    SeedCompany(dm);

    SECTION("delete datamapper")
    {
        auto employee = dm.Query<Employee>().First().value();
        //! [doc-delete-datamapper]
        // DataMapper — bulk delete by predicate
        dm.Query<Employee, DataMapperOptions { .loadRelations = false }>()
            .WhereIn(FieldNameOf<&Employee::department>, std::vector { 1, 2, 3 })
            .Delete();

        // Delete a single loaded record by its primary key
        dm.Delete(employee);
        //! [doc-delete-datamapper]
        CHECK(dm.Query<Employee>().Count() == 0);
    }

    SECTION("delete builder")
    {
        //! [doc-delete-builder]
        // Query builder
        auto query = dm.FromTable("Employees").Delete().WhereIn("department_id", std::vector { 1, 2, 3 });
        //! [doc-delete-builder]
        CHECK(query.ToSql().contains("DELETE FROM"));
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Doc.Relationships", "[DocExample]")
{
    auto dm = DataMapper();
    auto const company = SeedCompany(dm);
    auto const id = dm.Query<Employee>().First().value().id.Value();
    auto const deptId = company.engineering.id.Value();

    //! [doc-relationships]
    auto employee = dm.QuerySingle<Employee>(id).value();
    dm.ConfigureRelationAutoLoading(employee);

    // BelongsTo: the parent record is fetched on demand. The FK here is nullable, so
    // Record() yields an optional; Unwrap turns the optional-reference into a value.
    if (employee.department)
    {
        auto const dept = employee.department.Record().transform(Unwrap).value();
        std::println("Department: {}", dept.name.Value());
    }

    // HasMany: Count() and All() on the collection
    auto department = dm.QuerySingle<Department>(deptId).value();
    dm.ConfigureRelationAutoLoading(department);
    std::println("{} employees", department.employees.Count());
    for (auto const& emp: department.employees.All())
        std::println("  {}", emp->lastName.Value());
    //! [doc-relationships]

    CHECK(department.employees.Count() == 3);
}

TEST_CASE_METHOD(SqlTestFixture, "Doc.DDL", "[DocExample]")
{
    auto dm = DataMapper();

    SECTION("create table datamapper")
    {
        //! [doc-create-table-datamapper]
        // Derive the schema from the record definitions, in dependency order.
        dm.CreateTables<Department, Employee>();
        //! [doc-create-table-datamapper]
        CHECK(dm.Query<Employee>().Count() == 0);
    }

    SECTION("create table migration")
    {
        //! [doc-create-table-migration]
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
        //! [doc-create-table-migration]
        CHECK(!plan.ToSql().empty());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Doc.Transactions", "[DocExample]")
{
    auto dm = DataMapper();
    dm.CreateTables<Department, Employee>();

    //! [doc-transaction]
    auto stmt = SqlStatement { dm.Connection() };
    {
        // SqlTransactionMode::COMMIT auto-commits on scope exit; ROLLBACK auto-rolls back.
        auto tx = SqlTransaction { stmt.Connection(), SqlTransactionMode::COMMIT };
        stmt.Prepare(R"(INSERT INTO "Employees" ("firstName", "lastName", "salary") VALUES (?, ?, ?))");
        std::ignore = stmt.Execute("Eve", "Stone", 70'000);

        tx.Commit(); // or tx.Rollback(); explicit calls are also available
    }
    //! [doc-transaction]
    CHECK(dm.Query<Employee>().Count() == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "Doc.CustomResult", "[DocExample]")
{
    auto dm = DataMapper();
    SeedCompany(dm);

    //! [doc-custom-result-usage]
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
    //! [doc-custom-result-usage]

    CHECK(dm.Query<DepartmentHeadcount>(query).size() == 2);
}
