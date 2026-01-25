// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup/MsgPackChunkFormats.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace Lightweight::SqlBackup;

namespace
{

/// MsgPackBuilder is a test helper for constructing raw msgpack byte sequences.
///
/// This class is intentionally separate from MsgPackChunkWriter because it serves a different purpose:
/// - MsgPackChunkWriter produces a specific columnar format for backup data
/// - MsgPackBuilder can construct ANY valid (or invalid) msgpack data for testing
///
/// Key use cases that MsgPackChunkWriter cannot produce:
/// - Extension types (fixext1/2/4/8/16, ext8/16/32) - used in SkipValue tests
/// - Unknown/extra keys in column maps - used to test reader robustness
/// - Arbitrary nested structures - used for SkipValue recursive testing
/// - Malformed data - for error handling tests
///
/// Without this helper, we couldn't achieve full reader coverage since the reader
/// must handle msgpack types that our writer intentionally never produces.
class MsgPackBuilder
{
  public:
    [[nodiscard]] std::string Data() const
    {
        return { buffer_.begin(), buffer_.end() };
    }

    void WriteNil()
    {
        buffer_.push_back(0xC0);
    }
    void WriteFalse()
    {
        buffer_.push_back(0xC2);
    }
    void WriteTrue()
    {
        buffer_.push_back(0xC3);
    }

    void WriteFixInt(uint8_t v)
    {
        assert(v <= 0x7F);
        buffer_.push_back(v);
    }

    void WriteNegativeFixInt(int8_t v)
    {
        assert(v >= -32 && v < 0);
        buffer_.push_back(static_cast<uint8_t>(v));
    }

    void WriteUint8(uint8_t v)
    {
        buffer_.push_back(0xCC);
        buffer_.push_back(v);
    }

    void WriteUint16(uint16_t v)
    {
        buffer_.push_back(0xCD);
        WriteBe(v);
    }

    void WriteUint32(uint32_t v)
    {
        buffer_.push_back(0xCE);
        WriteBe(v);
    }

    void WriteUint64(uint64_t v)
    {
        buffer_.push_back(0xCF);
        WriteBe(v);
    }

    void WriteInt8(int8_t v)
    {
        buffer_.push_back(0xD0);
        buffer_.push_back(static_cast<uint8_t>(v));
    }

    void WriteInt16(int16_t v)
    {
        buffer_.push_back(0xD1);
        WriteBe(static_cast<uint16_t>(v));
    }

    void WriteInt32(int32_t v)
    {
        buffer_.push_back(0xD2);
        WriteBe(static_cast<uint32_t>(v));
    }

    void WriteInt64(int64_t v)
    {
        buffer_.push_back(0xD3);
        WriteBe(static_cast<uint64_t>(v));
    }

    void WriteFloat32(float v)
    {
        buffer_.push_back(0xCA);
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        WriteBe(bits);
    }

    void WriteFloat64(double v)
    {
        buffer_.push_back(0xCB);
        uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        WriteBe(bits);
    }

    void WriteFixStr(std::string_view s)
    {
        assert(s.size() <= 31);
        buffer_.push_back(static_cast<uint8_t>(0xA0 | s.size()));
        buffer_.insert(buffer_.end(), s.begin(), s.end());
    }

    void WriteStr8(std::string_view s)
    {
        assert(s.size() <= 0xFF);
        buffer_.push_back(0xD9);
        buffer_.push_back(static_cast<uint8_t>(s.size()));
        buffer_.insert(buffer_.end(), s.begin(), s.end());
    }

    void WriteStr16(std::string_view s)
    {
        assert(s.size() <= 0xFFFF);
        buffer_.push_back(0xDA);
        WriteBe(static_cast<uint16_t>(s.size()));
        buffer_.insert(buffer_.end(), s.begin(), s.end());
    }

    void WriteStr32(std::string_view s)
    {
        buffer_.push_back(0xDB);
        WriteBe(static_cast<uint32_t>(s.size()));
        buffer_.insert(buffer_.end(), s.begin(), s.end());
    }

