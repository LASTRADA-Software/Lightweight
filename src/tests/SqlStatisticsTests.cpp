// SPDX-License-Identifier: Apache-2.0

// clang-format off
#include "Utils.hpp" // must precede Entities.hpp, which uses the Member() macro defined there
#include "DataMapper/Entities.hpp"
// clang-format on

#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/DataMapper/Pool.hpp>
#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlStatistics.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace Lightweight;
using namespace std::chrono_literals;

namespace
{

/// Resets the process-wide collector around a test so counters observed here belong to this test
/// only. Catch2 runs test cases sequentially, so a scoped reset is enough isolation.
struct ScopedStatisticsReset
{
    ScopedStatisticsReset()
    {
        SqlStatistics::Instance().Reset();
    }
    ScopedStatisticsReset(ScopedStatisticsReset const&) = delete;
    ScopedStatisticsReset& operator=(ScopedStatisticsReset const&) = delete;
    ScopedStatisticsReset(ScopedStatisticsReset&&) = delete;
    ScopedStatisticsReset& operator=(ScopedStatisticsReset&&) = delete;
    ~ScopedStatisticsReset()
    {
        SqlStatistics::Instance().Reset();
    }
};

} // namespace

/// All-fixed-width columns, so DataMapper::CreateAll routes this through the native batch path —
/// a single array-bound SQLExecute, which is what SqlStatisticsOperation::ExecuteBatch measures.
/// (A record carrying a std::string or an optional takes the per-row soft path instead.)
struct StatsBatchRecord
{
    Field<int64_t, PrimaryKey::AutoAssign> id;
    Field<double> value;
    Field<int32_t> count;
};

// ================================================================================================
// Pure aggregation logic — no database required, and meaningful in every build configuration.
// ================================================================================================

TEST_CASE("SqlLatencyHistogram::BucketOf maps values to power-of-two buckets", "[SqlStatistics]")
{
    CHECK(SqlLatencyHistogram::BucketOf(0) == 0);
    CHECK(SqlLatencyHistogram::BucketOf(1) == 1);
    CHECK(SqlLatencyHistogram::BucketOf(2) == 2);
    CHECK(SqlLatencyHistogram::BucketOf(3) == 2);
    CHECK(SqlLatencyHistogram::BucketOf(4) == 3);
    CHECK(SqlLatencyHistogram::BucketOf(7) == 3);
    CHECK(SqlLatencyHistogram::BucketOf(8) == 4);
    CHECK(SqlLatencyHistogram::BucketOf(1023) == 10);
    CHECK(SqlLatencyHistogram::BucketOf(1024) == 11);

    SECTION("very large values saturate into the overflow bucket rather than overrunning the array")
    {
        CHECK(SqlLatencyHistogram::BucketOf((std::numeric_limits<std::uint64_t>::max)())
              == SqlLatencyHistogram::BucketCount - 1);
    }
}

TEST_CASE("SqlLatencyHistogram reports zero for an empty histogram", "[SqlStatistics]")
{
    auto const histogram = SqlLatencyHistogram {};
    CHECK(histogram.count == 0);
    CHECK(histogram.AverageMicroseconds() == 0.0);
    CHECK(histogram.PercentileMicroseconds(0.5) == 0);
    CHECK(histogram.PercentileMicroseconds(0.99) == 0);
}

TEST_CASE("SqlOperationStatistics::Total sums successes and failures", "[SqlStatistics]")
{
    auto stats = SqlOperationStatistics {};
    stats.succeeded = 7;
    stats.failed = 3;
    // A retry is not an extra operation — it is an attribute of one that is already counted.
    stats.retried = 2;
    CHECK(stats.Total() == 10);
}

TEST_CASE("SqlPoolStatistics::ReuseRate", "[SqlStatistics]")
{
    auto pool = SqlPoolStatistics {};
    CHECK(pool.ReuseRate() == 0.0); // nothing acquired yet

    pool.acquired = 4;
    pool.reused = 3;
    CHECK(pool.ReuseRate() == 0.75);
}

