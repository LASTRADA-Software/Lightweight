// SPDX-License-Identifier: Apache-2.0

#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup/BatchManager.hpp>
#include <Lightweight/SqlBackup/MsgPackChunkFormats.hpp>
#include <Lightweight/SqlBackup/RawColumnSlice.hpp>
#include <Lightweight/SqlBackup/RowIsolation.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Lightweight;
using namespace Lightweight::detail;
using namespace Lightweight::SqlBackup; // For BackupValue
using namespace Lightweight::SqlBackup::Tests;

namespace
{
/// The consecutive values `[first, last)`.
std::vector<int64_t> IotaVector(int64_t first, int64_t last)
{
    std::vector<int64_t> values;
    for (auto const value: std::views::iota(first, last))
        values.push_back(value);
    return values;
}
} // namespace

TEST_CASE("BatchManager: Basic Flow", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "id", .type = SqlColumnTypeDefinitions::Integer {} },
                                               { .name = "name", .type = SqlColumnTypeDefinitions::Text {} } };

    int flushCount = 0;
    size_t lastRowCount = 0;
    std::vector<std::vector<BackupValue>> receivedRows;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        flushCount++;
        lastRowCount = count;
        // Verify we got 2 columns
        REQUIRE(rawCols.size() == 2);
    };

    // Capacity 3
    BatchManager bm(MakeHooks(executor), cols, 3);

    bm.PushRow({ 1, "one" });
    REQUIRE(bm.rowCount == 1);
    REQUIRE(flushCount == 0);

    bm.PushRow({ 2, "two" });
    REQUIRE(bm.rowCount == 2);
    REQUIRE(flushCount == 0);

    bm.PushRow({ 3, "three" });
    // Should trigger flush
    REQUIRE(bm.rowCount == 0);
    REQUIRE(flushCount == 1);
    REQUIRE(lastRowCount == 3);

    bm.PushRow({ 4, "four" });
    REQUIRE(bm.rowCount == 1);
    REQUIRE(flushCount == 1);

    bm.Flush();
    REQUIRE(bm.rowCount == 0);
    REQUIRE(flushCount == 2);
    REQUIRE(lastRowCount == 1);
}

TEST_CASE("BatchManager: PushBatch splitting", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Integer {} } };

    int flushCount = 0;
    std::vector<size_t> flushedSizes;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& /*rawCols*/, size_t count) {
        flushCount++;
        flushedSizes.push_back(count);
    };

    // Capacity 2
    BatchManager bm(MakeHooks(executor), cols, 2);

    // Create a dummy batch with 5 items
    ColumnBatch sourceBatch;
    sourceBatch.rowCount = 5;
    sourceBatch.nullIndicators.resize(1);           // 1 column
    sourceBatch.nullIndicators[0].resize(5, false); // no nulls
    sourceBatch.columns.resize(1);

    std::vector<int64_t> data = { 10, 20, 30, 40, 50 };
    sourceBatch.columns[0] = data;

    bm.PushBatch(sourceBatch);

    // Expecting splits: 2, 2, 1
    // 1st flush: 2 items
    // 2nd flush: 2 items
    // Remaining 1 item in buffer (no flush yet)

    REQUIRE(flushCount == 2);
    REQUIRE(flushedSizes.size() == 2);
    REQUIRE(flushedSizes[0] == 2);
    REQUIRE(flushedSizes[1] == 2);
    REQUIRE(bm.rowCount == 1);

    bm.Flush();
    REQUIRE(flushCount == 3);
    REQUIRE(flushedSizes.back() == 1);
}

TEST_CASE("BatchManager: small rows fill batches to capacity (no spurious byte-budget flushes)", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "id", .type = SqlColumnTypeDefinitions::Integer {} },
                                               { .name = "name", .type = SqlColumnTypeDefinitions::Varchar { 255 } } };

    std::vector<size_t> flushedSizes;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& /*rawCols*/, size_t count) {
        flushedSizes.push_back(count);
    };

    BatchManager bm(MakeHooks(executor), cols, 4000);

    constexpr size_t RowCount = 20'000;
    ColumnBatch sourceBatch;
    sourceBatch.rowCount = RowCount;
    sourceBatch.nullIndicators.resize(2);
    sourceBatch.nullIndicators[0].resize(RowCount, false);
    sourceBatch.nullIndicators[1].resize(RowCount, false);
    sourceBatch.columns.resize(2);
    std::vector<int64_t> ids(RowCount);
    std::vector<std::string> names(RowCount, "short-value");
    sourceBatch.columns[0] = std::move(ids);
    sourceBatch.columns[1] = std::move(names);

    bm.PushBatch(sourceBatch);
    bm.Flush();

    // 20k small rows at capacity 4000 must produce exactly 5 full batches - the 64 MB projected-
    // bytes budget exists for large LOB values and must never trigger on small rows.
    REQUIRE(flushedSizes == std::vector<size_t> { 4000, 4000, 4000, 4000, 4000 });
}

TEST_CASE("BatchManager: oversize binary values are not truncated", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "blob", .type = SqlColumnTypeDefinitions::VarBinary { 0 } } };

    std::vector<size_t> receivedSizes;
    std::vector<uint8_t> firstValue;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        for (size_t r = 0; r < count; ++r)
        {
            REQUIRE(col.indicators[r] >= 0);
            receivedSizes.push_back(static_cast<size_t>(col.indicators[r]));
            if (receivedSizes.size() == 1)
            {
                auto const* base = reinterpret_cast<uint8_t const*>(col.data.data());
                firstValue.assign(base, base + col.indicators[0]);
            }
        }
    };

    BatchManager bm(MakeHooks(executor), cols, 4000);

    // 200'000 bytes - far beyond the old 64 KB fixed stride that silently truncated.
    std::vector<uint8_t> big(200'000);
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<uint8_t>((i * 31) + 7);
    bm.PushRow({ big });
    bm.Flush();

    REQUIRE(receivedSizes == std::vector<size_t> { 200'000 });
    REQUIRE(firstValue == big);
}

TEST_CASE("BatchManager: String Content", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = {
        { .name = "txt", .type = SqlColumnTypeDefinitions::Text { .size = 10 } } // maxLen 10
    };

    std::vector<std::string> capturedStrings;
    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        // Decode raw buffer (SQL_C_BINARY)
        char const* buf = reinterpret_cast<char const*>(col.data.data());
        size_t strideBytes = col.metadata.size;
        REQUIRE(strideBytes == 10);

        for (size_t i = 0; i < count; ++i)
        {
            char const* s = buf + (i * strideBytes);
            // Get length from indicator
            SQLLEN len = col.indicators[i];
            REQUIRE(len != SQL_NULL_DATA);
            capturedStrings.emplace_back(s, static_cast<size_t>(len));
            capturedIndicators.push_back(len);
        }
    };

    detail::BatchManager bm(MakeHooks(executor), cols, 3);

    bm.PushRow({ "Hello" });
    bm.PushRow({ "World" });

    // "LongString" -> 10 chars. maxLen 10.
    bm.PushRow({ "1234567890" });

    bm.Flush();

    REQUIRE(capturedStrings.size() == 3);
    REQUIRE(capturedStrings[0] == "Hello");
    REQUIRE(capturedIndicators[0] == 5);
    REQUIRE(capturedStrings[1] == "World");
    REQUIRE(capturedIndicators[1] == 5);

    // "1234567890" (10 chars). maxLen 10.
    // copyLen = min(10, 10) = 10.
    REQUIRE(capturedStrings[2] == "1234567890");
    REQUIRE(capturedIndicators[2] == 10);
}

// =============================================================================
// Numeric Column Type Tests
// =============================================================================

TEST_CASE("BatchManager: Smallint column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Smallint {} } };

    std::vector<int16_t> capturedValues;
    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_SHORT);
        REQUIRE(col.metadata.sqlType == SQL_SMALLINT);

        auto const* data = reinterpret_cast<int16_t const*>(col.data.data());
        for (size_t i = 0; i < count; ++i)
        {
            capturedValues.push_back(data[i]);
            capturedIndicators.push_back(col.indicators[i]);
        }
    };

    RunBatchManagerTest(executor, cols, { { int64_t { 100 } }, { int64_t { -200 } }, { std::monostate {} } }, 10);

    REQUIRE(capturedValues.size() == 3);
    REQUIRE(capturedValues[0] == 100);
    REQUIRE(capturedValues[1] == -200);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: Tinyint column on SQL Server binds unsigned 0..255", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Tinyint {} } };

    std::vector<uint8_t> capturedValues;
    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_UTINYINT);
        REQUIRE(col.metadata.sqlType == SQL_TINYINT);

        auto const* data = reinterpret_cast<uint8_t const*>(col.data.data());
        for (size_t i = 0; i < count; ++i)
        {
            capturedValues.push_back(data[i]);
            capturedIndicators.push_back(col.indicators[i]);
        }
    };

    RunBatchManagerTest(executor,
                        cols,
                        { { int64_t { 50 } }, { int64_t { 127 } }, { std::monostate {} }, { int64_t { 255 } } },
                        10,
                        SqlServerType::MICROSOFT_SQL);

    REQUIRE(capturedValues.size() == 4);
    REQUIRE(capturedValues[0] == 50);
    REQUIRE(capturedValues[1] == 127);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
    REQUIRE(capturedValues[3] == 255);
}

TEST_CASE("BatchManager: Tinyint column elsewhere binds a short holding -128..255", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Tinyint {} } };

    for (auto const serverType:
         { SqlServerType::SQLITE, SqlServerType::POSTGRESQL, SqlServerType::MYSQL, SqlServerType::UNKNOWN })
    {
        INFO(std::format("server type {}", static_cast<int>(serverType)));
        std::vector<int16_t> capturedValues;
        BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
            REQUIRE(rawCols[0].metadata.cType == SQL_C_SSHORT);
            REQUIRE(rawCols[0].metadata.sqlType == SQL_TINYINT);
            auto const* data = reinterpret_cast<int16_t const*>(rawCols[0].data.data());
            capturedValues.insert(capturedValues.end(), data, data + count);
        };

        // What an int8_t field (-128..127) or a uint8_t field (0..255) wrote.
        RunBatchManagerTest(executor,
                            cols,
                            { { int64_t { -128 } }, { int64_t { -5 } }, { std::string("-1") }, { int64_t { 255 } } },
                            10,
                            serverType);
        CHECK(capturedValues == std::vector<int16_t> { -128, -5, -1, 255 });
    }
}

TEST_CASE("BatchManager: Real/Double column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Real {} } };

    std::vector<double> capturedValues;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_DOUBLE);
        REQUIRE(col.metadata.sqlType == SQL_DOUBLE);

        auto const* data = reinterpret_cast<double const*>(col.data.data());
        for (size_t i = 0; i < count; ++i)
            capturedValues.push_back(data[i]);
    };

    RunBatchManagerTest(executor, cols, { { 1.23456 }, { -9.87654 } }, 10);

    REQUIRE(capturedValues.size() == 2);
    REQUIRE(capturedValues[0] == Catch::Approx(1.23456));
    REQUIRE(capturedValues[1] == Catch::Approx(-9.87654));
}

TEST_CASE("BatchManager: Bool column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Bool {} } };

    std::vector<int8_t> capturedValues;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_BIT);

        auto const* data = reinterpret_cast<int8_t const*>(col.data.data());
        for (size_t i = 0; i < count; ++i)
            capturedValues.push_back(data[i]);
    };

    RunBatchManagerTest(executor, cols, { { true }, { false } }, 10);

    REQUIRE(capturedValues.size() == 2);
    REQUIRE(capturedValues[0] == 1);
    REQUIRE(capturedValues[1] == 0);
}

