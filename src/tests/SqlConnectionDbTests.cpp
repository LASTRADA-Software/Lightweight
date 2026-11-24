// SPDX-License-Identifier: Apache-2.0

#include "ScriptedFaultSource.hpp"
#include "UniqueTempPath.hpp"
#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlTransaction.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using namespace Lightweight;
using namespace std::string_view_literals;
using Lightweight::Tests::ScopedFaultSource;
using Lightweight::Tests::ScriptedFaultSource;

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

// ================================================================================================
// SQLite connection settings: busy_timeout and journal_mode (#648)
// ================================================================================================

namespace
{

/// Reads a SQLite connection's effective busy timeout back from the engine.
int QueryBusyTimeout(SqlConnection& connection)
{
    auto stmt = SqlStatement { connection };
    return stmt.ExecuteDirectScalar<int>("PRAGMA busy_timeout").value_or(-1);
}

/// Reads a SQLite connection's effective journal mode back from the engine, lower-cased as SQLite
/// reports it.
std::string QueryJournalMode(SqlConnection& connection)
{
    auto stmt = SqlStatement { connection };
    return stmt.ExecuteDirectScalar<std::string>("PRAGMA journal_mode").value_or(std::string {});
}

/// The test connection string with its database file replaced by @p path, so that a test which switches
/// the journal mode (a persistent property of the file) leaves the shared test database untouched.
SqlConnectionString WithSqliteDatabaseFile(std::filesystem::path const& path)
{
    auto map = ParseConnectionString(SqlConnection::DefaultConnectionString());
    auto const utf8Path = path.u8string();
    map["DATABASE"] = std::string { utf8Path.begin(), utf8Path.end() };
    return BuildConnectionString(map);
}

/// @p connectionString with its DRIVER spelled through @p respell, which the driver manager matches
/// case-insensitively: the same driver under a name no earlier connection of this process used.
template <typename Respell>
SqlConnectionString WithDriverSpelling(SqlConnectionString const& connectionString, Respell const& respell)
{
    auto map = ParseConnectionString(connectionString);
    auto& driver = map["DRIVER"];
    std::ranges::transform(driver, driver.begin(), [&](char c) { return respell(static_cast<unsigned char>(c)); });
    return BuildConnectionString(map);
}

/// A scratch SQLite database file path, unique per test and per test run, so that runs sharing the
/// temporary directory never open one another's files.
std::filesystem::path ScratchSqliteDatabasePath(std::string_view name)
{
    return UniqueTempPath(std::format("lightweight-{}", name), ".db");
}

/// Removes a SQLite database file together with its journal and WAL side files.
void RemoveSqliteDatabaseFiles(std::filesystem::path const& path)
{
    auto ec = std::error_code {};
    for (auto const* suffix: { "", "-wal", "-shm", "-journal" })
        std::filesystem::remove(std::filesystem::path { path } += suffix, ec);
}

} // namespace

