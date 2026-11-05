// SPDX-License-Identifier: Apache-2.0
//
// SqlStatement::SetQueryTimeout against real drivers.
//
// What the three supported drivers do with SQL_ATTR_QUERY_TIMEOUT differs, and these tests pin it:
//
//   MS SQL Server  enforced; fails with SQLSTATE HYT00; retried by SqlRetryPolicy
//   PostgreSQL     enforced; fails with SQLSTATE 57014; not retried by SqlRetryPolicy
//   SQLite         ignored: the driver accepts the attribute, reads back 0, and the query runs to completion

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlErrorDetection.hpp>
#include <Lightweight/SqlRetryPolicy.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <tuple>

using namespace Lightweight;
using namespace std::chrono_literals;

namespace
{

/// A query that runs for much longer than the one-second timeout the tests set.
/// SQLite's is a CPU-bound count, as it has no sleep function.
std::string_view SlowQuery(SqlServerType serverType)
{
    switch (serverType)
    {
        case SqlServerType::MICROSOFT_SQL:
            return "WAITFOR DELAY '00:00:05'";
        case SqlServerType::POSTGRESQL:
            return "SELECT pg_sleep(5)";
        default:
            return "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < 12000000) SELECT COUNT(*) FROM "
                   "c";
    }
}

/// A query that outlasts a one-second timeout but returns quickly enough to run without one.
std::string_view TwoSecondQuery(SqlServerType serverType)
{
    return serverType == SqlServerType::MICROSOFT_SQL ? "WAITFOR DELAY '00:00:02'" : "SELECT pg_sleep(2)";
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "SetQueryTimeout: a new statement has no timeout", "[SqlStatement][QueryTimeout]")
{
    auto connection = SqlConnection {};
    auto const stmt = SqlStatement { connection };
    CHECK(stmt.QueryTimeout() == 0s);
}

TEST_CASE_METHOD(SqlTestFixture, "SetQueryTimeout: a negative timeout is refused", "[SqlStatement][QueryTimeout]")
{
    auto connection = SqlConnection {};
    auto stmt = SqlStatement { connection };
    CHECK_THROWS_AS(stmt.SetQueryTimeout(-1s), std::invalid_argument);
    CHECK(stmt.QueryTimeout() == 0s);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SetQueryTimeout: SQLite accepts the timeout, reads back 0 and runs to completion",
                 "[SqlStatement][QueryTimeout]")
{
    auto connection = SqlConnection {};
    if (connection.ServerType() != SqlServerType::SQLITE)
        SKIP("The other drivers enforce the timeout; see the next test case.");

    auto stmt = SqlStatement { connection };
    stmt.SetQueryTimeout(1s);
    CHECK(stmt.QueryTimeout() == 0s);

    auto cursor = stmt.ExecuteDirect(SlowQuery(connection.ServerType()));
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<int64_t>(1) == 12000000);
}

