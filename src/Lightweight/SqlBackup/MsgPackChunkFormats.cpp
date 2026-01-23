// SPDX-License-Identifier: Apache-2.0
#include "MsgPackChunkFormats.hpp"

#include <bit>
#include <cstdio>
#include <cstring>
#include <format>
#include <iostream>
#include <optional>
#include <sstream>

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    #pragma GCC diagnostic ignored "-Wsign-compare"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
    #pragma GCC diagnostic ignored "-Wunused-const-variable"
#endif

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif

namespace Lightweight::SqlBackup
{

namespace
{

    // Helper to append a value to a column, promoting the column type if necessary.
    void AppendToColumn(ColumnBatch::ColumnData& colData, std::vector<bool>& nulls, BackupValue const& val)
    {
        // Handle NULL
        if (std::holds_alternative<std::monostate>(val))
        {
            nulls.push_back(true);
            std::visit(
                [](auto& vec) {
                    using VecT = std::decay_t<decltype(vec)>;
                    if constexpr (!std::is_same_v<VecT, std::monostate>)
                        vec.emplace_back(); // default construct
                },
                colData);
            return;
        }

        nulls.push_back(false);

        // Append logic
        std::visit(
            [&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                {
                    // Should be unreachable due to check above, but needed for compilation
                    return;
                }
                else
                {
                    using VecT = std::vector<T>;

                    // 1. If column is unitialized, initialize with this type
                    if (std::holds_alternative<std::monostate>(colData))
                    {
                        colData = VecT {};
                        // Fill missing defaults if nulls were pushed before
                        auto& newVec = std::get<VecT>(colData);
                        // We already pushed 'v' via constructor? No, VecT{} is empty.
                        // We need to fill (nulls.size() - 1) defaults, then push v.
                        if (nulls.size() > 1)
                            newVec.resize(nulls.size() - 1);
                        newVec.push_back(v);
                        return;
                    }

                    // 2. If types match, append
                    if (auto* vec = std::get_if<VecT>(&colData))
                    {
                        vec->push_back(v);
                        return;
                    }

                    // 3. Mismatch - Promote
                    // Strategy: If everything can be represented as String (fallback), do that.
                    // Or if Int -> Double.

                    // Simple Fallback: Promote Column to String
                    // (Unless column is already String, then just promote Value to String)

                    std::vector<std::string> newStrVec;
                    bool colIsString = std::holds_alternative<std::vector<std::string>>(colData);

                    if (!colIsString)
                    {
                        // Convert existing column to string
                        std::visit(
                            [&](auto& existingVec) {
                                using ExVecT = std::decay_t<decltype(existingVec)>;
                                if constexpr (std::is_same_v<ExVecT, std::vector<std::string>>)
                                {
                                    // Already string (handled by outer check but safe here)
                                    newStrVec = std::move(existingVec);
                                }
                                else if constexpr (!std::is_same_v<ExVecT, std::monostate>)
                                {
                                    newStrVec.reserve(existingVec.size() + 1);
                                    for (auto const& elem: existingVec)
                                    {
                                        if constexpr (std::is_same_v<typename ExVecT::value_type, std::vector<uint8_t>>)
                                            newStrVec.push_back("<binary>"); // TODO: base64?
                                        else if constexpr (std::is_same_v<typename ExVecT::value_type, bool>)
                                            newStrVec.push_back(static_cast<bool>(elem) ? "true" : "false");
                                        else
                                            newStrVec.push_back(std::format("{}", elem));
                                    }
                                }
                            },
                            colData);

                        colData = std::move(newStrVec);
                    }

                    // Now append new value as string
                    auto& strVec = std::get<std::vector<std::string>>(colData);
                    if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
                        strVec.push_back("<binary>");
                    else
                        strVec.push_back(std::format("{}", v));
                }
            },
            val);
    }

    // --- MsgPack Implementation ---

