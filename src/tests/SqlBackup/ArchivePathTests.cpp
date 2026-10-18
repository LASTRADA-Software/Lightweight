// SPDX-License-Identifier: Apache-2.0

#include "../UniqueTempPath.hpp"
#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Zip/ZipArchive.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace Lightweight;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;

TEST_CASE_METHOD(SqlTestFixture, "Backup writes and reads an archive whose path is not ASCII", "[SqlBackup][ArchivePath]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const directory = UniqueTempPath("archive-path");
    auto const directoryCleanup = ScopedFileRemoved { directory };
    std::filesystem::create_directories(directory);

    {
        auto stmt = SqlStatement {};
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("ArchivePathSubject");
            migration.CreateTable("ArchivePathSubject").PrimaryKey("id", Integer {}).Column("v", Varchar { 32 });
        });
        stmt.Prepare(R"(INSERT INTO "ArchivePathSubject" ("id", "v") VALUES (?, ?))");
        (void) stmt.Execute(1, std::string { "alpha" });
    }

    // U+00FC is in Windows-1252; U+4E2D is not, so path.string() would throw for it.
    for (auto const& fileName: { std::u8string { u8"Sicherung-Müller.zip" }, std::u8string { u8"中-Müller.zip" } })
    {
        auto const path = directory / std::filesystem::path { fileName };
        auto const archiveCleanup = ScopedFileRemoved { path };
        auto progress = SqlBackup::Tests::LambdaProgressManager { [](SqlBackup::Progress const& p) {
            if (p.state == SqlBackup::Progress::State::Error)
                FAIL_CHECK("Backup error: " << p.message);
        } };
        SqlBackup::Backup(path,
                          SqlConnection::DefaultConnectionString(),
                          1,
                          progress,
                          /*schema=*/ {},
                          /*tableFilter=*/"ArchivePathSubject");

        REQUIRE(std::filesystem::exists(path));
        CHECK(SqlBackup::ReadArchiveSchemas(path).has_value());
        CHECK(Zip::ZipArchive::Open(path).has_value());
        auto const verification = SqlBackup::VerifyArchive(path);
        INFO(verification.error_or("ok"));
        CHECK(verification.has_value());
    }
}
