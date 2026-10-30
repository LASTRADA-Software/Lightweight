// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlQueryFormatter.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

using namespace Lightweight;
using Lightweight::SqlBackup::Tests::LambdaProgressManager;
using Lightweight::SqlBackup::Tests::RewriteArchiveMetadata;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;

namespace
{

constexpr std::string_view TableName = "restore_char";

/// Whether the test database compares CHAR(n) values with trailing pad semantics.
bool TargetPadsFixedChar()
{
    auto const connection = SqlConnection {};
    auto const* const formatter = SqlQueryFormatter::Get(connection.ServerType());
    return formatter != nullptr && formatter->ComparesFixedCharWithPadSemantics();
}

/// Creates `restore_char(ID, Code CHAR(5))` with the row `(1, @p code)`.
void CreateTable(std::string_view code)
{
    using namespace SqlColumnTypeDefinitions;
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable(TableName).PrimaryKey("ID", Integer {}).Column("Code", Char { .size = 5 });
    });
    (void) stmt.ExecuteDirect(std::format(R"(INSERT INTO "{}" ("ID", "Code") VALUES (1, '{}'))", TableName, code));
}

/// Backs the table up, rewrites the archive to come from @p serverName (declaring `Code` a CHAR(5),
/// as the drivers do not agree on how they report it), drops the table and restores the rewritten archive.
void RestoreAsServer(std::string_view serverName)
{
    std::filesystem::path const archive = "restore_char_backup.zip";
    std::filesystem::path const rewritten = "restore_char_rewritten.zip";
    auto const archiveCleaner = ScopedFileRemoved { archive };
    auto const rewrittenCleaner = ScopedFileRemoved { rewritten };

    LambdaProgressManager progress { [](SqlBackup::Progress const& update) {
        if (update.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Backup/Restore error: " << update.message);
    } };
    REQUIRE_NOTHROW(SqlBackup::Backup(archive, SqlConnection::DefaultConnectionString(), 1, progress));

    bool declared = false;
    RewriteArchiveMetadata(archive, rewritten, [&](std::string const& text) {
        auto metadata = nlohmann::json::parse(text);
        metadata["server"]["name"] = serverName;
        for (auto& table: metadata["schema"])
            if (table["name"].get<std::string>() == TableName)
                for (auto& column: table["columns"])
                    if (column["name"].get<std::string>() == "Code")
                    {
                        column["type"] = "char";
                        column["size"] = 5;
                        declared = true;
                    }
        return metadata.dump();
    });
    REQUIRE(declared);

    SqlStatement {}.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTable(TableName); });
    REQUIRE_NOTHROW(SqlBackup::Restore(rewritten, SqlConnection::DefaultConnectionString(), 1, progress));
}

/// Returns the ID of the row whose `Code` equals 'q', if any.
std::optional<int> IdOfCodeQ()
{
    auto stmt = SqlStatement {};
    auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT "ID" FROM "{}" WHERE "Code" = 'q')", TableName));
    if (!cursor.FetchRow())
        return std::nullopt;
    return cursor.GetColumn<int>(1);
}

/// Returns the stored `Code` of row 1.
std::optional<std::string> StoredCode()
{
    return SqlStatement {}.ExecuteDirectScalar<std::string>(
        std::format(R"(SELECT "Code" FROM "{}" WHERE "ID" = 1)", TableName));
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: CHAR padding from a padding source is trimmed on a non-padding target",
                 "[SqlBackup]")
{
    CreateTable("q    ");
    RestoreAsServer("Microsoft SQL Server");

    // Padding targets match 'q' against 'q    ' natively; non-padding ones only because the pad was trimmed.
    CHECK(IdOfCodeQ() == std::optional<int> { 1 });
    if (!TargetPadsFixedChar())
        CHECK(StoredCode() == std::optional<std::string> { "q" });
}

TEST_CASE_METHOD(SqlTestFixture, "SqlBackup: same-DBMS restore keeps CHAR trailing spaces", "[SqlBackup]")
{
    if (TargetPadsFixedChar())
        SKIP("the target pads CHAR(n) values itself");

    CreateTable("q ");
    std::filesystem::path const archive = "restore_char_same_backup.zip";
    auto const archiveCleaner = ScopedFileRemoved { archive };
    LambdaProgressManager progress { [](SqlBackup::Progress const& update) {
        if (update.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Backup/Restore error: " << update.message);
    } };
    REQUIRE_NOTHROW(SqlBackup::Backup(archive, SqlConnection::DefaultConnectionString(), 1, progress));
    SqlStatement {}.MigrateDirect([](SqlMigrationQueryBuilder& migration) { migration.DropTable(TableName); });
    REQUIRE_NOTHROW(SqlBackup::Restore(archive, SqlConnection::DefaultConnectionString(), 1, progress));

    CHECK(StoredCode() == std::optional<std::string> { "q " });
}

TEST_CASE_METHOD(SqlTestFixture, "SqlBackup: unknown archive server name disables CHAR trimming", "[SqlBackup]")
{
    if (TargetPadsFixedChar())
        SKIP("the target pads CHAR(n) values itself");

    CreateTable("q ");
    RestoreAsServer("SomethingElse");

    CHECK(StoredCode() == std::optional<std::string> { "q " });
}
