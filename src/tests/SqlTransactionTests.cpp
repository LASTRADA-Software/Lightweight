// SPDX-License-Identifier: Apache-2.0

#include "ScriptedFaultSource.hpp"
#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlTransaction.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <format>
#include <optional>
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

// ================================================================================================
// A commit the database refuses
//
// The database can end a transaction on its own (SQLite's INSERT OR ROLLBACK), or refuse the commit
// itself (a deferred foreign key on PostgreSQL). Either way the commit fails; the transaction object
// must still hand the connection back in autocommit mode, and say why the commit failed.
// ================================================================================================

namespace
{

/// Makes the open transaction on @p stmt's connection fail its commit, in a way this DBMS supports.
/// @return The text the commit's diagnostic holds; empty when the DBMS has no way to make a commit fail
///         that this test knows of.
std::string_view PrepareRefusedCommit(SqlStatement& stmt)
{
    switch (stmt.Connection().ServerType())
    {
        case SqlServerType::SQLITE:
            // Hitting the constraint rolls the whole transaction back, so there is nothing left to commit.
            (void) stmt.ExecuteDirect(R"(INSERT INTO "TxnRefused" ("id", "ref") VALUES (2, NULL))");
            CHECK_THROWS_AS(stmt.ExecuteDirect(R"(INSERT OR ROLLBACK INTO "TxnRefused" ("id", "ref") VALUES (1, NULL))"),
                            SqlException);
            return "cannot commit";
        case SqlServerType::POSTGRESQL:
            // The deferred foreign key is checked by COMMIT, which then fails.
            (void) stmt.ExecuteDirect(R"(INSERT INTO "TxnRefused" ("id", "ref") VALUES (2, 99))");
            return "23503";
        default:
            return {};
    }
}

/// Makes the next commit on @p stmt's connection fail: as the DBMS itself refuses one where this test
/// knows how, otherwise (SQL Server) through the fault seam.
class RefusedCommit
{
  public:
    /// @param stmt The statement whose connection holds the open transaction.
    explicit RefusedCommit(SqlStatement& stmt):
        _reason { PrepareRefusedCommit(stmt) }
    {
        if (!_reason.empty())
            return;
        _source.emplace(
            SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "HY000", .message = "commit refused" }, 1, "TryCommit");
        _installed.emplace(&*_source);
        _reason = "commit refused";
    }

    /// @return The text the failed commit's diagnostic holds.
    [[nodiscard]] std::string_view Reason() const noexcept
    {
        return _reason;
    }

  private:
    std::string_view _reason;
    std::optional<Tests::ScriptedFaultSource> _source;
    std::optional<Tests::ScopedFaultSource> _installed;
};

/// Checks that the connection of @p stmt is usable for transactions: the next one really is one, and its
/// rollback discards what it wrote.
void CheckUsableForTransactions(SqlStatement& stmt)
{
    {
        auto const transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::ROLLBACK };
        (void) stmt.ExecuteDirect(R"(INSERT INTO "TxnRefused" ("id", "ref") VALUES (4, NULL))");
    }
    CHECK_FALSE(stmt.Connection().TransactionActive());
    auto otherConnection = SqlConnection {};
    auto other = SqlStatement { otherConnection };
    CHECK(other.ExecuteDirectScalar<int>(R"(SELECT COUNT(*) FROM "TxnRefused" WHERE "id" = 4)").value_or(0) == 0);
}

/// Creates the table the refused-commit tests write to, with one committed row.
void CreateRefusedCommitTable(SqlStatement& stmt)
{
    auto const* const deferred = stmt.Connection().ServerType() == SqlServerType::POSTGRESQL
                                     ? R"(REFERENCES "TxnRefused" ("id") DEFERRABLE INITIALLY DEFERRED)"
                                     : "";
    (void) stmt.ExecuteDirect(R"(DROP TABLE IF EXISTS "TxnRefused")");
    (void) stmt.ExecuteDirect(
        std::format(R"(CREATE TABLE "TxnRefused" ("id" INTEGER NOT NULL PRIMARY KEY, "ref" INTEGER NULL {}))", deferred));
    (void) stmt.ExecuteDirect(R"(INSERT INTO "TxnRefused" ("id", "ref") VALUES (1, NULL))");
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlTransaction: a refused Commit names the reason and restores autocommit",
                 "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    CreateRefusedCommitTable(stmt);

    auto message = std::string {};
    {
        auto transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::COMMIT };
        auto const refused = RefusedCommit { stmt };
        try
        {
            transaction.Commit();
        }
        catch (SqlTransactionException const& error)
        {
            message = error.what();
        }
        // The exception names the reason: the driver's diagnostic, read before anything could clear it
        // (the SQLite ODBC driver hands a diagnostic record out only once). LastError() keeps it.
        INFO(message);
        CHECK(message.starts_with("Failed to commit the transaction: "));
        CHECK(message.contains(refused.Reason()));
        CHECK(std::format("{}", transaction.LastError().value_or(SqlErrorInfo {})).contains(refused.Reason()));
    }

    // The transaction object is gone; whatever the commit did, the connection is back in autocommit mode.
    CHECK_FALSE(stmt.Connection().TransactionActive());
    (void) stmt.ExecuteDirect(R"(INSERT INTO "TxnRefused" ("id", "ref") VALUES (3, NULL))");
    auto otherConnection = SqlConnection {};
    auto other = SqlStatement { otherConnection };
    CHECK(other.ExecuteDirectScalar<int>(R"(SELECT COUNT(*) FROM "TxnRefused" WHERE "id" = 3)").value_or(0) == 1);

    CheckUsableForTransactions(stmt);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlTransaction: the destructor restores autocommit after the database ended the transaction",
                 "[SqlTransaction]")
{
    for (auto const mode: { SqlTransactionMode::COMMIT, SqlTransactionMode::ROLLBACK })
    {
        INFO("mode " << std::format("{}", mode));
        auto stmt = SqlStatement {};
        CreateRefusedCommitTable(stmt);
        {
            // Declared first, so that an injected refusal is still installed when the destructor commits.
            auto refused = std::optional<RefusedCommit> {};
            auto const transaction = SqlTransaction { stmt.Connection(), mode };
            refused.emplace(stmt);
        }
        CHECK_FALSE(stmt.Connection().TransactionActive());
        CheckUsableForTransactions(stmt);
    }
}

