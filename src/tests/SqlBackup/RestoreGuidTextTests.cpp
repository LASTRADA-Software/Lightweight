// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/DataBinder/SqlGuid.hpp>
#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlQueryFormatter.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>

using namespace Lightweight;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;

namespace
{

/// Backs up the database, drops @p tableName, and restores the archive.
void RoundTrip(std::filesystem::path const& archive, std::string const& tableName)
{
    SqlBackup::NullProgressManager progress;
    REQUIRE_NOTHROW(SqlBackup::Backup(archive, SqlConnection::DefaultConnectionString(), 1, progress));
    SqlStatement {}.MigrateDirect([&](SqlMigrationQueryBuilder& migration) { migration.DropTable(tableName); });
    REQUIRE_NOTHROW(SqlBackup::Restore(archive, SqlConnection::DefaultConnectionString(), 1, progress));
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "SqlBackup: GUID column restores as the text SqlDataBinder writes", "[SqlBackup][SqlGuid]")
{
    using namespace SqlColumnTypeDefinitions;

    std::filesystem::path const archive = "restore_guid_text_backup.zip";
    auto const archiveCleaner = ScopedFileRemoved { archive };

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("restore_guid").PrimaryKey("ID", Integer {}).Column("G", Guid {});
    });
    auto const value = SqlGuid::UnsafeParse("6F9619FF-8B86-D011-B42D-00C04FC964FF");
    stmt.Prepare(R"(INSERT INTO "restore_guid" ("ID", "G") VALUES (1, ?))");
    (void) stmt.Execute(value);
    (void) stmt.ExecuteDirect(R"(INSERT INTO "restore_guid" ("ID", "G") VALUES (2, NULL))");

    RoundTrip(archive, "restore_guid");

    {
        auto cursor = stmt.ExecuteDirect(R"(SELECT "G" FROM "restore_guid" ORDER BY "ID")");
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetNullableColumn<SqlGuid>(1) == std::optional<SqlGuid> { value });
        REQUIRE(cursor.FetchRow());
        CHECK(!cursor.GetNullableColumn<SqlGuid>(1).has_value());
    }

    auto const* const formatter = SqlQueryFormatter::Get(stmt.Connection().ServerType());
    if (formatter == nullptr || !formatter->HasNativeGuidType())
        CHECK(stmt.ExecuteDirectScalar<std::string>(R"(SELECT "G" FROM "restore_guid" WHERE "ID" = 1)")
              == std::optional<std::string> { to_string(value) });
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: SQLite GUID text survives a SQLite round trip unchanged",
                 "[SqlBackup][SqlGuid]")
{
    using namespace SqlColumnTypeDefinitions;

    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::MICROSOFT_SQL);
    UNSUPPORTED_DATABASE(stmt, SqlServerType::POSTGRESQL);

    std::filesystem::path const archive = "restore_guid_sqlite_backup.zip";
    auto const archiveCleaner = ScopedFileRemoved { archive };

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("restore_guid_raw").PrimaryKey("ID", Integer {}).Column("G", Guid {});
    });
    (void) stmt.ExecuteDirect(
        R"(INSERT INTO "restore_guid_raw" ("ID", "G") VALUES (1, '{6f9619ff-8b86-d011-b42d-00c04fc964ff}'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "restore_guid_raw" ("ID", "G") VALUES (2, 'not-a-guid'))");

    RoundTrip(archive, "restore_guid_raw");

    CHECK(stmt.ExecuteDirectScalar<std::string>(R"(SELECT "G" FROM "restore_guid_raw" WHERE "ID" = 1)")
          == std::optional<std::string> { "{6f9619ff-8b86-d011-b42d-00c04fc964ff}" });
    CHECK(stmt.ExecuteDirectScalar<std::string>(R"(SELECT "G" FROM "restore_guid_raw" WHERE "ID" = 2)")
          == std::optional<std::string> { "not-a-guid" });
}