// =============================================================================
// DateTime Column Tests
// =============================================================================

TEST_CASE("BatchManager: DateTime column with fractional seconds", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "ts", .type = SqlColumnTypeDefinitions::DateTime {} } };

    std::vector<SQL_TIMESTAMP_STRUCT> capturedValues;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_TYPE_TIMESTAMP);

        auto const* data = reinterpret_cast<SQL_TIMESTAMP_STRUCT const*>(col.data.data());
        for (size_t i = 0; i < count; ++i)
            capturedValues.push_back(data[i]);
    };

    RunBatchManagerTest(executor,
                        cols,
                        { { std::string("2024-01-15T14:30:45.123") },
                          { std::string("2024-12-31T23:59:59") },
                          { std::string("2024-06-15T12:00:00.5") },
                          { std::monostate {} },
                          { std::string("") },
                          { std::string("NULL") } },
                        10);

    REQUIRE(capturedValues.size() == 6);

    // First: 2024-01-15T14:30:45.123
    REQUIRE(capturedValues[0].year == 2024);
    REQUIRE(capturedValues[0].month == 1);
    REQUIRE(capturedValues[0].day == 15);
    REQUIRE(capturedValues[0].hour == 14);
    REQUIRE(capturedValues[0].minute == 30);
    REQUIRE(capturedValues[0].second == 45);
    REQUIRE(capturedValues[0].fraction == 123'000'000); // 123ms in 100ns units

    // Second: 2024-12-31T23:59:59 (no fraction)
    REQUIRE(capturedValues[1].year == 2024);
    REQUIRE(capturedValues[1].month == 12);
    REQUIRE(capturedValues[1].day == 31);
    REQUIRE(capturedValues[1].fraction == 0);

    // Third: 2024-06-15T12:00:00.5 -> ".5" padded to ".500" = 500ms
    REQUIRE(capturedValues[2].fraction == 500'000'000);
}

TEST_CASE("BatchManager: DateTime PushFromBatch", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "ts", .type = SqlColumnTypeDefinitions::DateTime {} } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    ColumnBatch batch;
    batch.rowCount = 4;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, true, false, false };
    batch.columns[0] = std::vector<std::string> { "2024-01-01T00:00:00", "", "NULL", "2024-12-31T23:59:59" };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10);

    REQUIRE(capturedIndicators.size() == 4);
    REQUIRE(capturedIndicators[0] == sizeof(SQL_TIMESTAMP_STRUCT)); // Valid
    REQUIRE(capturedIndicators[1] == SQL_NULL_DATA);                // Null from indicator
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);                // Empty string -> NULL
    REQUIRE(capturedIndicators[3] == sizeof(SQL_TIMESTAMP_STRUCT)); // Valid
}

// =============================================================================
// Date Column Tests
// =============================================================================

TEST_CASE("BatchManager: Date column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "dt", .type = SqlColumnTypeDefinitions::Date {} } };

    std::vector<SQL_DATE_STRUCT> capturedValues;
    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_TYPE_DATE);

        auto const* data = reinterpret_cast<SQL_DATE_STRUCT const*>(col.data.data());
        for (size_t i = 0; i < count; ++i)
        {
            capturedValues.push_back(data[i]);
            capturedIndicators.push_back(col.indicators[i]);
        }
    };

    RunBatchManagerTest(executor,
                        cols,
                        { { std::string("2024-01-15") },
                          { std::string("1999-12-31") },
                          { std::monostate {} },
                          { std::string("") },
                          { std::string("NULL") } },
                        10);

    REQUIRE(capturedValues.size() == 5);
    REQUIRE(capturedValues[0].year == 2024);
    REQUIRE(capturedValues[0].month == 1);
    REQUIRE(capturedValues[0].day == 15);
    REQUIRE(capturedValues[1].year == 1999);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[3] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[4] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: Date PushFromBatch", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "dt", .type = SqlColumnTypeDefinitions::Date {} } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    ColumnBatch batch;
    batch.rowCount = 3;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, true, false };
    batch.columns[0] = std::vector<std::string> { "2024-06-15", "", "NULL" };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10);

    REQUIRE(capturedIndicators.size() == 3);
    REQUIRE(capturedIndicators[0] == sizeof(SQL_DATE_STRUCT));
    REQUIRE(capturedIndicators[1] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
}

// =============================================================================
// Time Column Tests
// =============================================================================

TEST_CASE("BatchManager: StringTime column (for MSSQL/PostgreSQL/SQLite)", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "tm", .type = SqlColumnTypeDefinitions::Time {} } };

    std::vector<std::string> capturedValues;
    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_CHAR);

        auto const* data = reinterpret_cast<char const*>(col.data.data());
        size_t stride = col.metadata.bufferLength;
        for (size_t i = 0; i < count; ++i)
        {
            SQLLEN len = col.indicators[i];
            capturedIndicators.push_back(len);
            if (len != SQL_NULL_DATA)
                capturedValues.emplace_back(data + (i * stride), static_cast<size_t>(len));
            else
                capturedValues.emplace_back("");
        }
    };

    // Default serverType is SQLite which uses StringTimeBatchColumn
    RunBatchManagerTest(executor,
                        cols,
                        { { std::string("14:30:45.123456") },
                          { std::string("23:59:59") },
                          { std::monostate {} },
                          { std::string("") },
                          { std::string("NULL") } },
                        10,
                        SqlServerType::SQLITE);

    REQUIRE(capturedValues.size() == 5);
    REQUIRE(capturedValues[0] == "14:30:45.123456");
    REQUIRE(capturedValues[1] == "23:59:59");
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[3] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[4] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: StringTime PushFromBatch", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "tm", .type = SqlColumnTypeDefinitions::Time {} } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    ColumnBatch batch;
    batch.rowCount = 3;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, true, false };
    batch.columns[0] = std::vector<std::string> { "12:00:00", "", "NULL" };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10, SqlServerType::SQLITE);

    REQUIRE(capturedIndicators.size() == 3);
    REQUIRE(capturedIndicators[0] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[1] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: TimeBatchColumn (for MySQL/UNKNOWN)", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "tm", .type = SqlColumnTypeDefinitions::Time {} } };

    std::vector<SQL_TIME_STRUCT> capturedTimes;
    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_TYPE_TIME);
        REQUIRE(col.metadata.sqlType == SQL_TYPE_TIME);

        auto const* data = reinterpret_cast<SQL_TIME_STRUCT const*>(col.data.data());
        for (size_t i = 0; i < count; ++i)
        {
            capturedTimes.push_back(data[i]);
            capturedIndicators.push_back(col.indicators[i]);
        }
    };

    // Use MySQL server type to trigger TimeBatchColumn
    RunBatchManagerTest(executor,
                        cols,
                        { { std::string("14:30:45") },
                          { std::string("23:59:59.123456") }, // Fractional seconds should be ignored
                          { std::monostate {} },              // NULL
                          { std::string("") },                // Empty -> NULL
                          { std::string("NULL") } },          // "NULL" -> NULL
                        10,
                        SqlServerType::MYSQL);

    REQUIRE(capturedTimes.size() == 5);
    REQUIRE(capturedIndicators.size() == 5);

    // First value: 14:30:45
    REQUIRE(capturedIndicators[0] == sizeof(SQL_TIME_STRUCT));
    REQUIRE(capturedTimes[0].hour == 14);
    REQUIRE(capturedTimes[0].minute == 30);
    REQUIRE(capturedTimes[0].second == 45);

    // Second value: 23:59:59 (fractional part ignored)
    REQUIRE(capturedIndicators[1] == sizeof(SQL_TIME_STRUCT));
    REQUIRE(capturedTimes[1].hour == 23);
    REQUIRE(capturedTimes[1].minute == 59);
    REQUIRE(capturedTimes[1].second == 59);

    // Third to fifth: all NULLs
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[3] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[4] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: TimeBatchColumn PushFromBatch", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "tm", .type = SqlColumnTypeDefinitions::Time {} } };

    std::vector<SQL_TIME_STRUCT> capturedTimes;
    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        auto const* data = reinterpret_cast<SQL_TIME_STRUCT const*>(col.data.data());
        for (size_t i = 0; i < count; ++i)
        {
            capturedTimes.push_back(data[i]);
            capturedIndicators.push_back(col.indicators[i]);
        }
    };

    // Use UNKNOWN server type to also trigger TimeBatchColumn
    ColumnBatch batch;
    batch.rowCount = 4;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, true, false, false };
    batch.columns[0] = std::vector<std::string> { "12:00:00", "", "08:15:30.999", "NULL" };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10, SqlServerType::UNKNOWN);

    REQUIRE(capturedIndicators.size() == 4);
    REQUIRE(capturedTimes.size() == 4);

    // First: 12:00:00
    REQUIRE(capturedIndicators[0] != SQL_NULL_DATA);
    REQUIRE(capturedTimes[0].hour == 12);
    REQUIRE(capturedTimes[0].minute == 0);
    REQUIRE(capturedTimes[0].second == 0);

    // Second: NULL (from null indicator)
    REQUIRE(capturedIndicators[1] == SQL_NULL_DATA);

    // Third: 08:15:30 (fractional part ignored)
    REQUIRE(capturedIndicators[2] != SQL_NULL_DATA);
    REQUIRE(capturedTimes[2].hour == 8);
    REQUIRE(capturedTimes[2].minute == 15);
    REQUIRE(capturedTimes[2].second == 30);

    // Fourth: NULL (from "NULL" string)
    REQUIRE(capturedIndicators[3] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: TimeBatchColumn ParseTime edge cases", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "tm", .type = SqlColumnTypeDefinitions::Time {} } };

    std::vector<SQL_TIME_STRUCT> capturedTimes;
    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        auto const* data = reinterpret_cast<SQL_TIME_STRUCT const*>(col.data.data());
        for (size_t i = 0; i < count; ++i)
        {
            capturedTimes.push_back(data[i]);
            capturedIndicators.push_back(col.indicators[i]);
        }
    };

    // Edge cases for ParseTime
    RunBatchManagerTest(executor,
                        cols,
                        { { std::string("00:00:00") },   // Midnight
                          { std::string("01:02:03") } }, // Single digit components
                        10,
                        SqlServerType::MYSQL);

    REQUIRE(capturedTimes.size() == 2);

    // Midnight
    REQUIRE(capturedIndicators[0] != SQL_NULL_DATA);
    REQUIRE(capturedTimes[0].hour == 0);
    REQUIRE(capturedTimes[0].minute == 0);
    REQUIRE(capturedTimes[0].second == 0);

    // 01:02:03
    REQUIRE(capturedIndicators[1] != SQL_NULL_DATA);
    REQUIRE(capturedTimes[1].hour == 1);
    REQUIRE(capturedTimes[1].minute == 2);
    REQUIRE(capturedTimes[1].second == 3);
}

TEST_CASE("BatchManager: TimeBatchColumn with monostate batch data", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "tm", .type = SqlColumnTypeDefinitions::Time {} } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    ColumnBatch batch;
    batch.rowCount = 2;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, false };
    batch.columns[0] = std::monostate {}; // No data -> all NULLs

    RunBatchManagerBatchTest(executor, cols, { batch }, 10, SqlServerType::MYSQL);

    REQUIRE(capturedIndicators.size() == 2);
    REQUIRE(capturedIndicators[0] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[1] == SQL_NULL_DATA);
}

