// SPDX-License-Identifier: Apache-2.0

#include "../../tools/dbtool/BackupDiff.hpp"
#include "../UniqueTempPath.hpp"
#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlBackup/Common.hpp>
#include <Lightweight/SqlBackup/Sha256.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Zip/ZipArchive.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

using namespace Lightweight;
using namespace std::string_view_literals;
using Lightweight::SqlBackup::BackupAttachment;
using Lightweight::SqlBackup::BackupSettings;
using Lightweight::SqlBackup::Sha256;
using Lightweight::SqlBackup::Tests::LambdaProgressManager;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;

namespace
{

/// The tables every case backs up, so the archives ignore whatever else the test database holds.
constexpr auto SampleTableFilter = "AttachmentCustomers,AttachmentOrders";

std::vector<std::uint8_t> ToBytes(std::string_view text)
{
    return { text.begin(), text.end() };
}

/// Attachments at the top level and in a folder, with a UTF-8 name, non-ASCII and zero bytes, and no content.
std::vector<BackupAttachment> SampleAttachments()
{
    return {
        { .name = "manifest.json", .content = ToBytes(R"({"kind":"sample"})") },
        { .name = "notes/readme.txt", .content = { 'n', 'o', 't', 'e', 's', 0x00, 0xC3, 0xA9, 0xFF, '\n' } },
        { .name = "r\xC3\xA9sum\xC3\xA9.txt", .content = ToBytes("summary") },
        { .name = "empty.txt", .content = {} },
    };
}

/// The names of @ref SampleAttachments as an archive lists them: sorted, without the `attachments/` prefix.
std::vector<std::string> SampleAttachmentNames()
{
    return { "empty.txt", "manifest.json", "notes/readme.txt", "r\xC3\xA9sum\xC3\xA9.txt" };
}

/// Attachment names a backup must refuse, and how its error message quotes the offending one.
struct InvalidAttachmentNames
{
    std::vector<std::string> names;
    std::string expectedMention;
};

/// Each invalid name behind a valid one, two names that differ only in letter case, and a name that is
/// also the folder of another.
std::vector<InvalidAttachmentNames> InvalidAttachmentCases()
{
    auto cases = std::vector<InvalidAttachmentNames> {};
    for (auto const name: { ""sv, "/x"sv, "a\\b"sv, "C:x"sv, "a//b"sv, "a/"sv, "./x"sv, "../x"sv, "x/../y"sv, "x/."sv })
        cases.push_back(
            { .names = { "manifest.json", std::string { name } }, .expectedMention = std::format("\"{}\"", name) });
    // Messages write control bytes and the bytes of malformed UTF-8 as \xHH.
    for (auto const& [name, mention]: {
             std::pair { "a\0b.txt"sv, R"("a\x00b.txt")"sv },
             std::pair { "a\x01"
                         "b.txt"sv,
                         R"("a\x01b.txt")"sv },
             std::pair { "tab\there.txt"sv, R"("tab\x09here.txt")"sv },
             std::pair { "del\x7F.txt"sv, R"("del\x7F.txt")"sv },
             std::pair { "caf\xE9.txt"sv, R"("caf\xE9.txt")"sv },
             std::pair { "\xC0\xAF.txt"sv, R"("\xC0\xAF.txt")"sv },
             std::pair { "\xED\xA0\x80.txt"sv, R"("\xED\xA0\x80.txt")"sv },
             std::pair { "\xF4\x90\x80\x80.txt"sv, R"("\xF4\x90\x80\x80.txt")"sv },
             std::pair { "x\xC3"sv, R"("x\xC3")"sv },
         })
        cases.push_back({ .names = { "manifest.json", std::string { name } }, .expectedMention = std::string { mention } });
    cases.push_back({ .names = { "Notes/Readme.txt", "notes/readme.TXT" }, .expectedMention = R"("notes/readme.TXT")" });
    cases.push_back({ .names = { "Notes", "notes/readme.txt" }, .expectedMention = R"("Notes")" });
    return cases;
}

/// The test database holding the sample tables: three customers and two orders.
struct SampleTablesFixture: SqlTestFixture
{
    SampleTablesFixture()
    {
        using namespace SqlColumnTypeDefinitions;

        auto stmt = SqlStatement {};
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.CreateTable("AttachmentCustomers").PrimaryKey("id", Integer {}).Column("name", Varchar { 32 });
            migration.CreateTable("AttachmentOrders").PrimaryKey("id", Integer {}).Column("customer_id", Integer {});
        });
        stmt.Prepare(R"(INSERT INTO "AttachmentCustomers" ("id", "name") VALUES (?, ?))");
        (void) stmt.Execute(1, std::string { "alpha" });
        (void) stmt.Execute(2, std::string { "beta" });
        (void) stmt.Execute(3, std::string { "gamma" });
        stmt.Prepare(R"(INSERT INTO "AttachmentOrders" ("id", "customer_id") VALUES (?, ?))");
        (void) stmt.Execute(10, 1);
        (void) stmt.Execute(11, 3);
    }
};

