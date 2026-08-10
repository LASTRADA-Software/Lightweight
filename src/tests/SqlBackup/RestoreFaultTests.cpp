// SPDX-License-Identifier: Apache-2.0
//
// Fault-path tests for the restore helpers that open their own connection.
//
// RestoreIndexes, ApplyDatabaseConstraints and RecreateDatabaseSchema each take a connection
// *string* and connect internally, so their "failed to connect" arms are reachable with nothing
// more than a connection string the driver manager cannot resolve — no fake connection, no
// injection layer. Those arms had no coverage because the functions were not exported from the
// shared library, so no test could link against them.
//
// Note on classification: a SQLite-level fault (unreachable driver, unwritable path, missing
// table) surfaces as SQLSTATE HY000, which detail::IsTransientError does NOT treat as transient.
// These tests therefore exercise the give-up/report arms. The transient *retry* arms need a
// genuine class-08/HYT00 error and are covered separately.

#include "../Utils.hpp"

#include <Lightweight/SqlBackup/Restore.hpp>
#include <Lightweight/SqlBackup/SqlBackup.hpp>
#include <Lightweight/SqlConnectInfo.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using Lightweight::SqlConnectionString;
using Lightweight::SqlBackup::TableInfo;
namespace detail = Lightweight::SqlBackup::detail;

namespace
{

/// Records progress updates so a test can assert on what the failure path reported.
class RecordingProgressManager: public Lightweight::SqlBackup::ProgressManager
{
  public:
    void Update(Lightweight::SqlBackup::Progress const& p) override
    {
        messages.emplace_back(p.message);
        states.emplace_back(p.state);
    }

    void AllDone() override {}

    [[nodiscard]] bool HasErrorContaining(std::string_view needle) const
    {
        for (auto i = size_t { 0 }; i < messages.size(); ++i)
            if (states[i] == Lightweight::SqlBackup::Progress::State::Error && messages[i].contains(needle))
                return true;
        return false;
    }

    std::vector<std::string> messages;
    std::vector<Lightweight::SqlBackup::Progress::State> states;
};

/// A connection string naming a driver the ODBC driver manager cannot resolve, so Connect() fails
/// for real rather than being simulated.
SqlConnectionString UnreachableConnectionString()
{
    return SqlConnectionString { "DRIVER=NoSuchDriver_Lightweight;Database=nowhere" };
}

/// One minimal table, enough for the helpers to have something to iterate over. Every member is
/// left value-initialized except the two that identify the table: the failure happens at connect
/// time, before columns, foreign keys or indexes are ever read.
std::map<std::string, TableInfo> OneTable()
{
    auto tableMap = std::map<std::string, TableInfo> {};
    auto& users = tableMap["Users"];
    users.fields = R"("id")";
    users.isBinaryColumn = { false };
    return tableMap;
}

} // namespace

TEST_CASE("SqlBackup::detail::RestoreIndexes reports a connect failure and returns", "[SqlBackup][faults]")
{
    RecordingProgressManager progress;

    detail::RestoreIndexes(UnreachableConnectionString(), "", OneTable(), progress);

    CHECK(progress.HasErrorContaining("Failed to connect for index restoration"));
}

TEST_CASE("SqlBackup::detail::ApplyDatabaseConstraints reports a connect failure and returns", "[SqlBackup][faults]")
{
    RecordingProgressManager progress;

    detail::ApplyDatabaseConstraints(UnreachableConnectionString(), "", OneTable(), progress);

    CHECK(progress.HasErrorContaining("Failed to connect for FK constraints"));
}

TEST_CASE("SqlBackup::detail::RecreateDatabaseSchema creates no tables when it cannot connect", "[SqlBackup][faults]")
{
    RecordingProgressManager progress;

    auto const created = detail::RecreateDatabaseSchema(UnreachableConnectionString(), "", OneTable(), progress);

    // Nothing can be created without a connection, and the caller relies on the empty set to skip
    // data restoration for every table.
    CHECK(created.empty());
    CHECK_FALSE(progress.messages.empty());
}