// =============================================================================
// GUID Column Tests
// =============================================================================

TEST_CASE("BatchManager: GUID column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "id", .type = SqlColumnTypeDefinitions::Guid {} } };

    std::vector<SQLLEN> capturedIndicators;
    std::vector<uint8_t> capturedFirstGuid;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_GUID);

        auto const* bytes = reinterpret_cast<uint8_t const*>(col.data.data());
        capturedFirstGuid.assign(bytes, bytes + sizeof(SqlGuid));

        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    RunBatchManagerTest(executor,
                        cols,
                        { { std::string("12345678-1234-1234-1234-123456789ABC") },
                          { std::monostate {} },
                          { std::string("") },
                          { std::string("NULL") } },
                        10,
                        SqlServerType::MICROSOFT_SQL);

    REQUIRE(capturedIndicators.size() == 4);
    REQUIRE(capturedIndicators[0] == sizeof(SqlGuid));
    REQUIRE(capturedIndicators[1] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[3] == SQL_NULL_DATA);

    // The batch is bound as a raw SQL_C_GUID array, which carries Data1/Data2/Data3 in native wire
    // byte order — not the canonical order SqlGuid::TryParse() produced from the archived string.
    // Asserting the indicator alone cannot see a missing conversion here (see issue #596).
    auto expected = SqlGuid::UnsafeParse("12345678-1234-1234-1234-123456789ABC");
    SwapGuidWireByteOrder(expected.data);
    CHECK(std::ranges::equal(capturedFirstGuid, expected.data));
}

TEST_CASE("BatchManager: a braced GUID binds as the same bytes as the unbraced one", "[SqlBackup][SqlGuid]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "id", .type = SqlColumnTypeDefinitions::Guid {} } };

    for (auto const serverType: { SqlServerType::MICROSOFT_SQL, SqlServerType::POSTGRESQL })
    {
        std::vector<uint8_t> captured;
        BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
            auto const* bytes = reinterpret_cast<uint8_t const*>(rawCols[0].data.data());
            captured.assign(bytes, bytes + (count * sizeof(SqlGuid)));
        };

        RunBatchManagerTest(executor,
                            cols,
                            { { std::string("6f9619ff-8b86-d011-b42d-00c04fc964ff") },
                              { std::string("{6f9619ff-8b86-d011-b42d-00c04fc964ff}") },
                              { std::string("{6F9619FF-8B86-D011-B42D-00C04FC964FF}") } },
                            10,
                            serverType);

        REQUIRE(captured.size() == 3 * sizeof(SqlGuid));
        auto const first = std::span(captured).first(sizeof(SqlGuid));
        CHECK(std::ranges::equal(first, std::span(captured).subspan(sizeof(SqlGuid), sizeof(SqlGuid))));
        CHECK(std::ranges::equal(first, std::span(captured).subspan(2 * sizeof(SqlGuid), sizeof(SqlGuid))));
    }
}

TEST_CASE("BatchManager: GUID PushFromBatch", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "id", .type = SqlColumnTypeDefinitions::Guid {} } };

    std::vector<SQLLEN> capturedIndicators;
    std::vector<uint8_t> capturedFirstGuid;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        auto const* bytes = reinterpret_cast<uint8_t const*>(col.data.data());
        capturedFirstGuid.assign(bytes, bytes + sizeof(SqlGuid));
        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    ColumnBatch batch;
    batch.rowCount = 3;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, true, false };
    batch.columns[0] = std::vector<std::string> { "AAAAAAAA-BBBB-4CCC-DDDD-EEEEEEEEEEEE", // Valid UUID (version 4)
                                                  "",
                                                  "NULL" };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10, SqlServerType::MICROSOFT_SQL);

    REQUIRE(capturedIndicators.size() == 3);
    REQUIRE(capturedIndicators[0] == sizeof(SqlGuid));
    REQUIRE(capturedIndicators[1] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);

    // Same wire byte-order contract as the single-value push above, reached through the
    // column-batch code path.
    auto expected = SqlGuid::UnsafeParse("AAAAAAAA-BBBB-4CCC-DDDD-EEEEEEEEEEEE");
    SwapGuidWireByteOrder(expected.data);
    CHECK(std::ranges::equal(capturedFirstGuid, expected.data));
}

namespace
{
struct CapturedTextColumn
{
    SQLSMALLINT cType = 0;
    SQLSMALLINT sqlType = 0;
    SQLULEN columnSize = 0;
    std::vector<std::string> texts;
    std::vector<SQLLEN> indicators;
};

/// Pushes @p rows into a single-GUID-column BatchManager for @p serverType and captures the first
/// flush's bound column, each cell as DescribeRawCell() renders it.
CapturedTextColumn CaptureGuidColumn(std::vector<SqlBackup::BackupValue> const& rows, SqlServerType serverType)
{
    std::vector<SqlColumnDeclaration> const cols = { { .name = "id", .type = SqlColumnTypeDefinitions::Guid {} } };
    CapturedTextColumn captured;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        captured.cType = col.metadata.cType;
        captured.sqlType = col.metadata.sqlType;
        captured.columnSize = col.metadata.size;
        for (auto const row: std::views::iota(size_t { 0 }, count))
        {
            captured.indicators.push_back(col.indicators[row]);
            captured.texts.push_back(DescribeRawCell(col, row));
        }
    };
    std::vector<std::vector<SqlBackup::BackupValue>> rowVectors;
    rowVectors.reserve(rows.size());
    for (auto const& row: rows)
        rowVectors.push_back({ row });
    RunBatchManagerTest(executor, cols, rowVectors, 10, serverType);
    return captured;
}
} // namespace

TEST_CASE("BatchManager binds GUID as text on SQLite", "[SqlBackup][BatchManager]")
{
    auto const captured =
        CaptureGuidColumn({ std::string("6F9619FF-8B86-D011-B42D-00C04FC964FF"), std::monostate {} }, SqlServerType::SQLITE);

    CHECK(captured.cType == SQL_C_CHAR);
    CHECK(captured.sqlType == SQL_VARCHAR);
    // A VARCHAR bound with ColumnSize 0 is an invalid precision to strict drivers.
    CHECK(captured.columnSize >= 36);
    REQUIRE(captured.indicators.size() == 2);
    CHECK(captured.texts[0] == "6F9619FF-8B86-D011-B42D-00C04FC964FF");
    CHECK(captured.indicators[1] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager keeps non-canonical GUID text on SQLite", "[SqlBackup][BatchManager]")
{
    CapturedTextColumn captured;
    REQUIRE_NOTHROW(
        captured = CaptureGuidColumn({ std::string("{6f9619ff-8b86-d011-b42d-00c04fc964ff}"), std::string("not-a-guid") },
                                     SqlServerType::SQLITE));

    REQUIRE(captured.texts.size() == 2);
    // The ColumnSize covers the longest text, not just a canonical GUID.
    CHECK(captured.columnSize >= 38);
    CHECK(captured.texts[0] == "{6f9619ff-8b86-d011-b42d-00c04fc964ff}");
    CHECK(captured.texts[1] == "not-a-guid");
}

TEST_CASE("BatchManager binds GUID natively on MSSQL and PostgreSQL", "[SqlBackup][BatchManager]")
{
    for (auto const serverType: { SqlServerType::MICROSOFT_SQL, SqlServerType::POSTGRESQL })
    {
        CAPTURE(serverType);
        auto const captured = CaptureGuidColumn({ std::string("6F9619FF-8B86-D011-B42D-00C04FC964FF") }, serverType);
        CHECK(captured.cType == SQL_C_GUID);
    }
}

// =============================================================================
// NVarchar/NChar Column Tests
// =============================================================================

TEST_CASE("BatchManager: NVarchar column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "txt",
                                                 .type = SqlColumnTypeDefinitions::NVarchar { .size = 100 } } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_WCHAR);

        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    RunBatchManagerTest(
        executor, cols, { { std::string("Hello World") }, { std::string("Unicode: äöü") }, { std::monostate {} } }, 10);

    REQUIRE(capturedIndicators.size() == 3);
    REQUIRE(capturedIndicators[0] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[1] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: NChar column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "txt", .type = SqlColumnTypeDefinitions::NChar { .size = 50 } } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_WCHAR);

        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    RunBatchManagerTest(executor, cols, { { std::string("Fixed width") }, { std::monostate {} } }, 10);

    REQUIRE(capturedIndicators.size() == 2);
    REQUIRE(capturedIndicators[0] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[1] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: NVarchar PushFromBatch with various types", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "txt",
                                                 .type = SqlColumnTypeDefinitions::NVarchar { .size = 100 } } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    // Test with int64 batch data (should convert to string)
    ColumnBatch batch;
    batch.rowCount = 3;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, false, true };
    batch.columns[0] = std::vector<int64_t> { 100, 200, 0 };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10);

    REQUIRE(capturedIndicators.size() == 3);
    REQUIRE(capturedIndicators[0] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[1] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: NVarchar PushFromBatch with bools", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "txt",
                                                 .type = SqlColumnTypeDefinitions::NVarchar { .size = 100 } } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    ColumnBatch batch;
    batch.rowCount = 2;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, false };
    batch.columns[0] = std::vector<bool> { true, false };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10);

    REQUIRE(capturedIndicators.size() == 2);
    REQUIRE(capturedIndicators[0] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[1] != SQL_NULL_DATA);
}

// =============================================================================
// Binary Column Tests
// =============================================================================

TEST_CASE("BatchManager: Binary column Push", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "bin",
                                                 .type = SqlColumnTypeDefinitions::VarBinary { .size = 100 } } };

    std::vector<SQLLEN> capturedIndicators;
    std::vector<std::vector<uint8_t>> capturedData;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_BINARY);

        auto const* data = reinterpret_cast<uint8_t const*>(col.data.data());
        size_t stride = col.metadata.bufferLength;
        for (size_t i = 0; i < count; ++i)
        {
            SQLLEN len = col.indicators[i];
            capturedIndicators.push_back(len);
            if (len != SQL_NULL_DATA && len > 0)
                capturedData.emplace_back(data + (i * stride), data + (i * stride) + len);
            else
                capturedData.emplace_back();
        }
    };

    RunBatchManagerTest(executor,
                        cols,
                        { { std::vector<uint8_t> { 0x01, 0x02, 0x03 } },
                          { std::string("Hello") },
                          { std::string("") },
                          { std::monostate {} } },
                        10);

    REQUIRE(capturedIndicators.size() == 4);
    REQUIRE(capturedIndicators[0] == 3);
    REQUIRE(capturedData[0] == std::vector<uint8_t> { 0x01, 0x02, 0x03 });
    REQUIRE(capturedIndicators[1] == 5);
    REQUIRE(capturedIndicators[2] == 0); // Empty string
    REQUIRE(capturedIndicators[3] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: Binary PushFromBatch with binary data", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "bin",
                                                 .type = SqlColumnTypeDefinitions::VarBinary { .size = 100 } } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    ColumnBatch batch;
    batch.rowCount = 3;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, false, true };
    batch.columns[0] = std::vector<std::vector<uint8_t>> { { 0xAA, 0xBB, 0xCC }, { 0x01 }, {} };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10);

    REQUIRE(capturedIndicators.size() == 3);
    REQUIRE(capturedIndicators[0] == 3);
    REQUIRE(capturedIndicators[1] == 1);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: Binary PushFromBatch with hex strings", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "bin",
                                                 .type = SqlColumnTypeDefinitions::VarBinary { .size = 100 } } };

    std::vector<SQLLEN> capturedIndicators;
    std::vector<std::vector<uint8_t>> capturedData;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        auto const* data = reinterpret_cast<uint8_t const*>(col.data.data());
        size_t stride = col.metadata.bufferLength;
        for (size_t i = 0; i < count; ++i)
        {
            SQLLEN len = col.indicators[i];
            capturedIndicators.push_back(len);
            if (len != SQL_NULL_DATA && len > 0)
                capturedData.emplace_back(data + (i * stride), data + (i * stride) + len);
            else
                capturedData.emplace_back();
        }
    };

    // Hex string input (uppercase)
    ColumnBatch batch;
    batch.rowCount = 4;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, false, false, false };
    batch.columns[0] = std::vector<std::string> {
        "AABBCC",     // uppercase hex
        "aabbcc",     // lowercase hex
        "0102030405", // longer hex
        ""            // empty
    };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10);

    REQUIRE(capturedIndicators.size() == 4);
    REQUIRE(capturedIndicators[0] == 3);
    REQUIRE(capturedData[0] == std::vector<uint8_t> { 0xAA, 0xBB, 0xCC });
    REQUIRE(capturedIndicators[1] == 3);
    REQUIRE(capturedData[1] == std::vector<uint8_t> { 0xAA, 0xBB, 0xCC });
    REQUIRE(capturedIndicators[2] == 5);
    REQUIRE(capturedData[2] == std::vector<uint8_t> { 0x01, 0x02, 0x03, 0x04, 0x05 });
    REQUIRE(capturedIndicators[3] == 0); // Empty
}