TEST_CASE("ToStringView names every operation and rejects out-of-range values", "[SqlStatistics]")
{
    CHECK(ToStringView(SqlStatisticsOperation::Execute) == "Execute");
    CHECK(ToStringView(SqlStatisticsOperation::ExecuteDirect) == "ExecuteDirect");
    CHECK(ToStringView(SqlStatisticsOperation::ExecuteBatch) == "ExecuteBatch");
    CHECK(ToStringView(SqlStatisticsOperation::Prepare) == "Prepare");
    CHECK(ToStringView(SqlStatisticsOperation::Fetch) == "Fetch");
    CHECK(ToStringView(SqlStatisticsOperation::PoolAcquire) == "PoolAcquire");
    CHECK(ToStringView(SqlStatisticsOperation::Count) == "Unknown");
}

// ================================================================================================
// Recording. These only assert non-zero counters in a build that actually collects; in a build
// without LIGHTWEIGHT_ENABLE_STATISTICS the API must still be callable and read back all-zero.
// ================================================================================================

TEST_CASE("SqlStatistics records counts and latency per operation", "[SqlStatistics]")
{
    auto const reset = ScopedStatisticsReset {};
    auto& stats = SqlStatistics::Instance();

    stats.RecordOperation(SqlStatisticsOperation::Execute, 10us, false);
    stats.RecordOperation(SqlStatisticsOperation::Execute, 20us, false);
    stats.RecordOperation(SqlStatisticsOperation::Execute, 30us, true);
    stats.RecordRetry(SqlStatisticsOperation::Execute);

    auto const snapshot = stats.Snapshot();
    auto const& execute = snapshot[SqlStatisticsOperation::Execute];

    if constexpr (SqlStatistics::IsEnabled())
    {
        CHECK(execute.succeeded == 2);
        CHECK(execute.failed == 1);
        CHECK(execute.retried == 1);
        CHECK(execute.Total() == 3);

        CHECK(execute.latency.count == 3);
        CHECK(execute.latency.totalMicroseconds == 60);
        CHECK(execute.latency.minMicroseconds == 10);
        CHECK(execute.latency.maxMicroseconds == 30);
        CHECK(execute.latency.AverageMicroseconds() == 20.0);

        // Power-of-two bucketing rounds up to the bucket's upper bound, but never past the largest
        // sample actually observed.
        CHECK(execute.latency.PercentileMicroseconds(1.0) == 30);
        CHECK(execute.latency.PercentileMicroseconds(0.0) >= 10);

        // Out-of-range and non-finite percentiles are clamped rather than reaching the
        // float-to-integer cast, where NaN would be undefined behaviour.
        CHECK(execute.latency.PercentileMicroseconds(-1.0) == execute.latency.PercentileMicroseconds(0.0));
        CHECK(execute.latency.PercentileMicroseconds(2.0) == 30);
        CHECK(execute.latency.PercentileMicroseconds(std::numeric_limits<double>::quiet_NaN()) <= 30);

        SECTION("other operations are untouched")
        {
            CHECK(snapshot[SqlStatisticsOperation::ExecuteBatch].Total() == 0);
            CHECK(snapshot[SqlStatisticsOperation::Prepare].Total() == 0);
        }
    }
    else
    {
        // Disabled build: the calls compile and run, but nothing is collected.
        CHECK(execute.Total() == 0);
        CHECK(execute.latency.count == 0);
    }
}

TEST_CASE("SqlStatistics::Reset clears every counter", "[SqlStatistics]")
{
    auto const reset = ScopedStatisticsReset {};
    auto& stats = SqlStatistics::Instance();

    stats.RecordOperation(SqlStatisticsOperation::Execute, 5us, false);
    stats.RecordRowsFetched(10, true);
    stats.RecordConnectionOpened();
    stats.RecordPoolAcquire(1us, true, true);
    stats.RecordPoolRelease(true);
    stats.RecordPoolOccupancy(3, 4);

    stats.Reset();
    auto const snapshot = stats.Snapshot();

    CHECK(snapshot[SqlStatisticsOperation::Execute].Total() == 0);
    CHECK(snapshot[SqlStatisticsOperation::Execute].latency.count == 0);
    CHECK(snapshot[SqlStatisticsOperation::Execute].latency.minMicroseconds == 0);
    CHECK(snapshot.rowsFetched == 0);
    CHECK(snapshot.blockFetches == 0);
    CHECK(snapshot.connectionsOpened == 0);
    CHECK(snapshot.pool.acquired == 0);
    CHECK(snapshot.pool.released == 0);
    CHECK(snapshot.pool.discarded == 0);
    CHECK(snapshot.pool.idle == 0);
    CHECK(snapshot.pool.checkedOut == 0);
    CHECK(snapshot.pool.waitLatency.count == 0);
}

