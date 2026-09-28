// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlTransaction.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <source_location>
#include <stdexcept>
#include <string>

using namespace Lightweight;

namespace
{

int RowCountInEmployees(SqlStatement& stmt)
{
    auto cursor = stmt.ExecuteDirect(R"(SELECT COUNT(*) FROM "Employees")");
    (void) cursor.FetchRow();
    return cursor.GetColumn<int>(1);
}

} // namespace

// ================================================================================================
// Manual Commit / Rollback (DB-dependent)
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlTransaction: explicit Commit persists changes", "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    REQUIRE(stmt.Connection().TransactionsAllowed());
    CreateEmployeesTable(stmt);

    {
        auto transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::NONE };
        stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
        (void) stmt.Execute("Alice", "Smith", 50'000);
        REQUIRE(stmt.Connection().TransactionActive());
        transaction.Commit();
        REQUIRE_FALSE(stmt.Connection().TransactionActive());
    }

    CHECK(RowCountInEmployees(stmt) == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlTransaction: explicit Rollback discards changes", "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    REQUIRE(stmt.Connection().TransactionsAllowed());
    CreateEmployeesTable(stmt);

    {
        auto transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::NONE };
        stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
        (void) stmt.Execute("Alice", "Smith", 50'000);
        transaction.Rollback();
        REQUIRE_FALSE(stmt.Connection().TransactionActive());
    }

    CHECK(RowCountInEmployees(stmt) == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlTransaction: TryCommit returns true on success", "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    auto transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::NONE };
    stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
    (void) stmt.Execute("Bob", "B", 1'000);
    CHECK(transaction.TryCommit());
    // Calling TryCommit a second time after the transaction has ended is a no-op-state test:
    // the connection is back in autocommit, so the dtor's TryCommit (after mode reset) won't fire.
    CHECK(RowCountInEmployees(stmt) == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlTransaction: TryRollback returns true on success", "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    auto transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::NONE };
    stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
    (void) stmt.Execute("Carol", "C", 1'000);
    CHECK(transaction.TryRollback());
    CHECK(RowCountInEmployees(stmt) == 0);
}

// ================================================================================================
// SqlTransactionMode::NONE — destructor is a no-op
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlTransaction: NONE mode leaves changes pending until explicit action",
                 "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    {
        auto transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::NONE };
        stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
        (void) stmt.Execute("Dave", "D", 1'000);
        // Destructor runs without committing/rolling back; transaction stays open on the connection.
    }

    // After the SqlTransaction object is destroyed in NONE mode, AUTOCOMMIT is still off, so
    // we still need to wrap up the open transaction to leave the connection in a usable state.
    auto wrap = SqlTransaction { stmt.Connection(), SqlTransactionMode::ROLLBACK };
    (void) wrap.TryRollback();

    CHECK(RowCountInEmployees(stmt) == 0);
}

// ================================================================================================
// Auto-commit / auto-rollback via destructor
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlTransaction: destructor commit is idempotent after explicit Commit", "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    {
        auto transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::COMMIT };
        stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
        (void) stmt.Execute("Eve", "E", 1'000);
        transaction.Commit();
        // mode reset to NONE — destructor does nothing further
    }

    CHECK(RowCountInEmployees(stmt) == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlTransaction: destructor rollback is idempotent after explicit Rollback",
                 "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    {
        auto transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::ROLLBACK };
        stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
        (void) stmt.Execute("Frank", "F", 1'000);
        transaction.Rollback();
    }

    CHECK(RowCountInEmployees(stmt) == 0);
}

// ================================================================================================
// Connection() accessor
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlTransaction::Connection returns the original connection", "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    auto& conn = stmt.Connection();
    auto transaction = SqlTransaction { conn, SqlTransactionMode::ROLLBACK };
    CHECK(&transaction.Connection() == &conn);
}

// ================================================================================================
// Move semantics
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlTransaction is move-constructible", "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    auto firstHandle = SqlTransaction { stmt.Connection(), SqlTransactionMode::NONE };
    auto* connBefore = &firstHandle.Connection();

    auto secondHandle = SqlTransaction { std::move(firstHandle) };
    CHECK(&secondHandle.Connection() == connBefore);

    // Wrap up to leave connection in autocommit
    secondHandle.Rollback();
}

// ================================================================================================
// SqlTransactionException — thrown when Rollback/Commit fails
// ================================================================================================