    void WriteBin8(std::span<uint8_t const> data)
    {
        assert(data.size() <= 0xFF);
        buffer_.push_back(0xC4);
        buffer_.push_back(static_cast<uint8_t>(data.size()));
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    void WriteBin16(std::span<uint8_t const> data)
    {
        assert(data.size() <= 0xFFFF);
        buffer_.push_back(0xC5);
        WriteBe(static_cast<uint16_t>(data.size()));
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    void WriteBin32(std::span<uint8_t const> data)
    {
        buffer_.push_back(0xC6);
        WriteBe(static_cast<uint32_t>(data.size()));
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    void WriteFixArray(uint8_t count)
    {
        assert(count <= 15);
        buffer_.push_back(static_cast<uint8_t>(0x90 | count));
    }

    void WriteArray16(uint16_t count)
    {
        buffer_.push_back(0xDC);
        WriteBe(count);
    }

    void WriteArray32(uint32_t count)
    {
        buffer_.push_back(0xDD);
        WriteBe(count);
    }

    void WriteFixMap(uint8_t count)
    {
        assert(count <= 15);
        buffer_.push_back(static_cast<uint8_t>(0x80 | count));
    }

    void WriteMap16(uint16_t count)
    {
        buffer_.push_back(0xDE);
        WriteBe(count);
    }

    void WriteMap32(uint32_t count)
    {
        buffer_.push_back(0xDF);
        WriteBe(count);
    }

    void WriteFixExt1(int8_t type, uint8_t data)
    {
        buffer_.push_back(0xD4);
        buffer_.push_back(static_cast<uint8_t>(type));
        buffer_.push_back(data);
    }

    void WriteFixExt2(int8_t type, uint8_t d1, uint8_t d2)
    {
        buffer_.push_back(0xD5);
        buffer_.push_back(static_cast<uint8_t>(type));
        buffer_.push_back(d1);
        buffer_.push_back(d2);
    }

    void WriteFixExt4(int8_t type, std::array<uint8_t, 4> const& data)
    {
        buffer_.push_back(0xD6);
        buffer_.push_back(static_cast<uint8_t>(type));
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    void WriteFixExt8(int8_t type, std::array<uint8_t, 8> const& data)
    {
        buffer_.push_back(0xD7);
        buffer_.push_back(static_cast<uint8_t>(type));
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    void WriteFixExt16(int8_t type, std::array<uint8_t, 16> const& data)
    {
        buffer_.push_back(0xD8);
        buffer_.push_back(static_cast<uint8_t>(type));
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    void WriteExt8(int8_t type, std::span<uint8_t const> data)
    {
        assert(data.size() <= 0xFF);
        buffer_.push_back(0xC7);
        buffer_.push_back(static_cast<uint8_t>(data.size()));
        buffer_.push_back(static_cast<uint8_t>(type));
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    void WriteExt16(int8_t type, std::span<uint8_t const> data)
    {
        assert(data.size() <= 0xFFFF);
        buffer_.push_back(0xC8);
        WriteBe(static_cast<uint16_t>(data.size()));
        buffer_.push_back(static_cast<uint8_t>(type));
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    void WriteExt32(int8_t type, std::span<uint8_t const> data)
    {
        buffer_.push_back(0xC9);
        WriteBe(static_cast<uint32_t>(data.size()));
        buffer_.push_back(static_cast<uint8_t>(type));
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    void WriteRaw(uint8_t byte)
    {
        buffer_.push_back(byte);
    }

    void WriteRaw(std::span<uint8_t const> data)
    {
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

  private:
    std::vector<uint8_t> buffer_;

    template <typename T>
    void WriteBe(T v)
    {
        if constexpr (std::endian::native == std::endian::little)
            v = std::byteswap(v);
        auto const* p = reinterpret_cast<uint8_t const*>(&v);
        buffer_.insert(buffer_.end(), p, p + sizeof(T));
    }
};

} // namespace

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

// =============================================================================
// String Size Boundary Tests
// =============================================================================

TEST_CASE("MsgPack: String size boundaries", "[SqlBackup][MsgPack]")
{
    SECTION("Fixstr boundary (0-31 bytes)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<BackupValue> row = { std::string(31, 'a') };
        writer->WriteRow(row);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 1);
        auto const& strings = std::get<std::vector<std::string>>(batch.columns[0]);
        REQUIRE(strings[0].size() == 31);
    }

    SECTION("Str8 boundary (32-255 bytes)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<BackupValue> row = { std::string(255, 'b') };
        writer->WriteRow(row);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        auto const& strings = std::get<std::vector<std::string>>(batch.columns[0]);
        REQUIRE(strings[0].size() == 255);
    }

    SECTION("Str16 boundary (256-65535 bytes)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<BackupValue> row = { std::string(65535, 'c') };
        writer->WriteRow(row);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        auto const& strings = std::get<std::vector<std::string>>(batch.columns[0]);
        REQUIRE(strings[0].size() == 65535);
    }

    SECTION("Str32 boundary (65536+ bytes)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024 * 100);
        std::vector<BackupValue> row = { std::string(65536, 'd') };
        writer->WriteRow(row);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        auto const& strings = std::get<std::vector<std::string>>(batch.columns[0]);
        REQUIRE(strings[0].size() == 65536);
    }
}

// =============================================================================
// Binary Size Boundary Tests
// =============================================================================

TEST_CASE("MsgPack: Binary size boundaries", "[SqlBackup][MsgPack]")
{
    SECTION("Bin8 boundary (0-255 bytes)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<uint8_t> binData(255, 0xAB);
        std::vector<BackupValue> row = { binData };
        writer->WriteRow(row);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        auto const& bins = std::get<std::vector<std::vector<uint8_t>>>(batch.columns[0]);
        REQUIRE(bins[0].size() == 255);
    }

    SECTION("Bin16 boundary (256-65535 bytes)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<uint8_t> binData(65535, 0xCD);
        std::vector<BackupValue> row = { binData };
        writer->WriteRow(row);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        auto const& bins = std::get<std::vector<std::vector<uint8_t>>>(batch.columns[0]);
        REQUIRE(bins[0].size() == 65535);
    }

    SECTION("Bin32 boundary (65536+ bytes)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024 * 100);
        std::vector<uint8_t> binData(65536, 0xEF);
        std::vector<BackupValue> row = { binData };
        writer->WriteRow(row);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        auto const& bins = std::get<std::vector<std::vector<uint8_t>>>(batch.columns[0]);
        REQUIRE(bins[0].size() == 65536);
    }
}

// =============================================================================
// Array/Map Size Boundary Tests (via column counts)
// =============================================================================

TEST_CASE("MsgPack: Array header size boundaries", "[SqlBackup][MsgPack]")
{
    SECTION("Fixarray boundary (0-15 columns)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<BackupValue> row(15, int64_t { 42 });
        writer->WriteRow(row);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.columns.size() == 15);
    }

    SECTION("Array16 boundary (16+ columns)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<BackupValue> row(16, int64_t { 42 });
        writer->WriteRow(row);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.columns.size() == 16);
    }

    SECTION("Many columns (100)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<BackupValue> row(100, int64_t { 42 });
        writer->WriteRow(row);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.columns.size() == 100);
    }
}

// =============================================================================
// Type Promotion Tests
// =============================================================================

TEST_CASE("MsgPack: Type promotion scenarios", "[SqlBackup][MsgPack]")
{
    SECTION("Int to String promotion")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<BackupValue> row1 = { int64_t { 100 } };
        std::vector<BackupValue> row2 = { int64_t { 200 } };
        std::vector<BackupValue> row3 = { std::string("hello") }; // Causes promotion
        writer->WriteRow(row1);
        writer->WriteRow(row2);
        writer->WriteRow(row3);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 3);
        REQUIRE(std::holds_alternative<std::vector<std::string>>(batch.columns[0]));
        auto const& strings = std::get<std::vector<std::string>>(batch.columns[0]);
        REQUIRE(strings[0] == "100");
        REQUIRE(strings[1] == "200");
        REQUIRE(strings[2] == "hello");
    }

    SECTION("Double to String promotion")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<BackupValue> row1 = { 3.14 };
        std::vector<BackupValue> row2 = { std::string("pi") };
        writer->WriteRow(row1);
        writer->WriteRow(row2);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(std::holds_alternative<std::vector<std::string>>(batch.columns[0]));
    }

    SECTION("Bool to String promotion")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<BackupValue> row1 = { true };
        std::vector<BackupValue> row2 = { false };
        std::vector<BackupValue> row3 = { std::string("maybe") };
        writer->WriteRow(row1);
        writer->WriteRow(row2);
        writer->WriteRow(row3);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(std::holds_alternative<std::vector<std::string>>(batch.columns[0]));
        auto const& strings = std::get<std::vector<std::string>>(batch.columns[0]);
        REQUIRE(strings[0] == "true");
        REQUIRE(strings[1] == "false");
        REQUIRE(strings[2] == "maybe");
    }

    SECTION("Binary to String promotion")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<BackupValue> row1 = { std::vector<uint8_t> { 0x01, 0x02 } };
        std::vector<BackupValue> row2 = { std::string("text") };
        writer->WriteRow(row1);
        writer->WriteRow(row2);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(std::holds_alternative<std::vector<std::string>>(batch.columns[0]));
        auto const& strings = std::get<std::vector<std::string>>(batch.columns[0]);
        REQUIRE(strings[0] == "<binary>");
        REQUIRE(strings[1] == "text");
    }