TEST_CASE("SqlStatistics records pool acquire/release cycles", "[SqlStatistics]")
{
    auto const reset = ScopedStatisticsReset {};
    auto& stats = SqlStatistics::Instance();

    stats.RecordPoolAcquire(0us, true, false);  // served from idle, no wait
    stats.RecordPoolAcquire(0us, false, false); // freshly created, no wait
    stats.RecordPoolAcquire(500us, true, true); // blocked until one came back
    stats.RecordPoolRelease(false);
    stats.RecordPoolRelease(true); // over capacity: destroyed rather than idled
    stats.RecordPoolOccupancy(2, 5);

    auto const snapshot = stats.Snapshot();

    if constexpr (SqlStatistics::IsEnabled())
    {
        CHECK(snapshot.pool.acquired == 3);
        CHECK(snapshot.pool.reused == 2);
        CHECK(snapshot.pool.waited == 1);
        CHECK(snapshot.pool.released == 2);
        CHECK(snapshot.pool.discarded == 1);
        CHECK(snapshot.pool.idle == 2);
        CHECK(snapshot.pool.checkedOut == 5);

        // Only the acquisition that actually blocked contributes a wait sample; otherwise the
        // distribution would be swamped by zeros from the fast path.
        CHECK(snapshot.pool.waitLatency.count == 1);
        CHECK(snapshot.pool.waitLatency.maxMicroseconds == 500);

        CHECK(snapshot.pool.ReuseRate() == Catch::Approx(2.0 / 3.0));

        // Every acquisition, waited or not, lands in the PoolAcquire operation slot.
        CHECK(snapshot[SqlStatisticsOperation::PoolAcquire].Total() == 3);
    }
    else
    {
        CHECK(snapshot.pool.acquired == 0);
    }
}

TEST_CASE("SqlStatistics separates row counts from block round-trips", "[SqlStatistics]")
{
    auto const reset = ScopedStatisticsReset {};
    auto& stats = SqlStatistics::Instance();

    stats.RecordRowsFetched(64, true); // one block-prefetch round-trip yielding 64 rows
    stats.RecordRowsFetched(1, false); // one row-at-a-time fetch
    stats.RecordRowsFetched(1, false);

    auto const snapshot = stats.Snapshot();

    if constexpr (SqlStatistics::IsEnabled())
    {
        CHECK(snapshot.rowsFetched == 66);
        // The point of the distinction: 66 rows cost only 1 block round-trip, not 66.
        CHECK(snapshot.blockFetches == 1);
    }
    else
    {
        CHECK(snapshot.rowsFetched == 0);
    }
}

TEST_CASE("SqlStatisticsScope classifies a throwing region as failed", "[SqlStatistics]")
{
    auto const reset = ScopedStatisticsReset {};

    // Both scopes exist only for their destructors, and a statistics-disabled build compiles them
    // down to an empty object - hence `[[maybe_unused]]` rather than an unread local.
    {
        [[maybe_unused]] auto const scope = SqlStatisticsScope { SqlStatisticsOperation::Execute };
    }

    // Expressed through Catch2 rather than a bare try/catch: the exception has to escape the scope
    // for it to observe the failure, and an empty `catch` block states nothing about that intent.
    CHECK_THROWS_AS(
        [] {
            [[maybe_unused]] auto const scope = SqlStatisticsScope { SqlStatisticsOperation::Execute };
            throw std::runtime_error { "boom" };
        }(),
        std::runtime_error);

    if constexpr (SqlStatistics::IsEnabled())
    {
        // The scope infers failure from an in-flight exception, so no call site needs to say so.
        auto const snapshot = SqlStatistics::Instance().Snapshot();
        CHECK(snapshot[SqlStatisticsOperation::Execute].succeeded == 1);
        CHECK(snapshot[SqlStatisticsOperation::Execute].failed == 1);
    }
}

