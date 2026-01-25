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
}

TEST_CASE("MsgPackChunkFormats: Chunk Limit", "[SqlBackup]")
{
    auto writer = CreateMsgPackChunkWriter(100); // Small limit

    std::vector<BackupValue> row;
    row.emplace_back(std::string(80, 'x')); // 80 bytes string

    writer->WriteRow(row);
    REQUIRE_FALSE(writer->IsChunkFull());

    writer->WriteRow(row);
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

    [[maybe_unused]] std::string data = writer->Flush();

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