// =============================================================================
// Varchar/Char Column Tests
// =============================================================================

TEST_CASE("BatchManager: Varchar column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "txt", .type = SqlColumnTypeDefinitions::Varchar { .size = 50 } } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_CHAR);

        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    RunBatchManagerTest(executor,
                        cols,
                        { { std::string("Hello") },
                          { int64_t { 12345 } }, // Number converted to string
                          { 1.23456 },           // Double converted to string
                          { true },              // Bool converted to string
                          { std::monostate {} } },
                        10);

    REQUIRE(capturedIndicators.size() == 5);
    REQUIRE(capturedIndicators[0] == 5);
    REQUIRE(capturedIndicators[1] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[2] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[3] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[4] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: Char column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "txt", .type = SqlColumnTypeDefinitions::Char { .size = 10 } } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_CHAR);

        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    RunBatchManagerTest(executor, cols, { { std::string("ABC") }, { std::monostate {} } }, 10);

    REQUIRE(capturedIndicators.size() == 2);
    REQUIRE(capturedIndicators[0] == 3);
    REQUIRE(capturedIndicators[1] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: Decimal column as string", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = {
        { .name = "val", .type = SqlColumnTypeDefinitions::Decimal { .precision = 10, .scale = 2 } }
    };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_CHAR);

        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    RunBatchManagerTest(executor, cols, { { std::string("12345.67") }, { 99.99 }, { std::monostate {} } }, 10);

    REQUIRE(capturedIndicators.size() == 3);
    REQUIRE(capturedIndicators[0] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[1] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
}

// =============================================================================
// String PushFromBatch with various source types
// =============================================================================

TEST_CASE("BatchManager: String PushFromBatch with int64", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "txt", .type = SqlColumnTypeDefinitions::Text { .size = 50 } } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    ColumnBatch batch;
    batch.rowCount = 3;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, false, true };
    batch.columns[0] = std::vector<int64_t> { 100, 200, 0 };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10);

    REQUIRE(capturedIndicators.size() == 3);
    REQUIRE(capturedIndicators[0] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[1] != SQL_NULL_DATA);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: String PushFromBatch with bools", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "txt", .type = SqlColumnTypeDefinitions::Text { .size = 50 } } };

    std::vector<std::string> capturedValues;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        auto const* data = reinterpret_cast<char const*>(col.data.data());
        size_t stride = col.metadata.bufferLength;
        for (size_t i = 0; i < count; ++i)
        {
            SQLLEN len = col.indicators[i];
            if (len != SQL_NULL_DATA)
                capturedValues.emplace_back(data + (i * stride), static_cast<size_t>(len));
        }
    };

    ColumnBatch batch;
    batch.rowCount = 2;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, false };
    batch.columns[0] = std::vector<bool> { true, false };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10);

    REQUIRE(capturedValues.size() == 2);
    REQUIRE(capturedValues[0] == "1");
    REQUIRE(capturedValues[1] == "0");
}

// =============================================================================
// Numeric PushFromBatch Tests
// =============================================================================

TEST_CASE("BatchManager: Integer PushFromBatch with string conversion", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Integer {} } };

    std::vector<int32_t> capturedValues;
    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        auto const* data = reinterpret_cast<int32_t const*>(col.data.data());
        for (size_t i = 0; i < count; ++i)
        {
            capturedValues.push_back(data[i]);
            capturedIndicators.push_back(col.indicators[i]);
        }
    };

    ColumnBatch batch;
    batch.rowCount = 4;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, false, false, false };
    batch.columns[0] = std::vector<std::string> { "123", "NULL", "", "456" };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10);

    REQUIRE(capturedIndicators.size() == 4);
    REQUIRE(capturedValues[0] == 123);
    REQUIRE(capturedIndicators[1] == SQL_NULL_DATA); // "NULL" string
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA); // Empty string
    REQUIRE(capturedValues[3] == 456);
}

TEST_CASE("BatchManager: Integer PushFromBatch with bools", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Integer {} } };

    std::vector<int32_t> capturedValues;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        auto const* data = reinterpret_cast<int32_t const*>(col.data.data());
        for (size_t i = 0; i < count; ++i)
            capturedValues.push_back(data[i]);
    };

    ColumnBatch batch;
    batch.rowCount = 2;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, false };
    batch.columns[0] = std::vector<bool> { true, false };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10);

    REQUIRE(capturedValues.size() == 2);
    REQUIRE(capturedValues[0] == 1);
    REQUIRE(capturedValues[1] == 0);
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

TEST_CASE("BatchManager: Empty PushBatch does nothing", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Integer {} } };

    int flushCount = 0;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const&, size_t) {
        flushCount++;
    };

    BatchManager bm(MakeHooks(executor), cols, 10);

    ColumnBatch emptyBatch;
    emptyBatch.rowCount = 0;
    bm.PushBatch(emptyBatch);

    REQUIRE(bm.rowCount == 0);
    REQUIRE(flushCount == 0);
}

TEST_CASE("BatchManager: Mismatched row size does nothing", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Integer {} } };

    int flushCount = 0;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const&, size_t) {
        flushCount++;
    };

    BatchManager bm(MakeHooks(executor), cols, 10);

    // Push row with wrong number of columns (2 instead of 1)
    bm.PushRow({ 1, 2 });
    REQUIRE(bm.rowCount == 0);

    // Push batch with wrong number of columns
    ColumnBatch batch;
    batch.rowCount = 1;
    batch.columns.resize(2); // Wrong!
    batch.nullIndicators.resize(2);
    bm.PushBatch(batch);
    REQUIRE(bm.rowCount == 0);

    REQUIRE(flushCount == 0);
}

TEST_CASE("BatchManager: Flush exception clears state", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Integer {} } };

    int flushCount = 0;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const&, size_t) {
        flushCount++;
        throw std::runtime_error("Simulated error");
    };

    BatchManager bm(MakeHooks(executor), cols, 10);
    bm.PushRow({ 1 });
    bm.PushRow({ 2 });

    REQUIRE(bm.rowCount == 2);

    REQUIRE_THROWS_AS(bm.Flush(), std::runtime_error);

    // After exception, state should be cleared
    REQUIRE(bm.rowCount == 0);
    REQUIRE(flushCount == 1);
}

TEST_CASE("BatchManager: Empty Flush does nothing", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Integer {} } };

    int flushCount = 0;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const&, size_t) {
        flushCount++;
    };

    BatchManager bm(MakeHooks(executor), cols, 10);
    bm.Flush(); // Nothing to flush

    REQUIRE(flushCount == 0);
}

TEST_CASE("BatchManager: Numeric Push with string 'NULL'", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Integer {} } };

    std::vector<SQLLEN> capturedIndicators;

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    RunBatchManagerTest(executor, cols, { { std::string("NULL") }, { std::string("123") } }, 10);

    REQUIRE(capturedIndicators.size() == 2);
    REQUIRE(capturedIndicators[0] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[1] != SQL_NULL_DATA);
}

// =============================================================================
// Large Size Columns
// =============================================================================

TEST_CASE("BatchManager: Large NVarchar triggers LONGVARCHAR", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "txt",
                                                 .type = SqlColumnTypeDefinitions::NVarchar { .size = 5000 } } };

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t) {
        auto const& col = rawCols[0];
        // Large size should trigger SQL_WLONGVARCHAR
        REQUIRE((col.metadata.sqlType == SQL_WLONGVARCHAR || col.metadata.sqlType == SQL_WVARCHAR));
    };

    RunBatchManagerTest(executor, cols, { { std::string("test") } }, 10);
}

TEST_CASE("BatchManager: MAX Binary column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = {
        { .name = "bin", .type = SqlColumnTypeDefinitions::VarBinary { .size = 0 } } // 0 = MAX
    };

    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t) {
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.sqlType == SQL_LONGVARBINARY);
    };

    RunBatchManagerTest(executor, cols, { { std::vector<uint8_t> { 0x01 } } }, 10);
}

// =============================================================================
// Values that do not convert to their column's type
// =============================================================================

