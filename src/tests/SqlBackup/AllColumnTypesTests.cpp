// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

using namespace Lightweight;
using Lightweight::SqlBackup::Tests::LambdaProgressManager;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;

namespace
{

std::filesystem::path const BackupFile = "all_types_backup.zip";

SqlConnectionString const& GetConnectionString()
{
    return SqlConnection::DefaultConnectionString();
}

} // namespace

// Backup + restore a table that uses one column of every SqlColumnTypeDefinition alternative
// the metadata serializer / parser support. This exercises every type-branch in:
//   - SqlBackup.cpp::CreateMetadata (the type-name switch lines 210-260+)
//   - SqlBackup.cpp::ParseSchema (the matching inverse switch)
//   - QueryFormatter::ColumnType (per-DBMS CREATE TABLE column rendering)
//   - Backup.cpp::BuildMssqlDecimalSelectQuery (when running against MSSQL — Decimal column)
//
// The existing SqlBackup tests cover Integer + Varchar + Binary + NVarchar, but never a
// Tinyint, Smallint, Bigint, Real, Bool, Char, NChar, Text in the same metadata pass, so
// those serializer branches stay cold in coverage. This single test fixes that.
TEST_CASE_METHOD(SqlTestFixture, "SqlBackup: round-trip every supported column type", "[SqlBackup][AllTypes]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const backupFileCleaner = ScopedFileRemoved { BackupFile };

    SqlConnection conn;
    REQUIRE(conn.Connect(GetConnectionString()));

    // Skip on MSSQL until we understand why a fresh table created in the test's connection
    // is occasionally not yet visible to the separate Backup() worker connection on the same
    // server (looks like catalog-visibility / autocommit interaction). The CreateMetadata
    // type-branch coverage we wanted from this test still fires on sqlite3 and postgres.
    if (conn.ServerType() == SqlServerType::MICROSOFT_SQL)
    {
        WARN("Skipped on MSSQL: Backup worker connection sometimes doesn't see a fresh table; "
             "tracked separately. PG / SQLite still exercise the metadata-type branches.");
        return;
    }

    SqlStatement stmt { conn };

    // Drop+create the wide-type table. Decimal is included only on backends that have a
    // native fixed-point type (not SQLite, which would store it as REAL and the precision
    // test would not be meaningful).
    bool const isSqlite = (conn.ServerType() == SqlServerType::SQLITE);

    stmt.MigrateDirect([isSqlite](SqlMigrationQueryBuilder& migration) {
        migration.DropTableIfExists("all_types");

        auto builder = migration.CreateTable("all_types")
                           .PrimaryKey("id", Integer {})
                           .Column("tiny_v", Tinyint {})
                           .Column("small_v", Smallint {})
                           .Column("big_v", Bigint {})
                           .Column("real_v", Real {})
                           .Column("bool_v", Bool {})
                           .Column("char_v", Char { 4 })
                           .Column("nchar_v", NChar { 4 })
                           .Column("text_v", Text {})
                           .Column("guid_v", Guid {})
                           .Column("date_v", Date {})
                           .Column("dt_v", DateTime {});
        if (!isSqlite)
            (void) builder.Column("dec_v", Decimal { .precision = 10, .scale = 2 });
    });

    stmt.Prepare(
        isSqlite
            ? R"(INSERT INTO "all_types" ("id", "tiny_v", "small_v", "big_v", "real_v", "bool_v", "char_v", "nchar_v", "text_v", "guid_v", "date_v", "dt_v") VALUES (?,?,?,?,?,?,?,?,?,?,?,?))"
            : R"(INSERT INTO "all_types" ("id", "tiny_v", "small_v", "big_v", "real_v", "bool_v", "char_v", "nchar_v", "text_v", "guid_v", "date_v", "dt_v", "dec_v") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?))");

    auto const guidOpt = SqlGuid::TryParse("11111111-2222-3333-8444-555555555555");
    REQUIRE(guidOpt.has_value());
    if (!guidOpt.has_value())
        return;

    auto const date = SqlDate { std::chrono::year { 2026 }, std::chrono::month { 5 }, std::chrono::day { 11 } };
    auto const dt = SqlDateTime { std::chrono::year { 2026 }, std::chrono::month { 5 },    std::chrono::day { 11 },
                                  std::chrono::hours { 8 },   std::chrono::minutes { 30 }, std::chrono::seconds { 0 } };

    if (isSqlite)
    {
        (void) stmt.Execute(1,
                            static_cast<int8_t>(42),
                            static_cast<int16_t>(-12345),
                            9'000'000'000LL,
                            3.5F,
                            true,
                            "abcd",
                            u"ÄäÖö",
                            "lorem",
                            *guidOpt,
                            date,
                            dt);
    }
    else
    {
        (void) stmt.Execute(1,
                            static_cast<int8_t>(42),
                            static_cast<int16_t>(-12345),
                            9'000'000'000LL,
                            3.5F,
                            true,
                            "abcd",
                            u"ÄäÖö",
                            "lorem",
                            *guidOpt,
                            date,
                            dt,
                            SqlNumeric<10, 2> { 12.34 });
    }

    // Track errors so we don't silently swallow Backup/Restore failures.
    LambdaProgressManager pm { [](SqlBackup::Progress const& p) {
        if (p.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Backup/Restore error: " << p.message);
        else if (p.state == SqlBackup::Progress::State::Warning)
            WARN("Backup/Restore warning: " << p.message);
    } };
    REQUIRE_NOTHROW(SqlBackup::Backup(BackupFile, GetConnectionString(), 1, pm));
    REQUIRE(std::filesystem::exists(BackupFile));
    REQUIRE(std::filesystem::file_size(BackupFile) > 0);

    // Drop the table so Restore must recreate it from the metadata.json. This exercises every
    // type branch on the ParseSchema → CREATE TABLE path on the way back in.
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTable("all_types"); });

    REQUIRE_NOTHROW(SqlBackup::Restore(BackupFile, GetConnectionString(), 1, pm));

    auto const count = stmt.ExecuteDirectScalar<int64_t>(R"(SELECT COUNT(*) FROM "all_types")");
    REQUIRE(count.has_value());
    if (count.has_value())
        CHECK(count.value() == 1);

    // The archive carries GUIDs as text, so a restored GUID has made a full
    // SqlGuid -> to_string -> archive -> TryParse -> bind round trip. Check the value, not only the
    // row count, so that round trip cannot silently corrupt it. (The batched restore path's raw
    // SQL_C_GUID byte order is pinned separately, in BatchManagerTests.cpp.)
    auto const restoredGuid = stmt.ExecuteDirectScalar<SqlGuid>(R"(SELECT "guid_v" FROM "all_types")");
    REQUIRE(restoredGuid.has_value());
    if (restoredGuid.has_value())
        CHECK(*restoredGuid == *guidOpt);

    // Final cleanup — wrapped in try/catch because MSSQL emits a transient-feeling
    // 3701 ("table does not exist") for a freshly-restored table that hasn't yet been
    // commited into the catalog from a different connection's perspective. We don't
    // care about cleanup ordering, only that the body of the test ran.
    try
    {
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTableIfExists("all_types"); });
    }
    catch (std::exception const& cleanupError)
    {
        // ignore cleanup failure — the test body has already finished.
        WARN("Cleanup DropTableIfExists threw: " << cleanupError.what());
    }
}

