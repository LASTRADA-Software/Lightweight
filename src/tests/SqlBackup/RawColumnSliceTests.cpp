// SPDX-License-Identifier: Apache-2.0

#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup/BatchManager.hpp>
#include <Lightweight/SqlBackup/RawColumnSlice.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace Lightweight;
using namespace Lightweight::detail;
using namespace Lightweight::SqlBackup;
using namespace Lightweight::SqlBackup::Tests;

namespace
{

/// Describes every cell of @p column.
std::vector<std::string> DescribeAll(SqlRawColumn const& column)
{
    std::vector<std::string> cells;
    for (auto const row: std::views::iota(size_t { 0 }, column.indicators.size()))
        cells.push_back(DescribeRawCell(column, row));
    return cells;
}

/// Builds a one-column batch from @p rows and returns the description of every cell of the full
/// column, checking on the way that SliceRows(1, 2) yields the cells of rows 1 and 2.
std::vector<std::string> DescribeAndSlice(SqlColumnTypeDefinition const& type,
                                          std::vector<std::vector<BackupValue>> const& rows,
                                          SqlServerType serverType = SqlServerType::UNKNOWN)
{
    std::vector<SqlColumnDeclaration> const cols = { { .name = "c", .type = type } };
    std::vector<std::string> all;
    bool executed = false;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        executed = true;
        REQUIRE(rawCols.size() == 1);
        REQUIRE(count == rows.size());
        all = DescribeAll(rawCols[0]);

        auto const slice = SliceRows(rawCols[0], 1, 2);
        CHECK(slice.indicators.size() == 2);
        CHECK(slice.metadata.cType == rawCols[0].metadata.cType);
        CHECK(slice.metadata.bufferLength == rawCols[0].metadata.bufferLength);
        CHECK(slice.data.size() == 2 * rawCols[0].metadata.bufferLength);
        CHECK(DescribeAll(slice) == std::vector<std::string> { all[1], all[2] });
    };
    RunBatchManagerTest(executor, cols, rows, 10, serverType);
    REQUIRE(executed);
    return all;
}

} // namespace

TEST_CASE("RawColumnSlice: int64 column", "[RawColumnSlice]")
{
    auto const cells =
        DescribeAndSlice(SqlColumnTypeDefinitions::Bigint {},
                         { { int64_t { 7 } }, { int64_t { 42 } }, { std::monostate {} }, { int64_t { -3 } } });
    CHECK(cells == std::vector<std::string> { "7", "42", "NULL", "-3" });
}

TEST_CASE("RawColumnSlice: integer, smallint, bool and real columns", "[RawColumnSlice]")
{
    CHECK(DescribeAndSlice(SqlColumnTypeDefinitions::Integer {}, { { int64_t { 1 } }, { int64_t { 2 } }, { int64_t { 3 } } })
          == std::vector<std::string> { "1", "2", "3" });
    CHECK(
        DescribeAndSlice(SqlColumnTypeDefinitions::Smallint {}, { { int64_t { 1 } }, { int64_t { -2 } }, { int64_t { 3 } } })
        == std::vector<std::string> { "1", "-2", "3" });
    CHECK(DescribeAndSlice(SqlColumnTypeDefinitions::Bool {}, { { true }, { false }, { true } })
          == std::vector<std::string> { "1", "0", "1" });
    CHECK(DescribeAndSlice(SqlColumnTypeDefinitions::Real {}, { { 1.5 }, { 2.25 }, { std::monostate {} } })
          == std::vector<std::string> { "1.5", "2.25", "NULL" });
}

TEST_CASE("RawColumnSlice: string column", "[RawColumnSlice]")
{
    auto const cells = DescribeAndSlice(
        SqlColumnTypeDefinitions::Varchar { 20 },
        { { std::string("abc") }, { std::string("a much longer value") }, { std::monostate {} }, { std::string("x") } });
    CHECK(cells == std::vector<std::string> { "abc", "a much longer value", "NULL", "x" });
}

TEST_CASE("RawColumnSlice: wide string column", "[RawColumnSlice]")
{
    auto const cells = DescribeAndSlice(SqlColumnTypeDefinitions::NVarchar { 20 },
                                        { { std::string("abc") },
                                          { std::string("gr\xC3\xBC\xC3\x9F"
                                                        "e \xE2\x82\xAC") },
                                          { std::monostate {} },
                                          { std::string("") } });
    CHECK(cells[0] == "abc");
    CHECK(cells[1]
          == "gr\xC3\xBC\xC3\x9F"
             "e \xE2\x82\xAC");
    CHECK(cells[2] == "NULL");
}