TEST_CASE("SqlTransactionException carries a message", "[SqlTransaction]")
{
    SqlTransactionException const ex { "boom" };
    std::string const what = ex.what();
    CHECK(what == "boom");
}

// ================================================================================================
// Failure paths stay noexcept (#583)
//
// TryCommit(), TryRollback() and the destructor are noexcept, yet they report a failure through the
// installed SqlLogger - which is user code, free to throw. A throw escaping them would terminate.
// ================================================================================================

namespace
{

/// Throws from every error callback while it is the active logger.
class ThrowingErrorLogger: public SqlLogger::Null
{
  public:
    ThrowingErrorLogger():
        _previous { &SqlLogger::GetLogger() }
    {
        SqlLogger::SetLogger(*this);
    }

    ~ThrowingErrorLogger() override
    {
        SqlLogger::SetLogger(*_previous);
    }

    ThrowingErrorLogger(ThrowingErrorLogger const&) = delete;
    ThrowingErrorLogger(ThrowingErrorLogger&&) = delete;
    ThrowingErrorLogger& operator=(ThrowingErrorLogger const&) = delete;
    ThrowingErrorLogger& operator=(ThrowingErrorLogger&&) = delete;

    void OnError(SqlError /*errorCode*/, std::source_location /*sourceLocation*/) override
    {
        throw std::runtime_error { "logger failure" };
    }

    void OnError(SqlErrorInfo const& /*errorInfo*/, std::source_location /*sourceLocation*/) override
    {
        throw std::runtime_error { "logger failure" };
    }

  private:
    SqlLogger* _previous;
};

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlTransaction: a failing commit or rollback reports false and never throws",
                 "[SqlTransaction]")
{
    auto connection = SqlConnection {};
    {
        auto transaction = SqlTransaction { connection, SqlTransactionMode::COMMIT };
        // Ending the connection under the transaction makes both SQLEndTran calls fail.
        connection.Close();

        auto const logger = ThrowingErrorLogger {};
        CHECK_FALSE(transaction.TryCommit());
        CHECK_FALSE(transaction.TryRollback());
    } // the destructor's commit fails the same way, and must not terminate either
}

// ================================================================================================
// Isolation modes — formatter coverage and DriverDefault round-trip on SQLite
// ================================================================================================

TEST_CASE("std::formatter<SqlTransactionMode> renders human-readable text", "[SqlTransaction]")
{
    CHECK(std::format("{}", SqlTransactionMode::COMMIT) == "Commit");
    CHECK(std::format("{}", SqlTransactionMode::ROLLBACK) == "Rollback");
    CHECK(std::format("{}", SqlTransactionMode::NONE) == "None");
}

TEST_CASE("std::formatter<SqlIsolationMode> renders human-readable text", "[SqlTransaction]")
{
    CHECK(std::format("{}", SqlIsolationMode::DriverDefault) == "DriverDefault");
    CHECK(std::format("{}", SqlIsolationMode::ReadUncommitted) == "ReadUncommitted");
    CHECK(std::format("{}", SqlIsolationMode::ReadCommitted) == "ReadCommitted");
    CHECK(std::format("{}", SqlIsolationMode::RepeatableRead) == "RepeatableRead");
    CHECK(std::format("{}", SqlIsolationMode::Serializable) == "Serializable");
}

TEST_CASE_METHOD(SqlTestFixture, "SqlTransaction: DriverDefault isolation skips SQLSetConnectAttr", "[SqlTransaction]")
{
    // The DriverDefault path is a separate branch — exercise it explicitly.
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    auto transaction = SqlTransaction {
        stmt.Connection(),
        SqlTransactionMode::COMMIT,
        SqlIsolationMode::DriverDefault,
    };
    stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
    (void) stmt.Execute("Grace", "G", 1'000);
    transaction.Commit();

    CHECK(RowCountInEmployees(stmt) == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlTransaction: Serializable isolation works on SQLite", "[SqlTransaction]")
{
    // SQLite always serializes on disk; passing the explicit mode must not error.
    auto stmt = SqlStatement {};
    CreateEmployeesTable(stmt);

    auto transaction = SqlTransaction {
        stmt.Connection(),
        SqlTransactionMode::ROLLBACK,
        SqlIsolationMode::Serializable,
    };
    stmt.Prepare(R"(INSERT INTO "Employees" ("FirstName", "LastName", "Salary") VALUES (?, ?, ?))");
    (void) stmt.Execute("Hank", "H", 1'000);
    transaction.Rollback();

    CHECK(RowCountInEmployees(stmt) == 0);
}
