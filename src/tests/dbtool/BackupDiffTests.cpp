// SPDX-License-Identifier: Apache-2.0

#include "../../tools/dbtool/BackupDiff.hpp"
#include "../SqlBackup/TestHelpers.hpp"
#include "../Utils.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace Lightweight;
using namespace Lightweight::Tools;
using Lightweight::SqlBackup::Tests::LambdaProgressManager;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;

namespace
{

// Collects the streamed per-table events so a test can assert on them without parsing console text.
struct CapturingObserver: BackupDiffObserver
{
    std::vector<BackupDiffEvent> events;

    void OnEvent(BackupDiffEvent const& event) override
    {
        events.push_back(event);
    }
};

// A progress manager that fails the test on any backup error.
LambdaProgressManager MakeFailingProgressManager()
{
    return LambdaProgressManager { [](SqlBackup::Progress const& p) {
        if (p.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Backup error: " << p.message);
    } };
}

// Backs up ONLY the named table (tableFilter) so the archive content is isolated from whatever
// other tables happen to live in the shared test database — keeping the diff deterministic.
void BackupSingleTable(std::filesystem::path const& file, std::string const& table)
{
    auto pm = MakeFailingProgressManager();
    SqlBackup::Backup(file, SqlConnection::DefaultConnectionString(), 1, pm, /*schema=*/ {}, /*tableFilter=*/table);
    REQUIRE(std::filesystem::exists(file));
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "BackupDiff: identical archives compare equal with no differences", "[dbtool][BackupDiff]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const fileA = std::filesystem::path { "backupdiff_a.zip" };
    auto const fileB = std::filesystem::path { "backupdiff_b.zip" };
    auto const cleanA = ScopedFileRemoved { fileA };
    auto const cleanB = ScopedFileRemoved { fileB };

    {
        SqlStatement stmt {};
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("BackupDiffSubject");
            migration.CreateTable("BackupDiffSubject").PrimaryKey("id", Integer {}).Column("v", Varchar { 32 });
        });
        stmt.Prepare(R"(INSERT INTO "BackupDiffSubject" ("id", "v") VALUES (?, ?))");
        (void) stmt.Execute(1, std::string { "alpha" });
        (void) stmt.Execute(2, std::string { "beta" });
    }

    BackupSingleTable(fileA, "BackupDiffSubject");
    BackupSingleTable(fileB, "BackupDiffSubject");

    CapturingObserver observer;
    auto const result = BackupDiff(fileA, fileB, /*ignoreTables=*/ {}, &observer);

    CHECK(result.archivesReadable);
    CHECK_FALSE(result.differenceFound);
    CHECK(result.comparedTables == 1);
    CHECK(result.identicalTables == 1);
    CHECK(result.differingTables == 0);
    REQUIRE(observer.events.size() == 1);
    CHECK(observer.events.front().kind == BackupDiffEvent::Kind::Identical);
    CHECK(observer.events.front().leftRowCount == 2);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "BackupDiff: a differing row multiset is reported and can be ignored",
                 "[dbtool][BackupDiff]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const fileA = std::filesystem::path { "backupdiff_base.zip" };
    auto const fileC = std::filesystem::path { "backupdiff_changed.zip" };
    auto const cleanA = ScopedFileRemoved { fileA };
    auto const cleanC = ScopedFileRemoved { fileC };

    {
        SqlStatement stmt {};
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("BackupDiffSubject");
            migration.CreateTable("BackupDiffSubject").PrimaryKey("id", Integer {}).Column("v", Varchar { 32 });
        });
        stmt.Prepare(R"(INSERT INTO "BackupDiffSubject" ("id", "v") VALUES (?, ?))");
        (void) stmt.Execute(1, std::string { "alpha" });
        (void) stmt.Execute(2, std::string { "beta" });
    }
    BackupSingleTable(fileA, "BackupDiffSubject");

    // Add a row, so the candidate archive has an extra row-occurrence the baseline lacks.
    {
        SqlStatement stmt {};
        stmt.Prepare(R"(INSERT INTO "BackupDiffSubject" ("id", "v") VALUES (?, ?))");
        (void) stmt.Execute(3, std::string { "gamma" });
    }
    BackupSingleTable(fileC, "BackupDiffSubject");

    SECTION("the extra row makes the archives differ")
    {
        CapturingObserver observer;
        auto const result = BackupDiff(fileA, fileC, /*ignoreTables=*/ {}, &observer);

        CHECK(result.archivesReadable);
        CHECK(result.differenceFound);
        CHECK(result.differingTables == 1);
        REQUIRE(observer.events.size() == 1);
        auto const& event = observer.events.front();
        CHECK(event.kind == BackupDiffEvent::Kind::Differing);
        CHECK(event.onlyInRight == 1); // the added "gamma" row
        CHECK(event.onlyInLeft == 0);
    }

    SECTION("ignoring the differing table suppresses the failure but still reports it")
    {
        // Discover the sanitized table name from an un-ignored run, then ignore exactly that.
        CapturingObserver probe;
        auto const probed = BackupDiff(fileA, fileC, /*ignoreTables=*/ {}, &probe);
        REQUIRE(probed.differenceFound);
        REQUIRE(probe.events.size() == 1);
        auto const ignored = std::set<std::string> { probe.events.front().table };

        CapturingObserver observer;
        auto const result = BackupDiff(fileA, fileC, ignored, &observer);

        CHECK_FALSE(result.differenceFound); // the only difference is ignored
        CHECK(result.ignoredDifferences == 1);
        CHECK(result.differingTables == 1);
        REQUIRE(observer.events.size() == 1);
        CHECK(observer.events.front().kind == BackupDiffEvent::Kind::Differing);
        CHECK(observer.events.front().ignored);
    }

    SECTION("a nullptr observer is allowed; only the aggregate result is returned")
    {
        auto const result = BackupDiff(fileA, fileC, /*ignoreTables=*/ {}, nullptr);
        CHECK(result.differenceFound);
        CHECK(result.differingTables == 1);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "BackupDiff: an unreadable archive is reported, not silently passed",
                 "[dbtool][BackupDiff]")
{
    using namespace SqlColumnTypeDefinitions;

    auto const fileA = std::filesystem::path { "backupdiff_present.zip" };
    auto const cleanA = ScopedFileRemoved { fileA };
    {
        SqlStatement stmt {};
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("BackupDiffSubject");
            migration.CreateTable("BackupDiffSubject").PrimaryKey("id", Integer {}).Column("v", Varchar { 32 });
        });
    }
    BackupSingleTable(fileA, "BackupDiffSubject");

    auto const missing = std::filesystem::path { "backupdiff_does_not_exist.zip" };
    std::filesystem::remove(missing);

    CapturingObserver observer;
    auto const result = BackupDiff(missing, fileA, /*ignoreTables=*/ {}, &observer);

    CHECK_FALSE(result.archivesReadable);
    CHECK_FALSE(result.leftReadable);
    CHECK(result.rightReadable);
    CHECK(result.differenceFound);
    CHECK(observer.events.empty()); // no per-table comparison ran
}