TEST_CASE("SqlStatistics recording is safe from multiple threads", "[SqlStatistics]")
{
    auto const reset = ScopedStatisticsReset {};
    auto& stats = SqlStatistics::Instance();

    constexpr auto ThreadCount = std::size_t { 8 };
    constexpr auto PerThread = std::size_t { 1000 };

    auto threads = std::vector<std::jthread> {};
    for (auto const threadIndex: std::views::iota(std::size_t { 0 }, ThreadCount))
    {
        threads.emplace_back([&stats, threadIndex, PerThread = PerThread] {
            for (auto const iteration: std::views::iota(std::size_t { 0 }, PerThread))
            {
                // Spread values across buckets so min/max race on real contention.
                auto const microseconds = std::chrono::microseconds { 1 + (((threadIndex * PerThread) + iteration) % 997) };
                stats.RecordOperation(SqlStatisticsOperation::Execute, microseconds, false);
            }
        });
    }
    threads.clear(); // join

    if constexpr (SqlStatistics::IsEnabled())
    {
        auto const snapshot = stats.Snapshot();
        CHECK(snapshot[SqlStatisticsOperation::Execute].succeeded == ThreadCount * PerThread);
        CHECK(snapshot[SqlStatisticsOperation::Execute].latency.count == ThreadCount * PerThread);
        CHECK(snapshot[SqlStatisticsOperation::Execute].latency.minMicroseconds >= 1);
        CHECK(snapshot[SqlStatisticsOperation::Execute].latency.maxMicroseconds <= 997);
    }
}