/// A unique archive path in the temporary directory, removed again when the test ends.
struct TemporaryArchive
{
    std::filesystem::path path;
    ScopedFileRemoved cleanup { path };

    explicit TemporaryArchive(std::string_view stem):
        path { UniqueTempPath(stem, ".zip") }
    {
    }
};

/// A progress manager that collects the messages of the errors reported through it into @p errors.
LambdaProgressManager CollectErrors(std::vector<std::string>& errors)
{
    return LambdaProgressManager { [&errors](SqlBackup::Progress const& progress) {
        if (progress.state == SqlBackup::Progress::State::Error)
            errors.push_back(progress.message);
    } };
}

/// Backs up the sample tables to @p path.
void BackupSampleTables(std::filesystem::path const& path, BackupSettings const& settings)
{
    auto errors = std::vector<std::string> {};
    auto progress = CollectErrors(errors);
    SqlBackup::Backup(path,
                      SqlConnection::DefaultConnectionString(),
                      1,
                      progress,
                      /*schema=*/ {},
                      SampleTableFilter,
                      /*retrySettings=*/ {},
                      settings);
    INFO(SqlBackup::detail::JoinNames(errors));
    CHECK(errors.empty());
    REQUIRE(std::filesystem::exists(path));
}

/// Checks that the archive at @p path returns each of @p attachments byte for byte.
void CheckAttachmentContents(std::filesystem::path const& path, std::vector<BackupAttachment> const& attachments)
{
    for (auto const& attachment: attachments)
    {
        auto const content = SqlBackup::ReadArchiveAttachment(path, attachment.name);
        INFO(attachment.name << ": " << content.error_or("ok"));
        REQUIRE(content.has_value());
        CHECK(*content == attachment.content);
    }
}

/// The `files` object of the archive's checksums.json, or null when the archive has none.
nlohmann::json ListedChecksums(std::filesystem::path const& path)
{
    auto const archive = Zip::ZipArchive::Open(path);
    REQUIRE(archive.has_value());
    auto const index = archive->LocateEntry("checksums.json");
    if (!index)
        return nullptr;
    auto const text = archive->ReadEntryAsString(*index);
    REQUIRE(text.has_value());
    return nlohmann::json::parse(*text).at("files");
}

/// The compression method the archive at @p path records for its entry @p name.
zip_uint16_t EntryCompressionMethod(std::filesystem::path const& path, std::string_view name)
{
    auto const archive = Zip::ZipArchive::Open(path);
    REQUIRE(archive.has_value());
    auto const index = archive->LocateEntry(name);
    REQUIRE(index.has_value());
    zip_stat_t stat {};
    zip_stat_init(&stat);
    REQUIRE(zip_stat_index(archive->NativeHandle(), static_cast<zip_uint64_t>(index.value_or(-1)), 0, &stat) == 0);
    REQUIRE((stat.valid & ZIP_STAT_COMP_METHOD) != 0);
    return stat.comp_method;
}

