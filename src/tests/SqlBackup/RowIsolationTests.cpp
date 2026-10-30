// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup/RowIsolation.hpp>
#include <Lightweight/SqlError.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Lightweight;
using namespace Lightweight::detail;

namespace
{

/// Fake inserter: a range fails iff it contains a row of `bad`. Records every call and how
/// often each row was committed.
struct FakeTable
{
    FakeTable(std::size_t rows, std::set<std::size_t> badRows):
        rowCount { rows },
        bad { std::move(badRows) }
    {
    }

    std::size_t rowCount;
    std::set<std::size_t> bad;
    bool reportFailedRows = false;
    /// 1-based call number on which the inserter fails with a transient error, if any.
    std::optional<std::size_t> transientOnCall;

    std::vector<std::pair<std::size_t, std::size_t>> calls;
    std::vector<int> insertCount = std::vector<int>(rowCount, 0);
    std::vector<std::size_t> failedRows;
    std::vector<std::size_t> excludedRows;
    std::vector<std::size_t> settled;

    static SqlErrorInfo ConstraintError()
    {
        return SqlErrorInfo { .nativeErrorCode = 1, .sqlState = "23000", .message = "constraint violated" };
    }

    static SqlErrorInfo TransientError()
    {
        return SqlErrorInfo { .nativeErrorCode = 2, .sqlState = "08S01", .message = "link failure" };
    }

    [[nodiscard]] std::size_t CommittedPrefix() const
    {
        auto const rows = std::views::iota(std::size_t { 0 }, rowCount);
        auto const firstUnsettled = std::ranges::find_if(rows, [&](std::size_t row) { return !IsSettled(row); });
        return static_cast<std::size_t>(std::ranges::distance(rows.begin(), firstUnsettled));
    }

    [[nodiscard]] bool IsSettled(std::size_t row) const
    {
        return insertCount[row] > 0 || std::ranges::contains(failedRows, row) || std::ranges::contains(excludedRows, row);
    }

    RangeInserter Inserter()
    {
        return [this](std::size_t firstRow, std::size_t count) -> std::expected<void, BatchInsertError> {
            REQUIRE(count > 0);
            REQUIRE(firstRow + count <= rowCount);
            calls.emplace_back(firstRow, count);
            if (transientOnCall && calls.size() == *transientOnCall)
                return std::unexpected(BatchInsertError { .error = TransientError(), .reportedFailedRows = {} });
            auto const range = std::views::iota(firstRow, firstRow + count);
            auto const failing = range | std::views::filter([&](std::size_t row) { return bad.contains(row); })
                                 | std::ranges::to<std::vector>();
            if (!failing.empty())
                return std::unexpected(BatchInsertError {
                    .error = ConstraintError(),
                    .reportedFailedRows = reportFailedRows ? failing : std::vector<std::size_t> {},
                });
            for (auto const row: range)
                ++insertCount[row];
            return {};
        };
    }

    static FailureClassifier Classifier(FailureDisposition constraintDisposition = FailureDisposition::IsolateRows)
    {
        return [constraintDisposition](SqlErrorInfo const& error) {
            return error.sqlState == "08S01" ? FailureDisposition::Propagate : constraintDisposition;
        };
    }

    void Run(std::vector<bool> const& excluded, FailureDisposition constraintDisposition = FailureDisposition::IsolateRows)
    {
        InsertIsolatingFailures(
            rowCount,
            excluded,
            Inserter(),
            Classifier(constraintDisposition),
            [this](std::size_t row, SqlErrorInfo const& error) {
                CHECK(error.sqlState == "23000");
                failedRows.push_back(row);
            },
            [this](std::size_t row) {
                // Rows are settled in ascending order: nothing beyond an excluded row is inserted yet.
                CHECK(std::ranges::none_of(std::views::iota(row, rowCount),
                                           [&](std::size_t later) { return insertCount[later] > 0; }));
                excludedRows.push_back(row);
            },
            [this](std::size_t settledPrefix) {
                CHECK(settledPrefix <= CommittedPrefix());
                settled.push_back(settledPrefix);
            });
    }