TEST_CASE("RawColumnSlice: binary column", "[RawColumnSlice]")
{
    auto const cells = DescribeAndSlice(SqlColumnTypeDefinitions::VarBinary { 64 },
                                        { { std::vector<uint8_t> { 0x01, 0xAB } },
                                          { std::vector<uint8_t> { 0xDE, 0xAD, 0xBE, 0xEF, 0x00 } },
                                          { std::monostate {} },
                                          { std::vector<uint8_t>(40, 0x0F) } });
    CHECK(cells[0] == "0x01AB");
    CHECK(cells[1] == "0xDEADBEEF00");
    CHECK(cells[2] == "NULL");
    // 32 bytes are shown, the rest is cut and marked.
    std::string expected = "0x";
    std::ranges::for_each(std::views::iota(0, 32), [&](int) { expected += "0F"; });
    CHECK(cells[3] == expected + "\xE2\x80\xA6");
}

TEST_CASE("RawColumnSlice: native GUID column", "[RawColumnSlice]")
{
    auto const cells = DescribeAndSlice(SqlColumnTypeDefinitions::Guid {},
                                        { { std::string("12345678-1234-1234-1234-123456789ABC") },
                                          { std::string("6F9619FF-8B86-D011-B42D-00C04FC964FF") },
                                          { std::monostate {} } },
                                        SqlServerType::MICROSOFT_SQL);
    CHECK(cells[0] == "12345678-1234-1234-1234-123456789ABC");
    CHECK(cells[1] == "6F9619FF-8B86-D011-B42D-00C04FC964FF");
    CHECK(cells[2] == "NULL");
}

TEST_CASE("RawColumnSlice: GUID as text column", "[RawColumnSlice]")
{
    auto const cells = DescribeAndSlice(SqlColumnTypeDefinitions::Guid {},
                                        { { std::string("12345678-1234-1234-1234-123456789ABC") },
                                          { std::string("6F9619FF-8B86-D011-B42D-00C04FC964FF") },
                                          { std::monostate {} } },
                                        SqlServerType::SQLITE);
    CHECK(cells[0] == "12345678-1234-1234-1234-123456789ABC");
    CHECK(cells[2] == "NULL");
}

TEST_CASE("RawColumnSlice: date, time and timestamp columns", "[RawColumnSlice]")
{
    auto const stamps = DescribeAndSlice(
        SqlColumnTypeDefinitions::DateTime {},
        { { std::string("2024-01-15T14:30:45.123") }, { std::string("2024-12-31T23:59:59") }, { std::monostate {} } });
    CHECK(stamps[0] == "2024-01-15 14:30:45.123");
    CHECK(stamps[1] == "2024-12-31 23:59:59");
    CHECK(stamps[2] == "NULL");

    auto const dates =
        DescribeAndSlice(SqlColumnTypeDefinitions::Date {},
                         { { std::string("2024-01-15") }, { std::string("2024-12-31") }, { std::monostate {} } });
    CHECK(dates[0] == "2024-01-15");
    CHECK(dates[2] == "NULL");

    auto const times = DescribeAndSlice(SqlColumnTypeDefinitions::Time {},
                                        { { std::string("01:02:03") }, { std::string("23:59:58") }, { std::monostate {} } },
                                        SqlServerType::POSTGRESQL);
    CHECK(times[2] == "NULL");
    CHECK(times[1].starts_with("23:59:58"));
}

TEST_CASE("RawColumnSlice: SliceRows addresses rows by stride", "[RawColumnSlice]")
{
    std::vector<SqlColumnDeclaration> const cols = { { .name = "c", .type = SqlColumnTypeDefinitions::Varchar { 20 } } };
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t) {
        auto const& full = rawCols[0];
        auto const slice = SliceRows(full, 2, 1);
        CHECK(slice.data.data() == full.data.data() + (2 * full.metadata.bufferLength));
        CHECK(slice.indicators.data() == full.indicators.data() + 2);
        CHECK(SliceRows(full, 0, 0).indicators.empty());
    };
    RunBatchManagerTest(executor, cols, { { std::string("aa") }, { std::string("bbbb") }, { std::string("c") } }, 10);
}
