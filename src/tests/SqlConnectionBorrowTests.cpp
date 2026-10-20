// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlTransaction.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>

using namespace Lightweight;
using namespace std::chrono_literals;

namespace
{

// A temporary table lives in one session, so finding it through the borrowed connection proves the
// session is shared.
std::string BorrowProbeTable(SqlConnection const& connection)
{
    return connection.ServerType() == SqlServerType::MICROSOFT_SQL ? R"("#BorrowProbe")" : R"("BorrowProbe")";
}

void CreateBorrowProbeTable(SqlConnection& connection)
{
    auto const* const createTable =
        connection.ServerType() == SqlServerType::MICROSOFT_SQL ? "CREATE TABLE" : "CREATE TEMPORARY TABLE";
    auto stmt = SqlStatement { connection };
    (void) stmt.ExecuteDirect(std::format(
        R"({} {} ("Id" INTEGER NOT NULL, "Name" VARCHAR(10) NOT NULL))", createTable, BorrowProbeTable(connection)));
}

int CountBorrowProbeRows(SqlConnection& connection)
{
    auto stmt = SqlStatement { connection };
    return stmt.ExecuteDirectScalar<int>(std::format("SELECT COUNT(*) FROM {}", BorrowProbeTable(connection))).value_or(-1);
}

void InsertBorrowProbeRow(SqlConnection& connection)
{
    auto stmt = SqlStatement { connection };
    (void) stmt.ExecuteDirect(std::format(R"(INSERT INTO {} ("Id", "Name") VALUES (1, 'a'))", BorrowProbeTable(connection)));
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "Borrow runs statements on the lent handle", "[SqlConnection][Borrow]")
{
    auto owner = SqlConnection {};
    CreateBorrowProbeTable(owner);

    auto borrowed = SqlConnection::Borrow(owner.NativeHandle());
    InsertBorrowProbeRow(borrowed);

    CHECK(borrowed.NativeHandle() == owner.NativeHandle());
    CHECK(borrowed.ServerType() == owner.ServerType());
    CHECK(CountBorrowProbeRows(owner) == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "Borrow shares the owner's transaction", "[SqlConnection][Borrow]")
{
    auto owner = SqlConnection {};
    REQUIRE(owner.TransactionsAllowed());
    CreateBorrowProbeTable(owner);

    auto transaction = SqlTransaction { owner, SqlTransactionMode::ROLLBACK };
    {
        auto borrowed = SqlConnection::Borrow(owner.NativeHandle());
        CHECK(borrowed.TransactionActive());
        InsertBorrowProbeRow(borrowed);
    }
    CHECK(CountBorrowProbeRows(owner) == 1);

    transaction.Rollback();
    CHECK(CountBorrowProbeRows(owner) == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "Destroying a borrowed connection leaves the owner usable", "[SqlConnection][Borrow]")
{
    auto owner = SqlConnection {};
    {
        auto borrowed = SqlConnection::Borrow(owner.NativeHandle());
        auto stmt = SqlStatement { borrowed };
        CHECK(stmt.ExecuteDirectScalar<int>("SELECT 1") == 1);
    }

    CHECK(owner.IsAlive());
    auto stmt = SqlStatement { owner };
    CHECK(stmt.ExecuteDirectScalar<int>("SELECT 1") == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "Borrow leaves session settings alone", "[SqlConnection][Borrow]")
{
    auto owner = SqlConnection {};
    switch (owner.ServerType())
    {
        case SqlServerType::MICROSOFT_SQL: {
            // Borrow would otherwise apply the default truncation mode, turning ANSI_WARNINGS back off.
            owner.SetStringTruncationMode(SqlStringTruncationMode::Error);
            auto const optionsBefore = SqlStatement { owner }.ExecuteDirectScalar<int>("SELECT @@OPTIONS");

            auto borrowed = SqlConnection::Borrow(owner.NativeHandle());

            CHECK(SqlStatement { owner }.ExecuteDirectScalar<int>("SELECT @@OPTIONS") == optionsBefore);
            break;
        }
        case SqlServerType::SQLITE: {
            // Borrow would otherwise apply the default SQLite settings' 60 second busy timeout.
            REQUIRE(owner.SetSqliteSettings({ .busyTimeout = 1234ms }));

            auto borrowed = SqlConnection::Borrow(owner.NativeHandle());

            CHECK(SqlStatement { owner }.ExecuteDirectScalar<int>("PRAGMA busy_timeout") == 1234);
            break;
        }
        case SqlServerType::POSTGRESQL:
        case SqlServerType::MYSQL:
        case SqlServerType::UNKNOWN:
            break; // No session setting is applied at connect time on these backends.
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Connect on a borrowed connection fails", "[SqlConnection][Borrow]")
{
    auto owner = SqlConnection {};
    auto borrowed = SqlConnection::Borrow(owner.NativeHandle());

    CHECK_FALSE(borrowed.Connect(SqlConnection::DefaultConnectionString()));
    CHECK(borrowed.HandleOwnership() == SqlHandleOwnership::Borrowed);
    CHECK(owner.HandleOwnership() == SqlHandleOwnership::Owned);
    CHECK(borrowed.NativeHandle() == owner.NativeHandle());
}

TEST_CASE("Borrow rejects a null handle", "[SqlConnection][Borrow]")
{
    CHECK_THROWS_AS(SqlConnection::Borrow(SQL_NULL_HDBC), std::invalid_argument);
}

TEST_CASE_METHOD(SqlTestFixture, "A moved borrowed connection keeps borrowing", "[SqlConnection][Borrow]")
{
    auto owner = SqlConnection {};
    auto borrowed = SqlConnection::Borrow(owner.NativeHandle());
    auto moved = std::move(borrowed);

    CHECK(moved.HandleOwnership() == SqlHandleOwnership::Borrowed);
    moved.Close();
    CHECK(moved.NativeHandle() == SQL_NULL_HDBC);
    CHECK(owner.IsAlive());
}

TEST_CASE_METHOD(SqlTestFixture, "Borrow adopts the session's string-truncation mode", "[SqlConnection][Borrow]")
{
    auto owner = SqlConnection {};
    switch (owner.ServerType())
    {
        case SqlServerType::MICROSOFT_SQL: {
            (void) SqlStatement { owner }.ExecuteDirect("SET ANSI_WARNINGS ON");
            CHECK(SqlConnection::Borrow(owner.NativeHandle()).StringTruncationMode() == SqlStringTruncationMode::Error);

            (void) SqlStatement { owner }.ExecuteDirect("SET ANSI_WARNINGS OFF");
            CHECK(SqlConnection::Borrow(owner.NativeHandle()).StringTruncationMode() == SqlStringTruncationMode::Truncate);
            break;
        }
        case SqlServerType::SQLITE:
        case SqlServerType::POSTGRESQL:
        case SqlServerType::MYSQL:
        case SqlServerType::UNKNOWN:
            break; // The mode is not a session setting on these backends.
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Move-assigning onto a borrowed connection leaves the owner usable",
                 "[SqlConnection][Borrow]")
{
    auto owner = SqlConnection {};
    auto borrowed = SqlConnection::Borrow(owner.NativeHandle());
    {
        auto stmt = SqlStatement { borrowed };
        CHECK(stmt.ExecuteDirectScalar<int>("SELECT 1") == 1);
    }

    borrowed = SqlConnection {};

    CHECK(borrowed.HandleOwnership() == SqlHandleOwnership::Owned);
    CHECK(owner.IsAlive());
    auto stmt = SqlStatement { owner };
    CHECK(stmt.ExecuteDirectScalar<int>("SELECT 1") == 1);
}
