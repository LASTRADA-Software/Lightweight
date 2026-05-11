// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlBackup/Common.hpp>
#include <Lightweight/SqlBackup/SqlBackup.hpp>

#include <catch2/catch_test_macros.hpp>

using Lightweight::SqlException;
using Lightweight::SqlStatement;
namespace SqlColumnTypeDefinitions = Lightweight::SqlColumnTypeDefinitions;
using Lightweight::SqlBackup::ErrorTrackingProgressManager;
using Lightweight::SqlBackup::NullProgressManager;
using Lightweight::SqlBackup::Progress;
namespace backup_detail = Lightweight::SqlBackup::detail;

// ================================================================================================
// ErrorTrackingProgressManager — counts only Progress::State::Error events
// ================================================================================================

namespace
{

class TestProgressManager: public ErrorTrackingProgressManager
{
  public:
    void Update(Progress const& progress) override
    {
        ErrorTrackingProgressManager::Update(progress);
    }
    void AllDone() override {}
};

} // namespace

TEST_CASE("ErrorTrackingProgressManager: starts with zero errors", "[SqlBackup]")
{
    TestProgressManager pm;
    CHECK(pm.ErrorCount() == 0);
}

TEST_CASE("ErrorTrackingProgressManager: counts only Error states", "[SqlBackup]")
{
    TestProgressManager pm;

    auto const make = [](Progress::State s, std::string tableName, std::string message = {}, size_t rows = 0) {
        return Progress {
            .state = s,
            .tableName = std::move(tableName),
            .currentRows = rows,
            .totalRows = std::nullopt,
            .message = std::move(message),
        };
    };

    pm.Update(make(Progress::State::Started, "T"));
    pm.Update(make(Progress::State::InProgress, "T", {}, 1));
    pm.Update(make(Progress::State::Finished, "T", {}, 1));
    CHECK(pm.ErrorCount() == 0);

    pm.Update(make(Progress::State::Error, "T", "boom"));
    CHECK(pm.ErrorCount() == 1);

    pm.Update(make(Progress::State::Warning, "T", "ignored"));
    CHECK(pm.ErrorCount() == 1);

    pm.Update(make(Progress::State::Error, "T2", "again"));
    CHECK(pm.ErrorCount() == 2);
}

// ================================================================================================
// NullProgressManager — silent, but still tracks errors
// ================================================================================================

TEST_CASE("NullProgressManager: AllDone is a no-op", "[SqlBackup]")
{
    NullProgressManager pm;
    REQUIRE_NOTHROW(pm.AllDone());
    REQUIRE_NOTHROW(pm.SetMaxTableNameLength(40));
    REQUIRE_NOTHROW(pm.SetTotalItems(100));
    REQUIRE_NOTHROW(pm.AddTotalItems(50));
    REQUIRE_NOTHROW(pm.OnItemsProcessed(10));
}

TEST_CASE("NullProgressManager: still counts errors via the inherited tracker", "[SqlBackup]")
{
    NullProgressManager pm;
    pm.Update(Progress { .state = Progress::State::Error,
                         .tableName = "X",
                         .currentRows = 0,
                         .totalRows = std::nullopt,
                         .message = "broke" });
    CHECK(pm.ErrorCount() == 1);
}

// ================================================================================================
// detail::DropTableIfExists — DB-dependent
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlBackup::detail::DropTableIfExists drops an existing table", "[SqlBackup]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("DropMe").RequiredColumn("id", SqlColumnTypeDefinitions::Integer {}); });

    NullProgressManager pm;
    auto& conn = stmt.Connection();
    CHECK(backup_detail::DropTableIfExists(conn, /*schema=*/"", /*tableName=*/"DropMe", pm));

    // Verifying with a fresh statement avoids stale-state issues on the previous handle.
    auto verifyStmt = SqlStatement {};
    auto const _ = ScopedSqlNullLogger {};
    CHECK_THROWS_AS((void) verifyStmt.ExecuteDirect(R"(SELECT * FROM "DropMe")"), SqlException);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlBackup::detail::DropTableIfExists is a no-op for missing tables", "[SqlBackup]")
{
    auto stmt = SqlStatement {};
    NullProgressManager pm;
    auto& conn = stmt.Connection();
    // The contract says "true if table was dropped or didn't exist" — must not throw.
    CHECK(backup_detail::DropTableIfExists(conn, /*schema=*/"", /*tableName=*/"DoesNotExist", pm));
    CHECK(pm.ErrorCount() == 0);
}

// ================================================================================================
// Progress::State enumerators are distinct
// ================================================================================================

TEST_CASE("Progress::State enumerators are distinct", "[SqlBackup]")
{
    using S = Progress::State;
    CHECK(static_cast<int>(S::Started) != static_cast<int>(S::InProgress));
    CHECK(static_cast<int>(S::InProgress) != static_cast<int>(S::Finished));
    CHECK(static_cast<int>(S::Finished) != static_cast<int>(S::Error));
    CHECK(static_cast<int>(S::Error) != static_cast<int>(S::Warning));
}