/// Writes @p target with every entry of @p source followed by the entry @p name holding @p content.
void CopyArchiveAddingEntry(std::filesystem::path const& source,
                            std::filesystem::path const& target,
                            std::string_view name,
                            std::string_view content)
{
    auto const input = Zip::ZipArchive::Open(source);
    REQUIRE(input.has_value());
    auto output = Zip::ZipArchive::Create(target);
    REQUIRE(output.has_value());
    for (auto const& entry: input->GetAllEntries())
    {
        auto const entryContent = input->ReadEntry(entry.index);
        REQUIRE(entryContent.has_value());
        REQUIRE(output->AddBuffer(entry.name, *entryContent).has_value());
    }
    REQUIRE(output->AddString(name, content).has_value());
    REQUIRE(output->Close().has_value());
}

/// Empties the test database, restores @p path into it, and returns each table it then holds with its row count.
std::map<std::string, long long> RestoreIntoEmptyDatabase(std::filesystem::path const& path)
{
    {
        auto stmt = SqlStatement {};
        SqlTestFixture::DropAllTablesInDatabase(stmt);
    }

    auto errors = std::vector<std::string> {};
    auto progress = CollectErrors(errors);
    SqlBackup::Restore(path, SqlConnection::DefaultConnectionString(), 1, progress);
    INFO(SqlBackup::detail::JoinNames(errors));
    CHECK(errors.empty());

    auto stmt = SqlStatement {};
    auto rowCounts = std::map<std::string, long long> {};
    for (auto const& table: SqlTestFixture::GetAllTableNames(stmt))
    {
        if (table.starts_with("sqlite_"))
            continue;
        auto const rowCount = stmt.ExecuteDirectScalar<long long>(std::format(R"(SELECT COUNT(*) FROM "{}")", table));
        rowCounts.emplace(table, rowCount.value_or(-1));
    }
    return rowCounts;
}

} // namespace

TEST_CASE("ValidateAttachmentNames accepts relative UTF-8 paths", "[SqlBackup][Attachments]")
{
    auto const names = std::vector<std::string> {
        "manifest.json",      "notes/readme.txt",         "notes.txt",
        ".hidden/a..b/c.txt", "r\xC3\xA9sum\xC3\xA9.txt", "\xF0\x9F\x93\x84.txt",
    };

    auto const result = SqlBackup::detail::ValidateAttachmentNames(names);

    INFO(result.error_or("ok"));
    CHECK(result.has_value());
}

TEST_CASE("ValidateAttachmentNames names the offending attachment", "[SqlBackup][Attachments]")
{
    auto const [names, expectedMention] = GENERATE(from_range(InvalidAttachmentCases()));

    auto const result = SqlBackup::detail::ValidateAttachmentNames(names);

    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error(), Catch::Matchers::ContainsSubstring(expectedMention));
}

TEST_CASE("Backup refuses invalid attachment names before connecting and writes no archive", "[SqlBackup][Attachments]")
{
    auto const [names, expectedMention] = GENERATE(from_range(InvalidAttachmentCases()));
    auto settings = BackupSettings {};
    for (auto const& name: names)
        settings.attachments.push_back({ .name = name });
    auto const archive = TemporaryArchive { "attachments-invalid-name" };
    auto errors = std::vector<std::string> {};
    auto progress = CollectErrors(errors);
    // No driver of this name exists, so reaching the connect step would throw.
    auto const unusableConnection = SqlConnectionString { .value = "DRIVER={No Such Driver};Database=none" };

    REQUIRE_NOTHROW(SqlBackup::Backup(archive.path,
                                      unusableConnection,
                                      1,
                                      progress,
                                      /*schema=*/ {},
                                      /*tableFilter=*/"*",
                                      /*retrySettings=*/ {},
                                      settings));

    REQUIRE(errors.size() == 1);
    CHECK_THAT(errors.front(), Catch::Matchers::ContainsSubstring(expectedMention));
    auto temporaryDirectory = archive.path;
    temporaryDirectory += ".tmp";
    CHECK_FALSE(std::filesystem::exists(archive.path));
    CHECK_FALSE(std::filesystem::exists(temporaryDirectory));
}

