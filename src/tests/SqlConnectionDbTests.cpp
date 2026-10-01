// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <string_view>

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

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: a password carrying connection-string metacharacters logs in",
                 "[SqlConnection]")
{
    auto probe = SqlStatement {};

    // SQLite has no logins to create, and the remaining types are not part of the test matrix.
    UNSUPPORTED_DATABASE(probe, SqlServerType::SQLITE);
    UNSUPPORTED_DATABASE(probe, SqlServerType::MYSQL);
    UNSUPPORTED_DATABASE(probe, SqlServerType::UNKNOWN);

    // Every character the connection-string syntax could misread (#635): the `;` would end the
    // attribute, the `}` would end a brace-quoted value, and the `=` splits a key from its value.
    constexpr auto Login = std::string_view { "lightweight_quirky" };
    constexpr auto Password = std::string_view { "p;w}x=y" };

    struct LoginScript
    {
        SqlServerType server;
        std::array<std::string_view, 2> create;
        std::array<std::string_view, 2> drop;
        std::string_view whoAmI;
    };
    // Idempotent in both directions, so a run that died half-way does not poison the next one.
    constexpr auto Scripts = std::array {
        LoginScript {
            .server = SqlServerType::MICROSOFT_SQL,
            .create = { "IF SUSER_ID('lightweight_quirky') IS NULL "
                        "CREATE LOGIN [lightweight_quirky] WITH PASSWORD = 'p;w}x=y', CHECK_POLICY = OFF",
                        "IF USER_ID('lightweight_quirky') IS NULL "
                        "CREATE USER [lightweight_quirky] FOR LOGIN [lightweight_quirky]" },
            .drop = { "DROP USER IF EXISTS [lightweight_quirky]",
                      "IF SUSER_ID('lightweight_quirky') IS NOT NULL DROP LOGIN [lightweight_quirky]" },
            .whoAmI = "SELECT SUSER_NAME()",
        },
        LoginScript {
            .server = SqlServerType::POSTGRESQL,
            .create = { "DO $$ BEGIN IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'lightweight_quirky') "
                        "THEN CREATE ROLE lightweight_quirky LOGIN PASSWORD 'p;w}x=y'; END IF; END $$",
                        "SELECT 1" },
            .drop = { "DROP ROLE IF EXISTS lightweight_quirky", "SELECT 1" },
            .whoAmI = "SELECT current_user",
        },
    };
    auto const* script = static_cast<LoginScript const*>(nullptr);
    for (auto const& candidate: Scripts)
        if (candidate.server == probe.Connection().ServerType())
            script = &candidate;
    REQUIRE(script != nullptr);

    for (auto const sql: script->create)
        (void) probe.ExecuteDirect(sql); // throws on failure
    auto const dropLogin = detail::Finally([&] {
        for (auto const sql: script->drop)
            (void) probe.ExecuteDirect(sql);
    });

    // Swap the credentials into the test connection string; the builder has to brace-quote the
    // password and double its `}`, which is what the driver reads back.
    auto parameters = ParseConnectionString(SqlConnection::DefaultConnectionString());
    parameters.insert_or_assign("UID", std::string { Login });
    parameters.insert_or_assign("PWD", std::string { Password });
    // With `Trusted_Connection=yes` (the CI LocalDB leg) the driver authenticates as the Windows user
    // and ignores UID/PWD, which would prove nothing about the quoting: force SQL authentication.
    parameters.erase("TRUSTED_CONNECTION");
    auto const quoted = BuildConnectionString(parameters);
    CHECK(quoted.value.contains("PWD={p;w}}x=y}"));

    auto connection = SqlConnection { std::nullopt };
    if (!connection.Connect(quoted))
    {
        auto const error = connection.LastError();
        // A server restricted to Windows authentication refuses every SQL login, quoted or not; that
        // is a property of the instance, so skip only on that refusal and fail on anything else.
        if (error.message.contains("not associated with a trusted SQL Server connection"))
        {
            WARN(std::format(
                "TODO({}): this server does not accept SQL logins: {}", probe.Connection().ServerType(), error.message));
            return;
        }
        FAIL(std::format("Login failed: {} - {}", error.sqlState, error.message));
    }

    auto stmt = SqlStatement { connection };
    CHECK(stmt.ExecuteDirectScalar<std::string>(script->whoAmI) == Login);
}

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

