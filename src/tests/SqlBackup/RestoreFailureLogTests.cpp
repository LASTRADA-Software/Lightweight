// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup/RestoreFailureLog.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace Lightweight::SqlBackup;
using Lightweight::SqlBackup::detail::RestoreFailureLog;

TEST_CASE("RestoreFailureLog: adjacent rows of one chunk merge into ranges", "[RestoreFailureLog]")
{
    RestoreFailureLog log;
    for (std::uint64_t const row: { 1ULL, 2ULL, 3ULL, 7ULL })
        log.RecordRowFailure("T", "A", row, "23000", "boom", "k");
    auto const report = log.TableSnapshot("T");
    REQUIRE(report.failedRowRanges.size() == 2);
    CHECK(report.failedRowRanges[0].chunkPath == "A");
    CHECK(report.failedRowRanges[0].firstRow == 1);
    CHECK(report.failedRowRanges[0].lastRow == 3);
    CHECK(report.failedRowRanges[1].firstRow == 7);
    CHECK(report.failedRowRanges[1].lastRow == 7);
    CHECK(report.failedRowCount == 4);
    CHECK(report.HasFailures());
}

TEST_CASE("RestoreFailureLog: same row numbers in different chunks do not merge", "[RestoreFailureLog]")
{
    RestoreFailureLog log;
    log.RecordRowFailure("T", "A", 1, "23000", "boom", "k");
    log.RecordRowFailure("T", "B", 2, "23000", "boom", "k");
    log.RecordRowFailure("T", "A", 2, "23000", "boom", "k");
    auto const report = log.TableSnapshot("T");
    REQUIRE(report.failedRowRanges.size() == 2);
    CHECK(report.failedRowRanges[0].lastRow == 2);
    CHECK(report.failedRowRanges[1].chunkPath == "B");
}

TEST_CASE("RestoreFailureLog: row ranges are capped", "[RestoreFailureLog]")
{
    RestoreFailureLog log;
    for (std::uint64_t row = 0; row < 250; ++row)
        log.RecordRowFailure("T", "A", (row * 2) + 1, "23000", "boom", "k");
    auto const report = log.TableSnapshot("T");
    CHECK(report.failedRowRanges.size() == MaxReportedRowRanges);
    CHECK(report.rowsBeyondRangeCap == 150);
    CHECK(report.failedRowCount == 250);
}

TEST_CASE("RestoreFailureLog: reasons are deduplicated and capped", "[RestoreFailureLog]")
{
    RestoreFailureLog log;
    log.RecordRowFailure("T", "A", 1, "23000", "row 12 failed", "k1");
    log.RecordRowFailure("T", "A", 3, "23000", "row 99 failed", "k2");
    {
        auto const report = log.TableSnapshot("T");
        REQUIRE(report.reasons.size() == 1);
        CHECK(report.reasons[0].occurrences == 2);
    }

    RestoreFailureLog many;
    for (std::uint64_t i = 0; i < 25; ++i)
        many.RecordRowFailure("T", "A", (i * 2) + 1, "23000", "distinct message " + std::string(i + 1, 'x'), "k");
    auto const report = many.TableSnapshot("T");
    CHECK(report.reasons.size() == MaxReportedReasons);
    CHECK(report.occurrencesBeyondReasonCap == 5);

    RestoreFailureLog samples;
    for (std::uint64_t i = 0; i < 10; ++i)
        samples.RecordRowFailure("T", "A", (i * 2) + 1, "23000", "same", "key" + std::to_string(i));
    auto const sampled = samples.TableSnapshot("T");
    REQUIRE(sampled.reasons.size() == 1);
    CHECK(sampled.reasons[0].sampleKeys.size() == MaxSampleKeysPerReason);
    CHECK(sampled.reasons[0].occurrences == 10);
}

TEST_CASE("RestoreFailureLog: nulled cells aggregate per column", "[RestoreFailureLog]")
{
    RestoreFailureLog log;
    log.RecordNulledCell("T", "c", "first reason");
    log.RecordNulledCell("T", "c", "second reason");
    auto const report = log.TableSnapshot("T");
    REQUIRE(report.valueFailures.size() == 1);
    CHECK(report.valueFailures[0].column == "c");
    CHECK(report.valueFailures[0].cellsSetToNull == 2);
    CHECK(report.valueFailures[0].sampleReason == "first reason");
    CHECK(report.HasFailures());
}

TEST_CASE("RestoreFailureLog: table failure", "[RestoreFailureLog]")
{
    RestoreFailureLog log;
    log.RecordTableFailure("T", "HYC00 not supported", 1000);
    log.RecordTableFailure("T", "later", 5);
    auto const report = log.TableSnapshot("T");
    REQUIRE(report.tableFailure.has_value());
    CHECK(report.tableFailure == "HYC00 not supported");
    CHECK(report.failedRowCount == 1005);
}

TEST_CASE("RestoreFailureLog: a failed table's count becomes its missing rows", "[RestoreFailureLog]")
{
    RestoreFailureLog log;
    log.RecordRowFailure("T", "data/T/chunk_1.msgpack", 2, "23000", "duplicate", "2");
    log.RecordTableFailure("T", "HYC00 not supported", 0);
    log.SetFailedTableRowCount("T", 7);
    log.SetFailedTableRowCount("T", 7); // idempotent
    CHECK(log.TableSnapshot("T").failedRowCount == 7);
    CHECK(log.Snapshot().totalFailedRows == 7);

    // A table that did not fail as a whole keeps the count of its failed rows.
    log.RecordRowFailure("U", "data/U/chunk_1.msgpack", 1, "23000", "duplicate", "1");
    log.SetFailedTableRowCount("U", 50);
    CHECK(log.TableSnapshot("U").failedRowCount == 1);
}

TEST_CASE("RestoreFailureLog: concurrent recording", "[RestoreFailureLog]")
{
    RestoreFailureLog log;
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (std::uint64_t t = 0; t < 8; ++t)
        threads.emplace_back([&log, t] {
            for (std::uint64_t i = 0; i < 10'000; ++i)
                log.RecordRowFailure("T", "chunk" + std::to_string(t), i + 1, "23000", "boom", "k");
        });
    for (auto& thread: threads)
        thread.join();
    CHECK(log.TableSnapshot("T").failedRowCount == 80'000);
}

TEST_CASE("RestoreFailureLog: snapshot is sorted and totals", "[RestoreFailureLog]")
{
    RestoreFailureLog log;
    CHECK_FALSE(log.Snapshot().HasFailures());
    CHECK_FALSE(log.TableSnapshot("none").HasFailures());
    log.RecordRowFailure("Zed", "A", 1, "s", "m", "k");
    log.RecordRowFailure("Alpha", "A", 1, "s", "m", "k");
    log.RecordRowFailure("Alpha", "A", 5, "s", "m", "k");
    auto const report = log.Snapshot();
    REQUIRE(report.tables.size() == 2);
    CHECK(report.tables[0].tableName == "Alpha");
    CHECK(report.tables[1].tableName == "Zed");
    CHECK(report.totalFailedRows == 3);
    CHECK(report.HasFailures());
}

TEST_CASE("RestoreFailureLog: unattributed unreadable chunk", "[RestoreFailureLog]")
{
    RestoreFailureLog log;
    log.RecordUnreadableChunk("");
    auto const report = log.Snapshot();
    CHECK(report.unattributedChunkErrors == 1);
    CHECK(report.HasFailures());
    CHECK(report.tables.empty());

    log.RecordUnreadableChunk("T");
    CHECK(log.TableSnapshot("T").unreadableChunks == 1);
    CHECK(log.TableSnapshot("T").HasFailures());
}