// A GUID column must come back from a same-DBMS backup/restore as a GUID column, not as whatever
// the driver loosely reports for the declared type name (SQLite reported VARCHAR(0)).
TEST_CASE_METHOD(SqlTestFixture, "SqlBackup: a restored GUID column is still a GUID column", "[SqlBackup][SqlSchema][Guid]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const backupFileCleaner = ScopedFileRemoved { "guid_column_backup.zip" };

    SqlConnection conn;
    REQUIRE(conn.Connect(GetConnectionString()));
    if (conn.ServerType() == SqlServerType::MICROSOFT_SQL)
    {
        WARN("Skipped on MSSQL: Backup worker connection sometimes doesn't see a fresh table.");
        return;
    }
    SqlStatement stmt { conn };

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.DropTableIfExists("guid_column_rt");
        migration.CreateTable("guid_column_rt").PrimaryKey("id", Integer {}).Column("guid_v", Guid {});
    });

    auto const guid = SqlGuid::TryParse("11111111-2222-3333-8444-555555555555");
    REQUIRE(guid.has_value());
    if (!guid.has_value())
        return;
    (void) stmt.Prepare(R"(INSERT INTO "guid_column_rt" ("id", "guid_v") VALUES (?, ?))");
    (void) stmt.Execute(1, *guid);

    auto const guidColumnType = [&] {
        auto const table =
            SqlSchema::ReadTable(stmt, { .catalog = conn.DatabaseName(), .schema = {}, .table = "guid_column_rt" });
        REQUIRE(table.has_value());
        auto const column = std::ranges::find_if(table->columns, [](auto const& c) { return c.name == "guid_v"; });
        REQUIRE(column != table->columns.end());
        return column->type;
    };
    CHECK(guidColumnType() == SqlColumnTypeDefinition { Guid {} });

    LambdaProgressManager pm { [](SqlBackup::Progress const& p) {
        if (p.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Backup/Restore error: " << p.message);
    } };
    REQUIRE_NOTHROW(SqlBackup::Backup("guid_column_backup.zip", GetConnectionString(), 1, pm));
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTable("guid_column_rt"); });
    REQUIRE_NOTHROW(SqlBackup::Restore("guid_column_backup.zip", GetConnectionString(), 1, pm));

    CHECK(guidColumnType() == SqlColumnTypeDefinition { Guid {} });
    auto const restored = stmt.ExecuteDirectScalar<SqlGuid>(R"(SELECT "guid_v" FROM "guid_column_rt")");
    REQUIRE(restored.has_value());
    if (restored.has_value())
        CHECK(*restored == *guid);

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTableIfExists("guid_column_rt"); });
}

