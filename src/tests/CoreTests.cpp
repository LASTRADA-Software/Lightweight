// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataBinder/UnicodeConverter.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlDataBinder.hpp>
#include <Lightweight/SqlQuery.hpp>
#include <Lightweight/SqlQueryFormatter.hpp>
#include <Lightweight/SqlScopedTraceLogger.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/SqlTransaction.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cstdlib>
#include <list>

// NOLINTBEGIN(readability-container-size-empty)

#if defined(_MSC_VER)
    // Disable the warning C4834: discarding return value of function with 'nodiscard' attribute.
    // Because we are simply testing and demonstrating the library and not using it in production code.
    #pragma warning(disable : 4834)
#endif

using namespace std::string_view_literals;

int main(int argc, char** argv)
{
    auto result = SqlTestFixture::Initialize(argc, argv);
    if (auto const* exitCode = std::get_if<int>(&result))
        return *exitCode;

    std::tie(argc, argv) = std::get<SqlTestFixture::MainProgramArgs>(result);

    return Catch::Session().run(argc, argv);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlStatement: ctor std::nullopt")
{
    // Construct an empty SqlStatement, not referencing any SqlConnection.
    auto stmt = SqlStatement { std::nullopt };
    REQUIRE(!stmt.IsAlive());
    {
        // We expect an error to be logged, as stmt is not attached to any active connection,
        // so we actively ignore any SQL error being logged (to keep the executing output clean).
        auto const _ = ScopedSqlNullLogger {};
        CHECK_THROWS(!stmt.ExecuteDirectScalar<int>("SELECT 42").has_value());
    }

    // Get `stmt` valid by assigning it a valid SqlStatement
    stmt = SqlStatement {};
    REQUIRE(stmt.IsAlive());
    CHECK(stmt.ExecuteDirectScalar<int>("SELECT 42").value() == 42);
}

TEST_CASE_METHOD(SqlTestFixture, "select: get columns")
{
    auto stmt = SqlStatement {};
    stmt.ExecuteDirect("SELECT 42");
    (void) stmt.FetchRow();
    REQUIRE(stmt.GetColumn<int>(1) == 42);
    (void) stmt.FetchRow();
}

TEST_CASE_METHOD(SqlTestFixture, "move semantics", "[SqlConnection]")
{
    auto a = SqlConnection {};
    CHECK(a.IsAlive());

    auto b = std::move(a);
    CHECK(!a.IsAlive());
    CHECK(b.IsAlive());

    auto c = SqlConnection(std::move(b));
    CHECK(!a.IsAlive());
    CHECK(!b.IsAlive());
    CHECK(c.IsAlive());
}

TEST_CASE_METHOD(SqlTestFixture, "move semantics", "[SqlStatement]")
{
    auto conn = SqlConnection {};

    auto const TestRun = [](SqlStatement& stmt) {
        CHECK(stmt.ExecuteDirectScalar<int>("SELECT 42").value_or(-1) == 42);
    };

    auto a = SqlStatement { conn };
    CHECK(&conn == &a.Connection());
    CHECK(a.Connection().IsAlive());
    TestRun(a);

    auto b = std::move(a);
    CHECK(&conn == &b.Connection());
    CHECK(!a.IsAlive());
    CHECK(b.IsAlive());
    TestRun(b);

    auto c = SqlStatement(std::move(b));
    CHECK(&conn == &c.Connection());
    CHECK(!a.IsAlive());
    CHECK(!b.IsAlive());
    CHECK(c.IsAlive());
    TestRun(c);
}

TEST_CASE_METHOD(SqlTestFixture, "select: get column (invalid index)")
{
    auto stmt = SqlStatement {};
    stmt.ExecuteDirect("SELECT 42");
    (void) stmt.FetchRow();

    auto const _ = ScopedSqlNullLogger {}; // suppress the error message, we are testing for it

    CHECK_THROWS_AS(stmt.GetColumn<int>(2), std::invalid_argument);
    (void) stmt.FetchRow();
}

TEST_CASE_METHOD(SqlTestFixture, "execute bound parameters and select back: VARCHAR, INT")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    REQUIRE(!stmt.IsPrepared());
    stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
    REQUIRE(stmt.IsPrepared());

    stmt.Execute("Alice", "Smith", 50'000);
    stmt.Execute("Bob", "Johnson", 60'000);
    stmt.Execute("Charlie", "Brown", 70'000);

    stmt.ExecuteDirect(R"(SELECT COUNT(*) FROM "Employees")");
    REQUIRE(!stmt.IsPrepared());
    REQUIRE(stmt.NumColumnsAffected() == 1);
    (void) stmt.FetchRow();
    REQUIRE(stmt.GetColumn<int>(1) == 3);
    REQUIRE(!stmt.FetchRow());

    stmt.Prepare(R"(SELECT "FirstName", "LastName", "Salary" FROM "Employees" WHERE "Salary" >= ?)");
    REQUIRE(stmt.NumColumnsAffected() == 3);
    stmt.Execute(55'000);

    (void) stmt.FetchRow();
    REQUIRE(stmt.GetColumn<std::string>(1) == "Bob");
    REQUIRE(stmt.GetColumn<std::string>(2) == "Johnson");
    REQUIRE(stmt.GetColumn<int>(3) == 60'000);

    (void) stmt.FetchRow();
    REQUIRE(stmt.GetColumn<std::string>(1) == "Charlie");
    REQUIRE(stmt.GetColumn<std::string>(2) == "Brown");
    REQUIRE(stmt.GetColumn<int>(3) == 70'000);

    REQUIRE(!stmt.FetchRow());
}

TEST_CASE_METHOD(SqlTestFixture, "transaction: auto-rollback")
{
    auto stmt = SqlStatement {};
    REQUIRE(stmt.Connection().TransactionsAllowed());
    CreateEmployeesTable(stmt);

    {
        auto transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::ROLLBACK };
        stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
        stmt.Execute("Alice", "Smith", 50'000);
        REQUIRE(stmt.Connection().TransactionActive());
    }
    // transaction automatically rolled back

    REQUIRE(!stmt.Connection().TransactionActive());
    stmt.ExecuteDirect("SELECT COUNT(*) FROM \"Employees\"");
    (void) stmt.FetchRow();
    REQUIRE(stmt.GetColumn<int>(1) == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "transaction: auto-commit")
{
    auto stmt = SqlStatement {};
    REQUIRE(stmt.Connection().TransactionsAllowed());
    CreateEmployeesTable(stmt);

    {
        auto transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::COMMIT };
        stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
        stmt.Execute("Alice", "Smith", 50'000);
        REQUIRE(stmt.Connection().TransactionActive());
    }
    // transaction automatically committed

    REQUIRE(!stmt.Connection().TransactionActive());
    stmt.ExecuteDirect("SELECT COUNT(*) FROM \"Employees\"");
    (void) stmt.FetchRow();
    REQUIRE(stmt.GetColumn<int>(1) == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "execute binding output parameters (direct)")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    std::string firstName(20, '\0'); // pre-allocation for output parameter strings is important
    std::string lastName(20, '\0');  // ditto
    unsigned int salary {};

    stmt.Prepare(R"(SELECT "FirstName", "LastName", "Salary" FROM "Employees" WHERE "Salary" = ?)");
    stmt.BindOutputColumns(&firstName, &lastName, &salary);
    stmt.Execute(50'000);

    (void) stmt.FetchRow();
    CHECK(firstName == "Alice");
    CHECK(lastName == "Smith");
    CHECK(salary == 50'000);

    REQUIRE(!stmt.FetchRow());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlStatement.ExecuteBatch", "[SqlStatement]")
{
    auto stmt = SqlStatement {};

    CreateEmployeesTable(stmt);

    stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");

    // Ensure that the batch insert works with different types of containers
    // clang-format off
    auto const firstNames = std::array { "Alice"sv, "Bob"sv, "Charlie"sv }; // random access STL container (contiguous)
    auto const lastNames = std::list { "Smith"sv, "Johnson"sv, "Brown"sv }; // forward access STL container (non-contiguous)
    unsigned const salaries[3] = { 50'000, 60'000, 70'000 };                // C-style array
    // clang-format on

    stmt.ExecuteBatch(firstNames, lastNames, salaries);

    stmt.ExecuteDirect(R"(SELECT "FirstName", "LastName", "Salary" FROM "Employees" ORDER BY "Salary" DESC)");

    REQUIRE(stmt.FetchRow());
    REQUIRE(stmt.GetColumn<std::string>(1) == "Charlie");
    REQUIRE(stmt.GetColumn<std::string>(2) == "Brown");
    REQUIRE(stmt.GetColumn<int>(3) == 70'000);

    REQUIRE(stmt.FetchRow());
    REQUIRE(stmt.GetColumn<std::string>(1) == "Bob");
    REQUIRE(stmt.GetColumn<std::string>(2) == "Johnson");
    REQUIRE(stmt.GetColumn<int>(3) == 60'000);

    REQUIRE(stmt.FetchRow());
    REQUIRE(stmt.GetColumn<std::string>(1) == "Alice");
    REQUIRE(stmt.GetColumn<std::string>(2) == "Smith");
    REQUIRE(stmt.GetColumn<int>(3) == 50'000);

    REQUIRE(!stmt.FetchRow());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlStatement.ExecuteBatchNative", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::ORACLE);

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("Test")
            .Column("A", SqlColumnTypeDefinitions::Varchar { 8 })
            .Column("B", SqlColumnTypeDefinitions::Real {})
            .Column("C", SqlColumnTypeDefinitions::Integer {});
    });

    stmt.Prepare(R"(INSERT INTO "Test" ("A", "B", "C") VALUES (?, ?, ?))");

    // Ensure that the batch insert works with different types of contiguous containers
    auto const first = std::array<SqlFixedString<8>, 3> { "Hello", "World", "!" };
    auto const second = std::vector { 1.3, 2.3, 3.3 };
    unsigned const third[3] = { 50'000, 60'000, 70'000 };

    stmt.ExecuteBatchNative(first, second, third);

    stmt.ExecuteDirect(R"(SELECT "A", "B", "C" FROM "Test" ORDER BY "C" DESC)");

    REQUIRE(stmt.FetchRow());
    CHECK(stmt.GetColumn<std::string>(1) == "!");
    CHECK_THAT(stmt.GetColumn<double>(2), Catch::Matchers::WithinAbs(3.3, 0.000'001));
    CHECK(stmt.GetColumn<int>(3) == 70'000);

    REQUIRE(stmt.FetchRow());
    CHECK(stmt.GetColumn<std::string>(1) == "World");
    CHECK_THAT(stmt.GetColumn<double>(2), Catch::Matchers::WithinAbs(2.3, 0.000'001));
    CHECK(stmt.GetColumn<int>(3) == 60'000);

    REQUIRE(stmt.FetchRow());
    CHECK(stmt.GetColumn<std::string>(1) == "Hello");
    CHECK_THAT(stmt.GetColumn<double>(2), Catch::Matchers::WithinAbs(1.3, 0.000'001));
    CHECK(stmt.GetColumn<int>(3) == 50'000);

    REQUIRE(!stmt.FetchRow());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection: manual connect", "[SqlConnection]")
{
    auto conn = SqlConnection { std::nullopt };
    REQUIRE(!conn.IsAlive());

    CHECK(conn.Connect(SqlConnection::DefaultConnectionString()));
    CHECK(conn.IsAlive());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection: manual connect (invalid)", "[SqlConnection]")
{
    auto conn = SqlConnection { std::nullopt };
    REQUIRE(!conn.IsAlive());

    SqlConnectionDataSource const shouldNotExist { .datasource = "shouldNotExist",
                                                   .username = "shouldNotExist",
                                                   .password = "shouldNotExist" };

    auto const _ = ScopedSqlNullLogger {}; // suppress the error message, we are testing for it
    CHECK(!conn.Connect(shouldNotExist));
    CHECK(!conn.IsAlive());
}

TEST_CASE_METHOD(SqlTestFixture, "LastInsertId", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::ORACLE);

    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    // 3 because we inserted 3 rows
    REQUIRE(stmt.LastInsertId() == 3);
}

TEST_CASE_METHOD(SqlTestFixture, "SELECT * FROM Table", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);
    FillEmployeesTable(stmt);

    stmt.ExecuteDirect("SELECT * FROM \"Employees\"");

    auto result = stmt.GetResultCursor();

    REQUIRE(stmt.NumColumnsAffected() == 4);

    REQUIRE(result.FetchRow());
    CHECK(result.GetColumn<int>(1) == 1);
    CHECK(result.GetColumn<std::string>(2) == "Alice");
    CHECK(result.GetColumn<std::string>(3) == "Smith");
    CHECK(result.GetColumn<int>(4) == 50'000);

    REQUIRE(result.FetchRow());
    CHECK(result.GetColumn<int>(1) == 2);
    CHECK(result.GetColumn<std::string>(2) == "Bob");
    CHECK(result.GetColumn<std::string>(3) == "Johnson");
    CHECK(result.GetColumn<int>(4) == 60'000);

    REQUIRE(result.FetchRow());
    CHECK(result.GetColumn<int>(1) == 3);
    CHECK(result.GetColumn<std::string>(2) == "Charlie");
    CHECK(result.GetColumn<std::string>(3) == "Brown");
    CHECK(result.GetColumn<int>(4) == 70'000);

    REQUIRE(!result.FetchRow());
}

TEST_CASE_METHOD(SqlTestFixture, "GetNullableColumn", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("Test")
            .Column("Remarks1", SqlColumnTypeDefinitions::Varchar { 50 })
            .Column("Remarks2", SqlColumnTypeDefinitions::Varchar { 50 });
    });
    stmt.Prepare(R"(INSERT INTO "Test" ("Remarks1", "Remarks2") VALUES (?, ?))");
    stmt.Execute("Blurb", SqlNullValue);

    stmt.ExecuteDirect(R"(SELECT "Remarks1", "Remarks2" FROM "Test")");
    auto result = stmt.GetResultCursor();
    REQUIRE(result.FetchRow());
    auto const actual1 = result.GetNullableColumn<std::string>(1);
    auto const actual2 = result.GetNullableColumn<std::string>(2);
    CHECK(actual1.value_or("IS_NULL") == "Blurb");
    CHECK(!actual2.has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "Prepare and move", "[SqlStatement]")
{
    SqlStatement stmt;
    stmt = SqlStatement().Prepare("SELECT 42");
    stmt.Execute();
    REQUIRE(stmt.FetchRow());
    CHECK(stmt.GetColumn<int>(1) == 42);
}

// NOLINTEND(readability-container-size-empty)