TEST_CASE_METHOD(SqlTestFixture, "SetQueryTimeout: an enforced timeout fails the execution", "[SqlStatement][QueryTimeout]")
{
    auto connection = SqlConnection {};
    auto stmt = SqlStatement { connection };
    UNSUPPORTED_DATABASE(stmt, SqlServerType::SQLITE); // pinned by the test case above

    stmt.SetQueryTimeout(1s);
    CHECK(stmt.QueryTimeout() == 1s);

    auto const query = SlowQuery(connection.ServerType());
    std::string_view const expectedState = connection.ServerType() == SqlServerType::MICROSOFT_SQL ? "HYT00" : "57014";

    auto const verifyTimesOut = [&](auto const& execute) {
        auto const start = std::chrono::steady_clock::now();
        try
        {
            execute();
            FAIL("The query must have timed out.");
        }
        catch (SqlException const& error)
        {
            CHECK(error.info().sqlState == expectedState);
            CHECK(IsQueryTimeoutError(error.info()));
        }
        CHECK(std::chrono::steady_clock::now() - start < 4s);
    };

    SECTION("direct execution")
    {
        verifyTimesOut([&] { std::ignore = stmt.ExecuteDirect(query); });
    }

    SECTION("prepared execution")
    {
        stmt.Prepare(query);
        verifyTimesOut([&] { std::ignore = stmt.Execute(); });
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SetQueryTimeout: the statement and connection stay usable after a timeout",
                 "[SqlStatement][QueryTimeout]")
{
    auto connection = SqlConnection {};
    auto stmt = SqlStatement { connection };
    UNSUPPORTED_DATABASE(stmt, SqlServerType::SQLITE);

    stmt.SetQueryTimeout(1s);
    CHECK_THROWS_AS(std::ignore = stmt.ExecuteDirect(SlowQuery(connection.ServerType())), SqlException);

    SECTION("the same statement, still bounded, runs a quick query")
    {
        CHECK(stmt.ExecuteDirectScalar<int>("SELECT 1") == 1);
    }

    SECTION("clearing the timeout lifts the bound")
    {
        stmt.SetQueryTimeout(0s);
        CHECK(stmt.QueryTimeout() == 0s);
        CHECK_NOTHROW(std::ignore = stmt.ExecuteDirect(TwoSecondQuery(connection.ServerType())));
    }

    SECTION("another statement on the connection is not bounded")
    {
        auto other = SqlStatement { connection };
        CHECK(other.QueryTimeout() == 0s);
        CHECK_NOTHROW(std::ignore = other.ExecuteDirect(TwoSecondQuery(connection.ServerType())));
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SetQueryTimeout: a timeout does not end the transaction", "[SqlStatement][QueryTimeout]")
{
    using namespace SqlColumnTypeDefinitions;

    auto connection = SqlConnection {};
    auto stmt = SqlStatement { connection };
    UNSUPPORTED_DATABASE(stmt, SqlServerType::SQLITE);

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.DropTableIfExists("query_timeout_tx");
        migration.CreateTable("query_timeout_tx").PrimaryKey("id", Integer {});
    });

    {
        auto transaction = SqlTransaction { connection, SqlTransactionMode::ROLLBACK };
        std::ignore = stmt.ExecuteDirect("INSERT INTO query_timeout_tx (id) VALUES (1)");

        stmt.SetQueryTimeout(1s);
        CHECK_THROWS_AS(std::ignore = stmt.ExecuteDirect(SlowQuery(connection.ServerType())), SqlException);

        // The row written before the timeout is still there, so the timed-out query alone was undone.
        CHECK(stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM query_timeout_tx") == 1);
    }

    CHECK(stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM query_timeout_tx") == 0);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SetQueryTimeout: a pooled handle does not carry the timeout to its next user",
                 "[SqlStatement][QueryTimeout]")
{
    auto connection = SqlConnection {};
    auto probe = SqlStatement { connection };
    UNSUPPORTED_DATABASE(probe, SqlServerType::SQLITE);
    connection.SetPreparedStatementCacheCapacity(8);

    auto const query = TwoSecondQuery(connection.ServerType());
    {
        auto first = SqlStatement { connection };
        first.SetQueryTimeout(1s);
        first.Prepare(query);
    } // parks the handle in the cache

    auto second = SqlStatement { connection };
    second.Prepare(query);
    REQUIRE(connection.PreparedStatementCache().Stats().hits == 1);

    CHECK(second.QueryTimeout() == 0s);
    CHECK_NOTHROW(std::ignore = second.Execute());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SetQueryTimeout: the timeout follows the statement onto another handle",
                 "[SqlStatement][QueryTimeout]")
{
    auto connection = SqlConnection {};
    auto probe = SqlStatement { connection };
    UNSUPPORTED_DATABASE(probe, SqlServerType::SQLITE);
    connection.SetPreparedStatementCacheCapacity(8);

    auto const query = SlowQuery(connection.ServerType());

    SECTION("Prepare() takes a pooled handle")
    {
        {
            auto first = SqlStatement { connection };
            first.Prepare(query);
        } // parks the handle in the cache

        auto second = SqlStatement { connection };
        second.SetQueryTimeout(1s);
        second.Prepare(query);
        REQUIRE(connection.PreparedStatementCache().Stats().hits == 1);

        CHECK(second.QueryTimeout() == 1s);
        CHECK_THROWS_AS(std::ignore = second.Execute(), SqlException);
    }

    SECTION("Prepare() of another text parks the handle and allocates a new one")
    {
        auto stmt = SqlStatement { connection };
        stmt.SetQueryTimeout(1s);
        stmt.Prepare("SELECT 1");
        stmt.Prepare(query);

        CHECK(stmt.QueryTimeout() == 1s);
        CHECK_THROWS_AS(std::ignore = stmt.Execute(), SqlException);
    }

    SECTION("ExecuteDirect() parks the handle and allocates a new one")
    {
        auto stmt = SqlStatement { connection };
        stmt.SetQueryTimeout(1s);
        stmt.Prepare("SELECT 1");

        CHECK_THROWS_AS(std::ignore = stmt.ExecuteDirect(query), SqlException);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SetQueryTimeout: a retry policy retries the timeout on SQL Server only",
                 "[SqlStatement][QueryTimeout]")
{
    auto connection = SqlConnection {};
    auto stmt = SqlStatement { connection };
    UNSUPPORTED_DATABASE(stmt, SqlServerType::SQLITE);

    stmt.SetQueryTimeout(1s);
    auto const query = SlowQuery(connection.ServerType());
    auto const settings = SqlRetrySettings { .maxRetries = 1, .initialDelay = 1ms };

    auto attempts = std::size_t { 0 };
    CHECK_THROWS_AS(SqlRetryPolicy::For(connection.ServerType(), settings).Execute([&] {
        ++attempts;
        std::ignore = stmt.ExecuteDirect(query);
    }),
                    SqlException);

    // HYT00 is transient, as it also is for a login timeout, so each attempt spends the whole timeout again.
    CHECK(attempts == (connection.ServerType() == SqlServerType::MICROSOFT_SQL ? 2U : 1U));
}
