// SPDX-License-Identifier: Apache-2.0

#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup/BatchManager.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Lightweight;
using namespace Lightweight::detail;
using namespace Lightweight::SqlBackup; // For BackupValue
using namespace Lightweight::SqlBackup::Tests;

TEST_CASE("BatchManager: Basic Flow", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "id", .type = SqlColumnTypeDefinitions::Integer {} },
                                               { .name = "name", .type = SqlColumnTypeDefinitions::Text {} } };

    int flushCount = 0;
    size_t lastRowCount = 0;
    std::vector<std::vector<BackupValue>> receivedRows;

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        flushCount++;
        lastRowCount = count;
        // Verify we got 2 columns
        REQUIRE(rawCols.size() == 2);
    };

    // Capacity 3
    BatchManager bm(executor, cols, 3);

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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& /*rawCols*/, size_t count) {
        flushCount++;
        flushedSizes.push_back(count);
    };

    // Capacity 2
    BatchManager bm(executor, cols, 2);

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

TEST_CASE("BatchManager: String Content", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = {
        { .name = "txt", .type = SqlColumnTypeDefinitions::Text { .size = 10 } } // maxLen 10
    };

    std::vector<std::string> capturedStrings;
    std::vector<SQLLEN> capturedIndicators;

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    detail::BatchManager bm(executor, cols, 3);

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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

TEST_CASE("BatchManager: Tinyint column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Tinyint {} } };

    std::vector<int8_t> capturedValues;
    std::vector<SQLLEN> capturedIndicators;

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_TINYINT);
        REQUIRE(col.metadata.sqlType == SQL_TINYINT);

        auto const* data = reinterpret_cast<int8_t const*>(col.data.data());
        for (size_t i = 0; i < count; ++i)
        {
            capturedValues.push_back(data[i]);
            capturedIndicators.push_back(col.indicators[i]);
        }
    };

    RunBatchManagerTest(executor, cols, { { int64_t { 50 } }, { int64_t { 127 } }, { std::monostate {} } }, 10);

    REQUIRE(capturedValues.size() == 3);
    REQUIRE(capturedValues[0] == 50);
    REQUIRE(capturedValues[1] == 127);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: Real/Double column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Real {} } };

    std::vector<double> capturedValues;

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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
                          { std::string("NULL") },            // "NULL" -> NULL
                          { int64_t { 12345 } } },            // Non-string -> NULL
                        10,
                        SqlServerType::MYSQL);

    REQUIRE(capturedTimes.size() == 6);
    REQUIRE(capturedIndicators.size() == 6);

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

    // Third to sixth: all NULLs
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[3] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[4] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[5] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: TimeBatchColumn PushFromBatch", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "tm", .type = SqlColumnTypeDefinitions::Time {} } };

    std::vector<SQL_TIME_STRUCT> capturedTimes;
    std::vector<SQLLEN> capturedIndicators;

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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
                        { { std::string("00:00:00") }, // Midnight
                          { std::string("01:02:03") }, // Single digit components
                          { std::string("short") } },  // Too short string (< 8 chars) -> zeros
                        10,
                        SqlServerType::MYSQL);

    REQUIRE(capturedTimes.size() == 3);

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

    // Too short -> zeros (but not NULL, just zero-initialized)
    REQUIRE(capturedIndicators[2] != SQL_NULL_DATA);
    REQUIRE(capturedTimes[2].hour == 0);
    REQUIRE(capturedTimes[2].minute == 0);
    REQUIRE(capturedTimes[2].second == 0);
}

TEST_CASE("BatchManager: TimeBatchColumn with monostate batch data", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "tm", .type = SqlColumnTypeDefinitions::Time {} } };

    std::vector<SQLLEN> capturedIndicators;

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        REQUIRE(rawCols.size() == 1);
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.cType == SQL_C_GUID);

        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    RunBatchManagerTest(executor,
                        cols,
                        { { std::string("12345678-1234-1234-1234-123456789ABC") },
                          { std::string("INVALID-GUID") }, // Invalid -> NULL
                          { std::monostate {} },
                          { std::string("") },
                          { std::string("NULL") } },
                        10);

    REQUIRE(capturedIndicators.size() == 5);
    REQUIRE(capturedIndicators[0] == sizeof(SqlGuid));
    REQUIRE(capturedIndicators[1] == SQL_NULL_DATA); // Invalid GUID
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[3] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[4] == SQL_NULL_DATA);
}

TEST_CASE("BatchManager: GUID PushFromBatch", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "id", .type = SqlColumnTypeDefinitions::Guid {} } };

    std::vector<SQLLEN> capturedIndicators;

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
        auto const& col = rawCols[0];
        for (size_t i = 0; i < count; ++i)
            capturedIndicators.push_back(col.indicators[i]);
    };

    ColumnBatch batch;
    batch.rowCount = 4;
    batch.columns.resize(1);
    batch.nullIndicators.resize(1);
    batch.nullIndicators[0] = { false, true, false, false };
    batch.columns[0] = std::vector<std::string> { "AAAAAAAA-BBBB-4CCC-DDDD-EEEEEEEEEEEE", // Valid UUID (version 4)
                                                  "",
                                                  "NULL",
                                                  "INVALID" };

    RunBatchManagerBatchTest(executor, cols, { batch }, 10);

    REQUIRE(capturedIndicators.size() == 4);
    REQUIRE(capturedIndicators[0] == sizeof(SqlGuid));
    REQUIRE(capturedIndicators[1] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[2] == SQL_NULL_DATA);
    REQUIRE(capturedIndicators[3] == SQL_NULL_DATA); // Invalid GUID
}

// =============================================================================
// NVarchar/NChar Column Tests
// =============================================================================

TEST_CASE("BatchManager: NVarchar column", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "txt",
                                                 .type = SqlColumnTypeDefinitions::NVarchar { .size = 100 } } };

    std::vector<SQLLEN> capturedIndicators;

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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
    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const&, size_t) {
        flushCount++;
    };

    BatchManager bm(executor, cols, 10);

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
    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const&, size_t) {
        flushCount++;
    };

    BatchManager bm(executor, cols, 10);

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
    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const&, size_t) {
        flushCount++;
        throw std::runtime_error("Simulated error");
    };

    BatchManager bm(executor, cols, 10);
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
    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const&, size_t) {
        flushCount++;
    };

    BatchManager bm(executor, cols, 10);
    bm.Flush(); // Nothing to flush

    REQUIRE(flushCount == 0);
}

TEST_CASE("BatchManager: Numeric Push with string 'NULL'", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { .name = "val", .type = SqlColumnTypeDefinitions::Integer {} } };

    std::vector<SQLLEN> capturedIndicators;

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t) {
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

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t) {
        auto const& col = rawCols[0];
        REQUIRE(col.metadata.sqlType == SQL_LONGVARBINARY);
    };

    RunBatchManagerTest(executor, cols, { { std::vector<uint8_t> { 0x01 } } }, 10);
}