namespace
{

/// Pushes rows into a one-column BatchManager via @p push and returns the one failure it reports,
/// as "row <n>: <message>".
///
/// The column is pushed NOT NULL first: the test fails unless exactly one row is reported failed and
/// every other row is inserted. It is pushed again as nullable: then every row must be inserted and
/// the bad value set to NULL with the same reason.
/// @param column The column declaration.
/// @param rowCount The number of rows @p push pushes.
/// @param serverType The target server type.
/// @param push Pushes the rows.
std::string ReportedConversionFailure(SqlColumnDeclaration const& column,
                                      size_t rowCount,
                                      SqlServerType serverType,
                                      std::function<void(BatchManager&)> const& push)
{
    size_t insertedRows = 0;
    std::vector<std::string> failures;
    std::vector<std::string> nulledReasons;
    auto const run = [&](bool required) {
        auto declaration = column;
        declaration.required = required;
        BatchManager manager(
            BatchManagerHooks {
                .tryInsert = [&](std::span<SqlRawColumn const>, size_t count) -> std::expected<void, BatchInsertError> {
                    insertedRows += count;
                    return {};
                },
                .classify = {},
                .onRowFailed =
                    [&](size_t row, std::string_view, std::string_view message, std::string const&) {
                        failures.push_back(std::format("row {}: {}", row, message));
                    },
                .onCellNulled = [&](std::string_view, std::string_view reason) { nulledReasons.emplace_back(reason); },
                .onSettled = {},
            },
            { declaration },
            10,
            serverType);
        push(manager);
        manager.Flush();
        CHECK(manager.SettledRows() == rowCount);
    };

    run(true);
    CHECK(insertedRows == rowCount - 1);
    CHECK(nulledReasons.empty());

    insertedRows = 0;
    run(false);
    CHECK(insertedRows == rowCount);

    if (failures.size() != 1 || nulledReasons.size() != 1)
    {
        FAIL_CHECK(
            std::format("{} failed rows and {} nulled cells instead of one each", failures.size(), nulledReasons.size()));
        return {};
    }
    CHECK(failures[0].ends_with(std::format("column '{}': {}", column.name, nulledReasons[0])));
    return failures[0];
}

/// Pushes the rows of @p rows (one value each) via PushRow; see ReportedConversionFailure().
std::string PushRowsError(SqlColumnDeclaration const& column,
                          std::vector<BackupValue> const& rows,
                          SqlServerType serverType = SqlServerType::UNKNOWN)
{
    return ReportedConversionFailure(column, rows.size(), serverType, [&](BatchManager& manager) {
        for (auto const& value: rows)
            manager.PushRow({ value });
    });
}

/// Pushes @p data as one column batch (no NULLs) via PushBatch; see ReportedConversionFailure().
std::string PushBatchError(SqlColumnDeclaration const& column,
                           ColumnBatch::ColumnData data,
                           size_t rowCount,
                           SqlServerType serverType = SqlServerType::UNKNOWN)
{
    ColumnBatch batch;
    batch.rowCount = rowCount;
    batch.columns.push_back(std::move(data));
    batch.nullIndicators.emplace_back(rowCount, false);
    return ReportedConversionFailure(column, rowCount, serverType, [&](BatchManager& manager) { manager.PushBatch(batch); });
}

/// The server type the GUID conversion tests run as: one with a native GUID type, which parses GUIDs.
/// (Targets without one bind GUID text unchanged and never fail a value.)
constexpr auto NativeGuidServer = SqlServerType::MICROSOFT_SQL;

SqlColumnDeclaration const GuidColumn { .name = "TOKEN", .type = SqlColumnTypeDefinitions::Guid {} };

} // namespace

TEST_CASE("BatchManager: GUID column accepts a GUID whose version nibble is outside 1..5", "[SqlBackup]")
{
    // Written by Lightweight builds that stored GUIDs with a byte-swapped Data3; SQL Server accepts it.
    constexpr std::string_view text { "FCC64492-FF82-E040-9C3A-12AB34CD56EF" };

    std::vector<SQLLEN> capturedIndicators;
    std::vector<std::vector<uint8_t>> capturedGuids;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const* bytes = reinterpret_cast<uint8_t const*>(rawCols[0].data.data());
        for (auto const row: std::views::iota(size_t { 0 }, count))
        {
            capturedIndicators.push_back(rawCols[0].indicators[row]);
            capturedGuids.emplace_back(bytes + (row * sizeof(SqlGuid)), bytes + ((row + 1) * sizeof(SqlGuid)));
        }
    };

    RunBatchManagerTest(executor, { GuidColumn }, { { std::string(text) } }, 10, NativeGuidServer);

    ColumnBatch batch;
    batch.rowCount = 1;
    batch.columns.emplace_back(std::vector<std::string> { std::string(text) });
    batch.nullIndicators.emplace_back(1, false);
    RunBatchManagerBatchTest(executor, { GuidColumn }, { batch }, 10, NativeGuidServer);

    // The text's bytes, unchanged, in the SQL_C_GUID wire order the raw array bind expects.
    auto expected = SqlGuid::UnsafeParse(text);
    SwapGuidWireByteOrder(expected.data);
    REQUIRE(capturedIndicators == std::vector<SQLLEN> { sizeof(SqlGuid), sizeof(SqlGuid) });
    CHECK(std::ranges::equal(capturedGuids[0], expected.data));
    CHECK(std::ranges::equal(capturedGuids[1], expected.data));
}

TEST_CASE("BatchManager: a GUID that does not parse is reported, naming row, column and value", "[SqlBackup]")
{
    // The rows around the bad one are inserted; the row number counts them.
    CHECK(PushRowsError(
              GuidColumn,
              { std::string("12345678-1234-1234-1234-123456789ABC"), std::monostate {}, std::string("INVALID-GUID") },
              NativeGuidServer)
          == "row 3: column 'TOKEN': 'INVALID-GUID' is not a valid GUID");
    CHECK(PushBatchError(GuidColumn,
                         std::vector<std::string> { "12345678-1234-1234-1234-123456789ABC", "INVALID-GUID" },
                         2,
                         NativeGuidServer)
          == "row 2: column 'TOKEN': 'INVALID-GUID' is not a valid GUID");

    // A value of another kind is no GUID either.
    CHECK(PushRowsError(GuidColumn, { int64_t { 42 } }, NativeGuidServer)
          == "row 1: column 'TOKEN': 42 is not a valid GUID");
    CHECK(PushBatchError(GuidColumn, std::vector<std::vector<uint8_t>> { { 0x01, 0x02, 0x03 } }, 1, NativeGuidServer)
          == "row 1: column 'TOKEN': a 3-byte binary value is not a valid GUID");
}

TEST_CASE("BatchManager: a long unconvertible value is shortened in the report", "[SqlBackup]")
{
    auto const longValue = std::string(100, 'x');
    CHECK(PushRowsError(GuidColumn, { longValue }, NativeGuidServer)
          == std::format("row 1: column 'TOKEN': '{}...' (100 bytes) is not a valid GUID", std::string(64, 'x')));
}

TEST_CASE("BatchManager: numeric text that does not parse fails instead of becoming 0", "[SqlBackup]")
{
    using namespace SqlColumnTypeDefinitions;
    SqlColumnDeclaration const integer { .name = "AMOUNT", .type = Integer {} };

    CHECK(PushRowsError(integer, { std::string("abc") }) == "row 1: column 'AMOUNT': 'abc' is not a valid integer");
    CHECK(PushRowsError(integer, { std::string("12abc") }) == "row 1: column 'AMOUNT': '12abc' is not a valid integer");
    CHECK(PushBatchError(integer, std::vector<std::string> { "12", "abc" }, 2)
          == "row 2: column 'AMOUNT': 'abc' is not a valid integer");
    CHECK(PushRowsError(integer, { std::vector<uint8_t> { 0x01 } })
          == "row 1: column 'AMOUNT': a 1-byte binary value is not a valid integer");
    CHECK(PushBatchError(integer, std::vector<std::vector<uint8_t>> { { 0x01 } }, 1)
          == "row 1: column 'AMOUNT': a 1-byte binary value is not a valid integer");

    CHECK(PushRowsError({ .name = "TINY", .type = Tinyint {} }, { std::string("300") })
          == "row 1: column 'TINY': '300' is not a valid tinyint");
    CHECK(PushRowsError({ .name = "BIG", .type = Bigint {} }, { std::string("1.5") })
          == "row 1: column 'BIG': '1.5' is not a valid bigint");
    CHECK(PushBatchError({ .name = "SMALL", .type = Smallint {} }, std::vector<std::string> { "x" }, 1)
          == "row 1: column 'SMALL': 'x' is not a valid smallint");
    CHECK(PushRowsError({ .name = "RATIO", .type = Real {} }, { std::string("1.5x") })
          == "row 1: column 'RATIO': '1.5x' is not a valid real");
    CHECK(PushRowsError({ .name = "FLAG", .type = Bool {} }, { std::string("yes") })
          == "row 1: column 'FLAG': 'yes' is not a valid bool");
}

TEST_CASE("BatchManager: Bool column reads the true/false text a mixed-type column is archived as", "[SqlBackup]")
{
    std::vector<int8_t> capturedValues;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const* data = reinterpret_cast<int8_t const*>(rawCols[0].data.data());
        capturedValues.insert(capturedValues.end(), data, data + count);
    };

    ColumnBatch batch;
    batch.rowCount = 4;
    batch.columns.emplace_back(std::vector<std::string> { "true", "false", "1", "0" });
    batch.nullIndicators.emplace_back(4, false);
    RunBatchManagerBatchTest(executor, { { .name = "FLAG", .type = SqlColumnTypeDefinitions::Bool {} } }, { batch }, 10);

    CHECK(capturedValues == std::vector<int8_t> { 1, 0, 1, 0 });
}

TEST_CASE("BatchManager: Push treats an empty numeric text as NULL, as PushBatch does", "[SqlBackup]")
{
    std::vector<SQLLEN> capturedIndicators;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        std::ranges::copy(rawCols[0].indicators.first(count), std::back_inserter(capturedIndicators));
    };

    RunBatchManagerTest(
        executor, { { .name = "AMOUNT", .type = SqlColumnTypeDefinitions::Integer {} } }, { { std::string("") } }, 10);

    CHECK(capturedIndicators == std::vector<SQLLEN> { SQL_NULL_DATA });
}

TEST_CASE("BatchManager: a malformed date or time fails instead of becoming zero", "[SqlBackup]")
{
    using namespace SqlColumnTypeDefinitions;
    SqlColumnDeclaration const dateTime { .name = "CREATED", .type = DateTime {} };
    SqlColumnDeclaration const date { .name = "DAY", .type = Date {} };
    SqlColumnDeclaration const time { .name = "AT", .type = Time {} };

    CHECK(PushRowsError(dateTime, { std::string("garbage") })
          == "row 1: column 'CREATED': 'garbage' is not a valid datetime");
    CHECK(PushRowsError(dateTime, { std::string("2024-0x-02T03:04:05.000") })
          == "row 1: column 'CREATED': '2024-0x-02T03:04:05.000' is not a valid datetime");
    CHECK(PushBatchError(dateTime, std::vector<std::string> { "2024-01-02T03:04:05.0x0" }, 1)
          == "row 1: column 'CREATED': '2024-01-02T03:04:05.0x0' is not a valid datetime");
    CHECK(PushRowsError(dateTime, { int64_t { 20240102 } }) == "row 1: column 'CREATED': 20240102 is not a valid datetime");

    CHECK(PushRowsError(date, { std::string("2024-1-2") }) == "row 1: column 'DAY': '2024-1-2' is not a valid date");
    CHECK(PushBatchError(date, std::vector<std::string> { "20AB-01-02" }, 1)
          == "row 1: column 'DAY': '20AB-01-02' is not a valid date");
    CHECK(PushBatchError(date, std::vector<double> { 1.5 }, 1) == "row 1: column 'DAY': 1.5 is not a valid date");

    // SQL_TIME_STRUCT binding (MySQL / unknown servers).
    CHECK(PushRowsError(time, { std::string("short") }, SqlServerType::MYSQL)
          == "row 1: column 'AT': 'short' is not a valid time");
    CHECK(PushBatchError(time, std::vector<std::string> { "1a:02:03" }, 1, SqlServerType::MYSQL)
          == "row 1: column 'AT': '1a:02:03' is not a valid time");
    CHECK(PushRowsError(time, { int64_t { 12345 } }, SqlServerType::MYSQL)
          == "row 1: column 'AT': 12345 is not a valid time");
    // Text binding (MSSQL / PostgreSQL / SQLite): the server parses the text, but a number is no time.
    CHECK(PushRowsError(time, { int64_t { 7 } }, SqlServerType::MICROSOFT_SQL)
          == "row 1: column 'AT': 7 is not a valid time");
    CHECK(PushBatchError(time, std::vector<bool> { true }, 1, SqlServerType::MICROSOFT_SQL)
          == "row 1: column 'AT': true is not a valid time");
}