    void Run()
    {
        Run(std::vector<bool>(rowCount, false));
    }

    void CheckEveryGoodRowInsertedOnce(std::vector<bool> const& excluded) const
    {
        for (auto const row: std::views::iota(std::size_t { 0 }, rowCount))
        {
            INFO("row " << row);
            auto const expected = (bad.contains(row) || excluded[row]) ? 0 : 1;
            CHECK(insertCount[row] == expected);
        }
    }
};

std::vector<bool> ExcludedSet(std::size_t rowCount, std::initializer_list<std::size_t> rows)
{
    auto excluded = std::vector<bool>(rowCount, false);
    for (auto const row: rows)
        excluded[row] = true;
    return excluded;
}

} // namespace

TEST_CASE("RowIsolation: no bad rows inserts everything in one call", "[RowIsolation]")
{
    auto table = FakeTable(16, {});
    table.Run();
    REQUIRE(table.calls.size() == 1);
    CHECK(table.calls[0] == std::pair<std::size_t, std::size_t> { 0, 16 });
    CHECK(table.failedRows.empty());
    CHECK(table.excludedRows.empty());
    REQUIRE(!table.settled.empty());
    CHECK(table.settled.back() == 16);
    table.CheckEveryGoodRowInsertedOnce(std::vector<bool>(16, false));
}

TEST_CASE("RowIsolation: bad rows are isolated and reported exactly", "[RowIsolation]")
{
    constexpr std::size_t N = 16;
    auto const allRows = std::views::iota(std::size_t { 0 }, N) | std::ranges::to<std::set>();
    auto const badSet =
        GENERATE_COPY(std::set<std::size_t> { 0 }, std::set<std::size_t> { N - 1 }, std::set<std::size_t> { 5, 6 }, allRows);
    auto table = FakeTable(N, badSet);
    table.Run();

    auto const reported = std::set<std::size_t>(table.failedRows.begin(), table.failedRows.end());
    CHECK(reported == badSet);
    CHECK(table.failedRows.size() == badSet.size());
    CHECK(std::ranges::is_sorted(table.failedRows));
    table.CheckEveryGoodRowInsertedOnce(std::vector<bool>(N, false));
    REQUIRE(!table.settled.empty());
    CHECK(table.settled.back() == N);
}

TEST_CASE("RowIsolation: excluded rows split the runs and are never inserted", "[RowIsolation]")
{
    constexpr std::size_t N = 10;
    auto const excluded = ExcludedSet(N, { 3 });
    auto table = FakeTable(N, {});
    table.Run(excluded);

    REQUIRE(table.calls.size() == 2);
    CHECK(table.calls[0] == std::pair<std::size_t, std::size_t> { 0, 3 });
    CHECK(table.calls[1] == std::pair<std::size_t, std::size_t> { 4, N - 4 });
    CHECK(table.insertCount[3] == 0);
    CHECK(table.failedRows.empty());
    CHECK(table.excludedRows == std::vector<std::size_t> { 3 });
    table.CheckEveryGoodRowInsertedOnce(excluded);
}

TEST_CASE("RowIsolation: all rows excluded issues no insert", "[RowIsolation]")
{
    constexpr std::size_t N = 5;
    auto table = FakeTable(N, {});
    table.Run(std::vector<bool>(N, true));
    CHECK(table.calls.empty());
    CHECK(table.excludedRows == std::vector<std::size_t> { 0, 1, 2, 3, 4 });
    REQUIRE(!table.settled.empty());
    CHECK(table.settled.back() == N);
}

TEST_CASE("RowIsolation: zero rows issues no insert", "[RowIsolation]")
{
    auto table = FakeTable(0, {});
    table.Run();
    CHECK(table.calls.empty());
}

TEST_CASE("RowIsolation: driver-reported failed rows skip bisection", "[RowIsolation]")
{
    constexpr std::size_t N = 16;
    auto table = FakeTable(N, { 7 });
    table.reportFailedRows = true;
    table.Run();
    CHECK(table.failedRows == std::vector<std::size_t> { 7 });
    CHECK(table.calls.size() <= 3);
    table.CheckEveryGoodRowInsertedOnce(std::vector<bool>(N, false));
}

