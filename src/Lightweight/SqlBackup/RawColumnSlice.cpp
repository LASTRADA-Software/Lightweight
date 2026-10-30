// SPDX-License-Identifier: Apache-2.0

#include "../DataBinder/SqlGuid.hpp"
#include "../DataBinder/UnicodeConverter.hpp"
#include "RawColumnSlice.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <format>
#include <string_view>

namespace Lightweight::detail
{

namespace
{
    /// Number of bytes of a binary cell that DescribeRawCell() spells out before cutting the rest.
    constexpr std::size_t MaxDescribedBinaryBytes = 32;

    /// Reads the trivially copyable @p T stored at the start of @p cell.
    template <typename T>
    T Load(std::span<std::byte const> cell) noexcept
    {
        T value {};
        std::memcpy(&value, cell.data(), std::min(sizeof(T), cell.size()));
        return value;
    }

    /// Renders @p cell as `0x` followed by upper-case hex, cut after MaxDescribedBinaryBytes bytes.
    std::string DescribeBinary(std::span<std::byte const> cell)
    {
        std::string text = "0x";
        for (auto const byte: cell.first(std::min(cell.size(), MaxDescribedBinaryBytes)))
            text += std::format("{:02X}", std::to_integer<unsigned>(byte));
        if (cell.size() > MaxDescribedBinaryBytes)
            text += "\xE2\x80\xA6"; // U+2026 HORIZONTAL ELLIPSIS
        return text;
    }

    /// Renders the nanosecond @p fraction as `.fff` without trailing zeros; empty when it is zero.
    std::string DescribeFraction(SQLUINTEGER fraction)
    {
        if (fraction == 0)
            return {};
        auto digits = std::format("{:09}", fraction);
        digits.erase(digits.find_last_not_of('0') + 1);
        return "." + digits;
    }

    /// Renders @p cell holding a SQL_C_GUID in the driver's wire layout as canonical text.
    std::string DescribeGuid(std::span<std::byte const> cell)
    {
        SqlGuid guid {};
        std::memcpy(guid.data, cell.data(), std::min(sizeof(guid.data), cell.size()));
        SwapGuidWireByteOrder(guid.data);
        return to_string(guid);
    }

    /// Renders the in-use bytes of a character cell of @p cell (at most one stride) given its indicator.
    std::size_t UsedBytes(std::span<std::byte const> cell, SQLLEN indicator, std::size_t unitSize)
    {
        if (indicator >= 0)
            return std::min(static_cast<std::size_t>(indicator), cell.size());
        // SQL_NTS: the text runs up to the terminating NUL, or to the end of the cell.
        auto length = std::size_t { 0 };
        while (
            (length + 1) * unitSize <= cell.size()
            && !std::ranges::all_of(cell.subspan(length * unitSize, unitSize), [](auto b) { return b == std::byte { 0 }; }))
            ++length;
        return length * unitSize;
    }
} // namespace

SqlRawColumn SliceRows(SqlRawColumn const& column, std::size_t firstRow, std::size_t rowCount)
{
    auto const stride = static_cast<std::size_t>(column.metadata.bufferLength);
    assert(firstRow + rowCount <= column.indicators.size());
    assert((firstRow + rowCount) * stride <= column.data.size());
    return SqlRawColumn { .metadata = column.metadata,
                          .data = column.data.subspan(firstRow * stride, rowCount * stride),
                          .indicators = column.indicators.subspan(firstRow, rowCount) };
}

std::string DescribeRawCell(SqlRawColumn const& column, std::size_t row)
{
    assert(row < column.indicators.size());
    auto const indicator = column.indicators[row];
    if (indicator == SQL_NULL_DATA)
        return "NULL";

    auto const stride = static_cast<std::size_t>(column.metadata.bufferLength);
    auto const cell = column.data.subspan(row * stride, stride);

    switch (column.metadata.cType)
    {
        case SQL_C_CHAR: {
            auto const used = UsedBytes(cell, indicator, sizeof(char));
            return { reinterpret_cast<char const*>(cell.data()), used };
        }
        case SQL_C_WCHAR: {
            auto const used = UsedBytes(cell, indicator, sizeof(char16_t)) / sizeof(char16_t);
            std::u16string wide(used, char16_t { 0 });
            std::memcpy(wide.data(), cell.data(), used * sizeof(char16_t));
            auto const utf8 = ToUtf8(std::u16string_view { wide });
            return { reinterpret_cast<char const*>(utf8.data()), utf8.size() };
        }
        case SQL_C_SBIGINT:
            return std::format("{}", Load<std::int64_t>(cell));
        case SQL_C_UBIGINT:
            return std::format("{}", Load<std::uint64_t>(cell));
        case SQL_C_LONG: // legacy alias of the signed type
        case SQL_C_SLONG:
            return std::format("{}", Load<std::int32_t>(cell));
        case SQL_C_ULONG:
            return std::format("{}", Load<std::uint32_t>(cell));
        case SQL_C_SHORT:
        case SQL_C_SSHORT:
            return std::format("{}", Load<std::int16_t>(cell));
        case SQL_C_USHORT:
            return std::format("{}", Load<std::uint16_t>(cell));
        case SQL_C_TINYINT:
        case SQL_C_STINYINT:
            return std::format("{}", Load<std::int8_t>(cell));
        case SQL_C_UTINYINT:
            return std::format("{}", Load<std::uint8_t>(cell));
        case SQL_C_BIT:
            return std::format("{}", Load<std::int8_t>(cell));
        case SQL_C_DOUBLE:
            return std::format("{}", Load<double>(cell));
        case SQL_C_FLOAT:
            return std::format("{}", Load<float>(cell));
        case SQL_C_GUID:
            return DescribeGuid(cell);
        case SQL_C_TYPE_DATE: {
            auto const date = Load<SQL_DATE_STRUCT>(cell);
            return std::format("{:04}-{:02}-{:02}", date.year, date.month, date.day);
        }
        case SQL_C_TYPE_TIME: {
            auto const time = Load<SQL_TIME_STRUCT>(cell);
            return std::format("{:02}:{:02}:{:02}", time.hour, time.minute, time.second);
        }
        case SQL_C_TYPE_TIMESTAMP: {
            auto const ts = Load<SQL_TIMESTAMP_STRUCT>(cell);
            return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}{}",
                               ts.year,
                               ts.month,
                               ts.day,
                               ts.hour,
                               ts.minute,
                               ts.second,
                               DescribeFraction(ts.fraction));
        }
        default:
            break; // SQL_C_BINARY and anything unknown
    }
    auto const used = indicator >= 0 ? std::min(static_cast<std::size_t>(indicator), cell.size()) : cell.size();
    return DescribeBinary(cell.first(used));
}

} // namespace Lightweight::detail
