// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace Lightweight;

// ================================================================================================
// SqlConnection introspection getters (DB-dependent)
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::ServerName / ServerVersion / DriverName are non-empty", "[SqlConnection]")
{
    auto stmt = SqlStatement {};
    auto const& conn = stmt.Connection();

    CHECK_FALSE(conn.ServerName().empty());
    CHECK_FALSE(conn.ServerVersion().empty());
    CHECK_FALSE(conn.DriverName().empty());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::TransactionsAllowed reports a capability flag", "[SqlConnection]")
{
    auto stmt = SqlStatement {};
    // SQLite, MSSQL, and PostgreSQL all support transactions; we just exercise the call.
    CHECK(stmt.Connection().TransactionsAllowed());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::IsAlive returns true on a live connection", "[SqlConnection]")
{
    auto stmt = SqlStatement {};
    CHECK(stmt.Connection().IsAlive());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::LastUsed setter / getter round-trip", "[SqlConnection]")
{
    auto stmt = SqlStatement {};
    auto& conn = stmt.Connection();

    auto const t = std::chrono::steady_clock::now();
    conn.SetLastUsed(t);
    CHECK(conn.LastUsed() == t);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::RequireSuccess passes through SQL_SUCCESS", "[SqlConnection]")
{
    auto stmt = SqlStatement {};
    REQUIRE_NOTHROW(stmt.Connection().RequireSuccess(SQL_SUCCESS));
    REQUIRE_NOTHROW(stmt.Connection().RequireSuccess(SQL_SUCCESS_WITH_INFO));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::RequireSuccess throws on failure", "[SqlConnection]")
{
    auto stmt = SqlStatement {};
    auto const _ = ScopedSqlNullLogger {};
    CHECK_THROWS_AS(stmt.Connection().RequireSuccess(SQL_ERROR), SqlException);
}

// ================================================================================================
// Query / QueryAs / Migration builders
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::Query produces a builder that emits the expected SELECT", "[SqlConnection]")
{
    auto stmt = SqlStatement {};
    auto const sql = stmt.Connection().Query("Users").Select().Field("Name").All().ToSql();
    CHECK(sql.contains("Users"));
    CHECK(sql.contains("Name"));
    CHECK(sql.contains("SELECT"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::QueryAs adds a table alias", "[SqlConnection]")
{
    auto stmt = SqlStatement {};
    auto const sql = stmt.Connection().QueryAs("Users", "u").Select().Field("Name").All().ToSql();
    CHECK(sql.contains("Users"));
    CHECK(sql.contains("u"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::Migration returns a usable migration builder", "[SqlConnection]")
{
    auto stmt = SqlStatement {};
    auto migration = stmt.Connection().Migration();
    migration.CreateTable("Demo").RequiredColumn("id", SqlColumnTypeDefinitions::Integer {});
    auto const& plan = migration.GetPlan();
    REQUIRE_FALSE(plan.steps.empty());
}

// ================================================================================================
// PostConnectedHook is invoked exactly once on Connect()
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::SetPostConnectedHook fires on the next Connect", "[SqlConnection]")
{
    int invocations = 0;
    SqlConnection::SetPostConnectedHook([&](SqlConnection& /*conn*/) { ++invocations; });

    {
        auto fresh = SqlConnection { std::nullopt };
        CHECK(invocations == 0);

        REQUIRE(fresh.Connect(SqlConnection::DefaultConnectionString()));
        CHECK(invocations == 1);
    }

    // Restore the fixture's hook so subsequent tests still get post-connect setup.
    SqlConnection::SetPostConnectedHook(&SqlTestFixture::PostConnectedHook);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::ResetPostConnectedHook clears the hook", "[SqlConnection]")
{
    int invocations = 0;
    SqlConnection::SetPostConnectedHook([&](SqlConnection& /*conn*/) { ++invocations; });
    SqlConnection::ResetPostConnectedHook();

    auto fresh = SqlConnection { std::nullopt };
    REQUIRE(fresh.Connect(SqlConnection::DefaultConnectionString()));
    CHECK(invocations == 0);

    SqlConnection::SetPostConnectedHook(&SqlTestFixture::PostConnectedHook);
}

// ================================================================================================
// SqlConnection::Close + reuse — already partially covered; cover the explicit double-close path.
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::Close is idempotent", "[SqlConnection]")
{
    auto fresh = SqlConnection { std::nullopt };
    REQUIRE(fresh.Connect(SqlConnection::DefaultConnectionString()));
    REQUIRE(fresh.IsAlive());

    fresh.Close();
    fresh.Close(); // second Close must be a no-op, not crash
    CHECK_FALSE(fresh.IsAlive());
}