    // Constants (Mirrored from msgpack spec/header for self-sufficiency in reader)
    namespace Mp
    {
#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-const-variable"
#endif
        constexpr uint8_t Nil = 0xC0;
        constexpr uint8_t False = 0xC2;
        constexpr uint8_t True = 0xC3;
        constexpr uint8_t Bin8 = 0xC4;
        constexpr uint8_t Bin16 = 0xC5;
        constexpr uint8_t Bin32 = 0xC6;
        constexpr uint8_t Float32 = 0xCA;
        constexpr uint8_t Float64 = 0xCB;
        constexpr uint8_t Uint8 = 0xCC;
        constexpr uint8_t Uint16 = 0xCD;
        constexpr uint8_t Uint32 = 0xCE;
        constexpr uint8_t Uint64 = 0xCF;
        constexpr uint8_t Int8 = 0xD0;
        constexpr uint8_t Int16 = 0xD1;
        constexpr uint8_t Int32 = 0xD2;
        constexpr uint8_t Int64 = 0xD3;
        constexpr uint8_t Str8 = 0xD9;
        constexpr uint8_t Str16 = 0xDA;
        constexpr uint8_t Str32 = 0xDB;
        constexpr uint8_t Array16 = 0xDC;
        constexpr uint8_t Array32 = 0xDD;
        constexpr uint8_t Map16 = 0xDE;
        constexpr uint8_t Map32 = 0xDF;
#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif
    }; // namespace Mp

    class MsgPackChunkWriter: public ChunkWriter
    {
      public:
        explicit MsgPackChunkWriter(size_t limitBytes):
            limitBytes_(limitBytes)
        {
        }

        void WriteRow(std::span<BackupValue const> row) override
        {
            if (batch_.columns.empty())
            {
                batch_.columns.resize(row.size());
                batch_.nullIndicators.resize(row.size());
            }

            for (size_t i = 0; i < row.size(); ++i)
            {
                AppendToColumn(batch_.columns[i], batch_.nullIndicators[i], row[i]);

                // Track estimated size
                std::visit(
                    [&](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::string>)
                            estimatedBytes_ += arg.size() + 5;
                        else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
                            estimatedBytes_ += arg.size() + 5;
                        else if constexpr (std::is_same_v<T, std::monostate>)
                            estimatedBytes_ += 1;
                        else
                            estimatedBytes_ += 9; // 8 bytes + type
                    },
                    row[i]);
            }
            batch_.rowCount++;
        }

        std::string Flush() override
        {
            // Serialize batch_ to Packed Columnar MsgPack
            // Format: Array of Columns (each column is a Map)

            size_t numCols = batch_.columns.size();
            WriteArrayHeader(numCols);

            for (size_t i = 0; i < numCols; ++i)
            {
                // Write Column Map
                // { "t": type, "d": data, "n": nulls }
                // For simplicity, we just use a simplified format for now:
                // Map(3) or Map(2)

                auto& col = batch_.columns[i];
                auto& nulls = batch_.nullIndicators[i];

                WriteMapHeader(3);

                // 1. Type
                WriteString("t");
                std::visit(
                    [&](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>)
                            WriteString("nil");
                        else if constexpr (std::is_same_v<T, std::vector<int64_t>>)
                            WriteString("i64");
                        else if constexpr (std::is_same_v<T, std::vector<double>>)
                            WriteString("f64");
                        else if constexpr (std::is_same_v<T, std::vector<std::string>>)
                            WriteString("str");
                        else if constexpr (std::is_same_v<T, std::vector<std::vector<uint8_t>>>)
                            WriteString("bin");
                        else if constexpr (std::is_same_v<T, std::vector<bool>>)
                            WriteString("bool");
                    },
                    col);

                // 2. Data
                WriteString("d");
                std::visit(
                    [&](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>)
                        {
                            WriteNil();
                        }
                        else if constexpr (std::is_same_v<T, std::vector<int64_t>>)
                        {
                            // Packed Binary of 64-bit integers
                            WritePackedData(arg);
                        }
                        else if constexpr (std::is_same_v<T, std::vector<double>>)
                        {
                            WritePackedData(arg);
                        }
                        else if constexpr (std::is_same_v<T, std::vector<bool>>)
                        {
                            WriteBitPackedArray(arg);
                        }
                        else if constexpr (std::is_same_v<T, std::vector<std::string>>)
                        {
                            WriteArrayHeader(arg.size());
                            for (auto const& s: arg)
                                WriteString(s);
                        }
                        else if constexpr (std::is_same_v<T, std::vector<std::vector<uint8_t>>>)
                        {
                            WriteArrayHeader(arg.size());
                            for (auto const& bin: arg)
                            {
                                size_t const len = bin.size();
                                if (len <= 0xFF)
                                {
                                    WriteU8(Mp::Bin8);
                                    WriteU8(static_cast<uint8_t>(len));
                                }
                                else if (len <= 0xFFFF)
                                {
                                    WriteU8(Mp::Bin16);
                                    WriteBe(static_cast<uint16_t>(len));
                                }
                                else
                                {
                                    WriteU8(Mp::Bin32);
                                    WriteBe(static_cast<uint32_t>(len));
                                }
                                buffer_.insert(buffer_.end(), bin.begin(), bin.end());
                            }
                        }
                        else
                        {
                            // generic array
                            WriteArrayHeader(arg.size());
                            // ... implement if needed
                        }
                    },
                    col);

