// SPDX-License-Identifier: Apache-2.0

#include "../UniqueTempPath.hpp"
#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlBackup/Sha256.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Zip/ZipArchive.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

using namespace Lightweight;
using Lightweight::SqlBackup::Sha256;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;

namespace
{

using ArchiveEntries = std::vector<std::pair<std::string, std::string>>;

/// Writes an archive with the given entries.
void WriteArchive(std::filesystem::path const& path, ArchiveEntries const& entries)
{
    auto archive = Zip::ZipArchive::Create(path);
    REQUIRE(archive.has_value());
    for (auto const& [name, content]: entries)
        REQUIRE(archive->AddString(name, content).has_value());
    REQUIRE(archive->Close().has_value());
}

/// checksums.json listing @p files with their SHA-256.
std::string ChecksumsJson(ArchiveEntries const& files)
{
    auto json = nlohmann::json { { "algorithm", "sha256" }, { "files", nlohmann::json::object() } };
    for (auto const& [name, content]: files)
        json["files"][name] = Sha256::Hash(content);
    return json.dump();
}

std::string ReadFileBytes(std::filesystem::path const& path)
{
    auto file = std::ifstream { path, std::ios::binary };
    return std::string { std::istreambuf_iterator<char> { file }, std::istreambuf_iterator<char> {} };
}

} // namespace

TEST_CASE("VerifyArchive accepts an archive whose entries match checksums.json", "[SqlBackup][VerifyArchive]")
{
    auto const path = UniqueTempPath("verify-archive-ok", ".zip");
    auto const cleanup = ScopedFileRemoved { path };
    auto const data = ArchiveEntries { { "data/t/0", "abc" } };
    WriteArchive(path, { { "metadata.json", "{}" }, data[0], { "checksums.json", ChecksumsJson(data) } });

    auto const result = SqlBackup::VerifyArchive(path);

    INFO(result.error_or("ok"));
    REQUIRE(result.has_value());
    CHECK(result->verifiedEntries == 1);
    CHECK(result->archiveSha256 == Sha256::Hash(ReadFileBytes(path)));
}

TEST_CASE("VerifyArchive names an entry whose content does not match", "[SqlBackup][VerifyArchive]")
{
    auto const path = UniqueTempPath("verify-archive-mismatch", ".zip");
    auto const cleanup = ScopedFileRemoved { path };
    WriteArchive(path,
                 { { "metadata.json", "{}" },
                   { "data/t/0", "abc" },
                   { "checksums.json", ChecksumsJson({ { "data/t/0", "abd" } }) } });

    auto const result = SqlBackup::VerifyArchive(path);

    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error(), Catch::Matchers::ContainsSubstring("data/t/0"));
}

TEST_CASE("VerifyArchive refuses a data entry that checksums.json does not list", "[SqlBackup][VerifyArchive]")
{
    auto const path = UniqueTempPath("verify-archive-unlisted", ".zip");
    auto const cleanup = ScopedFileRemoved { path };
    WriteArchive(path,
                 { { "metadata.json", "{}" },
                   { "data/t/0", "abc" },
                   { "data/t/1", "def" },
                   { "checksums.json", ChecksumsJson({ { "data/t/0", "abc" } }) } });

    auto const result = SqlBackup::VerifyArchive(path);

    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error(), Catch::Matchers::ContainsSubstring("data/t/1"));
}

TEST_CASE("VerifyArchive refuses data without checksums.json, and an archive without metadata.json",
          "[SqlBackup][VerifyArchive]")
{
    auto const withoutChecksums = UniqueTempPath("verify-archive-no-checksums", ".zip");
    auto const withoutMetadata = UniqueTempPath("verify-archive-no-metadata", ".zip");
    auto const cleanupChecksums = ScopedFileRemoved { withoutChecksums };
    auto const cleanupMetadata = ScopedFileRemoved { withoutMetadata };
    WriteArchive(withoutChecksums, { { "metadata.json", "{}" }, { "data/t/0", "abc" } });
    auto const data = ArchiveEntries { { "data/t/0", "abc" } };
    WriteArchive(withoutMetadata, { data[0], { "checksums.json", ChecksumsJson(data) } });

    auto const first = SqlBackup::VerifyArchive(withoutChecksums);
    REQUIRE_FALSE(first.has_value());
    CHECK_THAT(first.error(), Catch::Matchers::ContainsSubstring("checksums.json"));

    auto const second = SqlBackup::VerifyArchive(withoutMetadata);
    REQUIRE_FALSE(second.has_value());
    CHECK_THAT(second.error(), Catch::Matchers::ContainsSubstring("metadata.json"));
}

