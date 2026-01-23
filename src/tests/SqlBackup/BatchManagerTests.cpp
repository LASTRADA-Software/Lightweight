// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup/BatchManager.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace Lightweight;
using namespace Lightweight::detail;
using namespace Lightweight::SqlBackup; // For BackupValue

TEST_CASE("BatchManager: Basic Flow", "[SqlBackup]")
{
    std::vector<SqlColumnDeclaration> cols = { { "id", SqlColumnTypeDefinitions::Integer {} },
                                               { "name", SqlColumnTypeDefinitions::Text {} } };

    int flushCount = 0;
    size_t lastRowCount = 0;
    std::vector<std::vector<BackupValue>> receivedRows;

    // We can't easily inspect raw columns content without decoding them back.
    // So checking row count and flush count is primary.
    // To check content, we might relies on knowledge of implementation (TypedBatchColumn).

    // Actually, we can check if it compiles and runs first.

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
    std::vector<SqlColumnDeclaration> cols = { { "val", SqlColumnTypeDefinitions::Integer {} } };

    int flushCount = 0;
    std::vector<size_t> flushedSizes;

    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t count) {
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

    // We need to populate column data.
    // BatchManager uses std::visit.
    // ColumnBatch::ColumnData is std::variant<... std::vector<int64_t> ...>?
    // Wait, BackupValue uses int64_t.
    // ColumnBatch definition in SqlBackupFormats.hpp

    // Let's assume int64_t is supported.
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
        { "txt", SqlColumnTypeDefinitions::Text { .size = 10 } } // maxLen 10
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
            char const* s = buf + i * strideBytes;
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