TEST_CASE("RowIsolation: reported rows outside the run fall back to bisection", "[RowIsolation]")
{
    constexpr std::size_t N = 8;
    auto table = FakeTable(N, { 2 });
    InsertIsolatingFailures(
        N,
        std::vector<bool>(N, false),
        [&](std::size_t firstRow, std::size_t count) -> std::expected<void, BatchInsertError> {
            auto result = table.Inserter()(firstRow, count);
            if (!result)
                result.error().reportedFailedRows = { 100 };
            return result;
        },
        FakeTable::Classifier(),
        [&](std::size_t row, SqlErrorInfo const&) { table.failedRows.push_back(row); },
        [](std::size_t) {},
        [](std::size_t) {});
    CHECK(table.failedRows == std::vector<std::size_t> { 2 });
    table.CheckEveryGoodRowInsertedOnce(std::vector<bool>(N, false));
}

TEST_CASE("RowIsolation: Propagate disposition throws SqlException", "[RowIsolation]")
{
    auto table = FakeTable(8, {});
    table.transientOnCall = 1;
    CHECK_THROWS_AS(table.Run(), SqlException);
}

TEST_CASE("RowIsolation: FailTable disposition throws TableRestoreFailure", "[RowIsolation]")
{
    auto table = FakeTable(8, { 4 });
    try
    {
        table.Run(std::vector<bool>(8, false), FailureDisposition::FailTable);
        FAIL("expected TableRestoreFailure");
    }
    catch (TableRestoreFailure const& failure)
    {
        CHECK(failure.error.sqlState == "23000");
        auto const what = std::string(failure.what());
        CHECK(what.contains("23000"));
        CHECK(what.contains("constraint violated"));
    }
    CHECK(table.calls.size() == 1);
}

TEST_CASE("RowIsolation: one bad row in a large batch needs logarithmic calls", "[RowIsolation]")
{
    constexpr std::size_t N = 4000;
    constexpr std::size_t log2Ceil = 12; // 2^12 = 4096 >= 4000
    auto const badRow = GENERATE(std::size_t { 0 }, std::size_t { 1234 }, std::size_t { N - 1 });
    auto table = FakeTable(N, { badRow });
    table.Run();
    CHECK(table.failedRows == std::vector<std::size_t> { badRow });
    CHECK(table.calls.size() <= (2 * log2Ceil) + 1);
    table.CheckEveryGoodRowInsertedOnce(std::vector<bool>(N, false));
}

TEST_CASE("RowIsolation: settled watermark is ordered and gap-free", "[RowIsolation]")
{
    constexpr std::size_t N = 10;
    auto const excluded = ExcludedSet(N, { 3 });
    auto table = FakeTable(N, { 6 });
    table.Run(excluded);

    REQUIRE(!table.settled.empty());
    CHECK(std::ranges::is_sorted(table.settled));
    CHECK(table.settled.back() == N);
    CHECK(table.excludedRows == std::vector<std::size_t> { 3 });
    CHECK(table.failedRows == std::vector<std::size_t> { 6 });
    table.CheckEveryGoodRowInsertedOnce(excluded);
    // Each settled value was checked against the gap-free prefix at the moment it was reported.
}

TEST_CASE("RowIsolation: transient error mid-bisection keeps the watermark exact", "[RowIsolation]")
{
    constexpr std::size_t N = 10;
    auto table = FakeTable(N, { 6 });
    table.transientOnCall = 3;
    CHECK_THROWS_AS(table.Run(), SqlException);

    // Calls: (0,10) fails, (0,5) commits, third call hits the transient error.
    REQUIRE(table.calls.size() == 3);
    CHECK(table.calls[1] == std::pair<std::size_t, std::size_t> { 0, 5 });
    REQUIRE(!table.settled.empty());
    CHECK(table.settled.back() == 5);
    CHECK(table.settled.back() == table.CommittedPrefix());
    CHECK(std::ranges::all_of(std::views::iota(std::size_t { 5 }, N),
                              [&](std::size_t row) { return table.insertCount[row] == 0; }));
    CHECK(table.failedRows.empty());
}
