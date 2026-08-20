// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <optional>

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

// ================================================================================================
// Configurable connection encryption (SQL_COPT_SS_ENCRYPT / Encrypt=)
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection: an explicitly encrypted connection is usable", "[SqlConnection]")
{
    auto probe = SqlStatement {};

    // Connection encryption is a SQL Server concept; the other backends configure TLS through their
    // own driver keywords and reject `Encrypt=`.
    UNSUPPORTED_DATABASE(probe, SqlServerType::SQLITE);
    UNSUPPORTED_DATABASE(probe, SqlServerType::POSTGRESQL);
    UNSUPPORTED_DATABASE(probe, SqlServerType::MYSQL);
    UNSUPPORTED_DATABASE(probe, SqlServerType::UNKNOWN);

    // ParseConnectionString upper-cases every key, so the overrides below must use the upper-cased
    // spelling too: a mixed-case key would land next to (not on top of) the existing entry, and which
    // of the two duplicates the driver honours is unspecified.
    auto parameters = ParseConnectionString(SqlConnection::DefaultConnectionString());
    parameters.insert_or_assign("ENCRYPT", std::string { FormatEncryptionMode(SqlEncryptionMode::Enabled) });
    // The CI SQL Server runs with a self-signed certificate, so the chain cannot be validated.
    parameters.insert_or_assign("TRUSTSERVERCERTIFICATE", "yes");

    // Not every SQL Server deployment can serve an encrypted channel: SQL Server Express LocalDB,
    // which the "MS SQL Server (LocalDB)" CI leg runs against, has no TLS endpoint at all and
    // rejects the handshake outright with 08001 "Encryption not supported on SQL Server". That is a
    // property of the instance rather than of the DBMS, so `UNSUPPORTED_DATABASE` (which keys on
    // `ServerType`) cannot express it - connect through the non-throwing overload and skip only on
    // that specific refusal, so a genuine failure of the `Encrypt=` plumbing still fails the test.
    auto connection = SqlConnection { std::nullopt };
    if (!connection.Connect(BuildConnectionString(parameters)))
    {
        auto const error = connection.LastError();
        if (error.message.contains("Encryption not supported"))
        {
            WARN(std::format("TODO({}): this server does not offer an encrypted endpoint: {}",
                             probe.Connection().ServerType(),
                             error.message));
            return;
        }
        FAIL(std::format("Encrypted connection failed: {} - {}", error.sqlState, error.message));
    }
    REQUIRE(connection.IsAlive());

    // The connection is not merely established — it round-trips a query over the encrypted channel.
    auto stmt = SqlStatement { connection };
    CHECK(stmt.ExecuteDirectScalar<int>("SELECT 42") == 42);

    // ... and the channel really is encrypted, rather than the keyword having been ignored.
    CHECK(
        stmt.ExecuteDirectScalar<std::string>("SELECT encrypt_option FROM sys.dm_exec_connections WHERE session_id = @@SPID")
        == "TRUE");
}