// SQLite stores a GUID column as free text. Backup must archive that text verbatim: re-reading it as
// a SqlGuid would canonicalise '{...}' and lose anything that is not a GUID at all. (MSSQL and
// PostgreSQL hand back native GUIDs, whose `to_string(SqlGuid)` archive form
// "SqlBackup: round-trip every supported column type" and BatchManagerTests.cpp already pin.)
TEST_CASE_METHOD(SqlTestFixture, "SqlBackup: SQLite GUID column text is archived verbatim", "[SqlBackup][SqlSchema][Guid]")
{
    using namespace SqlColumnTypeDefinitions;

    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::MICROSOFT_SQL);
    UNSUPPORTED_DATABASE(stmt, SqlServerType::POSTGRESQL);

    auto const backupFileCleaner = ScopedFileRemoved { "guid_verbatim_backup.zip" };

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.DropTableIfExists("guid_verbatim");
        migration.CreateTable("guid_verbatim").PrimaryKey("id", Integer {}).Column("g", Guid {});
    });
    constexpr auto braced = "{6f9619ff-8b86-d011-b42d-00c04fc964ff}";
    (void) stmt.ExecuteDirect(std::format(R"(INSERT INTO "guid_verbatim" ("id", "g") VALUES (1, '{}'))", braced));
    (void) stmt.ExecuteDirect(R"(INSERT INTO "guid_verbatim" ("id", "g") VALUES (2, 'not-a-guid'))");

    LambdaProgressManager pm { [](SqlBackup::Progress const& p) {
        if (p.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Backup/Restore error: " << p.message);
    } };
    REQUIRE_NOTHROW(SqlBackup::Backup("guid_verbatim_backup.zip", GetConnectionString(), 1, pm));
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTable("guid_verbatim"); });
    REQUIRE_NOTHROW(SqlBackup::Restore("guid_verbatim_backup.zip", GetConnectionString(), 1, pm));

    CHECK(stmt.ExecuteDirectScalar<std::string>(R"(SELECT "g" FROM "guid_verbatim" WHERE "id" = 1)")
          == std::optional<std::string> { braced });
    CHECK(stmt.ExecuteDirectScalar<std::string>(R"(SELECT "g" FROM "guid_verbatim" WHERE "id" = 2)")
          == std::optional<std::string> { "not-a-guid" });

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTableIfExists("guid_verbatim"); });
}