TEST_CASE_METHOD(SampleTablesFixture,
                 "Backup stores attachments that can be listed, read and verified",
                 "[SqlBackup][Attachments]")
{
    auto const archive = TemporaryArchive { "attachments-backup" };
    auto const attachments = SampleAttachments();
    BackupSampleTables(archive.path, BackupSettings { .attachments = attachments });

    auto const names = SqlBackup::ListArchiveAttachments(archive.path);
    INFO(names.error_or("ok"));
    REQUIRE(names.has_value());
    CHECK(*names == SampleAttachmentNames());
    CheckAttachmentContents(archive.path, attachments);

    auto const unknown = SqlBackup::ReadArchiveAttachment(archive.path, "notes/missing.txt");
    REQUIRE_FALSE(unknown.has_value());
    CHECK_THAT(unknown.error(), Catch::Matchers::ContainsSubstring("notes/missing.txt"));

    auto const files = ListedChecksums(archive.path);
    REQUIRE(files.is_object());
    for (auto const& attachment: attachments)
        CHECK(files.value("attachments/" + attachment.name, "")
              == Sha256::Hash(attachment.content.data(), attachment.content.size()));

    auto const verification = SqlBackup::VerifyArchive(archive.path);
    INFO(verification.error_or("ok"));
    REQUIRE(verification.has_value());
    CHECK(verification->verifiedEntries == files.size());
    CHECK(verification->verifiedEntries > attachments.size());
}

TEST_CASE_METHOD(SampleTablesFixture,
                 "Attachments are compressed with the backup's compression method",
                 "[SqlBackup][Attachments]")
{
    auto const method = GENERATE(SqlBackup::CompressionMethod::Store, SqlBackup::CompressionMethod::Deflate);
    auto const archive = TemporaryArchive { "attachments-compression" };
    auto const attachments = std::vector<BackupAttachment> {
        { .name = "large.txt", .content = std::vector<std::uint8_t>(64 * 1024, 'a') },
    };
    BackupSampleTables(archive.path, BackupSettings { .method = method, .schemaOnly = true, .attachments = attachments });

    CHECK(EntryCompressionMethod(archive.path, "attachments/large.txt") == static_cast<zip_uint16_t>(method));
    CheckAttachmentContents(archive.path, attachments);
}

TEST_CASE_METHOD(SampleTablesFixture,
                 "Restore of an archive with attachments yields the same tables as one without",
                 "[SqlBackup][Attachments]")
{
    auto const withAttachments = TemporaryArchive { "attachments-restore-with" };
    auto const withoutAttachments = TemporaryArchive { "attachments-restore-without" };
    BackupSampleTables(withAttachments.path, BackupSettings { .attachments = SampleAttachments() });
    BackupSampleTables(withoutAttachments.path, BackupSettings {});

    auto const restoredWith = RestoreIntoEmptyDatabase(withAttachments.path);
    auto const restoredWithout = RestoreIntoEmptyDatabase(withoutAttachments.path);

    CHECK(restoredWith == restoredWithout);
    CHECK(restoredWith == std::map<std::string, long long> { { "AttachmentCustomers", 3 }, { "AttachmentOrders", 2 } });
}

TEST_CASE_METHOD(SampleTablesFixture,
                 "A schema-only backup with attachments writes checksums.json",
                 "[SqlBackup][Attachments]")
{
    auto const withAttachments = TemporaryArchive { "attachments-schema-only" };
    auto const withoutAttachments = TemporaryArchive { "attachments-schema-only-none" };
    auto const attachments = SampleAttachments();
    BackupSampleTables(withAttachments.path, BackupSettings { .schemaOnly = true, .attachments = attachments });
    BackupSampleTables(withoutAttachments.path, BackupSettings { .schemaOnly = true });

    auto const files = ListedChecksums(withAttachments.path);
    REQUIRE(files.is_object());
    CHECK(files.size() == attachments.size());
    CHECK(ListedChecksums(withoutAttachments.path).is_null());

    auto const verification = SqlBackup::VerifyArchive(withAttachments.path);
    INFO(verification.error_or("ok"));
    REQUIRE(verification.has_value());
    CHECK(verification->verifiedEntries == attachments.size());
    CheckAttachmentContents(withAttachments.path, attachments);
}