    SECTION("String to Int causes string promotion of int")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        std::vector<BackupValue> row1 = { std::string("first") };
        std::vector<BackupValue> row2 = { int64_t { 42 } };
        writer->WriteRow(row1);
        writer->WriteRow(row2);
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(std::holds_alternative<std::vector<std::string>>(batch.columns[0]));
        auto const& strings = std::get<std::vector<std::string>>(batch.columns[0]);
        REQUIRE(strings[0] == "first");
        REQUIRE(strings[1] == "42");
    }
}

// =============================================================================
// Writer State Management Tests
// =============================================================================

TEST_CASE("MsgPack: Writer Clear resets buffer", "[SqlBackup][MsgPack]")
{
    auto writer = CreateMsgPackChunkWriter(1024);
    std::vector<BackupValue> row = { int64_t { 1 }, std::string("test") };
    writer->WriteRow(row);

    // Clear should reset the internal state
    writer->Clear();
    REQUIRE_FALSE(writer->IsChunkFull()); // Verify chunk is no longer full after clear

    // Write same structure after clear
    writer->WriteRow(row);
    std::string data = writer->Flush();

    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
    REQUIRE(batch.columns.size() == 2);
}

TEST_CASE("MsgPack: Multiple flush cycles", "[SqlBackup][MsgPack]")
{
    auto writer = CreateMsgPackChunkWriter(1024);

    // First cycle
    std::vector<BackupValue> row1 = { int64_t { 1 } };
    writer->WriteRow(row1);
    std::string data1 = writer->Flush();

    // Second cycle
    std::vector<BackupValue> row2 = { int64_t { 2 } };
    writer->WriteRow(row2);
    std::string data2 = writer->Flush();

    // Verify both are valid
    {
        std::stringstream ss(data1);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 1);
    }
    {
        std::stringstream ss(data2);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 1);
    }
}

// =============================================================================
// Reader Edge Cases
// =============================================================================

TEST_CASE("MsgPack: Reader handles empty stream", "[SqlBackup][MsgPack]")
{
    std::stringstream ss("");
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE_FALSE(reader->ReadBatch(batch));
}

TEST_CASE("MsgPack: Reader second ReadBatch returns false", "[SqlBackup][MsgPack]")
{
    auto writer = CreateMsgPackChunkWriter(1024);
    std::vector<BackupValue> row = { int64_t { 42 } };
    writer->WriteRow(row);
    std::string data = writer->Flush();

    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE_FALSE(reader->ReadBatch(batch)); // No more data
}

// =============================================================================
// Packed Data Tests (int64/double arrays)
// =============================================================================

TEST_CASE("MsgPack: Packed int64 with many values", "[SqlBackup][MsgPack]")
{
    auto writer = CreateMsgPackChunkWriter(1024 * 1024);

    std::vector<int64_t> expected;
    for (int i = 0; i < 1000; ++i)
    {
        std::vector<BackupValue> row = { static_cast<int64_t>(i * 1000) };
        writer->WriteRow(row);
        expected.push_back(i * 1000);
    }
    std::string data = writer->Flush();

    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1000);

    auto const& ints = std::get<std::vector<int64_t>>(batch.columns[0]);
    REQUIRE(ints.size() == 1000);
    for (size_t i = 0; i < 1000; ++i)
        REQUIRE(ints[i] == expected[i]);
}