// ================================================================================================
// End-to-end: the library's own instrumentation, driven through a real database connection.
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlStatistics captures Execute and fetch through a real query", "[SqlStatistics]")
{
    if constexpr (!SqlStatistics::IsEnabled())
        SUCCEED("Statistics collection disabled in this build");
    else
    {
        auto const reset = ScopedStatisticsReset {};
        auto& stats = SqlStatistics::Instance();

        auto dm = DataMapper {};
        dm.CreateTable<Person>();

        auto person = Person {};
        person.name = "John";
        person.is_active = true;
        dm.Create(person);

        auto const all = dm.Query<Person>().All();
        CHECK(all.size() == 1);

        auto const snapshot = stats.Snapshot();

        // CreateTable + Create + the SELECT all go through prepared or direct execution.
        CHECK((snapshot[SqlStatisticsOperation::Execute].Total() + snapshot[SqlStatisticsOperation::ExecuteDirect].Total())
              > 0);
        CHECK(snapshot[SqlStatisticsOperation::Prepare].Total() > 0);
        CHECK(snapshot.rowsFetched > 0);

        // Latency is recorded for whatever executed, and a real round-trip is never negative.
        auto const& execute = snapshot[SqlStatisticsOperation::Execute];
        if (execute.Total() > 0)
        {
            CHECK(execute.latency.count == execute.Total());
            CHECK(execute.latency.maxMicroseconds >= execute.latency.minMicroseconds);
        }
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlStatistics counts each fetched row exactly once", "[SqlStatistics]")
{
    if constexpr (!SqlStatistics::IsEnabled())
        SUCCEED("Statistics collection disabled in this build");
    else
    {
        auto const reset = ScopedStatisticsReset {};
        auto& stats = SqlStatistics::Instance();

        constexpr auto RowCount = std::size_t { 40 };

        auto setup = SqlStatement {};
        setup.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.CreateTable("StatsRows").PrimaryKey("Id", SqlColumnTypeDefinitions::Bigint {});
        });
        setup.Prepare(setup.Query("StatsRows").Insert().Set("Id", SqlWildcard));
        for (auto const index: std::views::iota(std::size_t { 1 }, RowCount + 1))
            (void) setup.Execute(static_cast<std::int64_t>(index));

        auto const readAll = [](SqlStatement& stmt) {
            auto rows = std::size_t { 0 };
            auto cursor = stmt.ExecuteDirect(R"(SELECT "Id" FROM "StatsRows")");
            while (cursor.FetchRow())
                ++rows;
            return rows;
        };

        // Block-prefetch path: a depth well below RowCount forces several SQLFetchScroll round-trips
        // plus the terminating empty one. Each row must be counted once — it used to be counted both
        // as part of its block and again when handed out, reporting exactly twice the real count.
        auto prefetching = SqlStatement {};
        prefetching.Connection().SetDefaultPrefetchDepth(8);
        stats.Reset();
        CHECK(readAll(prefetching) == RowCount);
        CHECK(stats.Snapshot().rowsFetched == RowCount);

        // Per-row path (prefetch off), as the reference: same rows, same count.
        auto perRow = SqlStatement {};
        perRow.Connection().SetDefaultPrefetchDepth(1);
        stats.Reset();
        CHECK(readAll(perRow) == RowCount);
        CHECK(stats.Snapshot().rowsFetched == RowCount);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlStatistics counts a failing statement as failed", "[SqlStatistics]")
{
    if constexpr (!SqlStatistics::IsEnabled())
        SUCCEED("Statistics collection disabled in this build");
    else
    {
        auto const reset = ScopedStatisticsReset {};
        auto& stats = SqlStatistics::Instance();

        auto stmt = SqlStatement {};
        CHECK_THROWS_AS(stmt.ExecuteDirect("SELECT * FROM a_table_that_does_not_exist"), SqlException);

        auto const snapshot = stats.Snapshot();
        // The failure is attributed to the operation, and it still contributes a latency sample.
        CHECK(snapshot[SqlStatisticsOperation::ExecuteDirect].failed >= 1);
        CHECK(snapshot[SqlStatisticsOperation::ExecuteDirect].latency.count >= 1);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlStatistics captures ExecuteBatch", "[SqlStatistics]")
{
    if constexpr (!SqlStatistics::IsEnabled())
        SUCCEED("Statistics collection disabled in this build");
    else
    {
        auto const reset = ScopedStatisticsReset {};
        auto& stats = SqlStatistics::Instance();

        auto dm = DataMapper {};
        dm.CreateTable<StatsBatchRecord>();

        auto const before = stats.Snapshot()[SqlStatisticsOperation::ExecuteBatch].Total();

        // StatsBatchRecord is all fixed-width, so CreateAll takes the native array-bound batch path
        // rather than falling back to one Execute per row.
        auto records = std::vector<StatsBatchRecord> {};
        for (auto const i: std::views::iota(1, 6))
            records.push_back({ .id = i, .value = i * 1.5, .count = i * 10 });
        dm.CreateAll(records);

        auto const after = stats.Snapshot()[SqlStatisticsOperation::ExecuteBatch].Total();
        CHECK(after > before);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlStatistics captures pool acquire/release cycles", "[SqlStatistics]")
{
    if constexpr (!SqlStatistics::IsEnabled())
        SUCCEED("Statistics collection disabled in this build");
    else
    {
        auto const reset = ScopedStatisticsReset {};
        auto& stats = SqlStatistics::Instance();

        {
            auto pool = Pool<PoolConfig { .initialSize = 2, .maxSize = 4 }> {};

            {
                auto first = pool.Acquire();
                auto second = pool.Acquire();
                (void) first;
                (void) second;

                auto const midpoint = stats.Snapshot();
                CHECK(midpoint.pool.acquired >= 2);
                // Both acquisitions came out of the two pre-created idle mappers, which leaves the
                // pool with nothing idle — a real assertion, unlike `checkedOut >= 0` on an unsigned.
                CHECK(midpoint.pool.reused >= 2);
                CHECK(midpoint.pool.idle == 0);
            }

            // Both mappers are back; acquiring again must reuse rather than create.
            auto const reusedBefore = stats.Snapshot().pool.reused;
            {
                auto third = pool.Acquire();
                (void) third;
            }
            auto const snapshot = stats.Snapshot();
            CHECK(snapshot.pool.reused > reusedBefore);
            CHECK(snapshot.pool.released >= 2);
            CHECK(snapshot.pool.ReuseRate() > 0.0);
        }
    }
}
