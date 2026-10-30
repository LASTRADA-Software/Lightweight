// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>

using namespace Lightweight;
using Lightweight::SqlBackup::Tests::LambdaProgressManager;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;

namespace
{

std::filesystem::path const BoolBackupFile = "bool_column_backup.zip";

// Returns the connection string the backup reads through. On PostgreSQL it forces psqlODBC's
// driver default `BoolsAsChar=1` (overriding a `BoolsAsChar=0` the test environment may carry),
// under which BOOLEAN columns are described as SQL_VARCHAR instead of SQL_BIT. Every other
// backend reports BOOLEAN/BIT as SQL_BIT and uses the default connection string unchanged.
SqlConnectionString BackupConnectionString(SqlServerType serverType)
{
    auto connectionString = SqlConnection::DefaultConnectionString();
    if (serverType == SqlServerType::POSTGRESQL)
        connectionString.value += ";BoolsAsChar=1";
    return connectionString;
}

// The concise SQL type the driver reports for column 1 of @p query's result set.
SQLLEN FetchFirstColumnConciseType(SqlConnectionString const& connectionString, std::string_view query)
{
    SqlConnection conn;
    REQUIRE(conn.Connect(connectionString));
    SqlStatement stmt { conn };
    auto cursor = stmt.ExecuteDirect(query);
    auto value = SQLLEN {};
    REQUIRE(SQL_SUCCEEDED(SQLColAttributeW(stmt.NativeHandle(), 1, SQL_DESC_CONCISE_TYPE, nullptr, 0, nullptr, &value)));
    return value;
}

} // namespace

// A Bool column must survive a backup/restore whether the driver describes it as SQL_BIT or as
// character data. The table (integer primary key + Bool) takes the array-fetch (RowArrayCursor)
// backup path, which used to read every Bool cell as an int64 and so failed the whole backup with
// "GetI64 called on a mismatched column binding" when psqlODBC ran with its default BoolsAsChar=1.
TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: Bool column round-trips when the driver reports it as text",
                 "[SqlBackup][Bool]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const backupFileCleaner = ScopedFileRemoved { BoolBackupFile };

    auto serverType = SqlServerType::UNKNOWN;
    {
        SqlConnection conn;
        REQUIRE(conn.Connect(SqlConnection::DefaultConnectionString()));
        serverType = conn.ServerType();
        SqlStatement stmt { conn };
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("bool_rt");
            migration.CreateTable("bool_rt").PrimaryKey("id", Integer {}).Column("flag", Bool {});
        });
        stmt.Prepare(R"(INSERT INTO "bool_rt" ("id", "flag") VALUES (?, ?))");
        (void) stmt.Execute(1, true);
        (void) stmt.Execute(2, false);
        (void) stmt.Execute(3, std::optional<bool> {});
    }

    auto const backupConnectionString = BackupConnectionString(serverType);

    // Pin the precondition, so a driver that stops honouring BoolsAsChar cannot turn this test vacuous.
    if (serverType == SqlServerType::POSTGRESQL)
        CHECK(FetchFirstColumnConciseType(backupConnectionString, R"(SELECT "flag" FROM "bool_rt")") != SQL_BIT);

    LambdaProgressManager pm { [](SqlBackup::Progress const& p) {
        if (p.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Backup/Restore error: " << p.message);
    } };

    REQUIRE_NOTHROW(SqlBackup::Backup(BoolBackupFile, backupConnectionString, 1, pm));
    REQUIRE(std::filesystem::exists(BoolBackupFile));

    {
        SqlConnection conn;
        REQUIRE(conn.Connect(SqlConnection::DefaultConnectionString()));
        SqlStatement stmt { conn };
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTable("bool_rt"); });
    }

    REQUIRE_NOTHROW(SqlBackup::Restore(BoolBackupFile, SqlConnection::DefaultConnectionString(), 1, pm));

    {
        SqlConnection conn;
        REQUIRE(conn.Connect(SqlConnection::DefaultConnectionString()));
        SqlStatement stmt { conn };
        CHECK(stmt.ExecuteDirectScalar<int64_t>(R"(SELECT COUNT(*) FROM "bool_rt")") == 3);
        CHECK(stmt.ExecuteDirectScalar<bool>(R"(SELECT "flag" FROM "bool_rt" WHERE "id" = 1)") == true);
        CHECK(stmt.ExecuteDirectScalar<bool>(R"(SELECT "flag" FROM "bool_rt" WHERE "id" = 2)") == false);
        CHECK_FALSE(stmt.ExecuteDirectScalar<bool>(R"(SELECT "flag" FROM "bool_rt" WHERE "id" = 3)").has_value());
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTableIfExists("bool_rt"); });
    }
}