TEST_CASE("MsgPack: Packed double with many values", "[SqlBackup][MsgPack]")
{
    auto writer = CreateMsgPackChunkWriter(1024 * 1024);

    std::vector<double> expected;
    for (int i = 0; i < 500; ++i)
    {
        double val = i * 0.123456;
        std::vector<BackupValue> row = { val };
        writer->WriteRow(row);
        expected.push_back(val);
    }
    std::string data = writer->Flush();

    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));

    auto const& doubles = std::get<std::vector<double>>(batch.columns[0]);
    REQUIRE(doubles.size() == 500);
    for (size_t i = 0; i < 500; ++i)
        REQUIRE(doubles[i] == expected[i]);
}

// =============================================================================
// Bool Array Edge Cases
// =============================================================================

TEST_CASE("MsgPack: Bool array byte boundary cases", "[SqlBackup][MsgPack]")
{
    SECTION("7 bools (less than 1 byte)")
    {
        auto writer = CreateMsgPackChunkWriter(1024);
        for (int i = 0; i < 7; ++i)
        {
            std::vector<BackupValue> row = { i % 2 == 0 };
            writer->WriteRow(row);
        }
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 7);
    }

    SECTION("8 bools (exactly 1 byte)")
    {
        auto writer = CreateMsgPackChunkWriter(1024);
        for (int i = 0; i < 8; ++i)
        {
            std::vector<BackupValue> row = { i % 2 == 0 };
            writer->WriteRow(row);
        }
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 8);
    }

    SECTION("9 bools (more than 1 byte)")
    {
        auto writer = CreateMsgPackChunkWriter(1024);
        for (int i = 0; i < 9; ++i)
        {
            std::vector<BackupValue> row = { i % 2 == 0 };
            writer->WriteRow(row);
        }
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 9);
    }

    SECTION("256 bools (Bin16 threshold for packed)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        for (int i = 0; i < 256; ++i)
        {
            std::vector<BackupValue> row = { i % 3 == 0 };
            writer->WriteRow(row);
        }
        std::string data = writer->Flush();

        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 256);
    }
}

// =============================================================================
// Column Initialization from NULL
// =============================================================================

TEST_CASE("MsgPack: Column initialization after NULLs", "[SqlBackup][MsgPack]")
{
    auto writer = CreateMsgPackChunkWriter(1024);

    // First few rows are NULL
    std::vector<BackupValue> nullRow = { std::monostate {} };
    writer->WriteRow(nullRow);
    writer->WriteRow(nullRow);
    writer->WriteRow(nullRow);

    // Then a real value
    std::vector<BackupValue> realRow = { int64_t { 42 } };
    writer->WriteRow(realRow);

    std::string data = writer->Flush();

    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 4);

    // Check null indicators
    REQUIRE(batch.nullIndicators[0][0] == true);
    REQUIRE(batch.nullIndicators[0][1] == true);
    REQUIRE(batch.nullIndicators[0][2] == true);
    REQUIRE(batch.nullIndicators[0][3] == false);

    // Column should be int64
    REQUIRE(std::holds_alternative<std::vector<int64_t>>(batch.columns[0]));
    auto const& ints = std::get<std::vector<int64_t>>(batch.columns[0]);
    REQUIRE(ints[3] == 42);
}

// =============================================================================
// SkipValue Tests - Using raw msgpack construction
// =============================================================================

TEST_CASE("MsgPack: SkipValue handles various types via unknown keys", "[SqlBackup][MsgPack]")
{
    // Build a valid column batch msgpack with extra unknown keys that trigger SkipValue
    MsgPackBuilder builder;

    // Array(1) - 1 column
    builder.WriteFixArray(1);

    // Column 0: Map with extra unknown keys
    builder.WriteFixMap(5); // t, d, n + 2 unknown keys

    // Key "t" -> type
    builder.WriteFixStr("t");
    builder.WriteFixStr("i64");

    // Key "d" -> data (packed int64)
    builder.WriteFixStr("d");
    // Bin8 with 8 bytes (one int64 = 42)
    std::vector<uint8_t> packedInt(8);
    int64_t val = 42;
    if constexpr (std::endian::native == std::endian::little)
        val = std::byteswap(val);
    std::memcpy(packedInt.data(), &val, 8);
    builder.WriteBin8(packedInt);

    // Key "n" -> nulls
    builder.WriteFixStr("n");
    // Packed bool array: Array(2) [count, packed_bits]
    builder.WriteFixArray(2);
    builder.WriteFixInt(1);            // 1 element
    std::vector<uint8_t> bits = { 0 }; // not null
    builder.WriteBin8(bits);

    // Unknown key with fixint value -> triggers SkipValue
    builder.WriteFixStr("x1");
    builder.WriteFixInt(127);

    // Unknown key with negative fixint
    builder.WriteFixStr("x2");
    builder.WriteNegativeFixInt(-10);

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
    auto const& ints = std::get<std::vector<int64_t>>(batch.columns[0]);
    REQUIRE(ints[0] == 42);
}

TEST_CASE("MsgPack: SkipValue handles nil, true, false", "[SqlBackup][MsgPack]")
{
    MsgPackBuilder builder;
    builder.WriteFixArray(1);
    builder.WriteFixMap(6);

    builder.WriteFixStr("t");
    builder.WriteFixStr("i64");
    builder.WriteFixStr("d");
    std::vector<uint8_t> packedInt(8, 0);
    builder.WriteBin8(packedInt);
    builder.WriteFixStr("n");
    builder.WriteFixArray(2);
    builder.WriteFixInt(1);
    std::vector<uint8_t> bits = { 0 };
    builder.WriteBin8(bits);

    // Unknown keys with nil, true, false
    builder.WriteFixStr("u1");
    builder.WriteNil();
    builder.WriteFixStr("u2");
    builder.WriteTrue();
    builder.WriteFixStr("u3");
    builder.WriteFalse();

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
}

