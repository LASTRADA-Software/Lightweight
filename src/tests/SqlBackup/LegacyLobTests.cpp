// SPDX-License-Identifier: Apache-2.0

// SQL Server's legacy large-object types (TEXT, NTEXT, IMAGE) through the schema readers and a backup.
//
// A round trip through our own code cannot see a dropped legacyLob flag: the restored column would simply
// be VARCHAR(MAX) holding the same data. So these tests create real columns with literal DDL and ask the
// server's catalog (sys.columns, sys.types) what it holds afterwards, and hash the data on the server.

#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/DataBinder/SqlBinary.hpp>
#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Tools/CxxModelPrinter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

using namespace Lightweight;
using Lightweight::SqlBackup::Tests::DropTables;
using Lightweight::SqlBackup::Tests::RecordingProgressManager;
using Lightweight::SqlBackup::Tests::RewriteArchive;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;
using Lightweight::Tools::CxxModelPrinter;

namespace
{

std::filesystem::path const BackupFile = "legacy_lob_backup.zip";
std::filesystem::path const RewrittenBackupFile = "legacy_lob_rewritten.zip";

/// Returns "column:type" for each column of @p tableName, as the server's catalog reports them.
std::vector<std::string> CatalogTypes(SqlStatement& stmt, std::string_view tableName)
{
    auto result = std::vector<std::string> {};
    auto cursor = stmt.ExecuteDirect(std::format("SELECT c.name, ty.name FROM sys.columns c "
                                                 "INNER JOIN sys.types ty ON c.user_type_id = ty.user_type_id "
                                                 "WHERE c.object_id = OBJECT_ID('{}') ORDER BY c.column_id",
                                                 tableName));
    while (cursor.FetchRow())
    {
        auto column = cursor.GetColumn<std::string>(1);
        auto const type = cursor.GetColumn<std::string>(2);
        result.push_back(std::format("{}:{}", column, type));
    }
    return result;
}

/// Returns one line per row of @p tableName with the length and SHA-256 of `t`, `n` and `i`, computed by the
/// server. A NULL has no length and no hash, so it cannot be mistaken for an empty value.
std::vector<std::string> ServerDigests(SqlStatement& stmt, std::string_view tableName)
{
    auto result = std::vector<std::string> {};
    auto cursor = stmt.ExecuteDirect(
        std::format("SELECT CONCAT(id, '|', DATALENGTH(t), ':', CONVERT(varchar(64), HASHBYTES('SHA2_256', CAST(t AS "
                    "varchar(max))), 2), "
                    "'|', DATALENGTH(n), ':', CONVERT(varchar(64), HASHBYTES('SHA2_256', CAST(n AS nvarchar(max))), 2), "
                    "'|', DATALENGTH(i), ':', CONVERT(varchar(64), HASHBYTES('SHA2_256', CAST(i AS varbinary(max))), 2)) "
                    "FROM {} ORDER BY id",
                    tableName));
    while (cursor.FetchRow())
        result.push_back(cursor.GetColumn<std::string>(1));
    return result;
}

/// Inserts into @p tableName the rows a legacy column has to survive: short, non-ASCII, NULL, empty, over the
/// 8000 byte limit of the sized types, and over the 64 KB the restore's batches are laid out for by default.
void InsertLegacyLobRows(SqlStatement& stmt, std::string_view tableName)
{
    auto const values = std::array<std::string_view, 6> {
        "1, 'short', N'short', 0x00FF10",
        "2, CHAR(228) + 'x' + CHAR(246), NCHAR(20013) + N'y', 0x41",
        "3, NULL, NULL, NULL",
        "4, '', N'', 0x",
        "5, REPLICATE(CAST('a' AS varchar(max)), 9000) + CHAR(228), REPLICATE(CAST(NCHAR(20013) AS nvarchar(max)), 9000), "
        "REPLICATE(CAST(0x00AB AS varbinary(max)), 4500)",
        "6, REPLICATE(CAST('b' AS varchar(max)), 100000), REPLICATE(CAST(N'c' AS nvarchar(max)), 70000), "
        "REPLICATE(CAST(0x01 AS varbinary(max)), 100000)",
    };
    for (auto const row: values)
        (void) stmt.ExecuteDirect(std::format("INSERT INTO {} (id, t, n, i) VALUES ({})", tableName, row));
}

/// Backs up the database, drops @p tableNames, and restores the archive at @ref BackupFile.
void BackupDropAndRestore(std::vector<std::string> const& tableNames)
{
    auto backupProgress = RecordingProgressManager {};
    SqlBackup::Backup(BackupFile, SqlConnection::DefaultConnectionString(), 1, backupProgress);
    REQUIRE(backupProgress.ErrorCount() == 0);

    DropTables(tableNames);

    auto restoreProgress = RecordingProgressManager {};
    (void) SqlBackup::Restore(BackupFile, SqlConnection::DefaultConnectionString(), 1, restoreProgress);
    REQUIRE(restoreProgress.ErrorCount() == 0);
}

/// Tells whether @p type carries the legacyLob flag.
bool IsLegacyLob(SqlColumnTypeDefinition const& type)
{
    return std::visit(
        [](auto const& alternative) {
            if constexpr (requires { alternative.legacyLob; })
                return alternative.legacyLob;
            else
                return false;
        },
        type);
}

/// Returns the column named @p columnName of @p table.
SqlSchema::Column const& ColumnOf(SqlSchema::Table const& table, std::string_view columnName)
{
    auto const column = std::ranges::find(table.columns, columnName, &SqlSchema::Column::name);
    REQUIRE(column != table.columns.end());
    return *column;
}

/// Reads @p tableName through the whole-database reader, which is the batched sys.* path on SQL Server.
SqlSchema::Table ReadWithAllTablesReader(SqlStatement& stmt, std::string_view tableName)
{
    auto const tables = SqlSchema::ReadAllTables(stmt, stmt.Connection().DatabaseName(), /*schema=*/"");
    auto const table = std::ranges::find(tables, tableName, &SqlSchema::Table::name);
    REQUIRE(table != tables.end());
    return *table;
}

/// Reads @p tableName through the single-table reader, which is the SQLColumns path on every database.
SqlSchema::Table ReadWithSingleTableReader(SqlStatement& stmt, std::string_view tableName)
{
    auto table = SqlSchema::ReadTable(
        stmt, SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = std::string(tableName) });
    REQUIRE(table.has_value());
    return table.has_value() ? std::move(*table) : SqlSchema::Table {};
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: TEXT, NTEXT and IMAGE keep their type and data through a backup and restore",
                 "[SqlBackup][LegacyLob]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("TEXT, NTEXT and IMAGE as distinct types exist on SQL Server only");

