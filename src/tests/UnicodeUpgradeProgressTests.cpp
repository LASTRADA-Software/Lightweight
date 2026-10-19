// SPDX-License-Identifier: Apache-2.0

#include "RecordingProgressTask.hpp"
#include "Utils.hpp"

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/UnicodeUpgradeProgress.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <string>
#include <tuple>
#include <vector>

using namespace Lightweight;
using Lightweight::detail::UnicodeUpgradeProgress;
using Lightweight::detail::UnicodeUpgradeTableProgress;
using SqlMigration::ExecutionMode;

namespace
{

std::string const Overall { UnicodeUpgradeProgress::OverallTitle };
std::string const Planning { UnicodeUpgradeProgress::PlanningTitle };

/// Starts with an empty manager, like the migration tests do.
class UnicodeProgressFixture: public SqlTestFixture
{
  public:
    UnicodeProgressFixture()
    {
        SqlMigration::MigrationManager::GetInstance().RemoveAllMigrations();
        SqlMigration::MigrationManager::GetInstance().RemoveAllReleases();
    }
    UnicodeProgressFixture(UnicodeProgressFixture const&) = delete;
    UnicodeProgressFixture(UnicodeProgressFixture&&) = delete;
    UnicodeProgressFixture& operator=(UnicodeProgressFixture const&) = delete;
    UnicodeProgressFixture& operator=(UnicodeProgressFixture&&) = delete;
    ~UnicodeProgressFixture() override
    {
        SqlMigration::MigrationManager::GetInstance().CloseDataMapper();
    }
};

} // namespace

TEST_CASE("UnicodeUpgradeProgress weights the overall task by rows and opens a task per unit and table",
          "[UnicodeUpgrade][progress]")
{
    auto journal = RecordingProgressTask::Journal {};
    {
        auto root = RecordingProgressTask { journal };
        auto progress = UnicodeUpgradeProgress { root };
        progress.Planned({ { { .name = "a", .rows = 100, .columns = 2 }, { .name = "b", .rows = 0, .columns = 1 } },
                           { { .name = "c", .rows = 50, .columns = 1 } } },
                         ExecutionMode::Execute);
        progress.BeginUnit(0);
        progress.UnitStep("drop dependent objects");
        progress.TableAltering(0);
        progress.TableAltered(0);
        progress.TableAltering(1);
        progress.TableAltered(1);
        progress.UnitStep("restore objects");
        progress.UnitCommitted();
        progress.BeginUnit(1);
        progress.TableAltering(0);
        progress.TableAltered(0);
        progress.UnitCommitted();
    }
    auto const unit1 = Overall + "/Unit 1 of 2";
    auto const unit2 = Overall + "/Unit 2 of 2";
    CHECK(journal.events
          == std::vector<std::string> {
              "begin " + Planning + " total=0",
              "end " + Planning,
              "begin " + Overall + " total=150",
              "begin " + unit1 + " total=2",
              "begin " + unit1 + "/a total=100",
              "begin " + unit1 + "/b total=0",
              "advance " + unit1 + "/a +0 drop dependent objects",
              "advance " + unit1 + "/b +0 drop dependent objects",
              "advance " + unit1 + "/a +0 alter 2 column(s)",
              "advance " + unit1 + "/a +100 done",
              "advance " + unit1 + " +1",
              "advance " + Overall + " +100",
              "advance " + unit1 + "/b +0 alter 1 column(s)",
              "advance " + unit1 + "/b +0 done",
              "advance " + unit1 + " +1",
              "advance " + Overall + " +0",
              "advance " + unit1 + "/a +0 restore objects",
              "advance " + unit1 + "/b +0 restore objects",
              "end " + unit1 + "/b",
              "end " + unit1 + "/a",
              "end " + unit1,
              "begin " + unit2 + " total=1",
              "begin " + unit2 + "/c total=50",
              "advance " + unit2 + "/c +0 alter 1 column(s)",
              "advance " + unit2 + "/c +50 done",
              "advance " + unit2 + " +1",
              "advance " + Overall + " +50",
              "end " + unit2 + "/c",
              "end " + unit2,
              "end " + Overall,
          });
}

TEST_CASE("UnicodeUpgradeProgress counts tables when no row count is known", "[UnicodeUpgrade][progress]")
{
    auto journal = RecordingProgressTask::Journal {};
    {
        auto root = RecordingProgressTask { journal };
        auto progress = UnicodeUpgradeProgress { root };
        progress.Planned({ { { .name = "a", .rows = 0, .columns = 1 } }, { { .name = "b", .rows = 0, .columns = 1 } } },
                         ExecutionMode::Execute);
        progress.BeginUnit(0);
        progress.TableAltering(0);
        progress.TableAltered(0);
        progress.UnitCommitted();
    }
    CHECK(std::ranges::contains(journal.events, "begin " + Overall + " total=2"));
    CHECK(std::ranges::contains(journal.events, "advance " + Overall + " +1"));
}