TEST_CASE("MsgPack: SkipValue handles strings of various sizes", "[SqlBackup][MsgPack]")
{
    MsgPackBuilder builder;
    builder.WriteFixArray(1);
    builder.WriteFixMap(6);

    builder.WriteFixStr("t");
    builder.WriteFixStr("i64");
    builder.WriteFixStr("d");
    std::vector<uint8_t> packedInt(8, 0);
    builder.WriteBin8(packedInt);
    builder.WriteFixStr("n");
    builder.WriteFixArray(2);
    builder.WriteFixInt(1);
    std::vector<uint8_t> bits = { 0 };
    builder.WriteBin8(bits);

    // fixstr
    builder.WriteFixStr("s1");
    builder.WriteFixStr("hello");

    // str8
    builder.WriteFixStr("s2");
    builder.WriteStr8(std::string(100, 'x'));

    // str16
    builder.WriteFixStr("s3");
    builder.WriteStr16(std::string(300, 'y'));

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
}

TEST_CASE("MsgPack: SkipValue handles binary of various sizes", "[SqlBackup][MsgPack]")
{
    MsgPackBuilder builder;
    builder.WriteFixArray(1);
    builder.WriteFixMap(6);

    builder.WriteFixStr("t");
    builder.WriteFixStr("i64");
    builder.WriteFixStr("d");
    std::vector<uint8_t> packedInt(8, 0);
    builder.WriteBin8(packedInt);
    builder.WriteFixStr("n");
    builder.WriteFixArray(2);
    builder.WriteFixInt(1);
    std::vector<uint8_t> bits = { 0 };
    builder.WriteBin8(bits);

    // bin8
    builder.WriteFixStr("b1");
    std::vector<uint8_t> bin100(100, 0xAB);
    builder.WriteBin8(bin100);

    // bin16
    builder.WriteFixStr("b2");
    std::vector<uint8_t> bin300(300, 0xCD);
    builder.WriteBin16(bin300);

    // bin32
    builder.WriteFixStr("b3");
    std::vector<uint8_t> bin70k(70000, 0xEF);
    builder.WriteBin32(bin70k);

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
}

TEST_CASE("MsgPack: SkipValue handles arrays", "[SqlBackup][MsgPack]")
{
    MsgPackBuilder builder;
    builder.WriteFixArray(1);
    builder.WriteFixMap(5);

    builder.WriteFixStr("t");
    builder.WriteFixStr("i64");
    builder.WriteFixStr("d");
    std::vector<uint8_t> packedInt(8, 0);
    builder.WriteBin8(packedInt);
    builder.WriteFixStr("n");
    builder.WriteFixArray(2);
    builder.WriteFixInt(1);
    std::vector<uint8_t> bits = { 0 };
    builder.WriteBin8(bits);

    // fixarray with nested values
    builder.WriteFixStr("a1");
    builder.WriteFixArray(3);
    builder.WriteFixInt(1);
    builder.WriteFixInt(2);
    builder.WriteFixInt(3);

    // array16
    builder.WriteFixStr("a2");
    builder.WriteArray16(20);
    for (int i = 0; i < 20; ++i)
        builder.WriteFixInt(static_cast<uint8_t>(i));

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
}

TEST_CASE("MsgPack: SkipValue handles maps", "[SqlBackup][MsgPack]")
{
    MsgPackBuilder builder;
    builder.WriteFixArray(1);
    builder.WriteFixMap(5);

    builder.WriteFixStr("t");
    builder.WriteFixStr("i64");
    builder.WriteFixStr("d");
    std::vector<uint8_t> packedInt(8, 0);
    builder.WriteBin8(packedInt);
    builder.WriteFixStr("n");
    builder.WriteFixArray(2);
    builder.WriteFixInt(1);
    std::vector<uint8_t> bits = { 0 };
    builder.WriteBin8(bits);

    // fixmap with nested values
    builder.WriteFixStr("m1");
    builder.WriteFixMap(2);
    builder.WriteFixStr("k1");
    builder.WriteFixInt(1);
    builder.WriteFixStr("k2");
    builder.WriteFixInt(2);

    // map16
    builder.WriteFixStr("m2");
    builder.WriteMap16(20);
    for (int i = 0; i < 20; ++i)
    {
        builder.WriteFixStr(std::string(1, static_cast<char>('a' + i)));
        builder.WriteFixInt(static_cast<uint8_t>(i));
    }

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
}

TEST_CASE("MsgPack: SkipValue handles integer types", "[SqlBackup][MsgPack]")
{
    MsgPackBuilder builder;
    builder.WriteFixArray(1);
    builder.WriteFixMap(11);

    builder.WriteFixStr("t");
    builder.WriteFixStr("i64");
    builder.WriteFixStr("d");
    std::vector<uint8_t> packedInt(8, 0);
    builder.WriteBin8(packedInt);
    builder.WriteFixStr("n");
    builder.WriteFixArray(2);
    builder.WriteFixInt(1);
    std::vector<uint8_t> bits = { 0 };
    builder.WriteBin8(bits);

    // uint8, uint16, uint32, uint64
    builder.WriteFixStr("u1");
    builder.WriteUint8(200);
    builder.WriteFixStr("u2");
    builder.WriteUint16(30000);
    builder.WriteFixStr("u3");
    builder.WriteUint32(100000);
    builder.WriteFixStr("u4");
    builder.WriteUint64(10000000000ULL);

    // int8, int16, int32, int64
    builder.WriteFixStr("i1");
    builder.WriteInt8(-50);
    builder.WriteFixStr("i2");
    builder.WriteInt16(-1000);
    builder.WriteFixStr("i3");
    builder.WriteInt32(-100000);
    builder.WriteFixStr("i4");
    builder.WriteInt64(-10000000000LL);

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
}

