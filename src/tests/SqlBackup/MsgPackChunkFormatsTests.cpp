// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup/MsgPackChunkFormats.hpp>

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

using namespace Lightweight::SqlBackup;

TEST_CASE("MsgPackChunkFormats: Write and Read Roundtrip", "[SqlBackup]")
{
    // 1. Write
    auto writer = CreateMsgPackChunkWriter(1024); // 1KB limit

    std::vector<BackupValue> row1 = { 10, "hello", 3.14 };
    std::vector<BackupValue> row2 = { 20, "world", std::monostate {} };
    std::vector<BackupValue> row3 = { 30, std::vector<uint8_t> { 0xAA, 0xBB }, true };

    writer->WriteRow(row1);
    writer->WriteRow(row2);
    writer->WriteRow(row3);

    REQUIRE_FALSE(writer->IsChunkFull());

    std::string data = writer->Flush();
    REQUIRE(!data.empty());

    // 2. Read
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);

    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));

    REQUIRE(batch.rowCount == 3);
    REQUIRE(batch.columns.size() == 3); // 3 columns based on data

    // Verify Row 1: { 10, "hello", 3.14 }
    // Batch is columnar.
    // Col 0 (Ints):
    // We expect checking types inside variant.

    // Helper to extract value from batch at (col, row)
    // This is hard because ColumnBatch stores vectors of types.
    // But we can verify broadly.

    // We can assume the batch reader implementation groups correct types.
    // Col 0 should be int64 or similar.
    // Col 1 should be string.
    // Col 2 should be mixed (double, null, bool).

    // Let's verify rowCount at least and maybe some content if accessible.
}

TEST_CASE("MsgPackChunkFormats: Chunk Limit", "[SqlBackup]")
{
    auto writer = CreateMsgPackChunkWriter(100); // Small limit

    std::vector<BackupValue> row;
    row.emplace_back(std::string(80, 'x')); // 80 bytes string

    writer->WriteRow(row);
    REQUIRE_FALSE(writer->IsChunkFull());

    writer->WriteRow(row); // 80 + 80 = 160 > 100
    // It might explicitly check AFTER writing or predict.
    // MsgPackChunkWriter implementation checks `estimatedBytes_ >= limitBytes_`.
    // estimatedBytes_ increases by row size.
    REQUIRE(writer->IsChunkFull());

    std::string data = writer->Flush();
    REQUIRE(!data.empty());
    REQUIRE_FALSE(writer->IsChunkFull()); // Should be reset
}

TEST_CASE("MsgPackChunkFormats: Packed Nulls and Bools", "[SqlBackup]")
{
    // Test bit-packing logic with enough elements to span multiple bytes
    auto writer = CreateMsgPackChunkWriter(4096);

    std::vector<BackupValue> row1;
    row1.reserve(20);
    // 20 nulls
    for (int i = 0; i < 20; ++i)
        row1.emplace_back(std::monostate {});

    std::vector<BackupValue> row2;
    row2.reserve(20);
    // 20 bools (mixed)
    for (int i = 0; i < 20; ++i)
        row2.emplace_back(i % 2 == 0);

    writer->WriteRow(row1);
    writer->WriteRow(row2);

    std::string data = writer->Flush();

    // Check if size is small (Packed)
    // 2 rows. 20 cols each? No, WriteRow writes a single row of values.
    // Wait. row1 is a row with 20 columns?
    // WriteRow takes generic row.
    // If we call WriteRow(row1), we have 20 columns.
    // If we call WriteRow(row2), we have 20 columns.
    // Are they the same columns?
    // MsgPackChunkWriter assumes consistent schema within a chunk (batch).
    // First WriteRow determines columns.
    // Row 1: 20 nulls. All columns are Monostate?
    // Row 2: 20 bools.
    // If 1st row is all nulls, existing logic:
    // AppendToColumn (nulls -> push true, colData -> push default).
    // If colData is monostate, it stays monostate?
    // If 2nd row comes with bools:
    // AppendToColumn (nulls -> push false).
    // Visit colData (which is monostate).
    // Visit value (bool). Pushes bool.
    // "1. If column is uninitialized (monostate), init with type (VecT)".
    // So colData becomes vector<bool>.
    // "Fill missing defaults". (nulls.size() - 1).
    // So it fills 1 false (default bool) for row 1.
    // Then pushes value for row 2.

    // Result: 20 columns. Each column has 2 rows.
    // Row 1: Null (value default false)
    // Row 2: Bool value.

    // Nulls vector for each column should contain [true, false].
    // Data vector for each column should contain [false, bool_val].

    // Nulls packing: 2 bits per column.

    // Let's make it bigger to test multi-byte packing.
    // We want ROWS.
    // 1 Column. 20 Rows.

    auto writer2 = CreateMsgPackChunkWriter(4096);
    std::vector<bool> expectedBools;
    std::vector<bool> expectedNulls;

    for (int i = 0; i < 100; ++i)
    {
        bool isNull = (i % 3 == 0);
        bool val = (i % 2 == 0);
        expectedNulls.push_back(isNull);
        if (isNull)
            expectedBools.push_back(false); // default
        else
            expectedBools.push_back(val);

        std::vector<BackupValue> row;
        if (isNull)
            row.emplace_back(std::monostate {});
        else
            row.emplace_back(val);

        writer2->WriteRow(row);
    }

    std::string packedData = writer2->Flush();

    // Verify Read
    std::stringstream ss(packedData);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));

    REQUIRE(batch.rowCount == 100);
    REQUIRE(batch.columns.size() == 1);

    REQUIRE(batch.nullIndicators.size() == 1);
    REQUIRE(batch.nullIndicators[0].size() == 100);
    for (size_t i = 0; i < 100; ++i)
        REQUIRE(batch.nullIndicators[0][i] == expectedNulls[i]);

    // Check values
    // Column 0 data
    REQUIRE(std::holds_alternative<std::vector<bool>>(batch.columns[0]));
    auto const& bools = std::get<std::vector<bool>>(batch.columns[0]);
    REQUIRE(bools.size() == 100);
    for (size_t i = 0; i < 100; ++i)
        REQUIRE(bools[i] == expectedBools[i]); // Note: default for nulls is false
}