TEST_CASE("UnicodeUpgradeProgress reports a dry run and an empty plan without opening the overall task",
          "[UnicodeUpgrade][progress]")
{
    auto journal = RecordingProgressTask::Journal {};
    {
        auto root = RecordingProgressTask { journal };
        UnicodeUpgradeProgress { root }.Planned(
            { { { .name = "a", .rows = 5, .columns = 2 }, { .name = "b", .rows = 1, .columns = 1 } } },
            ExecutionMode::DryRun);
        UnicodeUpgradeProgress { root }.Planned({}, ExecutionMode::Execute);
    }
    CHECK(journal.events
          == std::vector<std::string> {
              "begin " + Planning + " total=0",
              "end " + Planning,
              "log . info Would upgrade 3 column(s) across 2 table(s) in 1 unit(s).",
              "begin " + Planning + " total=0",
              "end " + Planning,
              "log . info No text columns to upgrade.",
          });
}

namespace
{

/// The `fail` events of @p journal, in order.
std::vector<std::string> Failures(RecordingProgressTask::Journal const& journal)
{
    auto failures = std::vector<std::string> {};
    std::ranges::copy_if(
        journal.events, std::back_inserter(failures), [](std::string const& event) { return event.starts_with("fail "); });
    return failures;
}

} // namespace

TEST_CASE("UnicodeUpgradeProgress fails the running table, its unit and the overall task", "[UnicodeUpgrade][progress]")
{
    auto journal = RecordingProgressTask::Journal {};
    {
        auto root = RecordingProgressTask { journal };
        auto progress = UnicodeUpgradeProgress { root };
        progress.Planned({ { { .name = "a", .rows = 10, .columns = 1 }, { .name = "b", .rows = 10, .columns = 1 } } },
                         ExecutionMode::Execute);
        progress.BeginUnit(0);
        progress.TableAltering(0);
        progress.Failed("boom");
    }
    auto const unit = Overall + "/Unit 1 of 1";
    CHECK(Failures(journal)
          == std::vector<std::string> { "fail " + unit + "/a boom", "fail " + unit + " boom", "fail " + Overall + " boom" });
    CHECK(std::ranges::contains(journal.events, "end " + Overall));
}

TEST_CASE("UnicodeUpgradeProgress fails only the unit and the overall task outside a table's ALTER statements",
          "[UnicodeUpgrade][progress]")
{
    auto journal = RecordingProgressTask::Journal {};
    {
        auto root = RecordingProgressTask { journal };
        auto progress = UnicodeUpgradeProgress { root };
        progress.Planned({ { { .name = "a", .rows = 10, .columns = 1 }, { .name = "b", .rows = 10, .columns = 1 } } },
                         ExecutionMode::Execute);
        progress.BeginUnit(0);
        progress.UnitStep("drop dependent objects");
        progress.TableAltering(0);
        progress.TableAltered(0);
        progress.TableAltering(1);
        progress.TableAltered(1);
        progress.UnitStep("restore objects");
        progress.Failed("boom");
    }
    auto const unit = Overall + "/Unit 1 of 1";
    CHECK(Failures(journal) == std::vector<std::string> { "fail " + unit + " boom", "fail " + Overall + " boom" });
}

TEST_CASE("UnicodeUpgradeProgress fails the planning task when planning fails", "[UnicodeUpgrade][progress]")
{
    auto journal = RecordingProgressTask::Journal {};
    {
        auto root = RecordingProgressTask { journal };
        auto progress = UnicodeUpgradeProgress { root };
        progress.Failed("boom");
    }
    CHECK(journal.events
          == std::vector<std::string> { "begin " + Planning + " total=0", "fail " + Planning + " boom", "end " + Planning });
}

TEST_CASE_METHOD(SqlTestFixture, "RowCountForProgress is 0 for a table it cannot count", "[UnicodeUpgrade][progress]")
{
    auto connection = SqlConnection {};
    CHECK(detail::RowCountForProgress(connection, { .catalog = {}, .schema = {}, .table = "no_such_table_for_progress" })
          == 0);
}