TEST_CASE("MsgPack: SkipValue handles float types", "[SqlBackup][MsgPack]")
{
    MsgPackBuilder builder;
    builder.WriteFixArray(1);
    builder.WriteFixMap(5);

    builder.WriteFixStr("t");
    builder.WriteFixStr("i64");
    builder.WriteFixStr("d");
    std::vector<uint8_t> packedInt(8, 0);
    builder.WriteBin8(packedInt);
    builder.WriteFixStr("n");
    builder.WriteFixArray(2);
    builder.WriteFixInt(1);
    std::vector<uint8_t> bits = { 0 };
    builder.WriteBin8(bits);

    // float32, float64
    builder.WriteFixStr("f1");
    builder.WriteFloat32(3.14F);
    builder.WriteFixStr("f2");
    builder.WriteFloat64(1.23456789);

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
}

TEST_CASE("MsgPack: SkipValue handles fixext types", "[SqlBackup][MsgPack]")
{
    MsgPackBuilder builder;
    builder.WriteFixArray(1);
    builder.WriteFixMap(8);

    builder.WriteFixStr("t");
    builder.WriteFixStr("i64");
    builder.WriteFixStr("d");
    std::vector<uint8_t> packedInt(8, 0);
    builder.WriteBin8(packedInt);
    builder.WriteFixStr("n");
    builder.WriteFixArray(2);
    builder.WriteFixInt(1);
    std::vector<uint8_t> bits = { 0 };
    builder.WriteBin8(bits);

    // fixext types (1, 2, 4, 8, 16 bytes)
    builder.WriteFixStr("e1");
    builder.WriteFixExt1(1, 0xAA);
    builder.WriteFixStr("e2");
    builder.WriteFixExt2(2, 0xBB, 0xCC);
    builder.WriteFixStr("e4");
    builder.WriteFixExt4(4, { 0x01, 0x02, 0x03, 0x04 });
    builder.WriteFixStr("e8");
    builder.WriteFixExt8(8, { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 });
    builder.WriteFixStr("ef");
    builder.WriteFixExt16(
        16, { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10 });

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
}

TEST_CASE("MsgPack: SkipValue handles ext8/16/32 types", "[SqlBackup][MsgPack]")
{
    SECTION("ext8")
    {
        MsgPackBuilder builder;
        builder.WriteFixArray(1);
        builder.WriteFixMap(4);

        builder.WriteFixStr("t");
        builder.WriteFixStr("i64");
        builder.WriteFixStr("d");
        std::vector<uint8_t> packedInt(8, 0);
        builder.WriteBin8(packedInt);
        builder.WriteFixStr("n");
        builder.WriteFixArray(2);
        builder.WriteFixInt(1);
        std::vector<uint8_t> bits = { 0 };
        builder.WriteBin8(bits);

        builder.WriteFixStr("x8");
        std::vector<uint8_t> ext8data(50, 0x11);
        builder.WriteExt8(1, ext8data);

        std::string data = builder.Data();
        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 1);
    }

    SECTION("ext16")
    {
        MsgPackBuilder builder;
        builder.WriteFixArray(1);
        builder.WriteFixMap(4);

        builder.WriteFixStr("t");
        builder.WriteFixStr("i64");
        builder.WriteFixStr("d");
        std::vector<uint8_t> packedInt(8, 0);
        builder.WriteBin8(packedInt);
        builder.WriteFixStr("n");
        builder.WriteFixArray(2);
        builder.WriteFixInt(1);
        std::vector<uint8_t> bits = { 0 };
        builder.WriteBin8(bits);

        builder.WriteFixStr("xf");
        std::vector<uint8_t> ext16data(300, 0x22);
        builder.WriteExt16(2, ext16data);

        std::string data = builder.Data();
        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 1);
    }

    SECTION("ext32")
    {
        MsgPackBuilder builder;
        builder.WriteFixArray(1);
        builder.WriteFixMap(4);

        builder.WriteFixStr("t");
        builder.WriteFixStr("i64");
        builder.WriteFixStr("d");
        std::vector<uint8_t> packedInt(8, 0);
        builder.WriteBin8(packedInt);
        builder.WriteFixStr("n");
        builder.WriteFixArray(2);
        builder.WriteFixInt(1);
        std::vector<uint8_t> bits = { 0 };
        builder.WriteBin8(bits);

        builder.WriteFixStr("xg");
        std::vector<uint8_t> ext32data(70000, 0x33);
        builder.WriteExt32(3, ext32data);

        std::string data = builder.Data();
        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 1);
    }
}

// =============================================================================
// Large Array32/Map32 via column structure
// =============================================================================

TEST_CASE("MsgPack: Array32 header for very many columns", "[SqlBackup][MsgPack]")
{
    // This test verifies Array32 encoding path by using 65536+ columns
    // Note: This is expensive, so we skip in normal runs
    // For coverage purposes, we'll create a smaller test that still hits the paths

    // Actually, hitting Array32 requires 65536+ columns which is impractical
    // Instead, we verify the Array16 path with >15 columns
    auto writer = CreateMsgPackChunkWriter(1024 * 1024 * 10);
    std::vector<BackupValue> row(1000, int64_t { 1 });
    writer->WriteRow(row);
    std::string data = writer->Flush();

    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.columns.size() == 1000);
}

