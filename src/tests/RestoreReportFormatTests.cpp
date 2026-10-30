// SPDX-License-Identifier: Apache-2.0

#include "../tools/shared/RestoreReportFormat.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace Lightweight;
using namespace Lightweight::SqlBackup;

namespace
{

TableRestoreReport MakeTableReport()
{
    auto report = TableRestoreReport {};
    report.tableName = "Orders";
    report.failedRowCount = 12;
    for (auto const first: { 1U, 10U, 20U, 30U, 40U, 50U, 60U })
        report.failedRowRanges.push_back({ .chunkPath = "data/Orders/chunk_0", .firstRow = first, .lastRow = first + 1 });
    report.failedRowRanges.push_back({ .chunkPath = "data/Orders/chunk_1", .firstRow = 7, .lastRow = 7 });
    report.rowsBeyondRangeCap = 4;
    report.reasons.push_back(
        { .sqlState = "23000", .message = "duplicate key", .occurrences = 12, .sampleKeys = { "1", "2" } });
    report.valueFailures.push_back({ .column = "Note", .cellsSetToNull = 3, .sampleReason = "bad text" });
    report.valueFailures.push_back({ .column = "Qty", .cellsSetToNull = 2, .sampleReason = "bad number" });
    report.unreadableChunks = 1;
    return report;
}

bool Contains(std::vector<std::string> const& lines, std::string_view needle)
{
    return std::ranges::any_of(lines, [&](std::string const& line) { return line.contains(needle); });
}

} // namespace

TEST_CASE("RestoreReportFormat: table details list at most five ranges and say how many more", "[RestoreReportFormat]")
{
    auto const lines = Tools::DescribeTableRestoreFailures(MakeTableReport());

    CHECK(Contains(lines, "data/Orders/chunk_0 rows 1–2"));
    CHECK(Contains(lines, "data/Orders/chunk_0 rows 40–41"));
    CHECK_FALSE(Contains(lines, "rows 50–51"));
    CHECK(Contains(lines, "... and 3 more row ranges"));
    CHECK(Contains(lines, "... and 4 more failed rows not listed"));
    CHECK(Contains(lines, "12 x 23000 duplicate key (e.g. key 1, 2)"));
    CHECK(Contains(lines, "column Note: 3 cells set to NULL (bad text)"));
    CHECK(Contains(lines, "1 chunks unreadable"));
}

TEST_CASE("RestoreReportFormat: a single failed row reads as a row, not a range", "[RestoreReportFormat]")
{
    auto report = TableRestoreReport {};
    report.failedRowCount = 1;
    report.failedRowRanges.push_back({ .chunkPath = "c", .firstRow = 7, .lastRow = 7 });
    CHECK(Tools::DescribeTableRestoreFailures(report) == std::vector<std::string> { "c row 7" });
}

TEST_CASE("RestoreReportFormat: a table failure is reported", "[RestoreReportFormat]")
{
    auto report = TableRestoreReport {};
    report.tableFailure = "HYC00 unsupported column type";
    CHECK(Tools::DescribeTableRestoreFailures(report)
          == std::vector<std::string> { "table failed: HYC00 unsupported column type" });
}

TEST_CASE("RestoreReportFormat: summary counts rows and nulled cells", "[RestoreReportFormat]")
{
    auto const report = MakeTableReport();
    CHECK(Tools::CountNulledCells(report) == 5);
    CHECK(Tools::SummarizeTableRestoreFailures(report) == "12 rows failed, 5 cells set to NULL");
}

TEST_CASE("RestoreReportFormat: a clean report renders nothing", "[RestoreReportFormat]")
{
    CHECK(Tools::DescribeRestoreReport({}).empty());
}

TEST_CASE("RestoreReportFormat: the restore block names tables, run-level problems and totals", "[RestoreReportFormat]")
{
    auto report = RestoreReport {};
    report.tables = { MakeTableReport() };
    report.totalFailedRows = 12;
    report.workerStopped = true;
    report.incompleteTables = { "Lines" };
    report.unattributedChunkErrors = 2;
    auto const lines = Tools::DescribeRestoreReport(report);

    REQUIRE_FALSE(lines.empty());
    CHECK(lines.front() == "Restore failures:");
    CHECK(Contains(lines, "Orders: 12 rows failed, 5 cells set to NULL"));
    CHECK(Contains(lines, "worker stopped early"));
    CHECK(Contains(lines, "incomplete: Lines"));
    CHECK(Contains(lines, "2 unreadable archive entries"));
    CHECK(lines.back() == "Total: 12 rows failed in 1 tables, 5 cells set to NULL");
}