    auto const backupFileCleaner = ScopedFileRemoved { BackupFile };
    (void) stmt.ExecuteDirect("CREATE TABLE legacy_lob (id INT PRIMARY KEY, t TEXT NULL, n NTEXT NULL, i IMAGE NULL)");
    InsertLegacyLobRows(stmt, "legacy_lob");

    auto const expectedTypes = std::vector<std::string> { "id:int", "t:text", "n:ntext", "i:image" };
    REQUIRE(CatalogTypes(stmt, "legacy_lob") == expectedTypes);
    auto const digests = ServerDigests(stmt, "legacy_lob");
    REQUIRE(digests.size() == 6);

    BackupDropAndRestore({ "legacy_lob" });

    auto restored = SqlStatement {};
    CHECK(CatalogTypes(restored, "legacy_lob") == expectedTypes);
    CHECK(ServerDigests(restored, "legacy_lob") == digests);

    // NULL stayed NULL and an empty value stayed empty: DATALENGTH tells them apart on the server.
    CHECK(restored.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM legacy_lob WHERE id = 3 AND t IS NULL AND n IS NULL AND i "
                                            "IS NULL")
          == 1);
    CHECK(restored.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM legacy_lob WHERE id = 4 AND DATALENGTH(t) = 0 AND "
                                            "DATALENGTH(n) = 0 AND DATALENGTH(i) = 0")
          == 1);
    // Values longer than the 8000 bytes a sized column can hold came back whole.
    CHECK(restored.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM legacy_lob WHERE id = 5 AND DATALENGTH(t) = 9001 AND "
                                            "DATALENGTH(n) = 18000 AND DATALENGTH(i) = 9000")
          == 1);
    CHECK(restored.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM legacy_lob WHERE id = 6 AND DATALENGTH(t) = 100000 AND "
                                            "DATALENGTH(n) = 140000 AND DATALENGTH(i) = 100000")
          == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: a table without a primary key backs up when its first column cannot be sorted",
                 "[SqlBackup][LegacyLob]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("TEXT, NTEXT and IMAGE as distinct types exist on SQL Server only");

    auto const backupFileCleaner = ScopedFileRemoved { BackupFile };

    // The three ways to reach the fallback order: the query builder with a sortable column after the LOB,
    // the query builder with nothing sortable, and the separate query built for a DECIMAL column.
    (void) stmt.ExecuteDirect("CREATE TABLE lob_first (t TEXT NULL, n NTEXT NULL, i IMAGE NULL, id INT NULL)");
    (void) stmt.ExecuteDirect("CREATE TABLE lob_only (t TEXT NULL, i IMAGE NULL)");
    (void) stmt.ExecuteDirect("CREATE TABLE lob_decimal (t TEXT NULL, d DECIMAL(10, 2) NULL)");
    (void) stmt.ExecuteDirect("INSERT INTO lob_first (t, n, i, id) VALUES ('first', N'first', 0x01, 1)");
    (void) stmt.ExecuteDirect("INSERT INTO lob_only (t, i) VALUES ('only', 0x02)");
    (void) stmt.ExecuteDirect("INSERT INTO lob_decimal (t, d) VALUES ('decimal', 12.34)");

    BackupDropAndRestore({ "lob_first", "lob_only", "lob_decimal" });

    auto restored = SqlStatement {};
    CHECK(CatalogTypes(restored, "lob_first") == std::vector<std::string> { "t:text", "n:ntext", "i:image", "id:int" });
    CHECK(CatalogTypes(restored, "lob_only") == std::vector<std::string> { "t:text", "i:image" });
    CHECK(CatalogTypes(restored, "lob_decimal") == std::vector<std::string> { "t:text", "d:decimal" });
    CHECK(restored.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM lob_first WHERE t LIKE 'first' AND id = 1") == 1);
    CHECK(restored.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM lob_only WHERE t LIKE 'only' AND DATALENGTH(i) = 1") == 1);
    CHECK(restored.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM lob_decimal WHERE t LIKE 'decimal' AND d = 12.34") == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlSchema: both readers report TEXT, NTEXT and IMAGE as legacy", "[SqlSchema][LegacyLob]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("TEXT, NTEXT and IMAGE as distinct types exist on SQL Server only");

    // The fixture has dropped every table, so nothing can still be using the alias type.
    (void) stmt.ExecuteDirect("IF TYPE_ID('legacy_lob_alias') IS NOT NULL DROP TYPE legacy_lob_alias");
    (void) stmt.ExecuteDirect("CREATE TYPE legacy_lob_alias FROM text");
    (void) stmt.ExecuteDirect(
        "CREATE TABLE legacy_read (t TEXT, n NTEXT, i IMAGE, a legacy_lob_alias, vm VARCHAR(MAX), nm NVARCHAR(MAX), "
        "bm VARBINARY(MAX), v VARCHAR(50))");

    for (auto const& table: { ReadWithAllTablesReader(stmt, "legacy_read"), ReadWithSingleTableReader(stmt, "legacy_read") })
    {
        CHECK(IsLegacyLob(ColumnOf(table, "t").type));
        CHECK(IsLegacyLob(ColumnOf(table, "n").type));
        CHECK(IsLegacyLob(ColumnOf(table, "i").type));
        CHECK(IsLegacyLob(ColumnOf(table, "a").type));
        CHECK_FALSE(IsLegacyLob(ColumnOf(table, "vm").type));
        CHECK_FALSE(IsLegacyLob(ColumnOf(table, "nm").type));
        CHECK_FALSE(IsLegacyLob(ColumnOf(table, "bm").type));
        CHECK_FALSE(IsLegacyLob(ColumnOf(table, "v").type));
    }

    (void) stmt.ExecuteDirect("DROP TABLE legacy_read");
    (void) stmt.ExecuteDirect("DROP TYPE legacy_lob_alias");
}

TEST_CASE_METHOD(SqlTestFixture, "SqlSchema: only SQL Server has legacy LOB columns", "[SqlSchema][LegacyLob]")
{
    // PostgreSQL and SQLite report a TEXT column under the very name SQL Server's legacy type has, with the
    // same ODBC long type. On SQL Server, Text {} is VARCHAR(MAX), so the expectation holds on all three.
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("not_legacy").Column("body", SqlColumnTypeDefinitions::Text {});
    });

    CHECK_FALSE(IsLegacyLob(ColumnOf(ReadWithAllTablesReader(stmt, "not_legacy"), "body").type));
    CHECK_FALSE(IsLegacyLob(ColumnOf(ReadWithSingleTableReader(stmt, "not_legacy"), "body").type));
}