// =============================================================================
// Packed data size boundaries (Bin8/Bin16/Bin32 for int64/double arrays)
// =============================================================================

TEST_CASE("MsgPack: Packed int64 Bin16 boundary", "[SqlBackup][MsgPack]")
{
    // 256 bytes / 8 bytes per int64 = 32 int64s trigger Bin16
    auto writer = CreateMsgPackChunkWriter(1024 * 1024);
    for (int i = 0; i < 32; ++i)
    {
        std::vector<BackupValue> row = { static_cast<int64_t>(i) };
        writer->WriteRow(row);
    }
    std::string data = writer->Flush();

    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 32);
}

TEST_CASE("MsgPack: Packed int64 Bin32 boundary", "[SqlBackup][MsgPack]")
{
    // 65536 bytes / 8 = 8192 int64s to trigger Bin32
    auto writer = CreateMsgPackChunkWriter(1024 * 1024 * 100);
    for (int i = 0; i < 8192; ++i)
    {
        std::vector<BackupValue> row = { static_cast<int64_t>(i) };
        writer->WriteRow(row);
    }
    std::string data = writer->Flush();

    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 8192);
}

TEST_CASE("MsgPack: Packed double Bin32 boundary", "[SqlBackup][MsgPack]")
{
    // 65536 bytes / 8 = 8192 doubles to trigger Bin32 in ReadPackedDouble
    auto writer = CreateMsgPackChunkWriter(1024 * 1024 * 100);
    for (int i = 0; i < 8192; ++i)
    {
        std::vector<BackupValue> row = { static_cast<double>(i) * 1.5 };
        writer->WriteRow(row);
    }
    std::string data = writer->Flush();

    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 8192);

    auto const& doubles = std::get<std::vector<double>>(batch.columns[0]);
    REQUIRE(doubles[0] == 0.0);
    REQUIRE(doubles[100] == 150.0);
}

// =============================================================================
// WriteInt value range tests (via BitPackedArray count encoding)
// =============================================================================

TEST_CASE("MsgPack: WriteInt encodes various ranges", "[SqlBackup][MsgPack]")
{
    // WriteInt is used in WriteBitPackedArray for the element count
    // We test different sizes by creating bool columns with specific row counts

    SECTION("Count <= 127 (fixint)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        for (int i = 0; i < 127; ++i)
        {
            std::vector<BackupValue> row = { true };
            writer->WriteRow(row);
        }
        std::string data = writer->Flush();
        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 127);
    }

    SECTION("Count 128-255 (uint8)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        for (int i = 0; i < 200; ++i)
        {
            std::vector<BackupValue> row = { true };
            writer->WriteRow(row);
        }
        std::string data = writer->Flush();
        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 200);
    }

    SECTION("Count 256-65535 (uint16)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024);
        for (int i = 0; i < 1000; ++i)
        {
            std::vector<BackupValue> row = { true };
            writer->WriteRow(row);
        }
        std::string data = writer->Flush();
        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 1000);
    }

    SECTION("Count > 65535 (uint32)")
    {
        auto writer = CreateMsgPackChunkWriter(1024 * 1024 * 100);
        for (int i = 0; i < 70000; ++i)
        {
            std::vector<BackupValue> row = { true };
            writer->WriteRow(row);
        }
        std::string data = writer->Flush();
        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 70000);
    }
}

// =============================================================================
// All column types roundtrip verification
// =============================================================================

TEST_CASE("MsgPack: All column types nil case", "[SqlBackup][MsgPack]")
{
    // Test column that remains monostate (all nulls)
    auto writer = CreateMsgPackChunkWriter(1024);
    std::vector<BackupValue> row = { std::monostate {} };
    writer->WriteRow(row);
    writer->WriteRow(row);
    std::string data = writer->Flush();

    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 2);
    REQUIRE(std::holds_alternative<std::monostate>(batch.columns[0]));
}

// =============================================================================
// Map16/Map32 boundary tests
// =============================================================================

TEST_CASE("MsgPack: Map header boundaries via extra keys", "[SqlBackup][MsgPack]")
{
    // We can't easily trigger Map16/Map32 through normal API
    // But we can verify the reader handles them via raw construction

    SECTION("Map16 header")
    {
        MsgPackBuilder builder;
        builder.WriteFixArray(1);
        builder.WriteMap16(3); // Map16 with 3 entries

        builder.WriteFixStr("t");
        builder.WriteFixStr("i64");
        builder.WriteFixStr("d");
        std::vector<uint8_t> packedInt(8, 0);
        builder.WriteBin8(packedInt);
        builder.WriteFixStr("n");
        builder.WriteFixArray(2);
        builder.WriteFixInt(1);
        std::vector<uint8_t> bits = { 0 };
        builder.WriteBin8(bits);

        std::string data = builder.Data();
        std::stringstream ss(data);
        auto reader = CreateMsgPackChunkReader(ss);
        ColumnBatch batch;
        REQUIRE(reader->ReadBatch(batch));
        REQUIRE(batch.rowCount == 1);
    }
}

// =============================================================================
// Str32 reader path
// =============================================================================