namespace
{

/// The schema `uup_rows` lives in: the dialect's default one, none on SQLite.
std::string DefaultSchemaOf(SqlServerType serverType)
{
    if (serverType == SqlServerType::MICROSOFT_SQL)
        return "dbo";
    if (serverType == SqlServerType::POSTGRESQL)
        return "public";
    return {};
}

/// Creates `uup_rows` with three rows and drops it when the returned guard goes away.
auto CreateRowCountTable(SqlStatement& stmt)
{
    using namespace SqlColumnTypeDefinitions;
    (void) stmt.ExecuteDirect(R"(DROP TABLE IF EXISTS "uup_rows")");
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& m) { m.CreateTable("uup_rows").PrimaryKey("id", Bigint()); });
    (void) stmt.ExecuteDirect(R"(INSERT INTO "uup_rows" ("id") VALUES (1), (2), (3))");
    if (stmt.Connection().ServerType() == SqlServerType::POSTGRESQL)
        (void) stmt.ExecuteDirect(R"(ANALYZE "uup_rows")");
    return Lightweight::detail::Finally([&stmt] { (void) stmt.ExecuteDirect(R"(DROP TABLE IF EXISTS "uup_rows")"); });
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "RowCountForProgress counts a populated schema-qualified table",
                 "[UnicodeUpgrade][progress]")
{
    auto connection = SqlConnection {};
    auto stmt = SqlStatement { connection };
    auto const cleanup = CreateRowCountTable(stmt);
    CHECK(detail::RowCountForProgress(
              connection, { .catalog = {}, .schema = DefaultSchemaOf(connection.ServerType()), .table = "uup_rows" })
          == 3);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "RowCountForProgress counts for a SQL Server user who only reads the table",
                 "[UnicodeUpgrade][progress]")
{
    auto connection = SqlConnection {};
    if (connection.ServerType() != SqlServerType::MICROSOFT_SQL)
    {
        SUCCEED("the catalog view without extra rights is SQL Server's");
        return;
    }
    auto stmt = SqlStatement { connection };
    auto const cleanup = CreateRowCountTable(stmt);
    (void) stmt.ExecuteDirect("IF USER_ID(N'uup_reader') IS NOT NULL DROP USER uup_reader");
    (void) stmt.ExecuteDirect("CREATE USER uup_reader WITHOUT LOGIN");
    auto const dropUser = Lightweight::detail::Finally(
        [&stmt] { (void) stmt.ExecuteDirect("IF USER_ID(N'uup_reader') IS NOT NULL DROP USER uup_reader"); });
    (void) stmt.ExecuteDirect("GRANT SELECT ON dbo.uup_rows TO uup_reader");
    (void) stmt.ExecuteDirect("EXECUTE AS USER = N'uup_reader'");
    auto const revert = Lightweight::detail::Finally([&stmt] { (void) stmt.ExecuteDirect("REVERT"); });
    CHECK(detail::RowCountForProgress(connection, { .catalog = {}, .schema = "dbo", .table = "uup_rows" }) == 3);
}

TEST_CASE_METHOD(UnicodeProgressFixture,
                 "UnicodeUpgradeTables reports rows and units, and stops after a committed unit on cancellation",
                 "[SqlMigration][UnicodeUpgrade][progress]")
{
    using namespace SqlColumnTypeDefinitions;
    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    auto stmt = SqlStatement { manager.GetDataMapper().Connection() };
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& m) {
        m.CreateTable("uup_one").PrimaryKey("id", Bigint()).Column("note", Varchar(40));
        m.CreateTable("uup_two").PrimaryKey("id", Bigint()).Column("note", Varchar(40));
    });
    (void) stmt.ExecuteDirect(R"(INSERT INTO "uup_one" ("id", "note") VALUES (1, 'a'), (2, 'b'), (3, 'c'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "uup_two" ("id", "note") VALUES (1, 'a'), (2, 'b'))");
    if (stmt.Connection().ServerType() == SqlServerType::POSTGRESQL)
        (void) stmt.ExecuteDirect(R"(ANALYZE "uup_one", "uup_two")");
    auto const declaration = SqlMigration::Migration<20991231000001> {
        "declare wide",
        [](SqlMigrationQueryBuilder& plan) {
            plan.CreateTable("uup_one").PrimaryKey("id", Bigint()).Column("note", NVarchar(40));
            plan.CreateTable("uup_two").PrimaryKey("id", Bigint()).Column("note", NVarchar(40));
        }
    };

    auto journal = RecordingProgressTask::Journal {};
    journal.cancelWhen = [](std::vector<std::string> const& events) {
        return std::ranges::contains(events, "end " + Overall + "/Unit 1 of 2");
    };
    auto root = RecordingProgressTask { journal };

    auto const plan = manager.UnicodeUpgradeTables(ExecutionMode::DryRun);
    if (plan.columns.empty())
    {
        // This driver reports these columns as wide (the Unicode drivers of PostgreSQL and SQLite): nothing to report.
        std::ignore = manager.UnicodeUpgradeTables(ExecutionMode::Execute, &root);
        CHECK(journal.events
              == std::vector<std::string> {
                  "begin " + Planning + " total=0", "end " + Planning, "log . info No text columns to upgrade." });
        return;
    }
    REQUIRE(plan.units.size() == 2);
    auto const result = manager.UnicodeUpgradeTables(ExecutionMode::Execute, &root);
    CHECK(result.wasCancelled);
    CHECK(result.committedUnitCount == 1);
    CHECK(std::ranges::contains(journal.events, "begin " + Overall + " total=5"));
    CHECK(std::ranges::contains(journal.events, "end " + Overall + "/Unit 1 of 2"));
    CHECK(std::ranges::none_of(
        journal.events, [](std::string const& event) { return event.starts_with("begin " + Overall + "/Unit 2 of 2"); }));
}