TEST_CASE("BatchManager: a value of the wrong kind for a text or binary column fails", "[SqlBackup]")
{
    using namespace SqlColumnTypeDefinitions;
    SqlColumnDeclaration const narrow { .name = "NAME", .type = Varchar { .size = 50 } };
    SqlColumnDeclaration const wide { .name = "WNAME", .type = NVarchar { .size = 50 } };
    SqlColumnDeclaration const binary { .name = "BLOB", .type = VarBinary { .size = 50 } };

    CHECK(PushRowsError(narrow, { std::vector<uint8_t> { 0x01, 0x02 } })
          == "row 1: column 'NAME': a 2-byte binary value is not a valid text");
    CHECK(PushBatchError(narrow, std::vector<std::vector<uint8_t>> { { 0x01 } }, 1)
          == "row 1: column 'NAME': a 1-byte binary value is not a valid text");
    CHECK(PushRowsError(wide, { std::vector<uint8_t> { 0x01 } })
          == "row 1: column 'WNAME': a 1-byte binary value is not a valid text");
    CHECK(PushBatchError(wide, std::vector<std::vector<uint8_t>> { { 0x01 } }, 1)
          == "row 1: column 'WNAME': a 1-byte binary value is not a valid text");

    CHECK(PushRowsError(binary, { int64_t { 5 } }) == "row 1: column 'BLOB': 5 is not a valid binary");
    CHECK(PushBatchError(binary, std::vector<int64_t> { 5 }, 1) == "row 1: column 'BLOB': 5 is not a valid binary");
    CHECK(PushBatchError(binary, std::vector<std::string> { "ABZZ" }, 1)
          == "row 1: column 'BLOB': 'ABZZ' is not a valid binary");
    CHECK(PushBatchError(binary, std::vector<std::string> { "ABC" }, 1)
          == "row 1: column 'BLOB': 'ABC' is not a valid binary");
}

TEST_CASE("BatchManager: a native number outside the column type's range fails instead of wrapping", "[SqlBackup]")
{
    using namespace SqlColumnTypeDefinitions;

    // TINYINT: 0..255 on SQL Server, -128..255 elsewhere; 300 fits nowhere.
    for (auto const serverType: { SqlServerType::MICROSOFT_SQL,
                                  SqlServerType::SQLITE,
                                  SqlServerType::POSTGRESQL,
                                  SqlServerType::MYSQL,
                                  SqlServerType::UNKNOWN })
    {
        INFO(std::format("server type {}", static_cast<int>(serverType)));
        CHECK(PushRowsError({ .name = "TINY", .type = Tinyint {} }, { int64_t { 300 } }, serverType)
              == "row 1: column 'TINY': 300 is not a valid tinyint");
        CHECK(PushRowsError({ .name = "TINY", .type = Tinyint {} }, { std::string("300") }, serverType)
              == "row 1: column 'TINY': '300' is not a valid tinyint");
        CHECK(PushRowsError({ .name = "TINY", .type = Tinyint {} }, { int64_t { 256 } }, serverType)
              == "row 1: column 'TINY': 256 is not a valid tinyint");
    }
    CHECK(PushBatchError(
              { .name = "TINY", .type = Tinyint {} }, std::vector<int64_t> { 1, -1 }, 2, SqlServerType::MICROSOFT_SQL)
          == "row 2: column 'TINY': -1 is not a valid tinyint");
    CHECK(PushBatchError({ .name = "TINY", .type = Tinyint {} }, std::vector<int64_t> { 1, -129 }, 2, SqlServerType::SQLITE)
          == "row 2: column 'TINY': -129 is not a valid tinyint");
    CHECK(PushRowsError({ .name = "SMALL", .type = Smallint {} }, { int64_t { 40000 } })
          == "row 1: column 'SMALL': 40000 is not a valid smallint");
    CHECK(PushRowsError({ .name = "AMOUNT", .type = Integer {} }, { 1e20 })
          == "row 1: column 'AMOUNT': 1e+20 is not a valid integer");
    CHECK(PushBatchError({ .name = "BIG", .type = Bigint {} }, std::vector<double> { 9223372036854775808.0 }, 1)
          == "row 1: column 'BIG': 9223372036854775808 is not a valid bigint");
}

TEST_CASE("BatchManager: a double that is exactly the integer type's minimum converts", "[SqlBackup]")
{
    std::vector<int64_t> capturedValues;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const* data = reinterpret_cast<int64_t const*>(rawCols[0].data.data());
        capturedValues.insert(capturedValues.end(), data, data + count);
    };
    RunBatchManagerTest(
        executor, { { .name = "BIG", .type = SqlColumnTypeDefinitions::Bigint {} } }, { { -9223372036854775808.0 } }, 10);
    CHECK(capturedValues == std::vector<int64_t> { std::numeric_limits<int64_t>::min() });
}

TEST_CASE("BatchManager: a date or time field with a sign fails", "[SqlBackup]")
{
    using namespace SqlColumnTypeDefinitions;

    CHECK(PushRowsError({ .name = "DAY", .type = Date {} }, { std::string("2024--1-02") })
          == "row 1: column 'DAY': '2024--1-02' is not a valid date");
    CHECK(PushRowsError({ .name = "CREATED", .type = DateTime {} }, { std::string("+024-01-02T03:04:05.000") })
          == "row 1: column 'CREATED': '+024-01-02T03:04:05.000' is not a valid datetime");
}

TEST_CASE("BatchManager: the report escapes control characters and never cuts a UTF-8 sequence", "[SqlBackup]")
{
    CHECK(PushRowsError(GuidColumn, { std::string("bad\n\tguid") }, NativeGuidServer)
          == "row 1: column 'TOKEN': 'bad\\x0A\\x09guid' is not a valid GUID");

    // 63 ASCII bytes, then a two-byte sequence across the 64-byte limit: the sequence is left out whole.
    auto const value = std::string(63, 'x') + "\xC3\xA9" + std::string(10, 'y');
    CHECK(PushRowsError(GuidColumn, { value }, NativeGuidServer)
          == std::format("row 1: column 'TOKEN': '{}...' (75 bytes) is not a valid GUID", std::string(63, 'x')));
}

namespace
{

/// Pushes @p values (as rows, and again as one batch) through a BatchManager for the single column
/// @p column and returns the text each flushed row binds, `std::nullopt` for NULL.
/// Both the row path and the batch path must agree, so the result of the two is concatenated.
std::vector<std::optional<std::string>> BoundCharTexts(SqlColumnDeclaration const& column,
                                                       std::vector<std::optional<std::string>> const& values,
                                                       SqlServerType target,
                                                       SqlServerType source)
{
    std::vector<std::optional<std::string>> bound;
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& raw = rawCols.at(0);
        auto const wide = raw.metadata.cType == SQL_C_WCHAR;
        auto const stride = raw.metadata.bufferLength;
        for (auto const row: std::views::iota(size_t { 0 }, count))
        {
            if (raw.indicators[row] == SQL_NULL_DATA)
            {
                bound.emplace_back(std::nullopt);
                continue;
            }
            auto const* const base = reinterpret_cast<char const*>(raw.data.data()) + (row * stride);
            if (!wide)
            {
                bound.emplace_back(std::string(base, static_cast<size_t>(raw.indicators[row])));
                continue;
            }
            std::string text;
            for (auto const index: std::views::iota(size_t { 0 }, static_cast<size_t>(raw.indicators[row]) / 2))
            {
                char16_t unit {};
                std::memcpy(&unit, base + (index * sizeof(char16_t)), sizeof(char16_t));
                text.push_back(static_cast<char>(unit)); // the test values are ASCII
            }
            bound.emplace_back(std::move(text));
        }
    };

    std::vector<std::vector<BackupValue>> rows;
    ColumnBatch batch;
    batch.rowCount = values.size();
    std::vector<std::string> texts;
    std::vector<bool> nulls;
    for (auto const& value: values)
    {
        rows.push_back({ value ? BackupValue { *value } : BackupValue { std::monostate {} } });
        texts.push_back(value.value_or(""));
        nulls.push_back(!value.has_value());
    }
    batch.columns.emplace_back(std::move(texts));
    batch.nullIndicators.push_back(std::move(nulls));

    RunBatchManagerTest(executor, { column }, rows, 10, target, source);
    auto const rowPathCount = bound.size();
    RunBatchManagerBatchTest(executor, { column }, { batch }, 10, target, source);
    REQUIRE(bound.size() == 2 * rowPathCount);
    CHECK(std::vector(bound.begin(), bound.begin() + static_cast<std::ptrdiff_t>(rowPathCount))
          == std::vector(bound.begin() + static_cast<std::ptrdiff_t>(rowPathCount), bound.end()));
    bound.resize(rowPathCount);
    return bound;
}

using Texts = std::vector<std::optional<std::string>>;

} // namespace

TEST_CASE("BatchManager: CHAR padding is trimmed from a padding source into a non-padding target", "[SqlBackup]")
{
    using namespace SqlColumnTypeDefinitions;
    Texts const padded { "q    ", "     ", std::nullopt, "a b  ", " x" };

    SECTION("CHAR, MSSQL -> SQLite")
    {
        CHECK(BoundCharTexts(
                  { .name = "C", .type = Char { .size = 5 } }, padded, SqlServerType::SQLITE, SqlServerType::MICROSOFT_SQL)
              == Texts { "q", "", std::nullopt, "a b", " x" });
    }

    SECTION("NCHAR, MSSQL -> SQLite")
    {
        CHECK(BoundCharTexts(
                  { .name = "C", .type = NChar { .size = 5 } }, padded, SqlServerType::SQLITE, SqlServerType::MICROSOFT_SQL)
              == Texts { "q", "", std::nullopt, "a b", " x" });
    }

    SECTION("CHAR, PostgreSQL -> SQLite")
    {
        CHECK(BoundCharTexts(
                  { .name = "C", .type = Char { .size = 5 } }, padded, SqlServerType::SQLITE, SqlServerType::POSTGRESQL)
              == Texts { "q", "", std::nullopt, "a b", " x" });
    }
}

TEST_CASE("BatchManager: CHAR padding is kept unless a padding source meets a non-padding target", "[SqlBackup]")
{
    using namespace SqlColumnTypeDefinitions;
    Texts const padded { "q    ", "     ", std::nullopt };

    SECTION("same DBMS (SQLite -> SQLite)")
    {
        CHECK(
            BoundCharTexts({ .name = "C", .type = Char { .size = 5 } }, padded, SqlServerType::SQLITE, SqlServerType::SQLITE)
            == padded);
    }

    SECTION("unknown source")
    {
        CHECK(BoundCharTexts(
                  { .name = "C", .type = Char { .size = 5 } }, padded, SqlServerType::SQLITE, SqlServerType::UNKNOWN)
              == padded);
    }

    SECTION("unknown target")
    {
        CHECK(BoundCharTexts(
                  { .name = "C", .type = Char { .size = 5 } }, padded, SqlServerType::UNKNOWN, SqlServerType::MICROSOFT_SQL)
              == padded);
    }

    SECTION("padding target (MSSQL -> PostgreSQL)")
    {
        CHECK(
            BoundCharTexts(
                { .name = "C", .type = Char { .size = 5 } }, padded, SqlServerType::POSTGRESQL, SqlServerType::MICROSOFT_SQL)
            == padded);
    }

    SECTION("VARCHAR is never trimmed")
    {
        CHECK(
            BoundCharTexts(
                { .name = "C", .type = Varchar { .size = 5 } }, padded, SqlServerType::SQLITE, SqlServerType::MICROSOFT_SQL)
            == padded);
    }

    SECTION("NVARCHAR is never trimmed")
    {
        CHECK(
            BoundCharTexts(
                { .name = "C", .type = NVarchar { .size = 5 } }, padded, SqlServerType::SQLITE, SqlServerType::MICROSOFT_SQL)
            == padded);
    }
}