TEST_CASE("VerifyArchive names an entry that checksums.json lists but the archive lacks", "[SqlBackup][VerifyArchive]")
{
    auto const path = UniqueTempPath("verify-archive-missing-entry", ".zip");
    auto const cleanup = ScopedFileRemoved { path };
    WriteArchive(path,
                 { { "metadata.json", "{}" },
                   { "data/t/0", "abc" },
                   { "checksums.json", ChecksumsJson({ { "data/t/0", "abc" }, { "data/t/1", "def" } }) } });

    auto const result = SqlBackup::VerifyArchive(path);

    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error(), Catch::Matchers::ContainsSubstring("data/t/1"));
}

TEST_CASE("VerifyArchive reports a malformed checksums.json", "[SqlBackup][VerifyArchive]")
{
    auto const path = UniqueTempPath("verify-archive-malformed", ".zip");
    auto const cleanup = ScopedFileRemoved { path };
    WriteArchive(path, { { "metadata.json", "{}" }, { "data/t/0", "abc" }, { "checksums.json", "{ not json" } });

    auto const result = SqlBackup::VerifyArchive(path);

    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error(), Catch::Matchers::ContainsSubstring("checksums.json"));
}

TEST_CASE("VerifyArchive accepts a schema-only archive with no data entries", "[SqlBackup][VerifyArchive]")
{
    auto const path = UniqueTempPath("verify-archive-schema-only", ".zip");
    auto const cleanup = ScopedFileRemoved { path };
    WriteArchive(path, { { "metadata.json", "{}" } });

    auto const result = SqlBackup::VerifyArchive(path);

    INFO(result.error_or("ok"));
    REQUIRE(result.has_value());
    CHECK(result->verifiedEntries == 0);
    CHECK(result->archiveSha256 == Sha256::Hash(ReadFileBytes(path)));
}

TEST_CASE("VerifyArchive reports a file that is not an archive", "[SqlBackup][VerifyArchive]")
{
    auto const path = UniqueTempPath("verify-archive-garbage", ".zip");
    auto const cleanup = ScopedFileRemoved { path };
    {
        auto file = std::ofstream { path, std::ios::binary };
        file << "this is not a zip archive";
    }

    CHECK_FALSE(SqlBackup::VerifyArchive(path).has_value());
    CHECK_FALSE(SqlBackup::VerifyArchive(UniqueTempPath("verify-archive-missing", ".zip")).has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "VerifyArchive accepts what Backup writes", "[SqlBackup][VerifyArchive]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const path = UniqueTempPath("verify-archive-backup", ".zip");
    auto const cleanup = ScopedFileRemoved { path };
    {
        auto stmt = SqlStatement {};
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("VerifyArchiveSubject");
            migration.CreateTable("VerifyArchiveSubject").PrimaryKey("id", Integer {}).Column("v", Varchar { 32 });
        });
        stmt.Prepare(R"(INSERT INTO "VerifyArchiveSubject" ("id", "v") VALUES (?, ?))");
        (void) stmt.Execute(1, std::string { "alpha" });
        (void) stmt.Execute(2, std::string { "beta" });
        (void) stmt.Execute(3, std::string { "gamma" });
    }

    auto progress = SqlBackup::Tests::LambdaProgressManager { [](SqlBackup::Progress const& p) {
        if (p.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Backup error: " << p.message);
    } };
    SqlBackup::Backup(
        path, SqlConnection::DefaultConnectionString(), 1, progress, /*schema=*/ {}, /*tableFilter=*/"VerifyArchiveSubject");
    REQUIRE(std::filesystem::exists(path));

    auto const result = SqlBackup::VerifyArchive(path);

    INFO(result.error_or("ok"));
    REQUIRE(result.has_value());
    CHECK(result->verifiedEntries >= 1);
    CHECK(result->archiveSha256 == Sha256::Hash(ReadFileBytes(path)));
}