TEST_CASE("SQLiteQueryFormatter: SQLite settings render as PRAGMA statements", "[SqlConnection][SQLite]")
{
    auto const& sqlite = SqlQueryFormatter::Sqlite();

    // The defaults keep the historical 60 s busy timeout and do not touch the journal mode.
    CHECK(sqlite.SqliteSettingsStatements(SqliteConnectionSettings {})
          == SqlQueryFormatter::StringList { "PRAGMA busy_timeout = 60000" });

    CHECK(sqlite.SqliteSettingsStatements(SqliteConnectionSettings {
              .busyTimeout = std::chrono::milliseconds { 1500 },
              .journalMode = SqliteJournalMode::Wal,
          })
          == SqlQueryFormatter::StringList { "PRAGMA busy_timeout = 1500", "PRAGMA journal_mode = WAL" });

    // Every journal mode other than Unchanged maps onto its PRAGMA keyword.
    auto const expectations = std::array {
        std::pair { SqliteJournalMode::Delete, std::string_view { "DELETE" } },
        std::pair { SqliteJournalMode::Truncate, std::string_view { "TRUNCATE" } },
        std::pair { SqliteJournalMode::Persist, std::string_view { "PERSIST" } },
        std::pair { SqliteJournalMode::Memory, std::string_view { "MEMORY" } },
        std::pair { SqliteJournalMode::Wal, std::string_view { "WAL" } },
        std::pair { SqliteJournalMode::Off, std::string_view { "OFF" } },
    };
    for (auto const& [mode, keyword]: expectations)
    {
        auto const statements = sqlite.SqliteSettingsStatements({ .journalMode = mode });
        REQUIRE(statements.size() == 2);
        CHECK(statements.back() == std::format("PRAGMA journal_mode = {}", keyword));
    }

    // A negative timeout is clamped to zero, an oversized one to the largest value SQLite accepts.
    CHECK(sqlite.SqliteSettingsStatements({ .busyTimeout = std::chrono::milliseconds { -5 } }).front()
          == "PRAGMA busy_timeout = 0");
    CHECK(sqlite.SqliteSettingsStatements({ .busyTimeout = std::chrono::hours { 24 * 365 } }).front()
          == "PRAGMA busy_timeout = 2147483647");

    // Every other dialect has no such settings, so nothing is issued there.
    auto const custom = SqliteConnectionSettings {
        .busyTimeout = std::chrono::milliseconds { 1 },
        .journalMode = SqliteJournalMode::Wal,
    };
    CHECK(SqlQueryFormatter::SqlServer().SqliteSettingsStatements(custom).empty());
    CHECK(SqlQueryFormatter::PostgrSQL().SqliteSettingsStatements(custom).empty());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection: SQLite settings default to a 60 s busy timeout", "[SqlConnection][SQLite]")
{
    CHECK(SqlConnection::DefaultSqliteSettings() == SqliteConnectionSettings {});

    auto connection = SqlConnection {};
    CHECK(connection.SqliteSettings() == SqliteConnectionSettings {});

    // The engine-side check only exists on SQLite; the inert case is covered by its own test below.
    if (connection.ServerType() == SqlServerType::SQLITE)
        CHECK(QueryBusyTimeout(connection) == 60'000);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: SQLite busy timeout and journal mode are applied on connect",
                 "[SqlConnection][SQLite]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        SKIP("SQLite-only: busy_timeout and journal_mode are SQLite pragmas");

    // WAL is a property of the database file, so use a scratch file rather than the shared test database.
    auto const path = ScratchSqliteDatabasePath("sqlite-settings");
    RemoveSqliteDatabaseFiles(path);
    auto const cleanup = detail::Finally([&] { RemoveSqliteDatabaseFiles(path); });
    REQUIRE(EnsureSqliteDatabaseFileExists(WithSqliteDatabaseFile(path)));

    {
        // An untouched file starts out in SQLite's default rollback-journal mode, and the default
        // settings leave it there.
        auto connection = SqlConnection { WithSqliteDatabaseFile(path) };
        CHECK(QueryJournalMode(connection) == "delete");
    }

    auto const custom = SqliteConnectionSettings {
        .busyTimeout = std::chrono::milliseconds { 1234 },
        .journalMode = SqliteJournalMode::Wal,
    };
    auto connection = SqlConnection { WithSqliteDatabaseFile(path), custom };
    CHECK(connection.SqliteSettings() == custom);
    CHECK(QueryBusyTimeout(connection) == 1234);
    CHECK(QueryJournalMode(connection) == "wal");

    // Changing the settings of an open connection applies them at once.
    auto const changed = SqliteConnectionSettings {
        .busyTimeout = std::chrono::milliseconds { 250 },
        .journalMode = SqliteJournalMode::Delete,
    };
    auto const effect = connection.SetSqliteSettings(changed);
    REQUIRE(effect.has_value());
    // The driver read its own wait from the connection string when connecting: the new one governs it from the
    // next connect. Changing only the journal mode takes effect in full.
    CHECK(effect.value_or(SqliteSettingsEffect::Applied) == SqliteSettingsEffect::BusyTimeoutAtNextConnect);
    CHECK(connection.SqliteSettings() == changed);
    CHECK(QueryBusyTimeout(connection) == 250);
    CHECK(QueryJournalMode(connection) == "delete");

    // A reconnect re-applies the connection's own settings, not the process-wide default.
    REQUIRE(connection.Connect(WithSqliteDatabaseFile(path)));
    CHECK(QueryBusyTimeout(connection) == 250);
    CHECK(connection
              .SetSqliteSettings({ .busyTimeout = std::chrono::milliseconds { 250 }, .journalMode = SqliteJournalMode::Wal })
              .value_or(SqliteSettingsEffect::BusyTimeoutAtNextConnect)
          == SqliteSettingsEffect::Applied);
}

TEST_CASE("ServerTypeFromDriverName tells SQLite from the driver's name or library path", "[SqlConnection]")
{
    // Every spelling a DRIVER= attribute or a DSN's Driver= entry takes for the SQLite ODBC driver.
    for (auto const name: { "SQLite3"sv,
                            "sqlite3"sv,
                            "{SQLite3 ODBC Driver}"sv,
                            "SqLiTe3"sv,
                            "/usr/lib/x86_64-linux-gnu/odbc/libsqlite3odbc.so"sv,
                            "/opt/homebrew/lib/libsqlite3odbc.dylib"sv,
                            R"(C:\Windows\System32\sqlite3odbc.dll)"sv })
    {
        INFO(name);
        CHECK(ServerTypeFromDriverName(name) == SqlServerType::SQLITE);
    }
    CHECK(ServerTypeFromDriverName("{PostgreSQL Unicode}") == SqlServerType::POSTGRESQL);
    CHECK(ServerTypeFromDriverName("/usr/lib/x86_64-linux-gnu/odbc/psqlodbcw.so") == SqlServerType::POSTGRESQL);
    CHECK(ServerTypeFromDriverName("ODBC Driver 18 for SQL Server") == SqlServerType::MICROSOFT_SQL);
    CHECK(ServerTypeFromDriverName("/opt/microsoft/msodbcsql18/lib64/libmsodbcsql-18.so") == SqlServerType::MICROSOFT_SQL);
    CHECK(ServerTypeFromDriverName("") == SqlServerType::UNKNOWN);
    CHECK(ServerTypeFromDriverName("Some Other Driver") == SqlServerType::UNKNOWN);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: the SQLite busy timeout reaches a driver named in another case",
                 "[SqlConnection][SQLite]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        SKIP("SQLite-only: the SQLite ODBC driver's Timeout attribute");

    auto const path = ScratchSqliteDatabasePath("sqlite-other-case-driver");
    RemoveSqliteDatabaseFiles(path);
    auto const cleanup = detail::Finally([&] { RemoveSqliteDatabaseFiles(path); });
    REQUIRE(EnsureSqliteDatabaseFileExists(WithSqliteDatabaseFile(path)));

    // Upper-case first letter, the rest lower case ("Sqlite3"): a spelling no other test uses. The driver
    // decision a connect remembers is per spelling and process-wide, and the all-lower-case spelling must
    // stay unseen for "a non-ASCII SQLite path reaches a driver no connection reached before".
    auto first = true;
    auto const otherCase = WithDriverSpelling(WithSqliteDatabaseFile(path), [&](unsigned char c) {
        auto const upper = std::exchange(first, false);
        return static_cast<char>(upper ? std::toupper(c) : std::tolower(c));
    });
    {
        auto const _ = ScopedSqlNullLogger {};
        if (!SqlConnection { std::nullopt }.Connect(otherCase))
            SKIP("The driver manager does not match driver names case-insensitively");
    }

    auto writer = SqlConnection { WithSqliteDatabaseFile(path) };
    auto writerStmt = SqlStatement { writer };
    std::ignore = writerStmt.ExecuteDirect("CREATE TABLE probe (id INTEGER)");
    auto transaction = SqlTransaction { writer };
    std::ignore = writerStmt.ExecuteDirect("INSERT INTO probe (id) VALUES (1)");

    // The driver got the busy timeout of 0 as its Timeout attribute, so it gives up at once instead of after
    // its own 100 s.
    {
        auto connection = SqlConnection { std::nullopt, SqliteConnectionSettings { .busyTimeout = {} } };
        REQUIRE(connection.Connect(otherCase));
        auto stmt = SqlStatement { connection };
        auto const started = std::chrono::steady_clock::now();
        auto const failed = [&] {
            try
            {
                std::ignore = stmt.ExecuteDirect("PRAGMA journal_mode = WAL");
                return false;
            }
            catch (SqlException const&)
            {
                return true;
            }
        }();
        CHECK(failed);
        CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds { 10 });
    }

    transaction.Rollback();
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: a connection whose connects pass no driver attribute applies a busy timeout at once",
                 "[SqlConnection][SQLite]")
{
    auto const changed = SqliteConnectionSettings { .busyTimeout = std::chrono::milliseconds { 250 } };

    SECTION("borrowed handle")
    {
        auto owner = SqlConnection {};
        auto borrowed = SqlConnection::Borrow(owner.NativeHandle());
        CHECK(borrowed.SetSqliteSettings(changed).value_or(SqliteSettingsEffect::BusyTimeoutAtNextConnect)
              == SqliteSettingsEffect::Applied);
    }

    SECTION("data-source connect")
    {
        if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
            SKIP("SQLite-only: the SQLite ODBC driver's data source");
        auto connection = SqlConnection { std::nullopt };
        {
            auto const _ = ScopedSqlNullLogger {};
            auto source = SqlConnectionDataSource {};
            source.datasource = "SQLite3 Datasource";
            if (!connection.Connect(source))
                SKIP("No 'SQLite3 Datasource' data source on this machine");
        }
        CHECK(connection.SetSqliteSettings(changed).value_or(SqliteSettingsEffect::BusyTimeoutAtNextConnect)
              == SqliteSettingsEffect::Applied);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: the default SQLite settings reach connections made on the caller's behalf",
                 "[SqlConnection][SQLite]")
{
    auto const restore = detail::Finally([] { SqlConnection::SetDefaultSqliteSettings(SqliteConnectionSettings {}); });
    auto const custom = SqliteConnectionSettings { .busyTimeout = std::chrono::milliseconds { 4321 } };
    SqlConnection::SetDefaultSqliteSettings(custom);
    CHECK(SqlConnection::DefaultSqliteSettings() == custom);

    // A default-constructed DataMapper is how the connection pool creates its connections.
    auto mapper = DataMapper {};
    CHECK(mapper.Connection().SqliteSettings() == custom);
    if (mapper.Connection().ServerType() == SqlServerType::SQLITE)
        CHECK(QueryBusyTimeout(mapper.Connection()) == 4321);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: a SQLite setting that cannot be applied is reported",
                 "[SqlConnection][SQLite]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        SKIP("SQLite-only: the failure is SQLite refusing a journal-mode switch on a locked database");

    auto const path = ScratchSqliteDatabasePath("sqlite-settings-failure");
    RemoveSqliteDatabaseFiles(path);
    auto const cleanup = detail::Finally([&] { RemoveSqliteDatabaseFiles(path); });
    REQUIRE(EnsureSqliteDatabaseFileExists(WithSqliteDatabaseFile(path)));

    // A writer holds the database file locked by an open write transaction.
    auto writer = SqlConnection { WithSqliteDatabaseFile(path) };
    auto writerStmt = SqlStatement { writer };
    std::ignore = writerStmt.ExecuteDirect("CREATE TABLE probe (id INTEGER)");
    auto transaction = SqlTransaction { writer };
    std::ignore = writerStmt.ExecuteDirect("INSERT INTO probe (id) VALUES (1)");

    // Switching to WAL needs an exclusive lock, which a zero busy timeout gives up on at once.
    auto const impatientWal = SqliteConnectionSettings {
        .busyTimeout = std::chrono::milliseconds { 0 },
        .journalMode = SqliteJournalMode::Wal,
    };
    auto const _ = ScopedSqlNullLogger {};
    // The zero busy timeout takes effect: neither failure waits for the lock. The SQLite ODBC driver waits by
    // its own connect-time timeout (100 s by default) whatever PRAGMA busy_timeout says.
    auto const started = std::chrono::steady_clock::now();

    // While connecting, the failure fails the connect rather than leaving the journal mode unapplied. It
    // reports its own error, not the one an earlier failed connect left behind.
    auto connection = SqlConnection { std::nullopt, impatientWal };
    CHECK_FALSE(connection.Connect(SqlConnectionString { "DRIVER={Lightweight No Such Driver}" }));
    auto const earlierError = connection.LastError();
    REQUIRE(earlierError.sqlState != SqlErrorInfo {}.sqlState);
    CHECK_FALSE(connection.Connect(WithSqliteDatabaseFile(path)));
    CHECK(connection.LastError().sqlState != earlierError.sqlState);

    // On an open connection, the setter reports it.
    auto open = SqlConnection { WithSqliteDatabaseFile(path), SqliteConnectionSettings { .busyTimeout = {} } };
    auto const applied = open.SetSqliteSettings(impatientWal);
    CHECK_FALSE(applied.has_value());
    CHECK(open.SqliteSettings() == impatientWal);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds { 10 });

    transaction.Rollback();
}

TEST_CASE("SqlQueryFormatter: the SQLite driver's connect-time busy timeout", "[SqlConnection][SQLite]")
{
    using std::chrono::milliseconds;
    CHECK(SqlQueryFormatter::Sqlite().DriverConnectionAttributes({ .busyTimeout = milliseconds { 2500 } })
          == SqlConnectionStringMap { { "TIMEOUT", "2500" } });
    // The driver takes a Timeout below 1 as 1000000 ms, so zero becomes the shortest wait it has.
    CHECK(SqlQueryFormatter::Sqlite().DriverConnectionAttributes({ .busyTimeout = milliseconds { 0 } })
          == SqlConnectionStringMap { { "TIMEOUT", "1" } });
    CHECK(SqlQueryFormatter::PostgrSQL().DriverConnectionAttributes({ .busyTimeout = milliseconds { 0 } }).empty());
    CHECK(SqlQueryFormatter::SqlServer().DriverConnectionAttributes({ .busyTimeout = milliseconds { 0 } }).empty());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: a SQLite driver Timeout in the connection string wins over the busy timeout",
                 "[SqlConnection][SQLite]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        SKIP("SQLite-only: the SQLite ODBC driver's Timeout attribute");

    auto const path = ScratchSqliteDatabasePath("sqlite-explicit-timeout");
    RemoveSqliteDatabaseFiles(path);
    auto const cleanup = detail::Finally([&] { RemoveSqliteDatabaseFiles(path); });
    REQUIRE(EnsureSqliteDatabaseFileExists(WithSqliteDatabaseFile(path)));

    auto writer = SqlConnection { WithSqliteDatabaseFile(path) };
    auto writerStmt = SqlStatement { writer };
    std::ignore = writerStmt.ExecuteDirect("CREATE TABLE probe (id INTEGER)");
    auto transaction = SqlTransaction { writer };
    std::ignore = writerStmt.ExecuteDirect("INSERT INTO probe (id) VALUES (1)");
    auto const _ = ScopedSqlNullLogger {};

    // The application's own Timeout=1 is kept, so the journal-mode switch gives up at once instead of
    // waiting the minute the settings ask for.
    auto map = ParseConnectionString(WithSqliteDatabaseFile(path));
    map["TIMEOUT"] = "1";
    auto connection = SqlConnection {
        std::nullopt,
        SqliteConnectionSettings { .busyTimeout = std::chrono::seconds { 60 }, .journalMode = SqliteJournalMode::Wal },
    };
    auto const started = std::chrono::steady_clock::now();
    CHECK_FALSE(connection.Connect(BuildConnectionString(map)));
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds { 10 });

    transaction.Rollback();
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection: SQLite settings are inert on other backends", "[SqlConnection]")
{
    if (SqlConnection {}.ServerType() == SqlServerType::SQLITE)
        SKIP("Covers non-SQLite backends; on SQLite the settings take effect, see the tests above");

    // Settings that would visibly change a SQLite connection: connecting with them and changing them must
    // succeed without issuing anything, and the connection must stay fully usable.
    auto const custom = SqliteConnectionSettings {
        .busyTimeout = std::chrono::milliseconds { 1 },
        .journalMode = SqliteJournalMode::Wal,
    };
    auto connection = SqlConnection { SqlConnection::DefaultConnectionString(), custom };
    CHECK(connection.SqliteSettings() == custom);
    CHECK(connection.SetSqliteSettings({ .journalMode = SqliteJournalMode::Off }).has_value());

    auto stmt = SqlStatement { connection };
    CHECK(stmt.ExecuteDirectScalar<int>("SELECT 1").value_or(0) == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: a non-ASCII SQLite database path in the connection string is kept",
                 "[SqlConnection][SQLite]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        SKIP("SQLite-only: the connection string names the database file");

    auto const scratch = ScratchSqliteDatabasePath("non-ascii-path");
    auto const path = scratch.parent_path() / (std::filesystem::path { u8"Café-" } += scratch.filename());
    RemoveSqliteDatabaseFiles(path);
    auto const cleanup = detail::Finally([&] { RemoveSqliteDatabaseFiles(path); });

    {
        auto connection = SqlConnection { WithSqliteDatabaseFile(path) };
        REQUIRE(connection.IsAlive());
        auto stmt = SqlStatement { connection };
        std::ignore = stmt.ExecuteDirect("CREATE TABLE probe (id INTEGER)");
    }
    CHECK(std::filesystem::exists(path));
}

namespace
{

/// Reads the real diagnostics and counts the reads for connection handles, which a connect makes
/// only after an attempt failed.
class CountingDiagnosticSource final: public SqlDiagnosticSource
{
  public:
    [[nodiscard]] SqlErrorInfo Diagnose(SQLSMALLINT handleType, SQLHANDLE handle) override
    {
        if (handleType == SQL_HANDLE_DBC)
            ++_connectionReads;
        SetDiagnosticSource(nullptr);
        auto const restore = detail::Finally([this] { SetDiagnosticSource(this); });
        switch (handleType)
        {
            case SQL_HANDLE_DBC:
                return SqlErrorInfo::FromConnectionHandle(handle);
            case SQL_HANDLE_STMT:
                return SqlErrorInfo::FromStatementHandle(handle);
            default:
                return SqlErrorInfo::FromEnvironmentHandle(handle);
        }
    }

    [[nodiscard]] int ConnectionReads() const noexcept
    {
        return _connectionReads;
    }

  private:
    int _connectionReads = 0;
};

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: a non-ASCII SQLite path reaches a driver no connection reached before",
                 "[SqlConnection][SQLite]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        SKIP("SQLite-only: the connection string names the database file");

    // Unseen, so the connect goes wide, probes the driver, and reconnects with UTF-8 if it is ANSI-only.
    auto const scratch = ScratchSqliteDatabasePath("unseen-driver");
    auto const path = scratch.parent_path() / (std::filesystem::path { u8"crème-" } += scratch.filename());
#if !defined(_WIN32)
    // Where the first, wide attempt reached an ANSI-only driver: narrowed to Latin-1 bytes.
    auto const narrowed = scratch.parent_path() / (std::string { "cr\xE8me-" } + scratch.filename().string());
#endif
    auto const cleanup = detail::Finally([&] {
        RemoveSqliteDatabaseFiles(path);
#if !defined(_WIN32)
        RemoveSqliteDatabaseFiles(narrowed);
#endif
    });

    auto const connectionString =
        WithDriverSpelling(WithSqliteDatabaseFile(path), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // Only the non-Windows checks below depend on it: the Windows driver manager supplies the wide
    // entry points itself, so the narrowed file name never comes into play there.
    [[maybe_unused]] auto acceptsWideText = true;
    auto connectDiagnostics = CountingDiagnosticSource {};
    {
        auto connection = [&] {
            SetDiagnosticSource(&connectDiagnostics);
            auto const restore = detail::Finally([] { SetDiagnosticSource(nullptr); });
            return SqlConnection { connectionString };
        }();
        REQUIRE(connection.IsAlive());
        acceptsWideText = connection.AcceptsWideQueryText();
        auto stmt = SqlStatement { connection };
        std::ignore = stmt.ExecuteDirect("CREATE TABLE probe (id INTEGER)");
    }
    CHECK(std::filesystem::exists(path));
#if defined(__APPLE__)
    // APFS refuses a file name that is not valid UTF-8, so on an ANSI-only driver the wide attempt
    // fails to open the narrowed name, and the connect is retried with the UTF-8 bytes.
    CHECK_FALSE(std::filesystem::exists(narrowed));
    if (!acceptsWideText)
        CHECK(connectDiagnostics.ConnectionReads() > 0);
#elif !defined(_WIN32)
    // The documented cost of a first connect to an unseen ANSI-only driver: the wide attempt opened
    // the narrowed name, which the driver creates as an empty file.
    CHECK(std::filesystem::exists(narrowed) == !acceptsWideText);
#endif
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: a wide connect an unseen driver fails on non-ASCII text is retried as UTF-8",
                 "[SqlConnection][SQLite]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        SKIP("SQLite-only: the connection string names the database file");

    // The directory exists under its UTF-8 name only, so the narrowed path of a wide attempt that
    // reaches an ANSI-only driver cannot be opened and that attempt fails.
    auto const scratch = ScratchSqliteDatabasePath("failed-wide-connect");
    auto const directory = scratch.parent_path() / (std::filesystem::path { u8"naïve-" } += scratch.stem());
    std::filesystem::create_directories(directory);
    auto const path = directory / "database.db";
    auto const cleanup = detail::Finally([&] {
        auto ec = std::error_code {};
        std::filesystem::remove_all(directory, ec);
    });

    auto const connectionString =
        WithDriverSpelling(WithSqliteDatabaseFile(path), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    {
        auto connection = SqlConnection { connectionString };
        REQUIRE(connection.IsAlive());
        auto stmt = SqlStatement { connection };
        std::ignore = stmt.ExecuteDirect("CREATE TABLE probe (id INTEGER)");
    }
    CHECK(std::filesystem::exists(path));
}

namespace
{

/// Reports the same SQLSTATE for every diagnostic, so a test decides why a connect failed.
class FixedSqlStateSource final: public SqlDiagnosticSource
{
  public:
    explicit FixedSqlStateSource(std::string sqlState):
        _sqlState { std::move(sqlState) }
    {
    }

    [[nodiscard]] SqlErrorInfo Diagnose(SQLSMALLINT /*handleType*/, SQLHANDLE /*handle*/) override
    {
        return SqlErrorInfo { .nativeErrorCode = 0, .sqlState = _sqlState, .message = "scripted failure" };
    }

  private:
    std::string _sqlState;
};

/// Connects to @p connectionString, without retries, while every diagnostic reports @p sqlState.
bool ConnectReportingSqlState(SqlConnectionString const& connectionString, std::string sqlState)
{
    auto source = FixedSqlStateSource { std::move(sqlState) };
    SetDiagnosticSource(&source);
    auto const restore = detail::Finally([] { SetDiagnosticSource(nullptr); });
    auto connection = SqlConnection { std::nullopt };
    connection.SetConnectRetrySettings(SqlRetrySettings { .maxRetries = 0 });
    return connection.Connect(connectionString);
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: a failed wide connect is retried as UTF-8 unless the server was not reached",
                 "[SqlConnection][SQLite]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        SKIP("SQLite-only: the connection string names the database file");
    if (SqlConnection {}.AcceptsWideQueryText())
        SKIP("Covers ANSI-only drivers: only there does a wide connect fail on narrowed text");

    // As above: the wide attempt fails because the narrowed directory name does not exist.
    auto const scratch = ScratchSqliteDatabasePath("retry-by-sqlstate");
    auto const directory = scratch.parent_path() / (std::filesystem::path { u8"naïve-" } += scratch.stem());
    std::filesystem::create_directories(directory);
    auto const path = directory / "database.db";
    auto const cleanup = detail::Finally([&] {
        auto ec = std::error_code {};
        std::filesystem::remove_all(directory, ec);
    });
    auto const _ = ScopedSqlNullLogger {};

    // A driver spelling no other connection uses, so the connect makes the wide attempt: the letters
    // in the positions @p upperCaseMask sets are upper case, the others lower case.
    auto const unseenSpelling = [&](unsigned upperCaseMask) {
        auto position = 0U;
        return WithDriverSpelling(WithSqliteDatabaseFile(path), [&](unsigned char c) {
            auto const upper = ((upperCaseMask >> (position++ % 8U)) & 1U) != 0;
            return static_cast<char>(upper ? std::toupper(c) : std::tolower(c));
        });
    };

    // A failed connect is not remembered, so these may share one spelling.
    for (auto const* sqlState: { "08001", "08S01", "HYT00", "HYT01" })
    {
        INFO("SQLSTATE " << sqlState);
        CHECK_FALSE(ConnectReportingSqlState(unseenSpelling(0b0101'0101U), sqlState));
        CHECK_FALSE(std::filesystem::exists(path));
    }

    for (auto const& [sqlState, upperCaseMask]: { std::pair { "HY000", 0b1010'1010U }, std::pair { "28000", 0b0110'0110U } })
    {
        INFO("SQLSTATE " << sqlState);
        CHECK(ConnectReportingSqlState(unseenSpelling(upperCaseMask), sqlState));
        CHECK(std::filesystem::exists(path));
        RemoveSqliteDatabaseFiles(path);
    }
}
TEST_CASE_METHOD(SqlTestFixture, "SqlConnection: Connect on a connected connection", "[SqlConnection]")
{
    auto conn = SqlConnection {};
    REQUIRE(conn.IsAlive());

    SECTION("No statement allocated")
    {
        auto const sessionBefore = conn.SessionId();
        REQUIRE(conn.Connect(SqlConnection::DefaultConnectionString()));
        CHECK(conn.SessionId() != sessionBefore);
        CHECK(SqlStatement { conn }.ExecuteDirectScalar<int>("SELECT 42") == std::optional { 42 });
    }

    SECTION("A statement still allocated")
    {
        // On SQLite the attempt names another database file, so a refused one shows which target the
        // connection reports afterwards.
        auto const isSqlite = conn.ServerType() == SqlServerType::SQLITE;
        auto const path = ScratchSqliteDatabasePath("connect-refused");
        auto const cleanup = detail::Finally([&] {
            if (isSqlite)
                RemoveSqliteDatabaseFiles(path);
        });
        auto const target = isSqlite ? WithSqliteDatabaseFile(path) : SqlConnection::DefaultConnectionString();
        auto const connectionStringBefore = conn.ConnectionString();

        auto stmt = SqlStatement { conn };
        stmt.Prepare("SELECT 42");
        auto const sessionBefore = conn.SessionId();
        if (conn.Connect(target))
        {
            // The session ended; the statement gets a fresh handle on its next use.
            CHECK(conn.SessionId() != sessionBefore);
            CHECK(conn.ConnectionString().value == target.value);
        }
        else
        {
            // The driver refused to end the session while a statement is allocated (the SQLite ODBC
            // driver does): the refusal is reported and the session goes on untouched, connection
            // string included.
            CHECK(conn.LastError().sqlState == "25000");
            CHECK(conn.SessionId() == sessionBefore);
            CHECK(conn.ConnectionString().value == connectionStringBefore.value);
        }
        CHECK(stmt.ExecuteDirectScalar<int>("SELECT 42") == std::optional { 42 });
    }
}

namespace
{

/// Records the SQLSTATE of every error reported to the logger while it is installed.
class ErrorCapturingLogger: public SqlLogger::Null
{
  public:
    ErrorCapturingLogger()
    {
        SqlLogger::SetLogger(*this);
    }

    ErrorCapturingLogger(ErrorCapturingLogger const&) = delete;
    ErrorCapturingLogger(ErrorCapturingLogger&&) = delete;
    ErrorCapturingLogger& operator=(ErrorCapturingLogger const&) = delete;
    ErrorCapturingLogger& operator=(ErrorCapturingLogger&&) = delete;

    ~ErrorCapturingLogger() override
    {
        SqlLogger::SetLogger(m_previousLogger);
    }

    void OnError(SqlErrorInfo const& errorInfo, std::source_location /*sourceLocation*/) override
    {
        m_sqlStates.push_back(errorInfo.sqlState);
    }

    /// @return The SQLSTATE of every error reported so far, in order.
    [[nodiscard]] std::vector<std::string> const& SqlStates() const noexcept
    {
        return m_sqlStates;
    }

  private:
    SqlLogger& m_previousLogger = SqlLogger::GetLogger();
    std::vector<std::string> m_sqlStates;
};

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection: Close with a prepared statement alive", "[SqlConnection]")
{
    // On SQLite a scratch database file shows that Close() really closes the database: an open SQLite file
    // cannot be renamed on Windows.
    auto const isSqlite = SqlConnection {}.ServerType() == SqlServerType::SQLITE;
    auto const path = ScratchSqliteDatabasePath("close-with-statement");
    auto const renamed = std::filesystem::path { path }.concat(".closed");
    auto const cleanup = detail::Finally([&] {
        if (!isSqlite)
            return;
        RemoveSqliteDatabaseFiles(path);
        RemoveSqliteDatabaseFiles(renamed);
    });
    if (isSqlite)
        RemoveSqliteDatabaseFiles(path);

    auto conn = isSqlite ? SqlConnection { WithSqliteDatabaseFile(path) } : SqlConnection {};
    REQUIRE(conn.IsAlive());
    {
        auto stmt = SqlStatement { conn };
        stmt.Prepare("SELECT 42");
        {
            auto cursor = stmt.Execute();
            REQUIRE(cursor.FetchRow());
            CHECK(cursor.GetColumn<int>(1) == 42);
        }

        {
            // Close() reports a refused disconnect (the SQLite ODBC driver's 25000 while a statement is
            // allocated) to the logger: it must report nothing. This holds on every platform, where the
            // file rename below proves the database closed only on Windows.
            auto const logger = ErrorCapturingLogger {};
            conn.Close();
            CHECK(logger.SqlStates().empty());
        }
        CHECK_FALSE(conn.IsAlive());
        if (isSqlite)
        {
            auto error = std::error_code {};
            std::filesystem::rename(path, renamed, error);
            CHECK_FALSE(error);
        }

        // The statement's handle went with the session: using it again is an error, not a crash.
        CHECK_THROWS_AS(stmt.Prepare("SELECT 43"), SqlException);
    } // and its destructor frees nothing twice
}

TEST_CASE_METHOD(SqlTestFixture, "SqlStatement: executing after its session ended is a clean error", "[SqlConnection]")
{
    auto conn = SqlConnection {};
    auto stmt = SqlStatement { conn };
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("session_end").RequiredColumn("Value", SqlColumnTypeDefinitions::Integer {});
    });
    auto const insert = R"(INSERT INTO "session_end" ("Value") VALUES (?))"sv;
    auto const sqlStateOf = [](auto&& action) -> std::string {
        try
        {
            action();
            return "no error";
        }
        catch (SqlException const& error)
        {
            return error.info().sqlState;
        }
    };
    auto const endSession = [&] {
        conn.Close();
        REQUIRE(conn.Connect(SqlConnection::DefaultConnectionString()));
    };

    stmt.Prepare(insert);
    (void) stmt.Execute(1);
    endSession();
    CHECK(sqlStateOf([&] { (void) stmt.Execute(2); }) == "08003");
    // And again: the statement keeps reporting it until it is prepared on the live session.
    CHECK(sqlStateOf([&] { (void) stmt.Execute(2); }) == "08003");

    // Prepared again in the new session, it works.
    stmt.Prepare(insert);
    (void) stmt.Execute(3);
    endSession();
    CHECK(sqlStateOf([&] { (void) stmt.ExecuteBatch(std::array { 4, 5 }); }) == "08003");

    stmt.Prepare(insert);
    (void) stmt.ExecuteBatch(std::array { 6, 7 });
    CHECK(SqlStatement { conn }.ExecuteDirectScalar<int>(R"(SELECT COUNT(*) FROM "session_end")") == std::optional { 4 });
}

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor: destroyed after its connection closed", "[SqlConnection]")
{
    // The cursor's teardown must not pass the handle its connection's Close() freed to the driver. Under
    // a sanitizer that would be a use after free; without one it shows when the freed address is reused
    // by a statement of the next session, whose cursor the stale teardown would close.
    auto conn = SqlConnection {};
    SqlStatement { conn }.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("cursor_close").RequiredColumn("Value", SqlColumnTypeDefinitions::Integer {});
    });
    {
        auto stmt = SqlStatement { conn };
        stmt.Prepare(R"(INSERT INTO "cursor_close" ("Value") VALUES (?))");
        for (auto const value: std::views::iota(1, 4))
            (void) stmt.Execute(value);
    }
    auto const query = R"(SELECT "Value" FROM "cursor_close" ORDER BY "Value")"sv;

    for (auto const depth: { PrefetchDepthDefault, std::size_t { 1 } })
    {
        INFO("prefetch depth " << depth);
        conn.SetDefaultPrefetchDepth(depth);
        {
            auto stmt = SqlStatement { conn };
            auto cursor = stmt.ExecuteDirect(query);
            REQUIRE(cursor.FetchRow());
            conn.Close();
        } // cursor and statement destroyed after Close()
        REQUIRE(conn.Connect(SqlConnection::DefaultConnectionString()));
        conn.SetDefaultPrefetchDepth(depth);

        auto stale = SqlStatement { conn };
        auto staleCursor = std::optional<SqlResultCursor> { stale.ExecuteDirect(query) };
        REQUIRE(staleCursor->FetchRow());
        conn.Close();
        REQUIRE(conn.Connect(SqlConnection::DefaultConnectionString()));
        conn.SetDefaultPrefetchDepth(depth);

        // A statement of the new session reads while the stale cursor goes.
        auto fresh = SqlStatement { conn };
        auto freshCursor = fresh.ExecuteDirect(query);
        REQUIRE(freshCursor.FetchRow());
        staleCursor.reset();
        auto values = std::vector<int> { freshCursor.GetColumn<int>(1) };
        while (freshCursor.FetchRow())
            values.push_back(freshCursor.GetColumn<int>(1));
        CHECK(values == std::vector { 1, 2, 3 });
    }
}

namespace
{

/// The query every ended-session test reads: three rows of "cursor_fetch_closed".
constexpr auto CursorFetchClosedQuery = R"(SELECT "Value" FROM "cursor_fetch_closed" ORDER BY "Value")"sv;

/// Creates "cursor_fetch_closed" holding the values 1, 2 and 3.
void CreateCursorFetchClosedTable(SqlConnection& conn)
{
    SqlStatement { conn }.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("cursor_fetch_closed").RequiredColumn("Value", SqlColumnTypeDefinitions::Integer {});
    });
    auto stmt = SqlStatement { conn };
    stmt.Prepare(R"(INSERT INTO "cursor_fetch_closed" ("Value") VALUES (?))");
    for (auto const value: std::views::iota(1, 4))
        (void) stmt.Execute(value);
}

/// Connects @p conn again if an earlier step closed it.
void RequireConnected(SqlConnection& conn)
{
    if (!conn.IsAlive())
        REQUIRE(conn.Connect(SqlConnection::DefaultConnectionString()));
}

/// Ends @p conn's session: Close(), and with @p reconnect Connect() again.
void EndSession(SqlConnection& conn, bool reconnect)
{
    conn.Close();
    if (reconnect)
        REQUIRE(conn.Connect(SqlConnection::DefaultConnectionString()));
}

/// The SQLSTATE @p action throws, or "no error".
std::string SqlStateOfAction(std::function<void()> const& action)
{
    try
    {
        action();
        return "no error";
    }
    catch (SqlException const& error)
    {
        return error.info().sqlState;
    }
}

/// Checks that @p action fails with 08003, and again when repeated: the statement keeps reporting it.
void CheckEndedSessionTwice(std::function<void()> const& action)
{
    CHECK(SqlStateOfAction(action) == "08003");
    CHECK(SqlStateOfAction(action) == "08003");
}

/// A cursor that fetched a row, its session ended: the next fetch fails with 08003.
void CheckFetchAfterSessionEnd(SqlConnection& conn, bool reconnect, std::size_t prefetchDepth)
{
    RequireConnected(conn);
    conn.SetDefaultPrefetchDepth(prefetchDepth);
    auto stmt = SqlStatement { conn };
    auto cursor = stmt.ExecuteDirect(CursorFetchClosedQuery);
    REQUIRE(cursor.FetchRow());
    EndSession(conn, reconnect);
    CheckEndedSessionTwice([&] { (void) cursor.FetchRow(); });
}

/// A row-array fetch that fetched a block, its session ended: the next block fails with 08003.
void CheckFetchArrayAfterSessionEnd(SqlConnection& conn, bool reconnect)
{
    RequireConnected(conn);
    auto stmt = SqlStatement { conn };
    auto cursor = stmt.ExecuteBatchFetch(CursorFetchClosedQuery, 2);
    REQUIRE(cursor.FetchArray() == 2);
    EndSession(conn, reconnect);
    CheckEndedSessionTwice([&] { (void) cursor.FetchArray(); });
}

/// A cursor kept across its session's end: @p action on it fails with 08003.
void CheckCursorActionAfterSessionEnd(SqlConnection& conn,
                                      bool reconnect,
                                      std::function<void(SqlResultCursor&)> const& action)
{
    RequireConnected(conn);
    conn.SetDefaultPrefetchDepth(1); // bindings reach the driver at once
    auto stmt = SqlStatement { conn };
    auto cursor = stmt.ExecuteDirect(CursorFetchClosedQuery);
    EndSession(conn, reconnect);
    CheckEndedSessionTwice([&] { action(cursor); });
}

/// A statement prepared before its session ended: @p action on it fails with 08003.
void CheckStatementActionAfterSessionEnd(SqlConnection& conn,
                                         bool reconnect,
                                         std::function<void(SqlStatement&)> const& action)
{
    RequireConnected(conn);
    auto stmt = SqlStatement { conn };
    stmt.Prepare(R"(SELECT "Value" FROM "cursor_fetch_closed" WHERE "Value" > ?)");
    EndSession(conn, reconnect);
    CheckEndedSessionTwice([&] { action(stmt); });
}

} // namespace

/// A record of one integer column, for the record-binding entry points. At namespace scope, not in an
/// anonymous namespace: MSVC cannot reflect a type with internal linkage.
struct EndedSessionValueRecord
{
    std::int64_t value {};
};

TEST_CASE_METHOD(SqlTestFixture, "SqlResultCursor: fetching after its connection closed fails with 08003", "[SqlConnection]")
{
    auto conn = SqlConnection {};
    CreateCursorFetchClosedTable(conn);

    // Closed, and closed then connected again; with the rest of a prefetched block in memory, and per row;
    // and a row-array fetch.
    for (auto const reconnect: { false, true })
    {
        for (auto const depth: { PrefetchDepthDefault, std::size_t { 1 } })
        {
            INFO("reconnect " << reconnect << ", prefetch depth " << depth);
            CheckFetchAfterSessionEnd(conn, reconnect, depth);
        }
        INFO("reconnect " << reconnect << ", FetchArray");
        CheckFetchArrayAfterSessionEnd(conn, reconnect);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlResultCursor: every entry point after its connection closed fails with 08003",
                 "[SqlConnection]")
{
    // Every other entry that reaches the driver with the cursor's handle reports the same, instead of
    // handing the freed handle to the driver.
    auto conn = SqlConnection {};
    CreateCursorFetchClosedTable(conn);
    auto value = std::int64_t {};
    auto record = EndedSessionValueRecord {};
    auto records = std::vector<EndedSessionValueRecord> {};
    auto const cursorActions = std::array<std::pair<std::string_view, std::function<void(SqlResultCursor&)>>, 7> { {
        { "ConsumePendingResults", [](SqlResultCursor& cursor) { cursor.ConsumePendingResults(); } },
        { "FetchAllRowWise",
          [&](SqlResultCursor& cursor) {
              cursor.FetchAllRowWise(records, 4, [](EndedSessionValueRecord& row) -> std::int64_t& { return row.value; });
          } },
        { "NumRowsAffected", [](SqlResultCursor& cursor) { (void) cursor.NumRowsAffected(); } },
        { "NumColumnsAffected", [](SqlResultCursor& cursor) { (void) cursor.NumColumnsAffected(); } },
        { "BindOutputColumns", [&](SqlResultCursor& cursor) { cursor.BindOutputColumns(&value); } },
        { "BindOutputColumnsToRecord", [&](SqlResultCursor& cursor) { cursor.BindOutputColumnsToRecord(&record); } },
        { "BindOutputColumn", [&](SqlResultCursor& cursor) { cursor.BindOutputColumn(1, &value); } },
    } };
    for (auto const reconnect: { false, true })
    {
        for (auto const& [name, action]: cursorActions)
        {
            INFO("reconnect " << reconnect << ", " << name);
            CheckCursorActionAfterSessionEnd(conn, reconnect, action);
        }
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlStatement: a prepared statement's entry points after its connection closed fail with 08003",
                 "[SqlConnection]")
{
    // A prepared statement kept across Close(): binding its parameters and its attributes, and its diagnostics.
    auto conn = SqlConnection {};
    CreateCursorFetchClosedTable(conn);
    auto const statementActions = std::array<std::pair<std::string_view, std::function<void(SqlStatement&)>>, 4> { {
        { "BindInputParameter",
          [](SqlStatement& stmt) {
              auto const parameter = 2;
              stmt.BindInputParameter(1, parameter);
          } },
        { "SetQueryTimeout", [](SqlStatement& stmt) { stmt.SetQueryTimeout(std::chrono::seconds { 5 }); } },
        { "QueryTimeout", [](SqlStatement& stmt) { (void) stmt.QueryTimeout(); } },
        { "LastError", [](SqlStatement& stmt) { throw SqlException(stmt.LastError()); } },
    } };
    for (auto const reconnect: { false, true })
    {
        for (auto const& [name, action]: statementActions)
        {
            INFO("reconnect " << reconnect << ", " << name);
            CheckStatementActionAfterSessionEnd(conn, reconnect, action);
        }
    }
}

namespace
{

/// Counts the connections the logger is told were opened and closed while it is installed.
class ConnectionEventCountingLogger: public SqlLogger::Null
{
  public:
    ConnectionEventCountingLogger()
    {
        SqlLogger::SetLogger(*this);
    }
    ConnectionEventCountingLogger(ConnectionEventCountingLogger const&) = delete;
    ConnectionEventCountingLogger(ConnectionEventCountingLogger&&) = delete;
    ConnectionEventCountingLogger& operator=(ConnectionEventCountingLogger const&) = delete;
    ConnectionEventCountingLogger& operator=(ConnectionEventCountingLogger&&) = delete;
    ~ConnectionEventCountingLogger() override
    {
        SqlLogger::SetLogger(m_previousLogger);
    }

    void OnConnectionOpened(SqlConnection const& /*connection*/) override
    {
        ++m_opened;
        m_sequence += 'o';
    }

    void OnConnectionClosed(SqlConnection const& connection) override
    {
        ++m_closed;
        m_sequence += 'c';
        // Empty once the session has ended: the driver manager answers 08003 for a disconnected handle.
        m_closedServerNames.push_back(connection.ServerName());
    }

    /// @return What SqlConnection::ServerName() answered at each closed report, in order.
    [[nodiscard]] std::vector<std::string> const& ClosedServerNames() const noexcept
    {
        return m_closedServerNames;
    }

    /// @return The opened ('o') and closed ('c') reports, in the order they came.
    [[nodiscard]] std::string const& Sequence() const noexcept
    {
        return m_sequence;
    }

    void OnError(SqlErrorInfo const& errorInfo, std::source_location /*sourceLocation*/) override
    {
        ++m_errors;
        m_lastError = errorInfo;
    }

    /// @return The last error reported.
    [[nodiscard]] SqlErrorInfo const& LastError() const noexcept
    {
        return m_lastError;
    }

    /// @return The errors reported.
    [[nodiscard]] int Errors() const noexcept
    {
        return m_errors;
    }

    /// @return The connections reported opened.
    [[nodiscard]] int Opened() const noexcept
    {
        return m_opened;
    }

    /// @return The connections reported closed.
    [[nodiscard]] int Closed() const noexcept
    {
        return m_closed;
    }

  private:
    SqlLogger& m_previousLogger = SqlLogger::GetLogger();
    int m_opened = 0;
    int m_closed = 0;
    int m_errors = 0;
    std::string m_sequence;
    std::vector<std::string> m_closedServerNames;
    SqlErrorInfo m_lastError;
};

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: the SQLite busy timeout reaches the driver through a DSN without a second connect",
                 "[SqlConnection][SQLite]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        SKIP("SQLite-only: the SQLite ODBC driver's Timeout attribute");

    auto const path = ScratchSqliteDatabasePath("sqlite-dsn-timeout");
    RemoveSqliteDatabaseFiles(path);
    auto const cleanup = detail::Finally([&] { RemoveSqliteDatabaseFiles(path); });
    REQUIRE(EnsureSqliteDatabaseFileExists(WithSqliteDatabaseFile(path)));

    // The SQLite driver's installer registers this data source; the connection string names no DRIVER.
    auto map = ParseConnectionString(WithSqliteDatabaseFile(path));
    map.erase("DRIVER");
    map["DSN"] = "SQLite3 Datasource";
    auto const throughDataSource = BuildConnectionString(map);
    {
        auto const _ = ScopedSqlNullLogger {};
        if (!SqlConnection { std::nullopt }.Connect(throughDataSource))
            SKIP("No 'SQLite3 Datasource' data source on this machine");
    }

    auto writer = SqlConnection { WithSqliteDatabaseFile(path) };
    auto writerStmt = SqlStatement { writer };
    std::ignore = writerStmt.ExecuteDirect("CREATE TABLE probe (id INTEGER)");
    auto transaction = SqlTransaction { writer };
    std::ignore = writerStmt.ExecuteDirect("INSERT INTO probe (id) VALUES (1)");

    // One connect: one opened event and one post-connect hook run, and the driver gives up at once.
    auto hookRuns = 0;
    SqlConnection::SetPostConnectedHook([&hookRuns](SqlConnection& /*connection*/) { ++hookRuns; });
    auto const resetHook = detail::Finally([] { SqlConnection::ResetPostConnectedHook(); });
    {
        auto const events = ConnectionEventCountingLogger {};
        auto connection = SqlConnection { std::nullopt, SqliteConnectionSettings { .busyTimeout = {} } };
        REQUIRE(connection.Connect(throughDataSource));
        CHECK(events.Opened() == 1);
        CHECK(events.Closed() == 0);
        CHECK(hookRuns == 1);

        auto stmt = SqlStatement { connection };
        auto const started = std::chrono::steady_clock::now();
        auto const sqlState = [&]() -> std::string {
            try
            {
                std::ignore = stmt.ExecuteDirect("PRAGMA journal_mode = WAL");
                return "none";
            }
            catch (SqlException const& error)
            {
                return error.info().sqlState;
            }
        }();
        CHECK(sqlState != "none");
        CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds { 10 });
    }

    transaction.Rollback();
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection: a failed first connect reports no closing", "[SqlConnection]")
{
    auto const events = ConnectionEventCountingLogger {};
    {
        auto connection = SqlConnection { std::nullopt };
        CHECK_FALSE(connection.Connect(SqlConnectionString { "DRIVER={Lightweight No Such Driver}" }));
        auto const errorsOfConnect = events.Errors();
        connection.Close();
        CHECK(events.Errors() == errorsOfConnect); // no "connection not open" from the disconnect either
    } // and the destructor's Close() neither
    CHECK(events.Opened() == 0);
    CHECK(events.Closed() == 0);

    // A connection that was opened is reported closed once.
    {
        auto connection = SqlConnection {};
        REQUIRE(connection.IsAlive());
        connection.Close();
    }
    CHECK(events.Opened() == 1);
    CHECK(events.Closed() == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: a connect whose post-connect setup fails leaves the connection closed",
                 "[SqlConnection][SQLite]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        SKIP("SQLite-only: the failing setup is SQLite refusing a journal-mode switch on a locked database");

    auto const path = ScratchSqliteDatabasePath("sqlite-post-connect-failure");
    RemoveSqliteDatabaseFiles(path);
    auto const cleanup = detail::Finally([&] { RemoveSqliteDatabaseFiles(path); });
    REQUIRE(EnsureSqliteDatabaseFileExists(WithSqliteDatabaseFile(path)));

    // A writer holds the database locked, so the WAL switch the connect applies fails at once.
    auto writer = SqlConnection { WithSqliteDatabaseFile(path) };
    auto writerStmt = SqlStatement { writer };
    std::ignore = writerStmt.ExecuteDirect("CREATE TABLE probe (id INTEGER)");
    auto transaction = SqlTransaction { writer };
    std::ignore = writerStmt.ExecuteDirect("INSERT INTO probe (id) VALUES (1)");
    auto const impatientWal = SqliteConnectionSettings {
        .busyTimeout = std::chrono::milliseconds { 0 },
        .journalMode = SqliteJournalMode::Wal,
    };

    auto const events = ConnectionEventCountingLogger {};
    {
        auto connection = SqlConnection { std::nullopt, impatientWal };
        CHECK_FALSE(connection.Connect(WithSqliteDatabaseFile(path)));

        // The driver connected, but the connect failed: the error is the setup's (checked first: asking the
        // handle anything else posts a diagnostic of its own), and no session is left open.
        REQUIRE(events.Errors() > 0);
        CHECK(connection.LastError().sqlState == events.LastError().sqlState);
        CHECK(connection.LastError().message == events.LastError().message);
        CHECK(connection.SessionId() == 0);
        CHECK_FALSE(connection.IsAlive());
        auto const errorsOfConnect = events.Errors();
        connection.Close();
        CHECK(events.Errors() == errorsOfConnect); // no "connection not open" from the disconnect
    }
    CHECK(events.Sequence().empty()); // never reported opened, so never reported closed

    // Once the lock is gone, the same settings connect.
    transaction.Rollback();
    {
        auto connection = SqlConnection { std::nullopt, impatientWal };
        CHECK(connection.Connect(WithSqliteDatabaseFile(path)));
        CHECK(connection.IsAlive());
    }
    CHECK(events.Sequence() == "oc");
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlConnection: a connect whose autocommit setting fails leaves the connection closed",
                 "[SqlConnection]")
{
    // The driver connects; setting autocommit, the next step, is made to fail through the fault seam.
    auto const injected = SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "HY000", .message = "autocommit refused" };
    auto faults = ScriptedFaultSource { injected, 1, "ConnectDriver" };
    auto const events = ConnectionEventCountingLogger {};
    {
        auto connection = SqlConnection { std::nullopt };
        {
            auto const installed = ScopedFaultSource { &faults };
            CHECK_FALSE(connection.Connect(SqlConnection::DefaultConnectionString()));
        }
        CHECK(faults.RemainingFailures() == 0);
        CHECK(connection.LastError().sqlState == injected.sqlState);
        CHECK(connection.LastError().message == injected.message);
        CHECK(connection.SessionId() == 0);
        CHECK_FALSE(connection.IsAlive());
        auto const errorsOfConnect = events.Errors();
        connection.Close();
        CHECK(events.Errors() == errorsOfConnect);

        // The same connection connects once nothing fails.
        CHECK(connection.Connect(SqlConnection::DefaultConnectionString()));
        CHECK(connection.IsAlive());
    }
    CHECK(events.Sequence() == "oc");
}

TEST_CASE_METHOD(SqlTestFixture, "SqlConnection: a reconnect reports the replaced session closed", "[SqlConnection]")
{
    auto const events = ConnectionEventCountingLogger {};

    SECTION("Connect, then connect again")
    {
        {
            auto connection = SqlConnection {};
            REQUIRE(connection.IsAlive());
            REQUIRE(connection.Connect(SqlConnection::DefaultConnectionString()));
            CHECK(events.Sequence() == "oco"); // the first session closes before the second opens
        }
        CHECK(events.Sequence() == "ococ"); // and the second closes once, with the connection

        // Both are reported closed at the same point, once the session has ended (after the disconnect):
        // a logger sees one contract, whether Close() or a reconnect ended the session.
        REQUIRE(events.ClosedServerNames().size() == 2);
        CHECK(events.ClosedServerNames()[0] == events.ClosedServerNames()[1]);
        CHECK(events.ClosedServerNames()[1].empty());
    }

    SECTION("Connect, then a connect that fails")
    {
        {
            auto connection = SqlConnection {};
            REQUIRE(connection.IsAlive());
            // Connect() ends the current session before it attempts the new one, so when the new one
            // fails the connection is left without a session: the first one did end, and is reported so.
            CHECK_FALSE(connection.Connect(SqlConnectionString { "DRIVER={Lightweight No Such Driver}" }));
            CHECK(connection.SessionId() == 0);
            CHECK(events.Sequence() == "oc");
            auto const errorsBefore = events.Errors();
            connection.Close();
            CHECK(events.Errors() == errorsBefore);
        }
        CHECK(events.Sequence() == "oc"); // nothing more from Close() or the destructor
    }

    SECTION("A reconnect the driver refuses ends no session")
    {
        {
            auto connection = SqlConnection {};
            REQUIRE(connection.IsAlive());
            auto stmt = SqlStatement { connection };
            stmt.Prepare("SELECT 42");
            auto const sessionBefore = connection.SessionId();
            if (connection.Connect(SqlConnection::DefaultConnectionString()))
                CHECK(events.Sequence() == "oco");
            else
            {
                // The SQLite ODBC driver refuses to disconnect while a statement is allocated: the
                // session goes on, so nothing is reported closed.
                CHECK(connection.SessionId() == sessionBefore);
                CHECK(events.Sequence() == "o");
            }
        }
        CHECK(events.Opened() == events.Closed());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlTransaction: a commit refused as busy can be retried", "[SqlTransaction][SQLite]")
{
    if (SqlConnection {}.ServerType() != SqlServerType::SQLITE)
        SKIP("SQLite-only: a reader's SHARED lock refuses a writer's COMMIT in rollback-journal mode");

    // A private file in rollback-journal mode, waiting for no lock: a reader holding SHARED makes the
    // writer's COMMIT fail at once with "database is locked".
    auto const path = ScratchSqliteDatabasePath("busy-commit");
    RemoveSqliteDatabaseFiles(path);
    auto const cleanup = detail::Finally([&] { RemoveSqliteDatabaseFiles(path); });
    REQUIRE(EnsureSqliteDatabaseFileExists(WithSqliteDatabaseFile(path)));
    auto const impatient = SqliteConnectionSettings { .busyTimeout = {}, .journalMode = SqliteJournalMode::Delete };
    auto writer = SqlConnection { WithSqliteDatabaseFile(path), impatient };
    auto reader = SqlConnection { WithSqliteDatabaseFile(path), impatient };
    (void) SqlStatement { writer }.ExecuteDirect("CREATE TABLE busy_commit (id INTEGER PRIMARY KEY)");
    auto readerStmt = SqlStatement { reader };

    {
        auto readerTransaction = SqlTransaction { reader, SqlTransactionMode::ROLLBACK };
        (void) readerStmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM busy_commit"); // holds SHARED

        auto writerTransaction = SqlTransaction { writer, SqlTransactionMode::ROLLBACK };
        (void) SqlStatement { writer }.ExecuteDirect("INSERT INTO busy_commit (id) VALUES (1)");
        CHECK_FALSE(writerTransaction.TryCommit());
        CHECK(writerTransaction.LastError().value_or(SqlErrorInfo {}).message.contains("locked"));

        // Once the reader lets go, the same commit goes through.
        readerTransaction.Rollback();
        CHECK(writerTransaction.TryCommit());
    }
    CHECK(readerStmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM busy_commit").value_or(0) == 1);
}