                // 3. Nulls (Packed Bits)
                WriteString("n");
                WriteBitPackedArray(nulls);
            }

            std::string s(reinterpret_cast<char const*>(buffer_.data()), buffer_.size());
            Clear();
            return s;
        }

        void Clear() override
        {
            buffer_.clear();
            batch_.Clear();
            estimatedBytes_ = 0;
        }

        [[nodiscard]] bool IsChunkFull() const override
        {
            return estimatedBytes_ >= limitBytes_ || buffer_.size() >= limitBytes_ || batch_.rowCount >= 100000;
        }

      private:
        size_t limitBytes_;
        std::vector<uint8_t> buffer_;
        ColumnBatch batch_;
        size_t estimatedBytes_ = 0;

        // ... helper write functions ...
        void WriteNil()
        {
            buffer_.push_back(Mp::Nil);
        }
        void WriteBool(bool b)
        {
            buffer_.push_back(b ? Mp::True : Mp::False);
        }
        void WriteU8(uint8_t v)
        {
            buffer_.push_back(v);
        }
        template <typename T>
        void WriteBe(T v)
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                auto u = std::bit_cast<std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>>(v);
                WriteBe(u);
            }
            else
            {
                if constexpr (std::endian::native == std::endian::little)
                    v = std::byteswap(v);
                auto p = reinterpret_cast<uint8_t const*>(&v);
                buffer_.insert(buffer_.end(), p, p + sizeof(T));
            }
        }

        void WriteArrayHeader(size_t n)
        {
            if (n <= 15)
                WriteU8(static_cast<uint8_t>(0x90 | n));
            else if (n <= 0xFFFF)
            {
                WriteU8(Mp::Array16);
                WriteBe(static_cast<uint16_t>(n));
            }
            else
            {
                WriteU8(Mp::Array32);
                WriteBe(static_cast<uint32_t>(n));
            }
        }

        void WriteMapHeader(size_t n)
        {
            if (n <= 15)
                WriteU8(static_cast<uint8_t>(0x80 | n));
            else if (n <= 0xFFFF)
            {
                WriteU8(Mp::Map16);
                WriteBe(static_cast<uint16_t>(n));
            }
            else
            {
                WriteU8(Mp::Map32);
                WriteBe(static_cast<uint32_t>(n));
            }
        }

        void WriteString(std::string_view s)
        {
            size_t n = s.size();
            if (n <= 31)
                WriteU8(static_cast<uint8_t>(0xA0 | n));
            else if (n <= 0xFF)
            {
                WriteU8(Mp::Str8);
                WriteU8(static_cast<uint8_t>(n));
            }
            else if (n <= 0xFFFF)
            {
                WriteU8(Mp::Str16);
                WriteBe(static_cast<uint16_t>(n));
            }
            else
            {
                WriteU8(Mp::Str32);
                WriteBe(static_cast<uint32_t>(n));
            }
            buffer_.insert(buffer_.end(), s.begin(), s.end());
        }

        template <typename T>
        void WritePackedData(std::vector<T> const& vec)
        {
            size_t const byteBytes = vec.size() * sizeof(T);
            if (byteBytes <= 0xFF)
            {
                WriteU8(Mp::Bin8);
                WriteU8(static_cast<uint8_t>(byteBytes));
            }
            else if (byteBytes <= 0xFFFF)
            {
                WriteU8(Mp::Bin16);
                WriteBe(static_cast<uint16_t>(byteBytes));
            }
            else
            {
                WriteU8(Mp::Bin32);
                WriteBe(static_cast<uint32_t>(byteBytes));
            }

            for (auto v: vec)
            {
                WriteBe(v); // Writes byteswapped value
            }
        }

        void WriteInt(size_t v)
        {
            if (v <= 0x7F)
            {
                WriteU8(static_cast<uint8_t>(v));
            }
            else if (v <= 0xFF)
            {
                WriteU8(Mp::Uint8);
                WriteU8(static_cast<uint8_t>(v));
            }
            else if (v <= 0xFFFF)
            {
                WriteU8(Mp::Uint16);
                WriteBe(static_cast<uint16_t>(v));
            }
            else if (v <= 0xFFFFFFFF)
            {
                WriteU8(Mp::Uint32);
                WriteBe(static_cast<uint32_t>(v));
            }
            else
            {
                WriteU8(Mp::Uint64);
                WriteBe(static_cast<uint64_t>(v));
            }
        }

        void WriteBitPackedArray(std::vector<bool> const& vec)
        {
            WriteArrayHeader(2);
            WriteInt(vec.size());

            size_t const packedBytes = (vec.size() + 7) / 8;
            std::vector<uint8_t> packed(packedBytes, 0);
            for (size_t i = 0; i < vec.size(); ++i)
            {
                if (vec[i])
                    packed[i / 8] |= (1 << (7 - (i % 8)));
            }

            // Write Binary
            if (packedBytes <= 0xFF)
            {
                WriteU8(Mp::Bin8);
                WriteU8(static_cast<uint8_t>(packedBytes));
            }
            else if (packedBytes <= 0xFFFF)
            {
                WriteU8(Mp::Bin16);
                WriteBe(static_cast<uint16_t>(packedBytes));
            }
            else
            {
                WriteU8(Mp::Bin32);
                WriteBe(static_cast<uint32_t>(packedBytes));
            }
            buffer_.insert(buffer_.end(), packed.begin(), packed.end());
        }
    };

    class MsgPackChunkReader: public ChunkReader
    {
      public:
        explicit MsgPackChunkReader(std::istream& input)
        {
            input.seekg(0, std::ios::end);
            auto const size = input.tellg();
            if (size > 0)
            {
                input.seekg(0, std::ios::beg);
                buffer_.resize(static_cast<size_t>(size));
                input.read(reinterpret_cast<char*>(buffer_.data()), size);
            }
            cursor_ = buffer_.data();
            end_ = buffer_.data() + buffer_.size();
        }

        bool ReadBatch(ColumnBatch& batch) override
        {
            if (cursor_ >= end_)
                return false;

            // Auto-detect format?
            // If strictly new format: expect Array of Columns.
            // But if we want back-compat, we check if it looks like [Val, Val] or [ {t:..}, ... ]
            // Simplification: Assume new format if first element is Map?
            // Actually, "Array of Columns". So top level is Array.
            // Inside, element 0 is Column. Column is Map.
            // Old format: Array of Rows. Row is Array.
            // So: Check top array. Check first element. If Map -> New. If Array -> Old.

            // BUT: "Packed Writer" implemented above writes Array of Maps.
            // "Baseline Writer" wrote Array of Arrays.

            // Let's implement reading the packed columns first.
            batch.Clear();

            uint32_t len = 0;
            if (!ReadArrayHeader(len))
                return false;

            // Iterate columns
            batch.columns.resize(len);
            batch.nullIndicators.resize(len);

            for (uint32_t i = 0; i < len; ++i)
            {
                // Read Column Map
                size_t mapLen = 0;
                ReadMapHeader(mapLen);

                std::string typeStr;

                for (size_t k = 0; k < mapLen; ++k)
                {
                    std::string key = ReadString();
                    if (key == "t")
                        typeStr = ReadString();
                    else if (key == "d")
                    {
                        if (typeStr == "i64")
                            ReadPackedInt64(batch.columns[i]);
                        else if (typeStr == "f64")
                            ReadPackedDouble(batch.columns[i]);
                        else if (typeStr == "str")
                            ReadStringArray(batch.columns[i]);
                        else if (typeStr == "bin")
                        {
                            ReadBinaryArray(batch.columns[i]);
                        }
                        else if (typeStr == "bool")
                            ReadBoolArray(batch.columns[i]);
                        else
                        {
                            SkipValue(); // unknown type?
                        }
                    }
                    else if (key == "n")
                    {
                        ReadBoolArray(batch.nullIndicators[i]); // stored as bool array for now
                    }
                    else
                        SkipValue();
                }
            }

            // Populate rowCount
            if (!batch.columns.empty())
            {
                if (!batch.nullIndicators.empty())
                    batch.rowCount = batch.nullIndicators[0].size();
                else
                    batch.rowCount = 0;
            }
            else
            {
                batch.rowCount = 0;
            }
            return true;
        }

      private:
        std::vector<uint8_t> buffer_;
        uint8_t const* cursor_ = nullptr;
        uint8_t const* end_ = nullptr;

        template <typename T>
        T ReadBe()
        {
            if (cursor_ + sizeof(T) > end_)
                throw std::out_of_range("MsgPackReader: Unexpected EOF reading primitive");
            T val;
            std::memcpy(&val, cursor_, sizeof(T));
            cursor_ += sizeof(T);
            if constexpr (std::endian::native == std::endian::little)
                return std::byteswap(val);
            else
                return val;
        }

        bool ReadArrayHeader(uint32_t& len)
        {
            if (cursor_ >= end_)
                return false;
            uint8_t head = *cursor_;
            if ((head & 0xF0) == 0x90)
            {
                cursor_++;
                len = head & 0x0F;
                return true;
            }
            if (head == Mp::Array16)
            {
                cursor_++;
                len = ReadBe<uint16_t>();
                return true;
            }
            if (head == Mp::Array32)
            {
                cursor_++;
                len = ReadBe<uint32_t>();
                return true;
            }
            return false;
        }

        void ReadMapHeader(size_t& len)
        {
            if (cursor_ >= end_)
                throw std::out_of_range("MsgPackReader: Unexpected EOF in ReadMapHeader");
            uint8_t head = *cursor_;
            if ((head & 0xF0) == 0x80)
            {
                cursor_++;
                len = head & 0x0F;
                return;
            }
            if (head == Mp::Map16)
            {
                cursor_++;
                len = ReadBe<uint16_t>();
                return;
            }
            if (head == Mp::Map32)
            {
                cursor_++;
                len = ReadBe<uint32_t>();
                return;
            }
            throw std::runtime_error("Expected Map");
        }

        std::string ReadString()
        {
            if (cursor_ >= end_)
                throw std::out_of_range("MsgPackReader: Unexpected EOF in ReadString");
            uint8_t head = *cursor_;
            size_t len = 0;
            if ((head & 0xE0) == 0xA0)
            {
                cursor_++;
                len = head & 0x1F;
            }
            else if (head == Mp::Str8)
            {
                cursor_++;
                len = ReadBe<uint8_t>();
            }
            else if (head == Mp::Str16)
            {
                cursor_++;
                len = ReadBe<uint16_t>();
            }
            else if (head == Mp::Str32)
            {
                cursor_++;
                len = ReadBe<uint32_t>();
            }
            else
                throw std::runtime_error("Expected String");

            if (cursor_ + len > end_)
                throw std::out_of_range("EOF");
            std::string s(reinterpret_cast<char const*>(cursor_), len);
            cursor_ += len;
            return s;
        }

        void ReadPackedInt64(ColumnBatch::ColumnData& col)
        {
            if (cursor_ >= end_)
                throw std::out_of_range("MsgPackReader: Unexpected EOF in ReadPackedInt64");

            uint8_t head = *cursor_++;
            uint32_t bytes = 0;
            if (head == Mp::Bin8)
                bytes = ReadBe<uint8_t>();
            else if (head == Mp::Bin16)
                bytes = ReadBe<uint16_t>();
            else if (head == Mp::Bin32)
                bytes = ReadBe<uint32_t>();

            size_t const count = bytes / 8;
            std::vector<int64_t> vec(count);

            if (cursor_ + bytes > end_)
                throw std::out_of_range("MsgPackReader: Unexpected EOF in ReadPackedInt64 Data");

            if (bytes > 0)
            {
                std::memcpy(vec.data(), cursor_, bytes);
                cursor_ += bytes;

                if constexpr (std::endian::native == std::endian::little)
                {
                    // Auto-vectorizable
                    for (size_t i = 0; i < count; ++i)
                        vec[i] = std::byteswap(vec[i]);
                }
            }

            col = std::move(vec);
        }

        void ReadPackedDouble(ColumnBatch::ColumnData& col)
        {
            if (cursor_ >= end_)
                throw std::out_of_range("MsgPackReader: Unexpected EOF in ReadPackedDouble");

            uint8_t head = *cursor_++;
            uint32_t bytes = 0;
            if (head == Mp::Bin8)
                bytes = ReadBe<uint8_t>();
            else if (head == Mp::Bin16)
                bytes = ReadBe<uint16_t>();
            else if (head == Mp::Bin32)
                bytes = ReadBe<uint32_t>();

            size_t const count = bytes / 8;
            std::vector<double> vec(count);

            if (cursor_ + bytes > end_)
                throw std::out_of_range("MsgPackReader: Unexpected EOF in ReadPackedDouble Data");

            // Read as uint64_t to swap, then cast
            // However, we can read directly into the vector memory, then fix up
            // std::vector<double> has double. We need to treat them as uint64 for swapping.
            static_assert(sizeof(double) == sizeof(uint64_t));

            if (bytes > 0)
            {
                std::memcpy(vec.data(), cursor_, bytes);
                cursor_ += bytes;

                if constexpr (std::endian::native == std::endian::little)
                {
                    // Type punning via pointer cast is UB, but practically works on most compilers?
                    // Better: std::bit_cast loop or simply reading as distinct buffer if strict aliasing is a concern.
                    // But wait, we can just iterate.
                    uint64_t* ptr = reinterpret_cast<uint64_t*>(vec.data());
                    for (size_t i = 0; i < count; ++i)
                    {
                        ptr[i] = std::byteswap(ptr[i]);
                    }
                }
            }

            col = std::move(vec);
        }

        void ReadStringArray(ColumnBatch::ColumnData& col)
        {
            uint32_t len = 0;
            if (!ReadArrayHeader(len))
                throw std::out_of_range("MsgPackReader: Unexpected EOF in ReadStringArray");

            std::vector<std::string> vec;
            vec.reserve(len);
            for (uint32_t i = 0; i < len; ++i)
                vec.push_back(ReadString());
            col = std::move(vec);
        }

        std::vector<uint8_t> ReadBinary()
        {
            if (cursor_ >= end_)
                throw std::out_of_range("MsgPackReader: Unexpected EOF in ReadBinary");

            uint8_t head = *cursor_;
            size_t len = 0;
            if (head == Mp::Bin8)
            {
                cursor_++;
                len = ReadBe<uint8_t>();
            }
            else if (head == Mp::Bin16)
            {
                cursor_++;
                len = ReadBe<uint16_t>();
            }
            else if (head == Mp::Bin32)
            {
                cursor_++;
                len = ReadBe<uint32_t>();
            }
            else
                throw std::runtime_error("Expected Binary");

            if (cursor_ + len > end_)
                throw std::out_of_range("EOF reading binary data");

            std::vector<uint8_t> data(cursor_, cursor_ + len);
            cursor_ += len;
            return data;
        }

        void ReadBinaryArray(ColumnBatch::ColumnData& col)
        {
            uint32_t len = 0;
            if (!ReadArrayHeader(len))
                throw std::out_of_range("MsgPackReader: Unexpected EOF in ReadBinaryArray");

            std::vector<std::vector<uint8_t>> vec;
            vec.reserve(len);
            for (uint32_t i = 0; i < len; ++i)
                vec.push_back(ReadBinary());
            col = std::move(vec);
        }

        // Helper to peek without consuming
        [[nodiscard]] uint8_t Peek() const
        {
            if (cursor_ >= end_)
                throw std::out_of_range("MsgPackReader: Unexpected EOF in Peek");
            return *cursor_;
        }

        uint64_t ReadInt()
        {
            if (cursor_ >= end_)
                throw std::out_of_range("MsgPackReader: EOF in ReadInt");
            uint8_t head = *cursor_++;
            if (head <= 0x7F)
                return head;
            if (head == Mp::Uint8)
                return ReadBe<uint8_t>();
            if (head == Mp::Uint16)
                return ReadBe<uint16_t>();
            if (head == Mp::Uint32)
                return ReadBe<uint32_t>();
            if (head == Mp::Uint64)
                return ReadBe<uint64_t>();
            throw std::runtime_error("Expected Integer");
        }

        void ReadBoolArray(std::vector<bool>& vec)
        {
            uint32_t len = 0;
            if (!ReadArrayHeader(len))
                throw std::out_of_range("MsgPackReader: Unexpected EOF in ReadBoolArray");

            // Check for packed format: Array(2) [Int(Count), Bin(PackedBits)]
            // Legacy format: Array(N) [Bool, Bool, ...]
            // Distinguish by checking if first element is Integer or Bool.
            // If len != 2, it's likely legacy (unless we have exactly 2 bools).
            // If len == 2, we peek.

            bool isPacked = false;
            if (len == 2)
            {
                if (cursor_ < end_)
                {
                    uint8_t next = Peek();
                    // Legacy bool array must start with True(0xC3) or False(0xC2).
                    // If it starts with anything else (Integer), it's the packed format.
                    if (next != Mp::False && next != Mp::True)
                        isPacked = true;
                }
            }

            if (isPacked)
            {
                uint64_t elementCount = ReadInt();
                std::vector<uint8_t> packed = ReadBinary();

                vec.resize(elementCount);
                for (size_t i = 0; i < elementCount; ++i)
                {
                    uint8_t byte = packed[i / 8];
                    // Unpack high-bit first
                    bool b = (byte >> (7 - (i % 8))) & 1;
                    vec[i] = b;
                }
            }
            else
            {
                vec.resize(len);
                for (uint32_t i = 0; i < len; ++i)
                {
                    if (cursor_ >= end_)
                        throw std::out_of_range("MsgPackReader: Unexpected EOF in boolean array");
                    uint8_t h = *cursor_++;
                    vec[i] = (h == Mp::True);
                }
            }
        }

        void ReadBoolArray(ColumnBatch::ColumnData& col)
        {
            std::vector<bool> vec;
            ReadBoolArray(vec);
            col = std::move(vec);
        }

        void SkipValue()
        {
            if (cursor_ >= end_)
                return;
            uint8_t head = *cursor_++;

            if (head <= 0x7F)
                return; // positive fixint
            if ((head & 0xE0) == 0xE0)
                return; // negative fixint
            if (head == Mp::Nil || head == Mp::False || head == Mp::True)
                return;

            if ((head & 0xE0) == 0xA0)
            { // fixstr
                uint8_t len = head & 0x1F;
                cursor_ += len;
                return;
            }
            if ((head & 0xF0) == 0x90)
            { // fixarray
                uint8_t len = head & 0x0F;
                for (int i = 0; i < len; ++i)
                    SkipValue();
                return;
            }
            if ((head & 0xF0) == 0x80)
            { // fixmap
                uint8_t len = head & 0x0F;
                for (int i = 0; i < len * 2; ++i)
                    SkipValue();
                return;
            }

            switch (head)
            {
                case Mp::Bin8:
                case Mp::Str8:
                    cursor_ += ReadBe<uint8_t>();
                    break;
                case Mp::Bin16:
                case Mp::Str16:
                    cursor_ += ReadBe<uint16_t>();
                    break;
                case Mp::Bin32:
                case Mp::Str32:
                    cursor_ += ReadBe<uint32_t>();
                    break;
                case 0xCC: // uint8
                case 0xD0: // int8
                    cursor_ += 1;
                    break;
                case 0xCD: // uint16
                case 0xD1: // int16
                    cursor_ += 2;
                    break;
                case 0xCE: // uint32
                case 0xD2: // int32
                case 0xCA: // float32
                    cursor_ += 4;
                    break;
                case 0xCF: // uint64
                case 0xD3: // int64
                case 0xCB: // float64
                    cursor_ += 8;
                    break;

                case Mp::Array16: {
                    uint16_t len = ReadBe<uint16_t>();
                    for (int i = 0; i < len; ++i)
                        SkipValue();
                    break;
                }
                case Mp::Array32: {
                    uint32_t len = ReadBe<uint32_t>();
                    for (uint32_t i = 0; i < len; ++i)
                        SkipValue();
                    break;
                }
                case Mp::Map16: {
                    uint16_t len = ReadBe<uint16_t>();
                    for (int i = 0; i < len * 2; ++i)
                        SkipValue();
                    break;
                }
                case Mp::Map32: {
                    uint32_t len = ReadBe<uint32_t>();
                    for (uint32_t i = 0; i < len * 2; ++i)
                        SkipValue();
                    break;
                }
                case 0xD4:
                    cursor_ += 1;
                    break; // fixext 1
                case 0xD5:
                    cursor_ += 2;
                    break; // fixext 2
                case 0xD6:
                    cursor_ += 4;
                    break; // fixext 4
                case 0xD7:
                    cursor_ += 8;
                    break; // fixext 8
                case 0xD8:
                    cursor_ += 16;
                    break; // fixext 16
                case 0xC7:
                    cursor_ += ReadBe<uint8_t>();
                    cursor_++;
                    break; // ext8 (+1 for type)
                case 0xC8:
                    cursor_ += ReadBe<uint16_t>();
                    cursor_++;
                    break; // ext16
                case 0xC9:
                    cursor_ += ReadBe<uint32_t>();
                    cursor_++;
                    break; // ext32
                default:
                    throw std::runtime_error(std::format("Unknown msgpack header in SkipValue: {:02X}", head));
            }
        }
    };

} // namespace

std::unique_ptr<ChunkWriter> CreateMsgPackChunkWriter(size_t limitBytes)
{
    return std::make_unique<MsgPackChunkWriter>(limitBytes);
}

std::unique_ptr<ChunkReader> CreateMsgPackChunkReader(std::istream& input)
{
    return std::make_unique<MsgPackChunkReader>(input);
}

} // namespace Lightweight::SqlBackup