// ================================================================================================
// The transaction state machine through the connection fault seam, on every DBMS
// ================================================================================================

namespace
{

/// @return An injected diagnostic carrying @p message.
SqlErrorInfo InjectedError(std::string message)
{
    return SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "HY000", .message = std::move(message) };
}

/// @return Whether row @p id of "TxnRefused" is visible to another connection.
bool RowVisibleElsewhere(int id)
{
    auto connection = SqlConnection {};
    auto stmt = SqlStatement { connection };
    return stmt.ExecuteDirectScalar<int>(std::format(R"(SELECT COUNT(*) FROM "TxnRefused" WHERE "id" = {})", id)).value_or(0)
           == 1;
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlTransaction: a failed rollback leaves manual-commit mode until a rollback succeeds",
                 "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    CreateRefusedCommitTable(stmt);
    {
        auto transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::ROLLBACK };
        (void) stmt.ExecuteDirect(R"(INSERT INTO "TxnRefused" ("id", "ref") VALUES (2, NULL))");
        {
            auto source = Tests::ScriptedFaultSource { InjectedError("rollback refused"), 1, "TryRollback" };
            auto const installed = Tests::ScopedFaultSource { &source };
            CHECK_FALSE(transaction.TryRollback());
        }
        CHECK(transaction.LastError().value_or(SqlErrorInfo {}).message == "rollback refused");
        // Autocommit is not forced back on, which would commit a transaction the driver might still hold.
        CHECK(stmt.Connection().TransactionActive());

        CHECK(transaction.TryRollback());
        CHECK_FALSE(transaction.LastError().has_value());
    }
    CHECK_FALSE(stmt.Connection().TransactionActive());
    CHECK_FALSE(RowVisibleElsewhere(2));
    CheckUsableForTransactions(stmt);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlTransaction: a commit that cannot switch autocommit back on reports it",
                 "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    CreateRefusedCommitTable(stmt);
    {
        auto transaction = SqlTransaction { stmt.Connection(), SqlTransactionMode::COMMIT };
        (void) stmt.ExecuteDirect(R"(INSERT INTO "TxnRefused" ("id", "ref") VALUES (2, NULL))");
        {
            auto source = Tests::ScriptedFaultSource { InjectedError("autocommit refused"), 1, "RestoreAutoCommit" };
            auto const installed = Tests::ScopedFaultSource { &source };
            CHECK_THROWS_WITH(transaction.Commit(), "Failed to commit the transaction: HY000 (0) - autocommit refused");
        }
    } // the destructor finishes what the commit left: the switch back to autocommit
    CHECK_FALSE(stmt.Connection().TransactionActive());
    CHECK(RowVisibleElsewhere(2));
    CheckUsableForTransactions(stmt);
}

// ================================================================================================
// Moving a transaction hands it over: the moved-from object no longer ends it
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlTransaction: a moved-from transaction does not end the transaction", "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    CreateRefusedCommitTable(stmt);

    auto holder = std::optional<SqlTransaction> {};
    {
        auto first = SqlTransaction { stmt.Connection(), SqlTransactionMode::COMMIT };
        holder.emplace(std::move(first));
    } // the moved-from object goes away here; the transaction must stay open

    REQUIRE(stmt.Connection().TransactionActive());
    (void) stmt.ExecuteDirect(R"(INSERT INTO "TxnRefused" ("id", "ref") VALUES (2, NULL))");
    holder->Rollback();
    CHECK_FALSE(RowVisibleElsewhere(2));
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlTransaction: move assignment ends the target's transaction as its destructor would",
                 "[SqlTransaction]")
{
    auto stmt = SqlStatement {};
    CreateRefusedCommitTable(stmt);
    auto otherConnection = SqlConnection {};

    auto target = SqlTransaction { stmt.Connection(), SqlTransactionMode::ROLLBACK };
    (void) stmt.ExecuteDirect(R"(INSERT INTO "TxnRefused" ("id", "ref") VALUES (2, NULL))");
    {
        auto source = SqlTransaction { otherConnection, SqlTransactionMode::COMMIT };
        target = std::move(source);
    }

    // The target's own transaction was rolled back, as its ROLLBACK mode says ...
    REQUIRE_FALSE(stmt.Connection().TransactionActive());
    CHECK_FALSE(RowVisibleElsewhere(2));
    // ... and it now holds the other connection's, which the moved-from source left open.
    CHECK(otherConnection.TransactionActive());
    CHECK(&target.Connection() == &otherConnection);
    target.Commit();
    CHECK_FALSE(otherConnection.TransactionActive());
}