// =============================================================================
// Value failures, row isolation and the resume watermark (BatchManagerHooks)
// =============================================================================

namespace
{

/// One row as a slice passed to `tryInsert` carried it, each cell rendered by DescribeRawCell().
using RenderedRow = std::vector<std::string>;

/// One `onRowFailed` call.
struct ReportedRowFailure
{
    std::size_t rowInManager {};
    std::string sqlState;
    std::string message;
    std::string sampleKey;

    bool operator==(ReportedRowFailure const&) const = default;
};

/// A fake restore target: records what BatchManager inserts and reports, and fails the inserts
/// `failInsert` asks it to.
struct FakeRestoreTarget
{
    /// Decides the outcome of the `callIndex`-th (0-based) `tryInsert` call for @p rows.
    std::function<std::expected<void, BatchInsertError>(std::vector<RenderedRow> const& rows, std::size_t callIndex)>
        failInsert = [](std::vector<RenderedRow> const&, std::size_t) -> std::expected<void, BatchInsertError> {
        return {};
    };
    FailureClassifier classify;

    std::size_t insertCalls = 0;
    std::vector<RenderedRow> inserted;
    std::vector<ReportedRowFailure> failures;
    std::vector<std::pair<std::string, std::string>> nulledCells;
    std::optional<std::size_t> lastSettled;

    [[nodiscard]] BatchManagerHooks Hooks()
    {
        return BatchManagerHooks {
            .tryInsert = [this](std::span<SqlRawColumn const> slice,
                                std::size_t rowCount) -> std::expected<void, BatchInsertError> {
                auto rows = std::vector<RenderedRow> {};
                for (auto const row: std::views::iota(std::size_t { 0 }, rowCount))
                {
                    auto& rendered = rows.emplace_back();
                    for (auto const& column: slice)
                        rendered.push_back(DescribeRawCell(column, row));
                }
                auto result = failInsert(rows, insertCalls++);
                if (result)
                    inserted.insert(inserted.end(), rows.begin(), rows.end());
                return result;
            },
            .classify = classify,
            .onRowFailed =
                [this](std::size_t row, std::string_view sqlState, std::string_view message, std::string sampleKey) {
                    failures.push_back(ReportedRowFailure { .rowInManager = row,
                                                            .sqlState = std::string(sqlState),
                                                            .message = std::string(message),
                                                            .sampleKey = std::move(sampleKey) });
                },
            .onCellNulled = [this](std::string_view column,
                                   std::string_view reason) { nulledCells.emplace_back(column, reason); },
            .onSettled = [this](std::size_t settled) { lastSettled = settled; },
        };
    }
};

/// An insert error with the given SQLSTATE.
BatchInsertError InsertError(std::string sqlState, std::vector<std::size_t> reportedFailedRows = {})
{
    return BatchInsertError {
        .error = SqlErrorInfo { .nativeErrorCode = 0, .sqlState = std::move(sqlState), .message = "simulated failure" },
        .reportedFailedRows = std::move(reportedFailedRows)
    };
}

/// Classifies SQLSTATE 23000 (a constraint violation) as row-isolatable, everything else as Propagate.
FailureDisposition ClassifyConstraintViolations(SqlErrorInfo const& error)
{
    return error.sqlState == "23000" ? FailureDisposition::IsolateRows : FailureDisposition::Propagate;
}

/// `ID INTEGER PRIMARY KEY, T TINYINT [NOT NULL]`.
std::vector<SqlColumnDeclaration> IdAndTinyint(bool tinyintRequired)
{
    using namespace SqlColumnTypeDefinitions;
    return { { .name = "ID", .type = Integer {}, .primaryKey = SqlPrimaryKeyType::MANUAL, .required = true },
             { .name = "T", .type = Tinyint {}, .required = tinyintRequired } };
}

/// A two-column batch (ID, T) of @p ids and @p values.
ColumnBatch IdAndValueBatch(std::vector<int64_t> ids, std::vector<int64_t> values)
{
    ColumnBatch batch;
    batch.rowCount = ids.size();
    batch.columns.emplace_back(std::move(ids));
    batch.columns.emplace_back(std::move(values));
    batch.nullIndicators.emplace_back(batch.rowCount, false);
    batch.nullIndicators.emplace_back(batch.rowCount, false);
    return batch;
}

} // namespace

TEST_CASE("BatchManager: an unconvertible value in a nullable column becomes NULL", "[SqlBackup][BatchManager]")
{
    FakeRestoreTarget target;
    BatchManager manager(target.Hooks(), IdAndTinyint(false), 10, SqlServerType::SQLITE);

    manager.PushRow({ int64_t { 1 }, int64_t { 5 } });
    manager.PushRow({ int64_t { 2 }, int64_t { 300 } });
    manager.PushRow({ int64_t { 3 }, int64_t { 7 } });
    manager.Flush();

    CHECK(target.inserted == std::vector<RenderedRow> { { "1", "5" }, { "2", "NULL" }, { "3", "7" } });
    CHECK(target.nulledCells == std::vector<std::pair<std::string, std::string>> { { "T", "300 is not a valid tinyint" } });
    CHECK(target.failures.empty());
    CHECK(manager.SettledRows() == 3);
}

TEST_CASE("BatchManager: an unconvertible value in a NOT NULL column fails only its row", "[SqlBackup][BatchManager]")
{
    FakeRestoreTarget target;
    BatchManager manager(target.Hooks(), IdAndTinyint(true), 10, SqlServerType::SQLITE);

    SECTION("PushRow")
    {
        manager.PushRow({ int64_t { 1 }, int64_t { 5 } });
        manager.PushRow({ int64_t { 2 }, int64_t { 300 } });
        manager.PushRow({ int64_t { 3 }, int64_t { 7 } });
    }
    SECTION("PushBatch")
    {
        manager.PushBatch(IdAndValueBatch({ 1, 2, 3 }, { 5, 300, 7 }));
    }
    manager.Flush();

    // Row 2 never reaches the database.
    CHECK(target.inserted == std::vector<RenderedRow> { { "1", "5" }, { "3", "7" } });
    CHECK(
        target.failures
        == std::vector<ReportedRowFailure> {
            { .rowInManager = 2, .sqlState = "", .message = "column 'T': 300 is not a valid tinyint", .sampleKey = "2" } });
    CHECK(target.nulledCells.empty());
    CHECK(manager.SettledRows() == 3);
}

TEST_CASE("BatchManager: every row of a NOT NULL column unconvertible inserts nothing and throws nothing",
          "[SqlBackup][BatchManager]")
{
    FakeRestoreTarget target;
    BatchManager manager(target.Hooks(), IdAndTinyint(true), 10, SqlServerType::SQLITE);

    REQUIRE_NOTHROW(manager.PushBatch(IdAndValueBatch({ 1, 2, 3 }, { 300, 301, 302 })));
    REQUIRE_NOTHROW(manager.Flush());

    CHECK(target.insertCalls == 0);
    REQUIRE(target.failures.size() == 3);
    CHECK(target.failures[0].rowInManager == 1);
    CHECK(target.failures[1].rowInManager == 2);
    CHECK(target.failures[2].rowInManager == 3);
    CHECK(target.failures[2].message == "column 'T': 302 is not a valid tinyint");
    CHECK(manager.SettledRows() == 3);
    CHECK(manager.rowCount == 0);
}

TEST_CASE("BatchManager: a row the database rejects is isolated; the others are inserted", "[SqlBackup][BatchManager]")
{
    FakeRestoreTarget target;
    target.classify = ClassifyConstraintViolations;
    target.failInsert = [](std::vector<RenderedRow> const& rows, std::size_t) -> std::expected<void, BatchInsertError> {
        if (std::ranges::any_of(rows, [](RenderedRow const& row) { return row[0] == "2"; }))
            return std::unexpected { InsertError("23000") };
        return {};
    };
    BatchManager manager(target.Hooks(), IdAndTinyint(false), 10, SqlServerType::SQLITE);

    manager.PushBatch(IdAndValueBatch({ 1, 2, 3 }, { 5, 6, 7 }));
    manager.Flush();

    CHECK(target.inserted == std::vector<RenderedRow> { { "1", "5" }, { "3", "7" } });
    CHECK(target.failures
          == std::vector<ReportedRowFailure> {
              { .rowInManager = 2, .sqlState = "23000", .message = "simulated failure", .sampleKey = "2" } });
    CHECK(manager.SettledRows() == 3);
}

TEST_CASE("BatchManager: driver-reported failed rows are relative to the slice", "[SqlBackup][BatchManager]")
{
    // Row 1 is excluded up front, so the run [2, 5] starts at slice offset 1; the fake reports the
    // failing ID 4 by its index within the slice it was given.
    FakeRestoreTarget target;
    target.classify = ClassifyConstraintViolations;
    target.failInsert = [](std::vector<RenderedRow> const& rows, std::size_t) -> std::expected<void, BatchInsertError> {
        auto const failing = std::ranges::find_if(rows, [](RenderedRow const& row) { return row[0] == "4"; });
        if (failing == rows.end())
            return {};
        return std::unexpected { InsertError("23000", { static_cast<std::size_t>(failing - rows.begin()) }) };
    };
    BatchManager manager(target.Hooks(), IdAndTinyint(true), 10, SqlServerType::SQLITE);

    manager.PushBatch(IdAndValueBatch({ 1, 2, 3, 4, 5 }, { 300, 2, 3, 4, 5 }));
    manager.Flush();

    CHECK(target.inserted == std::vector<RenderedRow> { { "2", "2" }, { "3", "3" }, { "5", "5" } });
    REQUIRE(target.failures.size() == 2);
    CHECK(target.failures[0].rowInManager == 1);
    CHECK(
        target.failures[1]
        == ReportedRowFailure { .rowInManager = 4, .sqlState = "23000", .message = "simulated failure", .sampleKey = "4" });
    // Row 4 was reported by the driver, so no bisection: one failed call and the two runs around it.
    CHECK(target.insertCalls == 3);
}

TEST_CASE("BatchManager: PushBatch skips rows but counts them", "[SqlBackup][BatchManager]")
{
    FakeRestoreTarget target;
    target.classify = ClassifyConstraintViolations;
    target.failInsert = [](std::vector<RenderedRow> const& rows, std::size_t) -> std::expected<void, BatchInsertError> {
        if (std::ranges::any_of(rows, [](RenderedRow const& row) { return row[0] == "7"; }))
            return std::unexpected { InsertError("23000") };
        return {};
    };
    // Capacity 4: rows 5-8 are flushed as one batch, rows 9-10 as another.
    BatchManager manager(target.Hooks(), IdAndTinyint(false), 4, SqlServerType::SQLITE);

    auto const ids = IotaVector(1, 11);
    manager.PushBatch(IdAndValueBatch(ids, ids), 4);
    manager.Flush();

    CHECK(target.inserted
          == std::vector<RenderedRow> { { "5", "5" }, { "6", "6" }, { "8", "8" }, { "9", "9" }, { "10", "10" } });
    REQUIRE(target.failures.size() == 1);
    CHECK(target.failures[0].rowInManager == 7);
    CHECK(manager.SettledRows() == 10);
    CHECK(target.lastSettled == 10);
}