TEST_CASE_METHOD(SqlTestFixture,
                 "ddl2cpp: a legacy LOB column generates the same member as its modern type",
                 "[ddl2cpp][LegacyLob]")
{
    auto stmt = SqlStatement {};
    if (stmt.Connection().ServerType() != SqlServerType::MICROSOFT_SQL)
        SKIP("TEXT, NTEXT and IMAGE as distinct types exist on SQL Server only");

    (void) stmt.ExecuteDirect(
        "CREATE TABLE legacy_model (t TEXT, n NTEXT, i IMAGE, vm VARCHAR(MAX), nm NVARCHAR(MAX), bm VARBINARY(MAX))");
    auto const table = ReadWithAllTablesReader(stmt, "legacy_model");
    auto const generated = [&](std::string_view columnName) {
        return CxxModelPrinter::MakeType(
            ColumnOf(table, columnName), "legacy_model", /*forceUnicodeTextColumn=*/false, {}, SqlOptimalMaxColumnSize);
    };

    CHECK(generated("t") == "std::optional<Light::SqlMaxDynamicAnsiString>");
    CHECK(generated("n") == "std::optional<Light::SqlDynamicUtf16String<1073741823>>");
    CHECK(generated("i") == "std::optional<Light::SqlDynamicBinary<2147483647>>");
    CHECK(generated("t") == generated("vm"));
    CHECK(generated("n") == generated("nm"));
    CHECK(generated("i") == generated("bm"));
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: an archive that marks columns legacyLob restores on every database",
                 "[SqlBackup][LegacyLob]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const backupFileCleaner = ScopedFileRemoved { BackupFile };
    auto const rewrittenFileCleaner = ScopedFileRemoved { RewrittenBackupFile };

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("legacy_archive")
            .PrimaryKey("id", Integer {})
            .Column("t", Varchar { .size = 0 })
            .Column("n", NVarchar { .size = 0 })
            .Column("i", VarBinary { .size = 0 });
    });

    auto const text = std::string(9000, 'a');
    auto const wideText = std::u16string(9000, char16_t { 0x4E2D });
    auto binary = SqlBinary {};
    binary.resize(9000, 0xAB);
    stmt.Prepare(R"(INSERT INTO "legacy_archive" ("id", "t", "n", "i") VALUES (?, ?, ?, ?))");
    (void) stmt.Execute(1, text, wideText, binary);
    (void) stmt.Execute(2, SqlNullValue, SqlNullValue, SqlNullValue);

    auto backupProgress = RecordingProgressManager {};
    SqlBackup::Backup(BackupFile, SqlConnection::DefaultConnectionString(), 1, backupProgress);
    REQUIRE(backupProgress.ErrorCount() == 0);

    // Make the columns read as what a SQL Server backup writes for TEXT, NTEXT and IMAGE.
    RewriteArchive(BackupFile,
                   RewrittenBackupFile,
                   [](std::string const& name, std::string content) -> std::optional<std::pair<std::string, std::string>> {
                       if (name != "metadata.json")
                           return std::pair { name, std::move(content) };
                       auto metadata = nlohmann::json::parse(content);
                       for (auto& table: metadata["schema"])
                           for (auto& column: table["columns"])
                           {
                               auto const columnName = column["name"].get<std::string>();
                               if (columnName == "t")
                                   column.update({ { "type", "varchar" }, { "size", 2147483647 }, { "legacyLob", true } });
                               else if (columnName == "n")
                                   column.update({ { "type", "nvarchar" }, { "size", 1073741823 }, { "legacyLob", true } });
                               else if (columnName == "i")
                                   column.update({ { "type", "varbinary" }, { "size", 2147483647 }, { "legacyLob", true } });
                           }
                       return std::pair { name, metadata.dump() };
                   });

    DropTables({ "legacy_archive" });

    auto restoreProgress = RecordingProgressManager {};
    (void) SqlBackup::Restore(RewrittenBackupFile, SqlConnection::DefaultConnectionString(), 1, restoreProgress);
    REQUIRE(restoreProgress.ErrorCount() == 0);

    auto restored = SqlStatement {};
    if (restored.Connection().ServerType() == SqlServerType::MICROSOFT_SQL)
        CHECK(CatalogTypes(restored, "legacy_archive")
              == std::vector<std::string> { "id:int", "t:text", "n:ntext", "i:image" });

    auto cursor = restored.ExecuteDirect(R"(SELECT "t", "n", "i" FROM "legacy_archive" ORDER BY "id")");
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<std::string>(1) == text);
    CHECK(cursor.GetColumn<std::u16string>(2) == wideText);
    CHECK(cursor.GetColumn<SqlBinary>(3) == binary);
    REQUIRE(cursor.FetchRow());
    CHECK_FALSE(cursor.GetNullableColumn<std::string>(1).has_value());
    CHECK_FALSE(cursor.GetNullableColumn<std::u16string>(2).has_value());
    CHECK_FALSE(cursor.GetNullableColumn<SqlBinary>(3).has_value());
}