// ================================================================================================
// SqlConnection::Connect(SqlConnectionDataSource): the encryption request reaches the ODBC handle
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: a data-source encryption request is applied before connecting",
                 "[SqlConnection]")
{
    // SQL_COPT_SS_ENCRYPT is a *pre-connect* attribute, so Connect(SqlConnectionDataSource) applies it
    // before SQLConnectW ever resolves the DSN. This test exploits that ordering: pointing at a data
    // source that deliberately does not exist still runs the whole attribute path, so the mapping from
    // SqlEncryptionMode onto the ODBC value is exercised on every platform without needing a registered
    // DSN — neither the CI runners nor a developer checkout have one.
    auto const connectWith = [](SqlEncryptionMode mode) {
        auto connection = SqlConnection { std::nullopt };
        auto const connected = connection.Connect(SqlConnectionDataSource {
            .datasource = "LightweightNoSuchDSN",
            .username = "user",
            .password = "password",
            .timeout = std::chrono::seconds { 1 },
            .encryption = mode,
        });
        // No driver manager can resolve that data source, so the connect cannot succeed either way.
        REQUIRE_FALSE(connected);
        return connection.LastError();
    };

    // Without an opt-in the attribute is left untouched, so this is simply how the driver manager
    // reports a missing DSN on this platform. The SQLSTATE is captured rather than hardcoded: what
    // the assertions below care about is the delta, not the spelling. (Keep the name at or below
    // SQL_MAX_DSN_LENGTH, 32 characters — unixODBC rejects a longer one with HY090 before it ever
    // looks the data source up, which would make all three connects fail identically for the wrong
    // reason and the comparison below vacuous.)
    auto const baseline = connectWith(SqlEncryptionMode::DriverDefault);
    INFO(std::format("baseline SQLSTATE {}: {}", baseline.sqlState, baseline.message));

    // Opting in must not change *how* the connect fails — it must still fail on the data-source
    // lookup. A different SQLSTATE here means SQLSetConnectAttrW rejected the request itself, which
    // is the regression worth catching: a rejected attribute fails the connection outright rather
    // than silently downgrading a requested encrypted channel to plaintext.
    for (auto const mode: { SqlEncryptionMode::Disabled, SqlEncryptionMode::Enabled })
    {
        INFO(std::format("encryption mode: {}", FormatEncryptionMode(mode)));
        auto const error = connectWith(mode);
        INFO(std::format("SQLSTATE {}: {}", error.sqlState, error.message));
        CHECK(std::string_view { error.sqlState } == std::string_view { baseline.sqlState });
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection::Close leaves dependent statements safe to destroy", "[SqlConnection]")
{
    // Closing a connection frees its DBC handle, and the driver manager invalidates every statement
    // allocated from it at the same moment. A statement object outliving that must not free its now
    // dangling handle: under unixODBC that reads released driver memory and segfaults.
    auto mapper = DataMapper {};
    REQUIRE(mapper.Connection().IsAlive());

    mapper.Connection().Close();
    CHECK_FALSE(mapper.Connection().IsAlive());
    // `mapper` (and the SqlStatement it owns) is destroyed here; reaching the end of the test is the
    // assertion.
}

// ================================================================================================
// String truncation mode
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection string truncation mode: default and per-connection setter",
                 "[SqlConnection]")
{
    // The shipped default is Truncate.
    CHECK(SqlConnection::DefaultStringTruncationMode() == SqlStringTruncationMode::Truncate);

    auto stmt = SqlStatement {};
    auto& conn = stmt.Connection();

    // A freshly established connection adopts the default.
    CHECK(conn.StringTruncationMode() == SqlStringTruncationMode::Truncate);

    // The per-connection setter round-trips.
    conn.SetStringTruncationMode(SqlStringTruncationMode::Error);
    CHECK(conn.StringTruncationMode() == SqlStringTruncationMode::Error);
    conn.SetStringTruncationMode(SqlStringTruncationMode::Truncate);
    CHECK(conn.StringTruncationMode() == SqlStringTruncationMode::Truncate);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection string truncation mode governs an over-long write", "[SqlConnection]")
{
    auto stmt = SqlStatement {};
    auto& conn = stmt.Connection();

    // The two modes are distinguished by a session setting only on Microsoft SQL Server; on other
    // backends the mode is carried but there is no server rule to enforce, so there is nothing to
    // assert here.
    if (conn.ServerType() != SqlServerType::MICROSOFT_SQL)
        return;

    std::ignore = stmt.ExecuteDirect("CREATE TABLE #StringTruncationProbe (c VARCHAR(5))");

    // Error mode rejects a value that does not fit the column.
    conn.SetStringTruncationMode(SqlStringTruncationMode::Error);
    CHECK_THROWS(std::ignore = stmt.ExecuteDirect("INSERT INTO #StringTruncationProbe (c) VALUES ('ABCDEFGHIJ')"));

    // Truncate mode accepts it, storing the value shortened to the column width, with no error.
    conn.SetStringTruncationMode(SqlStringTruncationMode::Truncate);
    std::ignore = stmt.ExecuteDirect("INSERT INTO #StringTruncationProbe (c) VALUES ('ABCDEFGHIJ')");

    auto const stored = stmt.ExecuteDirectScalar<std::string>("SELECT c FROM #StringTruncationProbe");
    REQUIRE(stored.has_value());
    CHECK(stored.value_or(std::string {}) == "ABCDE");

    std::ignore = stmt.ExecuteDirect("DROP TABLE #StringTruncationProbe");
}