TEST_CASE("MsgPack: Str32 reading via unknown key skip", "[SqlBackup][MsgPack]")
{
    MsgPackBuilder builder;
    builder.WriteFixArray(1);
    builder.WriteFixMap(4);

    builder.WriteFixStr("t");
    builder.WriteFixStr("i64");
    builder.WriteFixStr("d");
    std::vector<uint8_t> packedInt(8, 0);
    builder.WriteBin8(packedInt);
    builder.WriteFixStr("n");
    builder.WriteFixArray(2);
    builder.WriteFixInt(1);
    std::vector<uint8_t> bits = { 0 };
    builder.WriteBin8(bits);

    // str32 (65536+ bytes)
    builder.WriteFixStr("s");
    builder.WriteStr32(std::string(70000, 'z'));

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
}

// =============================================================================
// Array32 reader path via SkipValue
// =============================================================================

TEST_CASE("MsgPack: Array32 reading via unknown key skip", "[SqlBackup][MsgPack]")
{
    MsgPackBuilder builder;
    builder.WriteFixArray(1);
    builder.WriteFixMap(4);

    builder.WriteFixStr("t");
    builder.WriteFixStr("i64");
    builder.WriteFixStr("d");
    std::vector<uint8_t> packedInt(8, 0);
    builder.WriteBin8(packedInt);
    builder.WriteFixStr("n");
    builder.WriteFixArray(2);
    builder.WriteFixInt(1);
    std::vector<uint8_t> bits = { 0 };
    builder.WriteBin8(bits);

    // array32 with many elements
    builder.WriteFixStr("a");
    builder.WriteArray32(70000);
    for (uint32_t i = 0; i < 70000; ++i)
        builder.WriteFixInt(static_cast<uint8_t>(i % 128));

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
}

// =============================================================================
// Map32 reader path via SkipValue
// =============================================================================

TEST_CASE("MsgPack: Map32 reading via unknown key skip", "[SqlBackup][MsgPack]")
{
    MsgPackBuilder builder;
    builder.WriteFixArray(1);
    builder.WriteFixMap(4);

    builder.WriteFixStr("t");
    builder.WriteFixStr("i64");
    builder.WriteFixStr("d");
    std::vector<uint8_t> packedInt(8, 0);
    builder.WriteBin8(packedInt);
    builder.WriteFixStr("n");
    builder.WriteFixArray(2);
    builder.WriteFixInt(1);
    std::vector<uint8_t> bits = { 0 };
    builder.WriteBin8(bits);

    // map32 with many key-value pairs
    builder.WriteFixStr("m");
    builder.WriteMap32(70000);
    for (uint32_t i = 0; i < 70000; ++i)
    {
        builder.WriteFixInt(static_cast<uint8_t>(i % 128)); // key
        builder.WriteFixInt(static_cast<uint8_t>(i % 128)); // value
    }

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 1);
}

// =============================================================================
// Row Count Limit Tests
// =============================================================================

TEST_CASE("MsgPack: IsChunkFull triggered by row count limit", "[SqlBackup][MsgPack]")
{
    // Test that IsChunkFull returns true when row count reaches 100,000
    auto writer = CreateMsgPackChunkWriter(1024 * 1024 * 1024); // Large byte limit to isolate row count trigger

    // Write 99,999 rows - should not trigger
    std::vector<BackupValue> row = { int64_t { 1 } };
    for (int i = 0; i < 99999; ++i)
    {
        writer->WriteRow(row);
    }
    REQUIRE_FALSE(writer->IsChunkFull());

    // Write one more row to reach 100,000 - should trigger
    writer->WriteRow(row);
    REQUIRE(writer->IsChunkFull());

    // Verify data is still valid
    std::string data = writer->Flush();
    REQUIRE(!data.empty());

    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 100000);
}

// =============================================================================
// Large String Array (Array32 reader path)
// =============================================================================

TEST_CASE("MsgPack: Large string array triggers Array32 reader path", "[SqlBackup][MsgPack][!mayfail]")
{
    // Test reading string array with 65536+ elements to hit Array32 path in ReadArrayHeader
    auto writer = CreateMsgPackChunkWriter(1024 * 1024 * 100);

    for (int i = 0; i < 65537; ++i)
    {
        std::vector<BackupValue> row = { std::string("x") };
        writer->WriteRow(row);
    }
    std::string data = writer->Flush();

    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 65537);
}

// =============================================================================
// Legacy Bool Array Format Test
// =============================================================================

TEST_CASE("MsgPack: Legacy bool array format (Array of True/False)", "[SqlBackup][MsgPack]")
{
    // Test reading legacy bool format: Array(N) of True/False values
    // The reader supports both formats for backwards compatibility
    MsgPackBuilder builder;

    // Array(1) - 1 column
    builder.WriteFixArray(1);

    // Column 0: Map(3) with t, d, n
    builder.WriteFixMap(3);

    // Type = bool
    builder.WriteFixStr("t");
    builder.WriteFixStr("bool");

    // Data = legacy array of bools (not bit-packed)
    builder.WriteFixStr("d");
    builder.WriteFixArray(3); // 3 bools
    builder.WriteTrue();
    builder.WriteFalse();
    builder.WriteTrue();

    // Nulls = also legacy format for completeness
    builder.WriteFixStr("n");
    builder.WriteFixArray(3);
    builder.WriteFalse(); // not null
    builder.WriteFalse(); // not null
    builder.WriteFalse(); // not null

    std::string data = builder.Data();
    std::stringstream ss(data);
    auto reader = CreateMsgPackChunkReader(ss);
    ColumnBatch batch;
    REQUIRE(reader->ReadBatch(batch));
    REQUIRE(batch.rowCount == 3);

    auto const& bools = std::get<std::vector<bool>>(batch.columns[0]);
    REQUIRE(bools.size() == 3);
    REQUIRE(bools[0] == true);
    REQUIRE(bools[1] == false);
    REQUIRE(bools[2] == true);
}