TEST_CASE_METHOD(SampleTablesFixture, "A tampered attachment fails verification and reading", "[SqlBackup][Attachments]")
{
    auto const original = TemporaryArchive { "attachments-tamper-source" };
    auto const tampered = TemporaryArchive { "attachments-tampered" };
    BackupSampleTables(original.path, BackupSettings { .attachments = SampleAttachments() });

    SqlBackup::Tests::RewriteArchive(
        original.path,
        tampered.path,
        [](std::string const& name, std::string content) -> std::optional<std::pair<std::string, std::string>> {
            if (name == "attachments/notes/readme.txt")
                content = "rewritten after the backup";
            return std::pair { name, std::move(content) };
        });

    auto const verification = SqlBackup::VerifyArchive(tampered.path);
    REQUIRE_FALSE(verification.has_value());
    CHECK_THAT(verification.error(), Catch::Matchers::ContainsSubstring("attachments/notes/readme.txt"));

    auto const content = SqlBackup::ReadArchiveAttachment(tampered.path, "notes/readme.txt");
    REQUIRE_FALSE(content.has_value());
    CHECK_THAT(content.error(), Catch::Matchers::ContainsSubstring("attachments/notes/readme.txt"));
    CHECK(SqlBackup::ReadArchiveAttachment(tampered.path, "manifest.json").has_value());
}

TEST_CASE_METHOD(SampleTablesFixture, "Entries added below attachments/ after the backup", "[SqlBackup][Attachments]")
{
    auto const original = TemporaryArchive { "attachments-extend-source" };
    auto const extended = TemporaryArchive { "attachments-extended" };
    BackupSampleTables(original.path, BackupSettings { .attachments = SampleAttachments() });

    SECTION("An unlisted attachment fails verification and reading")
    {
        CopyArchiveAddingEntry(original.path, extended.path, "attachments/late.txt", "added after the backup");

        auto const verification = SqlBackup::VerifyArchive(extended.path);
        REQUIRE_FALSE(verification.has_value());
        CHECK_THAT(verification.error(), Catch::Matchers::ContainsSubstring("attachments/late.txt"));

        auto const content = SqlBackup::ReadArchiveAttachment(extended.path, "late.txt");
        REQUIRE_FALSE(content.has_value());
        CHECK_THAT(content.error(),
                   Catch::Matchers::ContainsSubstring("attachments/late.txt is not listed in checksums.json"));

        auto const names = SqlBackup::ListArchiveAttachments(extended.path);
        REQUIRE(names.has_value());
        CHECK(*names
              == std::vector<std::string> {
                  "empty.txt", "late.txt", "manifest.json", "notes/readme.txt", "r\xC3\xA9sum\xC3\xA9.txt" });
    }

    SECTION("A name that breaks the naming rules fails listing")
    {
        CopyArchiveAddingEntry(original.path, extended.path, "attachments/../outside.txt", "outside");

        auto const names = SqlBackup::ListArchiveAttachments(extended.path);
        REQUIRE_FALSE(names.has_value());
        CHECK_THAT(names.error(), Catch::Matchers::ContainsSubstring(R"("../outside.txt")"));
    }

    SECTION("A folder entry is no attachment")
    {
        CopyArchiveAddingEntry(original.path, extended.path, "attachments/notes/", "");

        auto const verification = SqlBackup::VerifyArchive(extended.path);
        INFO(verification.error_or("ok"));
        CHECK(verification.has_value());

        auto const names = SqlBackup::ListArchiveAttachments(extended.path);
        REQUIRE(names.has_value());
        CHECK(*names == SampleAttachmentNames());
    }
}

TEST_CASE_METHOD(SampleTablesFixture,
                 "BackupDiff reports no difference between archives that differ only in attachments",
                 "[SqlBackup][Attachments]")
{
    auto const first = TemporaryArchive { "attachments-diff-first" };
    auto const second = TemporaryArchive { "attachments-diff-second" };
    BackupSampleTables(first.path, BackupSettings { .attachments = SampleAttachments() });
    BackupSampleTables(second.path,
                       BackupSettings { .attachments = { { .name = "other/notes.txt", .content = ToBytes("other") } } });

    auto const result = Tools::BackupDiff(first.path, second.path, /*ignoreTables=*/ {}, /*observer=*/nullptr);

    CHECK(result.archivesReadable);
    CHECK_FALSE(result.differenceFound);
    CHECK(result.comparedTables == 2);
    CHECK(result.identicalTables == 2);
    CHECK(result.differingTables == 0);
}