TEST_CASE("BatchManager: skipping every row of a batch settles them", "[SqlBackup][BatchManager]")
{
    FakeRestoreTarget target;
    BatchManager manager(target.Hooks(), IdAndTinyint(false), 10, SqlServerType::SQLITE);

    manager.PushBatch(IdAndValueBatch({ 1, 2 }, { 1, 2 }), 5);
    manager.PushBatch(IdAndValueBatch({ 3, 4 }, { 3, 4 }), 1);
    manager.Flush();

    CHECK(target.inserted == std::vector<RenderedRow> { { "4", "4" } });
    CHECK(manager.SettledRows() == 4);
}

TEST_CASE("BatchManager: SettledRows is the gap-free prefix when an insert error propagates", "[SqlBackup][BatchManager]")
{
    FakeRestoreTarget target;
    target.classify = ClassifyConstraintViolations;

    SECTION("the first insert fails: nothing is settled, not even the excluded last row")
    {
        target.failInsert = [](std::vector<RenderedRow> const&, std::size_t) -> std::expected<void, BatchInsertError> {
            return std::unexpected { InsertError("08S01") };
        };
        BatchManager manager(target.Hooks(), IdAndTinyint(true), 100, SqlServerType::SQLITE);
        auto const ids = IotaVector(1, 11);
        auto values = ids;
        values.back() = 300;
        manager.PushBatch(IdAndValueBatch(ids, values));

        CHECK_THROWS_AS(manager.Flush(), SqlException);
        CHECK(manager.SettledRows() == 0);
        CHECK(target.lastSettled == std::nullopt);
        CHECK(target.inserted.empty());
        CHECK(manager.rowCount == 0);
    }

    SECTION("a later insert fails after rows 1-5 were committed")
    {
        // Rows 1-10 fail as a whole (isolatable), are bisected into 1-5 (committed) and 6-10, which
        // then fails with a connection error. Row 11 is excluded but lies behind the gap.
        target.failInsert = [](std::vector<RenderedRow> const&,
                               std::size_t callIndex) -> std::expected<void, BatchInsertError> {
            switch (callIndex)
            {
                case 0:
                    return std::unexpected { InsertError("23000") };
                case 1:
                    return {};
                default:
                    return std::unexpected { InsertError("08S01") };
            }
        };
        BatchManager manager(target.Hooks(), IdAndTinyint(true), 100, SqlServerType::SQLITE);
        auto const ids = IotaVector(1, 12);
        auto values = ids;
        values.back() = 300;
        manager.PushBatch(IdAndValueBatch(ids, values));

        CHECK_THROWS_AS(manager.Flush(), SqlException);
        CHECK(manager.SettledRows() == 5);
        CHECK(target.lastSettled == 5);
        CHECK(target.inserted.size() == 5);
        CHECK(target.failures.empty());
    }
}

TEST_CASE("BatchManager: rows pushed after a failed flush keep their row numbers but not the watermark",
          "[SqlBackup][BatchManager]")
{
    FakeRestoreTarget target;
    target.classify = ClassifyConstraintViolations;
    target.failInsert = [](std::vector<RenderedRow> const&, std::size_t callIndex) -> std::expected<void, BatchInsertError> {
        if (callIndex == 0)
            return std::unexpected { InsertError("08S01") };
        return {};
    };
    BatchManager manager(target.Hooks(), IdAndTinyint(true), 100, SqlServerType::SQLITE);
    manager.PushBatch(IdAndValueBatch({ 1, 2 }, { 1, 2 }));
    CHECK_THROWS_AS(manager.Flush(), SqlException);

    manager.PushBatch(IdAndValueBatch({ 3, 4 }, { 300, 4 }));
    manager.Flush();

    REQUIRE(target.failures.size() == 1);
    CHECK(target.failures[0].rowInManager == 3);
    CHECK(target.inserted == std::vector<RenderedRow> { { "4", "4" } });
    // Rows 1 and 2 were never settled, so the watermark cannot pass them.
    CHECK(manager.SettledRows() == 0);
}

TEST_CASE("BatchManager: a row without a primary key is named by all its columns", "[SqlBackup][BatchManager]")
{
    using namespace SqlColumnTypeDefinitions;
    FakeRestoreTarget target;
    BatchManager manager(
        target.Hooks(),
        { { .name = "NAME", .type = Varchar { .size = 300 } }, { .name = "T", .type = Tinyint {}, .required = true } },
        10,
        SqlServerType::SQLITE);

    manager.PushRow({ std::string("alice"), int64_t { 300 } });
    manager.PushRow({ std::string(250, 'x'), int64_t { 301 } });
    manager.Flush();

    REQUIRE(target.failures.size() == 2);
    CHECK(target.failures[0].sampleKey == "alice, NULL");
    // Cut to 200 characters.
    CHECK(target.failures[1].sampleKey == std::string(200, 'x'));
}

TEST_CASE("BatchManager: an exception of tryInsert propagates from Flush", "[SqlBackup][BatchManager]")
{
    BatchManager manager(
        MakeHooks([](std::vector<SqlRawColumn> const&, size_t) { throw std::logic_error("executor failed"); }),
        IdAndTinyint(false),
        10,
        SqlServerType::SQLITE);
    manager.PushRow({ int64_t { 1 }, int64_t { 5 } });
    CHECK_THROWS_AS(manager.Flush(), std::logic_error);
    CHECK(manager.rowCount == 0);
    CHECK(manager.SettledRows() == 0);
}

TEST_CASE("BatchManager: a nulled cell of a row excluded for a NOT NULL value is not reported as nulled",
          "[SqlBackup][BatchManager]")
{
    using namespace SqlColumnTypeDefinitions;
    FakeRestoreTarget target;
    BatchManager manager(target.Hooks(),
                         { { .name = "ID", .type = Integer {}, .primaryKey = SqlPrimaryKeyType::MANUAL, .required = true },
                           { .name = "N", .type = Tinyint {} },
                           { .name = "R", .type = Tinyint {}, .required = true } },
                         10,
                         SqlServerType::SQLITE);

    manager.PushRow({ int64_t { 1 }, int64_t { 300 }, int64_t { 301 } });
    manager.Flush();

    CHECK(target.nulledCells.empty());
    REQUIRE(target.failures.size() == 1);
    CHECK(target.failures[0].message == "column 'R': 301 is not a valid tinyint");
    CHECK(target.inserted.empty());
}

TEST_CASE("BatchManager: a nulled cell of a row the database rejects is not reported as nulled", "[SqlBackup][BatchManager]")
{
    FakeRestoreTarget target;
    target.classify = ClassifyConstraintViolations;
    target.failInsert = [](std::vector<RenderedRow> const& rows, std::size_t) -> std::expected<void, BatchInsertError> {
        if (std::ranges::any_of(rows, [](RenderedRow const& row) { return row[0] == "2"; }))
            return std::unexpected { InsertError("23000") };
        return {};
    };
    BatchManager manager(target.Hooks(), IdAndTinyint(false), 10, SqlServerType::SQLITE);

    manager.PushBatch(IdAndValueBatch({ 1, 2, 3 }, { 300, 301, 7 }));
    manager.Flush();

    CHECK(target.inserted == std::vector<RenderedRow> { { "1", "NULL" }, { "3", "7" } });
    // Only row 1's cell reached the database as NULL.
    CHECK(target.nulledCells == std::vector<std::pair<std::string, std::string>> { { "T", "300 is not a valid tinyint" } });
    REQUIRE(target.failures.size() == 1);
    CHECK(target.failures[0].rowInManager == 2);
}

TEST_CASE("BatchManager: a nulled cell is reported once across a retry from SettledRows", "[SqlBackup][BatchManager]")
{
    FakeRestoreTarget target;
    target.classify = ClassifyConstraintViolations;
    target.failInsert = [](std::vector<RenderedRow> const&, std::size_t callIndex) -> std::expected<void, BatchInsertError> {
        if (callIndex == 0)
            return std::unexpected { InsertError("08S01") };
        return {};
    };
    auto const batch = IdAndValueBatch({ 1, 2, 3 }, { 5, 300, 7 });

    auto settled = std::size_t { 0 };
    {
        BatchManager manager(target.Hooks(), IdAndTinyint(false), 10, SqlServerType::SQLITE);
        manager.PushBatch(batch);
        CHECK_THROWS_AS(manager.Flush(), SqlException);
        settled = manager.SettledRows();
    }
    CHECK(settled == 0);
    CHECK(target.nulledCells.empty());

    BatchManager retry(target.Hooks(), IdAndTinyint(false), 10, SqlServerType::SQLITE);
    retry.PushBatch(batch, settled);
    retry.Flush();

    CHECK(target.inserted == std::vector<RenderedRow> { { "1", "5" }, { "2", "NULL" }, { "3", "7" } });
    CHECK(target.nulledCells == std::vector<std::pair<std::string, std::string>> { { "T", "300 is not a valid tinyint" } });
}

TEST_CASE("BatchManager: a row with two bad NOT NULL values is reported once, with the first", "[SqlBackup][BatchManager]")
{
    using namespace SqlColumnTypeDefinitions;
    FakeRestoreTarget target;
    BatchManager manager(target.Hooks(),
                         { { .name = "ID", .type = Integer {}, .primaryKey = SqlPrimaryKeyType::MANUAL, .required = true },
                           { .name = "A", .type = Tinyint {}, .required = true },
                           { .name = "B", .type = Tinyint {}, .required = true } },
                         10,
                         SqlServerType::SQLITE);

    manager.PushRow({ int64_t { 1 }, int64_t { 300 }, int64_t { 301 } });
    manager.PushRow({ int64_t { 2 }, int64_t { 3 }, int64_t { 4 } });
    manager.Flush();

    CHECK(
        target.failures
        == std::vector<ReportedRowFailure> {
            { .rowInManager = 1, .sqlState = "", .message = "column 'A': 300 is not a valid tinyint", .sampleKey = "1" } });
    CHECK(target.inserted == std::vector<RenderedRow> { { "2", "3", "4" } });
}

TEST_CASE("BatchManager: a bad value in a later capacity piece of one PushBatch lands in its own row",
          "[SqlBackup][BatchManager]")
{
    SECTION("NOT NULL: the row is excluded")
    {
        FakeRestoreTarget target;
        BatchManager manager(target.Hooks(), IdAndTinyint(true), 2, SqlServerType::SQLITE);
        manager.PushBatch(IdAndValueBatch({ 1, 2, 3, 4, 5 }, { 1, 2, 3, 300, 5 }));
        manager.Flush();

        CHECK(target.inserted == std::vector<RenderedRow> { { "1", "1" }, { "2", "2" }, { "3", "3" }, { "5", "5" } });
        REQUIRE(target.failures.size() == 1);
        CHECK(target.failures[0].rowInManager == 4);
        CHECK(target.failures[0].sampleKey == "4");
        CHECK(manager.SettledRows() == 5);
    }
    SECTION("nullable: the cell is NULL")
    {
        FakeRestoreTarget target;
        BatchManager manager(target.Hooks(), IdAndTinyint(false), 2, SqlServerType::SQLITE);
        manager.PushBatch(IdAndValueBatch({ 1, 2, 3, 4, 5 }, { 1, 2, 3, 300, 5 }));
        manager.Flush();

        CHECK(target.inserted
              == std::vector<RenderedRow> { { "1", "1" }, { "2", "2" }, { "3", "3" }, { "4", "NULL" }, { "5", "5" } });
        CHECK(target.nulledCells.size() == 1);
    }
}
